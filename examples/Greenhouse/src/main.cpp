// Demo application: a simulated greenhouse controller using ESP32WebDash.
// The control logic runs in its own task; the web UI never blocks it.
#include <Arduino.h>
#include <ESP32WebDash.h>
// Optional first-boot WiFi credentials (copy include/secrets.example.h to include/secrets.h).
// Without them the device starts the setup hotspot "greenhouse-node" right away.
#if __has_include("secrets.h")
#include "secrets.h"
#else
#define WIFI_SSID ""
#define WIFI_PASS ""
#endif

ESP32WebDash ui("Greenhouse Node");

// settings
Setting* autoWater;
Setting* threshold;
Setting* pumpSeconds;
Setting* ledColor;
Setting* ledBright;
Setting* interval;

// live values
Value* temp;
Value* hum;
Value* pump;
Value* statusLed;
Value* dryAlarm;
Value* fan;
Value* heater;
Value* loops;
Value* uptime;

static volatile uint32_t pumpUntil = 0;

static void controlTask(void*) {
  float t = 22.0f, h = 55.0f;
  uint32_t count = 0;
  TickType_t last = xTaskGetTickCount();
  for (;;) {
    // pretend to read sensors
    t += (random(-10, 11)) / 100.0f;
    h += (random(-20, 21)) / 100.0f - (millis() < pumpUntil ? -0.4f : 0.05f);
    h = constrain(h, 20.0f, 95.0f);

    bool pumping = millis() < pumpUntil;
    if (autoWater->asBool() && !pumping && h < threshold->asInt()) {
      pumpUntil = millis() + pumpSeconds->asInt() * 1000UL;
      ui.log('I', "humidity %.1f%% below threshold, pump on", h);
    }

    temp->set(t);
    hum->set(h);
    pump->set(pumping);
    statusLed->setLed(true, ledColor->asString().c_str(), ledBright->asInt() * 100 / 255);
    dryAlarm->set(h < 30.0f);
    fan->set((uint32_t)constrain((t - 20.0f) * 200.0f, 0.0f, 1023.0f));
    heater->set(t < 21.0f ? 60 : 0);
    loops->set(++count);
    uptime->set((uint32_t)(millis() / 1000));

    vTaskDelayUntil(&last, pdMS_TO_TICKS((uint32_t)(interval->asFloat() * 1000)));
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);

  ui.firmware("0.1.0").wifiDefaults(WIFI_SSID, WIFI_PASS);

  ui.group("Watering", "Automatic watering by soil humidity.");
  autoWater = &ui.addBool("auto", "Automatic watering", true).help("Waters when humidity drops below the threshold.");
  threshold = &ui.addSlider("threshold", "Humidity threshold", 45, 10, 90).unit("%");
  pumpSeconds = &ui.addInt("duration", "Pump duration", 30, 1, 600).unit("s").help("How long one watering run lasts.");
  ui.addSelect("mode", "Schedule", "day", {{"always", "Any time"}, {"day", "Daytime only"}, {"night", "Night only"}});

  ui.group("Display");
  ledColor = &ui.addColor("led_color", "Status LED color", "#3fb950");
  ledBright = &ui.addSlider("led_bright", "Status LED brightness", 180, 0, 255);
  interval = &ui.addFloat("interval", "Sensor interval", 1.0, 0.2, 60).step(0.1).unit("s")
                  .onChange([](Setting& s) { ui.log('I', "sensor interval now %.1f s", s.asFloat()); });

  temp = &ui.addValue("temp", "Temperature").unit("°C").decimals(1).icon("temp").range(0, 40);
  hum = &ui.addValue("hum", "Humidity").unit("%").decimals(0).icon("hum").range(0, 100);
  pump = &ui.addLed("pump", "Pump").icon("bolt");
  statusLed = &ui.addLed("status", "Status LED").icon("led");
  dryAlarm = &ui.addLed("alarm", "Dry alarm").color("#e5484d").icon("bell");
  fan = &ui.addPwm("fan", "Fan", 1023).icon("fan");
  heater = &ui.addPwm("heater", "Heater").icon("temp");
  loops = &ui.addValue("loops", "Control loops").icon("refresh");
  uptime = &ui.addUptime("uptime", "Uptime").icon("clock");

  ui.addAction("pump", "Run pump").help("Waters for the configured duration.").button("Start").icon("play").primary()
      .run([] { pumpUntil = millis() + pumpSeconds->asInt() * 1000UL; ui.notify("Pump started"); });
  ui.addAction("calib", "Calibrate sensor").help("Takes about 3 seconds.").button("Calibrate").icon("gauge")
      .run([] { delay(3000); ui.notify("Calibration finished"); });
  ui.addAction("reboot", "Restart").help("Settings are kept.").button("Restart").icon("refresh").confirm().group("Device")
      .run([] { ui.notify("Restarting…"); delay(500); ESP.restart(); });

  ui.begin();

  xTaskCreate(controlTask, "control", 4096, nullptr, 2, nullptr);
}

void loop() {
  ui.loop();
  delay(50);
}
