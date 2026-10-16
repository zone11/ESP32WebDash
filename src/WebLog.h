#pragma once
// Line-based log: tees to Serial and keeps a ring buffer for the web console.
#include <Arduino.h>
#include <ArduinoJson.h>
#include <deque>

namespace webdash {

class WebLog : public Print {
 public:
  explicit WebLog(uint16_t maxLines);

  size_t write(uint8_t c) override { return write(&c, 1); }
  size_t write(const uint8_t* buf, size_t len) override;  // info level, tees to Serial

  // Adds one complete line. toSerial=false for text that already went to the UART.
  void line(char level, const char* text, bool toSerial = true);

  // Appends lines with sequence number > since to arr; returns the newest sequence number.
  uint32_t collect(uint32_t since, JsonArray arr);
  uint32_t seq() const { return seq_; }

  // Captures ESP-IDF log output (ESP_LOGx) into this log.
  void hookIdfLog();

 private:
  struct Entry { uint32_t seq; String text; };
  void push(char level, const char* text);

  void* mutex_;
  std::deque<Entry> lines_;
  uint16_t max_;
  uint32_t seq_ = 0;
  String partial_;
};

}  // namespace webdash
