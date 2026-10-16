// WiFi station with setup-hotspot fallback, captive portal, optional login.
#include "ESP32WebDash.h"

#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>
#include <WiFi.h>

namespace webdash {

static constexpr uint32_t AP_RETRY_MS = 30000;     // STA retry while the hotspot is idle
static constexpr uint32_t AP_LINGER_MS = 60000;    // keep the hotspot after STA came back
static const char* AUTH_USER = "admin";

void ESP32WebDash::startWifi() {
  String host = hostname();
  WiFi.persistent(false);  // credentials live in our NVS namespace, not the IDF one
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(host.c_str());
  WiFi.setAutoReconnect(true);
  WiFi.onEvent([this, host](WiFiEvent_t, WiFiEventInfo_t) {
    staWarned_ = false;
    log('I', "WiFi connected: http://%s  (http://%s.local)", WiFi.localIP().toString().c_str(), host.c_str());
  }, ARDUINO_EVENT_WIFI_STA_GOT_IP);
  WiFi.onEvent([this](WiFiEvent_t, WiFiEventInfo_t info) {
    if (staWarned_) return;  // retries fail every few seconds; report once per outage
    staWarned_ = true;
    log('W', "WiFi disconnected (reason %u), retrying", info.wifi_sta_disconnected.reason);
  }, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

  String ssid = ssid_->asString();
  if (ssid.length()) WiFi.begin(ssid.c_str(), pass_->asString().c_str());
  else log('W', "no WiFi configured");
  staLostAt_ = millis();
  if (MDNS.begin(host.c_str())) MDNS.addService("http", "tcp", cfg_.port);
}

void ESP32WebDash::startAp() {
  String ssid = hostname();
  String pass = apPass_->asString();
  WiFi.setAutoReconnect(false);  // STA scans would make the hotspot hop channels
  WiFi.mode(WIFI_AP_STA);
  bool ok = WiFi.softAP(ssid.c_str(), pass.length() >= 8 ? pass.c_str() : nullptr);
  if (!ok) { log('E', "setup hotspot failed to start"); return; }
  if (!dns_) dns_ = new DNSServer();
  dns_->setErrorReplyCode(DNSReplyCode::NoError);
  dns_->start(53, "*", WiFi.softAPIP());
  apActive_ = true;
  lastRetry_ = millis();
  log('W', "setup hotspot '%s' active: http://%s", ssid.c_str(), WiFi.softAPIP().toString().c_str());
}

void ESP32WebDash::stopAp() {
  if (dns_) dns_->stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  apActive_ = false;
  log('I', "setup hotspot closed");
}

// Called every 500 ms from the service task.
void ESP32WebDash::wifiTick() {
  uint32_t now = millis();
  bool up = WiFi.status() == WL_CONNECTED;
  if (up) {
    if (!staUpAt_) staUpAt_ = now;
    staLostAt_ = 0;
    if (apActive_ && now - staUpAt_ > AP_LINGER_MS && WiFi.softAPgetStationNum() == 0) stopAp();
    return;
  }
  staUpAt_ = 0;
  if (!staLostAt_) staLostAt_ = now;
  String ssid = ssid_->asString();
  if (!apActive_) {
    if (apFallback_->asBool() && (ssid.isEmpty() || now - staLostAt_ > cfg_.apFallbackMs)) startAp();
    return;
  }
  // Hotspot active: retry the network only while nobody is using the hotspot.
  if (ssid.length() && WiFi.softAPgetStationNum() == 0 && now - lastRetry_ > AP_RETRY_MS) {
    lastRetry_ = now;
    WiFi.begin(ssid.c_str(), pass_->asString().c_str());
  }
}

// Requests arriving over the hotspot for a foreign host (OS connectivity checks,
// any URL typed in the browser) are redirected to the UI, which opens the captive portal.
bool ESP32WebDash::captiveRedirect(AsyncWebServerRequest* req) {
  if (!apActive_ || req->client()->localIP() != WiFi.softAPIP()) return false;
  String host = req->host();
  String ip = WiFi.softAPIP().toString();
  if (host == ip || host.startsWith(ip + ":") || host.equalsIgnoreCase(hostname() + ".local")) return false;
  req->redirect("http://" + ip + "/");
  return true;
}

void ESP32WebDash::updateAuth() {
  Lock l(*this);
  if (!auth_) {
    auth_ = new AsyncAuthenticationMiddleware();
    auth_->setAuthType(AsyncAuthType::AUTH_BASIC);
    auth_->setRealm(name_.c_str());
    auth_->setUsername(AUTH_USER);
  }
  auth_->setPassword(authPass_->str_.c_str());
  auth_->generateHash();
}

bool ESP32WebDash::authorized(AsyncWebServerRequest* req) {
  Lock l(*this);
  if (authOn_->num_ == 0 || authPass_->str_.isEmpty()) return true;
  return auth_->allowed(req);
}

}  // namespace webdash
