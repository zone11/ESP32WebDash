// Lifecycle, tasks and the HTTP / WebSocket API.
#include "ESP32WebDash.h"

#include <AsyncJson.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <stdarg.h>

#include "WebLog.h"

namespace webdash {

using Job = std::function<void()>;

// Served from firmware when the LittleFS UI is missing (e.g. after a failed filesystem update).
static const char RECOVERY_HTML[] PROGMEM = R"html(<!doctype html><meta charset=utf-8>
<meta name=viewport content="width=device-width,initial-scale=1"><title>Recovery</title>
<style>body{font:15px system-ui;max-width:520px;margin:40px auto;padding:0 16px;background:#101010;color:#eee}
input,button{font:inherit;margin:8px 0}p{color:#999}</style>
<h1>Recovery update</h1><p>The web UI is missing. Upload <b>littlefs.bin</b> (web UI) or <b>firmware.bin</b>.</p>
<input type=file id=f accept=.bin><br><button onclick=u()>Upload</button><p id=s></p>
<script>function u(){var f=document.getElementById('f').files[0],s=document.getElementById('s');if(!f)return;
var fd=new FormData();fd.append('file',f,f.name);var x=new XMLHttpRequest();
x.open('POST','/api/ota/'+(/littlefs|spiffs/i.test(f.name)?'fs':'firmware'));
x.upload.onprogress=function(e){s.textContent=Math.round(e.loaded*100/e.total)+' %'};
x.onload=function(){s.textContent=x.status==200?'Done, restarting...':'Failed: '+x.responseText};x.send(fd)}</script>)html";

static String slug(const String& s) {
  String out;
  for (char c : s) {
    if (isalnum((unsigned char)c)) out += (char)tolower(c);
    else if (out.length() && out[out.length() - 1] != '-') out += '-';
  }
  while (out.endsWith("-")) out.remove(out.length() - 1);
  return out.length() ? out : String("esp32");
}

ESP32WebDash::ESP32WebDash(const char* deviceName) : name_(deviceName) {
  mutex_ = xSemaphoreCreateRecursiveMutex();
}

ESP32WebDash::~ESP32WebDash() {
  // Instances are expected to live for the whole runtime; nothing is torn down.
}

void ESP32WebDash::lock() const { xSemaphoreTakeRecursive((SemaphoreHandle_t)mutex_, portMAX_DELAY); }
void ESP32WebDash::unlock() const { xSemaphoreGiveRecursive((SemaphoreHandle_t)mutex_); }

String ESP32WebDash::hostname() const { return hostname_ ? hostname_->asString() : slug(name_); }

// ---------------------------------------------------------------- log

Print& ESP32WebDash::log() {
  if (!weblog_) weblog_ = new WebLog(cfg_.logLines);
  return *weblog_;
}

void ESP32WebDash::log(char level, const char* fmt, ...) {
  char buf[200];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof buf, fmt, ap);
  va_end(ap);
  log();  // ensure the buffer exists
  weblog_->line(level, buf);
}

// ---------------------------------------------------------------- tasks

bool ESP32WebDash::defer(std::function<void()> fn) {
  if (!queue_) return false;
  auto* job = new Job(std::move(fn));
  if (xQueueSend((QueueHandle_t)queue_, &job, 0) != pdTRUE) {
    delete job;
    log('W', "worker queue full, job dropped");
    return false;
  }
  return true;
}

void ESP32WebDash::workerTask(void* arg) {
  auto* self = static_cast<ESP32WebDash*>(arg);
  Job* job;
  for (;;) {
    if (xQueueReceive((QueueHandle_t)self->queue_, &job, portMAX_DELAY) == pdTRUE) {
      (*job)();
      delete job;
    }
  }
}

// Pushes values and log lines to browsers and runs the WiFi state machine.
void ESP32WebDash::serviceTask(void* arg) {
  auto* self = static_cast<ESP32WebDash*>(arg);
  const uint32_t tick = 100;
  uint32_t n = 0;
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(tick));
    n++;
    uint32_t ms = n * tick;
    if (ms % 500 == 0) self->wifiTick();
    if (ms % 1000 == 0) self->ws_->cleanupClients();
    if (!self->ws_->count()) { self->logSent_ = self->weblog_->seq(); continue; }

