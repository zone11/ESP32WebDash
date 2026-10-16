#pragma once
// ESP32WebDash: schema-driven web UI for ESP32 projects.
//
//   ESP32WebDash ui("Greenhouse Node");
//   ui.group("Watering");
//   auto& threshold = ui.addSlider("threshold", "Humidity threshold", 45, 10, 90).unit("%");
//   auto& temp = ui.addValue("temp", "Temperature").unit("°C").decimals(1);
//   ui.addAction("pump", "Run pump").button("Start").run([] { pump.start(); });
//   ui.begin();
//   ...
//   temp.set(sensor.read());          // from any task
//   if (threshold.asInt() > 50) ...   // from any task
//
// Thread model: all registry access is guarded by one mutex. Web handlers run in the
// AsyncTCP task and never call app code; onChange callbacks and actions run on the
// library's worker task. Values are pushed to browsers by a separate low-priority task.

#include <Arduino.h>
#include <ArduinoJson.h>
#include <functional>
#include <initializer_list>
#include <memory>
#include <vector>

class AsyncWebServer;
class AsyncWebSocket;
class AsyncWebServerRequest;
class AsyncAuthenticationMiddleware;
class DNSServer;

namespace webdash {

class ESP32WebDash;
class WebLog;
// Shown instead of stored passwords; a PATCH with this value keeps the password.
constexpr const char* PASSWORD_MASK = "********";

// ---------------------------------------------------------------- settings
enum class SettingType : uint8_t { Bool, Int, Float, Slider, Text, Password, Select, Color };

class Setting {
 public:
  using Callback = std::function<void(Setting&)>;

  Setting& help(const char* text) { help_ = text; return *this; }
  Setting& unit(const char* text) { unit_ = text; return *this; }
  Setting& step(double s) { step_ = s; return *this; }
  Setting& maxLength(uint16_t n) { maxLen_ = n; return *this; }
  Setting& placeholder(const char* text) { placeholder_ = text; return *this; }
  // Text only: multi-line box with this many visible rows (keeps line breaks, e.g. for a PEM certificate).
  // NVS stores at most ~4000 bytes per string, so keep maxLength() below that.
  Setting& multiline(uint8_t rows = 6) { rows_ = rows; return *this; }
  // Changing this setting only takes effect after a restart (UI shows a hint).
  Setting& requiresRestart(bool on = true) { reboot_ = on; return *this; }
  // Called on the worker task after the value changed (web UI, import, reset or set()).
  Setting& onChange(Callback cb) { onChange_ = std::move(cb); return *this; }

  const char* id() const { return id_.c_str(); }
  SettingType type() const { return type_; }

  bool asBool() const;
  int32_t asInt() const;
  float asFloat() const;
  String asString() const;  // text, password, select, color (also works for numbers)

  // Change the value from app code (persisted, UI is notified).
  void set(bool v);
  void set(int v);
  void set(double v);
  void set(const char* v);
  void set(const String& v) { set(v.c_str()); }

 private:
  friend class ESP32WebDash;
  Setting(ESP32WebDash& owner, SettingType t, const char* id, const char* label, const char* group)
      : owner_(owner), type_(t), id_(id), label_(label), group_(group) {}

  ESP32WebDash& owner_;
  SettingType type_;
  String id_, label_, group_, help_, unit_, placeholder_;
  double min_ = 0, max_ = 0, step_ = 0;
  uint16_t maxLen_ = 64;
  uint8_t rows_ = 0;
  bool reboot_ = false;
  std::vector<std::pair<String, String>> options_;
  Callback onChange_;
  // current and default value; numbers in num, everything else in str
  double num_ = 0, defNum_ = 0;
  String str_, defStr_;
};

// ---------------------------------------------------------------- values
enum class ValueType : uint8_t { Number, Text, State, Led, Pwm, Uptime };

class Value {
 public:
  Value& unit(const char* text) { unit_ = text; return *this; }
  Value& icon(const char* name) { icon_ = name; return *this; }
  Value& decimals(uint8_t d) { decimals_ = d; return *this; }
  // Draws a meter (Number) or sets the raw duty range (Pwm, default 0..100).
  Value& range(double min, double max) { min_ = min; max_ = max; hasRange_ = true; return *this; }
  // Default LED color (#rrggbb). Without it the UI uses the accent color.
  Value& color(const char* hex) { color_ = hex; return *this; }

