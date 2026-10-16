#include "WebLog.h"

#include <esp_log.h>
#include <freertos/semphr.h>

namespace webdash {

static WebLog* idfTarget = nullptr;
static vprintf_like_t idfPrev = nullptr;

WebLog::WebLog(uint16_t maxLines) : max_(maxLines) { mutex_ = xSemaphoreCreateRecursiveMutex(); }

void WebLog::push(char level, const char* text) {
  char head[16];
  uint32_t ms = millis();
  snprintf(head, sizeof head, "[%6lu.%03lu] %c ", (unsigned long)(ms / 1000), (unsigned long)(ms % 1000), level);
  xSemaphoreTakeRecursive((SemaphoreHandle_t)mutex_, portMAX_DELAY);
  lines_.push_back({++seq_, String(head) + text});
  while (lines_.size() > max_) lines_.pop_front();
  xSemaphoreGiveRecursive((SemaphoreHandle_t)mutex_);
}

// Serial output always uses CRLF line endings, as serial terminals expect.
void WebLog::line(char level, const char* text, bool toSerial) {
  if (toSerial) Serial.printf("%c %s\r\n", level, text);
  push(level, text);
}

size_t WebLog::write(const uint8_t* buf, size_t len) {
  xSemaphoreTakeRecursive((SemaphoreHandle_t)mutex_, portMAX_DELAY);
  size_t start = 0;
  for (size_t i = 0; i < len; i++) {
    char c = (char)buf[i];
    if (c == '\r' || c == '\n') {
      Serial.write(buf + start, i - start);  // text before the line break, without it
      start = i + 1;
    }
    if (c == '\n') { Serial.write("\r\n"); push('I', partial_.c_str()); partial_ = ""; }
    else if (c != '\r') partial_ += c;
  }
  Serial.write(buf + start, len - start);
  xSemaphoreGiveRecursive((SemaphoreHandle_t)mutex_);
  return len;
}

uint32_t WebLog::collect(uint32_t since, JsonArray arr) {
  xSemaphoreTakeRecursive((SemaphoreHandle_t)mutex_, portMAX_DELAY);
  for (auto& e : lines_) if (e.seq > since) arr.add(e.text);
  uint32_t s = seq_;
  xSemaphoreGiveRecursive((SemaphoreHandle_t)mutex_);
  return s;
}

// IDF lines look like "\e[0;31mE (1234) tag: message\e[0m\n"; keep level, tag and message.
static int idfVprintf(const char* fmt, va_list ap) {
  va_list copy;
  va_copy(copy, ap);
  char buf[200];
  vsnprintf(buf, sizeof buf, fmt, copy);
  va_end(copy);
  if (idfTarget) {
    char clean[200];
    size_t n = 0;
    for (const char* p = buf; *p && n < sizeof clean - 1; p++) {
      if (*p == '\x1b') { while (*p && *p != 'm') p++; if (!*p) break; continue; }
      if (*p != '\n' && *p != '\r') clean[n++] = *p;
    }
    clean[n] = 0;
    const char* text = clean;
    char level = 'I';
    if (strchr("EWIDV", clean[0]) && clean[1] == ' ') {
      level = clean[0];
      text = clean + 2;
      if (*text == '(') { const char* e = strchr(text, ')'); if (e) text = e + 1 + (e[1] == ' '); }
    }
    if (*text) idfTarget->line(level, text, false);
  }
  return idfPrev ? idfPrev(fmt, ap) : vprintf(fmt, ap);
}

void WebLog::hookIdfLog() {
  idfTarget = this;
  idfPrev = esp_log_set_vprintf(idfVprintf);
}

}  // namespace webdash