    if (self->weblog_->seq() != self->logSent_ && ms % 200 == 0) {
      JsonDocument doc;
      self->logSent_ = self->weblog_->collect(self->logSent_, doc["log"].to<JsonArray>());
      self->broadcast(doc);
    }
    if (self->anyDirty_ && ms % self->cfg_.pushIntervalMs < tick) {
      JsonDocument doc;
      self->valuesSnapshot(doc, true);
      self->broadcast(doc);
    }
  }
}

void ESP32WebDash::broadcast(const JsonDocument& doc) {
  if (!ws_ || !ws_->count()) return;
  String out;
  serializeJson(doc, out);
  ws_->textAll(out);
}

void ESP32WebDash::notify(const char* message, bool error) {
  log(error ? 'E' : 'I', "%s", message);
  JsonDocument doc;
  doc["toast"] = message;
  if (error) doc["error"] = true;
  broadcast(doc);
}

// ---------------------------------------------------------------- begin

void ESP32WebDash::begin() {
  if (started_) return;
  log();
  weblog_->hookIdfLog();

  // Built-in settings come last on the settings page.
  group("Network", "WiFi connection and device name.");
  hostname_ = &addText("hostname", "Hostname", slug(name_).c_str())
                   .maxLength(32).requiresRestart()
                   .help("Name on the network and of the setup hotspot; reachable as <hostname>.local.");
  ssid_ = &addText("wifi_ssid", "WiFi name", defSsid_.c_str()).maxLength(32).requiresRestart();
  pass_ = &addPassword("wifi_pass", "WiFi password", defPass_.c_str()).maxLength(64).requiresRestart();
  apFallback_ = &addBool("ap_fallback", "Setup hotspot", true)
                     .help("Opens a WiFi hotspot with this page when the network is unreachable.");
  apPass_ = &addPassword("ap_pass", "Hotspot password").maxLength(64)
                 .help("At least 8 characters; empty = open hotspot.");

  group("Security");
  authOn_ = &addBool("auth_on", "Require login")
                 .help("Protects the web UI and API. User name: admin.");
  authPass_ = &addPassword("auth_pass", "Login password").maxLength(64)
                   .help("Login stays off while this is empty.");
  authPass_->onChange([this](Setting&) { updateAuth(); });
  updateAuth();

  queue_ = xQueueCreate(32, sizeof(Job*));
  xTaskCreate(workerTask, "webdash-worker", cfg_.workerStack, this, cfg_.taskPriority, nullptr);

  // PlatformIO's default tables name the partition "spiffs", others use "littlefs".
  if (!LittleFS.begin(true, "/littlefs", 10, "spiffs") && !LittleFS.begin(true, "/littlefs", 10, "littlefs"))
    log('E', "LittleFS mount failed (no 'spiffs' or 'littlefs' data partition?)");
  startWifi();

  server_ = new AsyncWebServer(cfg_.port);
  ws_ = new AsyncWebSocket("/ws");
  routes();
  server_->begin();
  started_ = true;
  xTaskCreate(serviceTask, "webdash-service", 4096, this, cfg_.taskPriority, nullptr);
  log('I', "%s %s: %u settings, %u values, %u actions", name_.c_str(), firmware_.c_str(),
      (unsigned)settings_.size(), (unsigned)values_.size(), (unsigned)actions_.size());
}

// ---------------------------------------------------------------- HTTP

void ESP32WebDash::sendJson(AsyncWebServerRequest* req, const JsonDocument& doc, int code) {
  AsyncResponseStream* res = req->beginResponseStream("application/json");
  res->setCode(code);
  res->addHeader("Cache-Control", "no-store");
  serializeJson(doc, *res);
  req->send(res);
}