  // Thread-safe setters, callable from any task.
  void set(double v);
  void set(int v) { set(static_cast<double>(v)); }
  void set(uint32_t v) { set(static_cast<double>(v)); }
  void set(bool on);
  void set(const char* text);
  void set(const String& text) { set(text.c_str()); }
  // LED with optional color (#rrggbb, nullptr = default) and dim level 0..100 (-1 = none).
  void setLed(bool on, const char* color = nullptr, int dim = -1);

 private:
  friend class ESP32WebDash;
  Value(ESP32WebDash& owner, ValueType t, const char* id, const char* label)
      : owner_(owner), type_(t), id_(id), label_(label) {}
  void toJson(JsonObject obj) const;  // writes "id": value

  ESP32WebDash& owner_;
  ValueType type_;
  String id_, label_, unit_, icon_, color_;
  uint8_t decimals_ = 255;
  bool hasRange_ = false;
  double min_ = 0, max_ = 0;
  bool hasValue_ = false, dirty_ = false;
  double num_ = 0;
  bool on_ = false;
  int dim_ = -1;
  String str_, ledColor_;
};

// ---------------------------------------------------------------- actions
enum class ActionStyle : uint8_t { Normal, Primary, Danger };

class Action {
 public:
  using Fn = std::function<void()>;
  Action& help(const char* text) { help_ = text; return *this; }
  Action& button(const char* text) { button_ = text; return *this; }
  Action& icon(const char* name) { icon_ = name; return *this; }
  Action& group(const char* name) { group_ = name; return *this; }
  Action& primary() { style_ = ActionStyle::Primary; return *this; }
  Action& danger() { style_ = ActionStyle::Danger; return *this; }
  Action& confirm(bool on = true) { confirm_ = on; return *this; }
  // Runs on the worker task, never in the web server task.
  Action& run(Fn fn) { fn_ = std::move(fn); return *this; }

 private:
  friend class ESP32WebDash;
  Action(const char* id, const char* label) : id_(id), label_(label) {}
  String id_, label_, help_, button_, icon_, group_;
  ActionStyle style_ = ActionStyle::Normal;
  bool confirm_ = false;
  Fn fn_;
};

// ---------------------------------------------------------------- main class
struct Config {
  uint16_t port = 80;
  uint32_t apFallbackMs = 20000;   // open the setup hotspot after this long without WiFi
  uint16_t logLines = 200;         // log history kept for the web console
  uint32_t pushIntervalMs = 1000;  // live value updates to browsers
  uint32_t workerStack = 6144;     // stack for onChange callbacks and actions
  UBaseType_t taskPriority = 1;
};

class ESP32WebDash {
 public:
  explicit ESP32WebDash(const char* deviceName);
  ~ESP32WebDash();

  ESP32WebDash& firmware(const char* version) { firmware_ = version; return *this; }
  // Logo in the top bar: a file from the project's web/ folder, e.g. "logo.svg".
  ESP32WebDash& logo(const char* path) { logo_ = path; return *this; }
  // First-boot WiFi credentials; afterwards the values stored in NVS are used.
  ESP32WebDash& wifiDefaults(const char* ssid, const char* pass) { defSsid_ = ssid; defPass_ = pass; return *this; }
  ESP32WebDash& config(const Config& c) { cfg_ = c; return *this; }

  // Settings added after group() belong to it (shown as one section).
  ESP32WebDash& group(const char* title, const char* help = nullptr);
  // Shows the current group only while setting `settingId` has `value` (e.g. a select). The settings stay
  // stored, exported and readable while hidden.
  ESP32WebDash& showWhen(const char* settingId, const char* value);
  Setting& addBool(const char* id, const char* label, bool def = false);
  Setting& addInt(const char* id, const char* label, int32_t def, int32_t min, int32_t max);
  Setting& addFloat(const char* id, const char* label, double def, double min, double max);
  Setting& addSlider(const char* id, const char* label, int32_t def, int32_t min, int32_t max);
  Setting& addText(const char* id, const char* label, const char* def = "");
  Setting& addPassword(const char* id, const char* label, const char* def = "");
  Setting& addSelect(const char* id, const char* label, const char* def,
                     std::initializer_list<std::pair<const char*, const char*>> options);
  // Same, for option lists built at runtime (value, label).
  Setting& addSelect(const char* id, const char* label, const char* def,
                     const std::vector<std::pair<String, String>>& options);
  Setting& addColor(const char* id, const char* label, const char* def = "#3d9fb0");

  Value& addValue(const char* id, const char* label);   // number
  Value& addLabel(const char* id, const char* label);   // text
  Value& addState(const char* id, const char* label);   // on/off
  Value& addLed(const char* id, const char* label);     // LED indicator
  Value& addPwm(const char* id, const char* label, uint32_t maxRaw = 100);
  Value& addUptime(const char* id, const char* label);  // seconds

