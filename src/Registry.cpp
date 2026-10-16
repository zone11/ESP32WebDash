// Settings / values / actions registry, NVS persistence, validation.
#include "ESP32WebDash.h"

#include <Preferences.h>
#include <math.h>

namespace webdash {

static const char* NVS_NS = "webdash";

static bool isNumeric(SettingType t) {
  return t == SettingType::Bool || t == SettingType::Int || t == SettingType::Float || t == SettingType::Slider;
}

static String fmtNum(double v) {
  char b[24];
  snprintf(b, sizeof b, "%g", v);
  return b;
}

static bool isHexColor(const String& s) {
  if (s.length() != 7 || s[0] != '#') return false;
  for (int i = 1; i < 7; i++) if (!isxdigit((unsigned char)s[i])) return false;
  return true;
}


// ---------------------------------------------------------------- Setting

bool Setting::asBool() const { Lock l(owner_); return num_ != 0; }
int32_t Setting::asInt() const { Lock l(owner_); return (int32_t)lround(num_); }
float Setting::asFloat() const { Lock l(owner_); return (float)num_; }
String Setting::asString() const {
  Lock l(owner_);
  if (!isNumeric(type_)) return str_;
  if (type_ == SettingType::Float) return fmtNum(num_);
  return String((long)lround(num_));
}

void Setting::set(bool v) { set(v ? 1.0 : 0.0); }
void Setting::set(int v) { set((double)v); }
void Setting::set(double v) {
  JsonDocument d;
  if (type_ == SettingType::Bool) d.set(v != 0); else d.set(v);
  double n; String s;
  { Lock l(owner_); if (owner_.validate(*this, d.as<JsonVariantConst>(), n, s).length()) return;
    if (!owner_.apply(*this, n, s)) return; }
  owner_.changed(*this);
}
void Setting::set(const char* v) {
  JsonDocument d; d.set(v);
  double n; String s;
  { Lock l(owner_); if (owner_.validate(*this, d.as<JsonVariantConst>(), n, s).length()) return;
    if (!owner_.apply(*this, n, s)) return; }
  owner_.changed(*this);
}

// ---------------------------------------------------------------- Value

void Value::set(double v) {
  Lock l(owner_);
  if (hasValue_ && num_ == v) return;
  num_ = v; on_ = v != 0; hasValue_ = dirty_ = true; owner_.markDirty();
}
void Value::set(bool on) {
  Lock l(owner_);
  if (hasValue_ && on_ == on && num_ == (on ? 1 : 0)) return;
  on_ = on; num_ = on ? 1 : 0; hasValue_ = dirty_ = true; owner_.markDirty();
}
void Value::set(const char* text) {
  Lock l(owner_);
  if (hasValue_ && str_ == text) return;
  str_ = text; hasValue_ = dirty_ = true; owner_.markDirty();
}
void Value::setLed(bool on, const char* color, int dim) {
  Lock l(owner_);
  String c = color ? color : "";
  if (hasValue_ && on_ == on && ledColor_ == c && dim_ == dim) return;
  on_ = on; ledColor_ = c; dim_ = dim; hasValue_ = dirty_ = true; owner_.markDirty();
}

void Value::toJson(JsonObject obj) const {
  if (!hasValue_) { obj[id_] = nullptr; return; }
  switch (type_) {
    case ValueType::Text: obj[id_] = str_; break;
    case ValueType::State: obj[id_] = on_; break;
    case ValueType::Led:
      if (ledColor_.isEmpty() && dim_ < 0) obj[id_] = on_;
      else {
        JsonObject o = obj[id_].to<JsonObject>();
        o["on"] = on_;
        if (ledColor_.length()) o["color"] = ledColor_;
        if (dim_ >= 0) o["dim"] = dim_;
      }
      break;
    default:
      if (decimals_ != 255) obj[id_] = serialized(String(num_, (unsigned)decimals_));
      else obj[id_] = num_;
  }
}

// ---------------------------------------------------------------- registration

ESP32WebDash& ESP32WebDash::group(const char* title, const char* help) {
  groups_.push_back({title, help ? help : ""});
  return *this;
}

Setting& ESP32WebDash::addSetting(SettingType t, const char* id, const char* label) {
  if (strlen(id) > 15) log_e("setting id '%s' is longer than 15 chars (NVS key limit)", id);
  if (groups_.empty()) group("Settings");
  settings_.emplace_back(new Setting(*this, t, id, label, groups_.back().title.c_str()));
  return *settings_.back();
}

Setting& ESP32WebDash::addBool(const char* id, const char* label, bool def) {
  Setting& s = addSetting(SettingType::Bool, id, label);
  s.defNum_ = def ? 1 : 0; load(s); return s;
}
Setting& ESP32WebDash::addInt(const char* id, const char* label, int32_t def, int32_t min, int32_t max) {
  Setting& s = addSetting(SettingType::Int, id, label);
  s.defNum_ = def; s.min_ = min; s.max_ = max; s.step_ = 1; load(s); return s;
}
Setting& ESP32WebDash::addFloat(const char* id, const char* label, double def, double min, double max) {
  Setting& s = addSetting(SettingType::Float, id, label);
  s.defNum_ = def; s.min_ = min; s.max_ = max; load(s); return s;
}
Setting& ESP32WebDash::addSlider(const char* id, const char* label, int32_t def, int32_t min, int32_t max) {
  Setting& s = addSetting(SettingType::Slider, id, label);
  s.defNum_ = def; s.min_ = min; s.max_ = max; s.step_ = 1; load(s); return s;
}
Setting& ESP32WebDash::addText(const char* id, const char* label, const char* def) {
  Setting& s = addSetting(SettingType::Text, id, label);
  s.defStr_ = def; load(s); return s;
}
Setting& ESP32WebDash::addPassword(const char* id, const char* label, const char* def) {
  Setting& s = addSetting(SettingType::Password, id, label);
  s.defStr_ = def; load(s); return s;
}
Setting& ESP32WebDash::addSelect(const char* id, const char* label, const char* def,
                                   std::initializer_list<std::pair<const char*, const char*>> options) {
  Setting& s = addSetting(SettingType::Select, id, label);
  for (auto& o : options) s.options_.emplace_back(o.first, o.second);
  s.defStr_ = def; load(s); return s;
}
Setting& ESP32WebDash::addColor(const char* id, const char* label, const char* def) {
  Setting& s = addSetting(SettingType::Color, id, label);
  s.defStr_ = def; load(s); return s;
}

Value& ESP32WebDash::addValue(const char* id, const char* label) {
  values_.emplace_back(new Value(*this, ValueType::Number, id, label)); return *values_.back();
}
Value& ESP32WebDash::addLabel(const char* id, const char* label) {
  values_.emplace_back(new Value(*this, ValueType::Text, id, label)); return *values_.back();
}
Value& ESP32WebDash::addState(const char* id, const char* label) {
  values_.emplace_back(new Value(*this, ValueType::State, id, label)); return *values_.back();
}
Value& ESP32WebDash::addLed(const char* id, const char* label) {
  values_.emplace_back(new Value(*this, ValueType::Led, id, label)); return *values_.back();
}
Value& ESP32WebDash::addPwm(const char* id, const char* label, uint32_t maxRaw) {
  values_.emplace_back(new Value(*this, ValueType::Pwm, id, label));
  return values_.back()->range(0, maxRaw);
}
Value& ESP32WebDash::addUptime(const char* id, const char* label) {
  values_.emplace_back(new Value(*this, ValueType::Uptime, id, label)); return *values_.back();
}
Action& ESP32WebDash::addAction(const char* id, const char* label) {
  actions_.emplace_back(new Action(id, label)); return *actions_.back();
}

Setting* ESP32WebDash::setting(const char* id) {
  for (auto& s : settings_) if (s->id_ == id) return s.get();
  return nullptr;
}
Value* ESP32WebDash::value(const char* id) {
  for (auto& v : values_) if (v->id_ == id) return v.get();
  return nullptr;
}

// ---------------------------------------------------------------- persistence

void ESP32WebDash::load(Setting& s) {
  static bool nsReady = false;
  Preferences p;
  if (!nsReady) {  // read-only open fails (and logs) until the namespace exists
    if (p.begin(NVS_NS, false)) p.end();
    nsReady = true;
  }
  bool has = p.begin(NVS_NS, true) && p.isKey(s.id());
  Lock l(*this);
  if (isNumeric(s.type_)) {
    s.num_ = has ? p.getDouble(s.id(), s.defNum_) : s.defNum_;
  } else {
    s.str_ = has ? p.getString(s.id(), s.defStr_) : s.defStr_;
  }
  p.end();
}

void ESP32WebDash::persist(Setting& s) {
  double num; String str;
  { Lock l(*this); num = s.num_; str = s.str_; }
  Preferences p;
  if (!p.begin(NVS_NS, false)) { log_e("NVS open failed"); return; }
  bool ok = isNumeric(s.type_) ? p.putDouble(s.id(), num) == sizeof(double) : p.putString(s.id(), str) == str.length() || str.isEmpty();
  p.end();
  if (!ok) log_e("NVS write failed for '%s'", s.id());
}

void ESP32WebDash::resetSettings() {
  Preferences p;
  if (p.begin(NVS_NS, false)) { p.clear(); p.end(); }
  std::vector<Setting*> touched;
  {
    Lock l(*this);
    for (auto& s : settings_) {
      if (apply(*s, s->defNum_, s->defStr_)) touched.push_back(s.get());
    }
  }
  for (auto* s : touched) changed(*s);
}

// ---------------------------------------------------------------- validation

String ESP32WebDash::validate(const Setting& s, JsonVariantConst v, double& n, String& str) const {
  const String name = "'" + s.label_ + "'";
  switch (s.type_) {
    case SettingType::Bool:
      if (v.is<bool>()) n = v.as<bool>() ? 1 : 0;
      else if (v.is<int>() && (v.as<int>() == 0 || v.as<int>() == 1)) n = v.as<int>();
      else return name + " must be true or false";
      return "";
    case SettingType::Int:
    case SettingType::Slider:
    case SettingType::Float: {
      if (!v.is<double>()) return name + " must be a number";
      n = v.as<double>();
      if (isnan(n) || isinf(n)) return name + " must be a number";
      if (s.type_ != SettingType::Float) {
        if (n != floor(n)) return name + " must be a whole number";
      }
      if (s.min_ != s.max_ && (n < s.min_ || n > s.max_))
        return name + " must be between " + fmtNum(s.min_) + " and " + fmtNum(s.max_);
      return "";
    }
    case SettingType::Select: {
      str = v.is<const char*>() ? String(v.as<const char*>()) : v.is<double>() ? v.as<String>() : String();
      for (auto& o : s.options_) if (o.first == str) return "";
      return name + ": unknown option";
    }
    case SettingType::Color:
      if (!v.is<const char*>()) return name + " must be a color";
      str = v.as<const char*>();
      if (!isHexColor(str)) return name + " must be a color like #3d9fb0";
      str.toLowerCase();
      return "";
    default:  // Text, Password
      if (!v.is<const char*>()) return name + " must be text";
      str = v.as<const char*>();
      if (str.length() > s.maxLen_) return name + " is longer than " + String(s.maxLen_) + " characters";
      return "";
  }
}

bool ESP32WebDash::apply(Setting& s, double num, const String& str) {
  if (isNumeric(s.type_)) {
    if (s.num_ == num) return false;
    s.num_ = num;
  } else {
    if (s.str_ == str) return false;
    s.str_ = str;
  }
  return true;
}

void ESP32WebDash::changed(Setting& s) {
  Setting* sp = &s;
  // NVS writes and app callbacks run on the worker, never in the caller's task.
  if (!started_ || !defer([this, sp] { persist(*sp); if (sp->onChange_) sp->onChange_(*sp); })) {
    persist(s);
    if (s.onChange_) s.onChange_(s);
  }
  if (started_) {
    JsonDocument doc;
    { Lock l(*this); settingJson(s, doc["settings"].to<JsonObject>(), false); }
    broadcast(doc);
  }
}

// ---------------------------------------------------------------- JSON

void ESP32WebDash::settingJson(const Setting& s, JsonObject out, bool secrets) const {
  switch (s.type_) {
    case SettingType::Bool: out[s.id_] = s.num_ != 0; break;
    case SettingType::Int:
    case SettingType::Slider: out[s.id_] = (long)lround(s.num_); break;
    case SettingType::Float: out[s.id_] = s.num_; break;
    case SettingType::Password:
      out[s.id_] = secrets ? s.str_ : (s.str_.isEmpty() ? String() : String(PASSWORD_MASK));
      break;
    default: out[s.id_] = s.str_;
  }
}

void ESP32WebDash::settingsJson(JsonObject out, bool secrets) const {
  Lock l(*this);
  for (auto& s : settings_) settingJson(*s, out, secrets);
}

void ESP32WebDash::schemaJson(JsonDocument& doc) const {
  static const char* SET_TYPES[] = {"bool", "int", "float", "range", "string", "password", "select", "color"};
  static const char* VAL_TYPES[] = {"number", "text", "bool", "led", "pwm", "uptime"};
  static const char* STYLES[] = {nullptr, "pri", "danger"};
  Lock l(*this);
  doc["device"]["name"] = name_;
  doc["device"]["firmware"] = firmware_;
  if (logo_.length()) doc["device"]["logo"] = logo_;

  JsonArray vals = doc["values"].to<JsonArray>();
  for (auto& v : values_) {
    JsonObject o = vals.add<JsonObject>();
    o["id"] = v->id_; o["label"] = v->label_;
    if (v->type_ != ValueType::Number) o["type"] = VAL_TYPES[(int)v->type_];
    if (v->unit_.length()) o["unit"] = v->unit_;
    if (v->icon_.length()) o["icon"] = v->icon_;
    if (v->color_.length()) o["color"] = v->color_;
    if (v->decimals_ != 255) o["decimals"] = v->decimals_;
    if (v->hasRange_) { o["min"] = v->min_; o["max"] = v->max_; }
  }

  JsonArray acts = doc["actions"].to<JsonArray>();
  for (auto& a : actions_) {
    JsonObject o = acts.add<JsonObject>();
    o["id"] = a->id_; o["label"] = a->label_;
    if (a->help_.length()) o["help"] = a->help_;
    if (a->button_.length()) o["button"] = a->button_;
    if (a->icon_.length()) o["icon"] = a->icon_;
    if (a->group_.length()) o["group"] = a->group_;
    if (STYLES[(int)a->style_]) o["style"] = STYLES[(int)a->style_];
    if (a->confirm_) o["confirm"] = true;
  }

  JsonArray groups = doc["settings"].to<JsonArray>();
  for (auto& g : groups_) {
    JsonObject go;
    for (auto& s : settings_) {
      if (s->group_ != g.title) continue;
      if (go.isNull()) {
        go = groups.add<JsonObject>();
        go["title"] = g.title;
        if (g.help.length()) go["help"] = g.help;
        go["items"].to<JsonArray>();
      }
      JsonObject o = go["items"].as<JsonArray>().add<JsonObject>();
      o["id"] = s->id_; o["type"] = SET_TYPES[(int)s->type_]; o["label"] = s->label_;
      if (s->help_.length()) o["help"] = s->help_;
      if (s->unit_.length()) o["unit"] = s->unit_;
      if (s->placeholder_.length()) o["placeholder"] = s->placeholder_;
      if (s->reboot_) o["reboot"] = true;
      if (isNumeric(s->type_) && s->type_ != SettingType::Bool && s->min_ != s->max_) { o["min"] = s->min_; o["max"] = s->max_; }
      if (s->step_ > 0) o["step"] = s->step_;
      if (s->type_ == SettingType::Text || s->type_ == SettingType::Password) o["max"] = s->maxLen_;
      if (s->type_ == SettingType::Text && s->rows_) o["rows"] = s->rows_;
      if (!s->options_.empty()) {
        JsonArray opts = o["options"].to<JsonArray>();
        for (auto& op : s->options_) { JsonArray p = opts.add<JsonArray>(); p.add(op.first); p.add(op.second); }
      }
    }
  }
}

void ESP32WebDash::valuesSnapshot(JsonDocument& doc, bool onlyDirty) {
  Lock l(*this);
  JsonObject out = doc["values"].to<JsonObject>();
  for (auto& v : values_) {
    if (onlyDirty && !v->dirty_) continue;
    v->toJson(out);
    v->dirty_ = false;
  }
  if (onlyDirty) anyDirty_ = false;
}

}  // namespace webdash