void ESP32WebDash::sendError(AsyncWebServerRequest* req, int code, const String& msg) {
  JsonDocument doc;
  doc["error"] = msg;
  sendJson(req, doc, code);
}

void ESP32WebDash::systemJson(JsonDocument& doc) const {
  doc["name"] = name_;
  doc["hostname"] = hostname();
  doc["firmware"] = firmware_;
  doc["chip"] = String(ESP.getChipModel()) + " rev" + String(ESP.getChipRevision());
  doc["mac"] = WiFi.macAddress();
  doc["ip"] = WiFi.localIP().toString();
  doc["ssid"] = WiFi.SSID();
  doc["rssi"] = WiFi.RSSI();
  doc["online"] = WiFi.status() == WL_CONNECTED;
  if (apActive_) { doc["mode"] = "ap"; doc["apIp"] = WiFi.softAPIP().toString(); }
  else doc["mode"] = "sta";
  doc["uptime"] = (uint32_t)(esp_timer_get_time() / 1000000ULL);
  doc["heap"] = ESP.getFreeHeap();
  doc["psram"] = ESP.getFreePsram();
  doc["fsUsed"] = LittleFS.usedBytes();
  doc["fsTotal"] = LittleFS.totalBytes();
}

// PATCH applies all-or-nothing; import skips unknown and invalid keys.
void ESP32WebDash::patchSettings(AsyncWebServerRequest* req, JsonVariantConst body, bool import) {
  JsonObjectConst obj = import && body["settings"].is<JsonObjectConst>() ? body["settings"].as<JsonObjectConst>()
                                                                          : body.as<JsonObjectConst>();
  if (obj.isNull()) return sendError(req, 400, "Expected a JSON object");

  struct Pending { Setting* s; double num; String str; };
  std::vector<Pending> pending;
  JsonDocument res;
  JsonArray errors = res["errors"].to<JsonArray>();
  int skipped = 0;
  {
    Lock l(*this);
    for (JsonPairConst kv : obj) {
      Setting* s = setting(kv.key().c_str());
      if (!s) {
        if (!import) return sendError(req, 400, String("Unknown setting '") + kv.key().c_str() + "'");
        skipped++;
        continue;
      }
      if (s->type() == SettingType::Password && kv.value().is<const char*>() &&
          strcmp(kv.value().as<const char*>(), PASSWORD_MASK) == 0) continue;  // unchanged
      Pending p{s, 0, String()};
      String err = validate(*s, kv.value(), p.num, p.str);
      if (err.length()) {
        if (!import) return sendError(req, 400, err);
        errors.add(err);
        skipped++;
        continue;
      }
      pending.push_back(std::move(p));
    }
  }
  std::vector<Setting*> touched;
  bool reboot = false;
  {
    Lock l(*this);
    for (auto& p : pending)
      if (apply(*p.s, p.num, p.str)) { touched.push_back(p.s); reboot |= p.s->reboot_; }
  }
  for (auto* s : touched) changed(*s);
  if (touched.size()) log('I', "%s: %u setting(s) changed", import ? "import" : "settings", (unsigned)touched.size());
  res["ok"] = true;
  res["reboot"] = reboot;
  res["applied"] = (int)touched.size();
  res["skipped"] = skipped;
  sendJson(req, res);
}