  Action& addAction(const char* id, const char* label);

  Setting* setting(const char* id);
  Value* value(const char* id);

  // Starts WiFi, web server, push and worker tasks. Returns immediately.
  void begin();
  // No-op, kept for sketches that call it; all work runs in the library's own tasks.
  void loop() {}

  // Log to Serial and the web console. log() prints info lines; log('W', ...) sets the level (E, W, I, D).
  Print& log();
  void log(char level, const char* fmt, ...) __attribute__((format(printf, 3, 4)));

  // True while the setup hotspot is active.
  bool apActive() const { return apActive_; }

  // Show a toast in all connected browsers.
  void notify(const char* message, bool error = false);
  // Run fn on the worker task.
  bool defer(std::function<void()> fn);

  const String& name() const { return name_; }
  String hostname() const;

 private:
  friend class Setting;
  friend class Value;
  friend class Lock;

  struct Group { String title, help, whenId, whenValue; };

  Setting& addSetting(SettingType t, const char* id, const char* label);
  void load(Setting& s);
  void persist(Setting& s);
  // Validates v for s; on success stores it in out (without applying). Returns error text or "".
  String validate(const Setting& s, JsonVariantConst v, double& outNum, String& outStr) const;
  bool apply(Setting& s, double num, const String& str);  // under lock; returns true if changed
  void changed(Setting& s);                              // persist + callbacks + UI notification
  void settingJson(const Setting& s, JsonObject out, bool secrets) const;
  void markDirty() { anyDirty_ = true; }

  void lock() const;
  void unlock() const;

  void routes();
  void schemaJson(JsonDocument& doc) const;
  void settingsJson(JsonObject out, bool secrets) const;
  void systemJson(JsonDocument& doc) const;
  void sendJson(AsyncWebServerRequest* req, const JsonDocument& doc, int code = 200);
  void sendError(AsyncWebServerRequest* req, int code, const String& msg);
  void patchSettings(AsyncWebServerRequest* req, JsonVariantConst body, bool import);
  void resetSettings();
  void valuesSnapshot(JsonDocument& doc, bool onlyDirty);
  void broadcast(const JsonDocument& doc);
  void startWifi();
  std::vector<String> ipv6Addresses() const;
  void wifiTick();
  void startAp();
  void stopAp();
  bool captiveRedirect(AsyncWebServerRequest* req);
  bool authorized(AsyncWebServerRequest* req);
  void updateAuth();
  void otaRoutes();

  static void workerTask(void* arg);
  static void serviceTask(void* arg);

  String name_, firmware_ = "0.0.0", defSsid_, defPass_, logo_;
  Config cfg_;
  std::vector<Group> groups_;
  std::vector<std::unique_ptr<Setting>> settings_;
  std::vector<std::unique_ptr<Value>> values_;
  std::vector<std::unique_ptr<Action>> actions_;
  Setting *hostname_ = nullptr, *ssid_ = nullptr, *pass_ = nullptr;
  Setting *ipv6_ = nullptr;
  Setting *apFallback_ = nullptr, *apPass_ = nullptr, *authOn_ = nullptr, *authPass_ = nullptr;
  volatile bool anyDirty_ = false;
  bool started_ = false;

  // WiFi state machine (service task)
  volatile bool apActive_ = false;
  volatile bool staWarned_ = false;
  uint32_t staLostAt_ = 0, staUpAt_ = 0, lastRetry_ = 0;
  DNSServer* dns_ = nullptr;

  AsyncAuthenticationMiddleware* auth_ = nullptr;
  WebLog* weblog_ = nullptr;
  uint32_t logSent_ = 0;
  volatile bool otaBusy_ = false;
  String otaError_;

  void* mutex_ = nullptr;   // SemaphoreHandle_t
  void* queue_ = nullptr;   // QueueHandle_t of std::function<void()>*
  AsyncWebServer* server_ = nullptr;
  AsyncWebSocket* ws_ = nullptr;
};

// Scoped registry lock (recursive).
class Lock {
 public:
  explicit Lock(const ESP32WebDash& o) : o_(o) { o_.lock(); }
  ~Lock() { o_.unlock(); }
  Lock(const Lock&) = delete;
  Lock& operator=(const Lock&) = delete;
 private:
  const ESP32WebDash& o_;
};

}  // namespace webdash

using webdash::ESP32WebDash;
using webdash::Setting;
using webdash::Value;
using webdash::Action;
