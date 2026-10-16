// Firmware and filesystem updates via HTTP upload (multipart, field "file").
#include "ESP32WebDash.h"

#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <Update.h>

namespace webdash {

static constexpr uint8_t ESP_IMAGE_MAGIC = 0xE9;

void ESP32WebDash::otaRoutes() {
  for (int kind = 0; kind < 2; kind++) {
    const bool fs = kind == 1;
    server_->on(fs ? "/api/ota/fs" : "/api/ota/firmware", HTTP_POST,
        [this](AsyncWebServerRequest* req) {
          if (otaError_.length() || Update.hasError()) {
            String err = otaError_.length() ? otaError_ : String(Update.errorString());
            log('E', "update failed: %s", err.c_str());
            otaBusy_ = false;
            return sendError(req, 400, err);
          }
          log('I', "update complete, restarting");
          JsonDocument doc; doc["ok"] = true; sendJson(req, doc);
          defer([] { delay(800); ESP.restart(); });
        },
        [this, fs](AsyncWebServerRequest* req, const String& name, size_t index, uint8_t* data, size_t len, bool final) {
          if (index == 0) {
            otaError_ = "";
            if (otaBusy_) { otaError_ = "Another update is running"; return; }
            if (len && (data[0] == ESP_IMAGE_MAGIC) == fs) {
              otaError_ = fs ? "This is a firmware image, not a filesystem image" : "Not a firmware image";
              return;
            }
            otaBusy_ = true;
            if (fs) LittleFS.end();
            log('I', "update started: %s (%s)", name.c_str(), fs ? "filesystem" : "firmware");
            if (!Update.begin(UPDATE_SIZE_UNKNOWN, fs ? U_SPIFFS : U_FLASH)) { otaError_ = Update.errorString(); return; }
          }
          if (otaError_.length()) return;
          if (Update.write(data, len) != len) { otaError_ = Update.errorString(); Update.abort(); return; }
          if (final && !Update.end(true)) otaError_ = Update.errorString();
        });
  }
}

}  // namespace webdash