void ESP32WebDash::routes() {
  AsyncWebServer& srv = *server_;

  // Captive portal redirect and optional login for everything, WebSocket included.
  srv.addMiddleware([this](AsyncWebServerRequest* req, ArMiddlewareNext next) {
    if (captiveRedirect(req)) return;
    if (!authorized(req)) return req->requestAuthentication(AsyncAuthType::AUTH_BASIC, name_.c_str());
    next();
  });

  // Most specific routes first: a handler for /a also matches /a/b.
  srv.on("/api/schema", HTTP_GET, [this](AsyncWebServerRequest* req) {
    JsonDocument doc; schemaJson(doc); sendJson(req, doc);
  });
  srv.on("/api/system/restart", HTTP_POST, [this](AsyncWebServerRequest* req) {
    JsonDocument doc; doc["ok"] = true; sendJson(req, doc);
    log('I', "restart requested");
    defer([] { delay(500); ESP.restart(); });
  });
  srv.on("/api/system", HTTP_GET, [this](AsyncWebServerRequest* req) {
    JsonDocument doc; systemJson(doc); sendJson(req, doc);
  });
  srv.on("/api/settings/export", HTTP_GET, [this](AsyncWebServerRequest* req) {
    JsonDocument doc;
    doc["device"] = name_;
    doc["hostname"] = hostname();
    doc["firmware"] = firmware_;
    settingsJson(doc["settings"].to<JsonObject>(), true);
    sendJson(req, doc);
  });
  srv.on("/api/settings/reset", HTTP_POST, [this](AsyncWebServerRequest* req) {
    log('W', "settings reset to defaults");
    defer([this] { resetSettings(); });
    JsonDocument doc; doc["ok"] = true; doc["reboot"] = true; sendJson(req, doc);
  });

  auto* imp = new AsyncCallbackJsonWebHandler("/api/settings/import",
      [this](AsyncWebServerRequest* req, JsonVariant& json) { patchSettings(req, json, true); });
  imp->setMethod(HTTP_POST);
  imp->setMaxContentLength(32768);
  srv.addHandler(imp);

  auto* patch = new AsyncCallbackJsonWebHandler("/api/settings",
      [this](AsyncWebServerRequest* req, JsonVariant& json) { patchSettings(req, json, false); });
  patch->setMethod(HTTP_PATCH | HTTP_POST);
  patch->setMaxContentLength(16384);
  srv.addHandler(patch);

  srv.on("/api/settings", HTTP_GET, [this](AsyncWebServerRequest* req) {
    JsonDocument doc; settingsJson(doc.to<JsonObject>(), false); sendJson(req, doc);
  });

  srv.on("/api/action", HTTP_POST, [this](AsyncWebServerRequest* req) {
    String id = req->url().substring(strlen("/api/action/"));
    Action* a = nullptr;
    for (auto& x : actions_) if (x->id_ == id) a = x.get();
    if (!a || !a->fn_) return sendError(req, 404, "Unknown action '" + id + "'");
    Action::Fn fn = a->fn_;
    if (!defer(fn)) return sendError(req, 503, "Busy, try again");
    log('I', "action: %s", a->label_.c_str());
    JsonDocument doc; doc["ok"] = true; doc["message"] = a->label_ + ": started"; sendJson(req, doc, 202);
  });

  otaRoutes();

  srv.on("/api", HTTP_ANY, [this](AsyncWebServerRequest* req) { sendError(req, 404, "Not found"); });

  ws_->onEvent([this](AsyncWebSocket*, AsyncWebSocketClient* client, AwsEventType type, void*, uint8_t*, size_t) {
    if (type != WS_EVT_CONNECT) return;
    JsonDocument doc;
    {
      Lock l(*this);
      JsonObject out = doc["values"].to<JsonObject>();
      for (auto& v : values_) v->toJson(out);
    }
    weblog_->collect(0, doc["log"].to<JsonArray>());
    String s; serializeJson(doc, s);
    client->text(s);
  });
  srv.addHandler(ws_);

  srv.on("/update", HTTP_GET, [](AsyncWebServerRequest* req) { req->send(200, "text/html", RECOVERY_HTML); });
  srv.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
    if (LittleFS.exists("/index.html.gz") || LittleFS.exists("/index.html")) {
      AsyncWebServerResponse* res = req->beginResponse(LittleFS, "/index.html", "text/html");
      res->addHeader("Cache-Control", "no-cache");
      req->send(res);
    } else {
      req->send(200, "text/html", RECOVERY_HTML);
    }
  });
  srv.serveStatic("/", LittleFS, "/").setCacheControl("no-cache");
  srv.onNotFound([](AsyncWebServerRequest* req) { req->send(404, "text/plain", "Not found"); });
}

}  // namespace webdash
