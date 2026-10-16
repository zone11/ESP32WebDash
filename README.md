# ESP32WebDash

A reusable web UI for ESP32 projects (PlatformIO + Arduino). The firmware declares settings, live values and
actions in a few lines of C++, and a generic web UI renders them, with no HTML per project.

- **Settings** in NVS (survive firmware and UI updates), validated on the device, JSON **export/import** for rollouts
- **Live values** pushed over WebSocket: numbers with meters, text, on/off, **LED** (color + dim), **PWM** (%)
- **Actions** (buttons) run on a worker task and never block the web server or your code
- **OTA** for firmware and web UI, with a built-in recovery page at `/update`
- **Setup hotspot** with captive portal when WiFi is unreachable, mDNS (`<hostname>.local`), optional IPv6
- **Optional login** (HTTP Basic, user `admin`) and a **live log console**
- The UI (≈12 KB gzipped, no external dependencies) lives on LittleFS; restyle it with CSS tokens

Tested with pioarduino (Arduino core 3.3) on ESP32-S3; it also builds with the official
`espressif32@6` platform (Arduino core 2.0) and on the classic ESP32.

## Add it to a project

**1. `platformio.ini`**

```ini
[env:myboard]
platform = https://github.com/pioarduino/platform-espressif32/releases/download/55.03.312-1/platform-espressif32.zip
framework = arduino
board = esp32-s3-devkitc-1
board_build.filesystem = littlefs
board_build.partitions = partitions.csv        ; copy one from partitions/ (see below)
lib_deps =
    https://github.com/zone11/ESP32WebDash.git#v0.1.2
```

**2. Partition table.** OTA needs two app slots plus a data partition for the UI. Copy the matching file from
[`partitions/`](partitions/) into the project as `partitions.csv` (it has to be a project file, because PlatformIO
reads it before libraries are installed):

| File | Flash | App slots | LittleFS |
|---|---|---|---|
| `esp32-4MB.csv` | 4 MB | 2 × 1.5 MB | 832 KB |
| `esp32-8MB.csv` | 8 MB | 2 × 3 MB | 1.8 MB |
| `esp32-16MB.csv` | 16 MB | 2 × 3 MB | 9.8 MB |

For 8/16 MB boards also set `board_build.flash_size` / `board_upload.flash_size`.

**3. Code**

```cpp
#include <ESP32WebDash.h>

ESP32WebDash ui("Greenhouse Node");
Setting* threshold;
Value *temp, *led, *fan;

void setup() {
  ui.firmware("1.0.0").wifiDefaults("my-ssid", "my-password");  // first boot only, then stored in NVS

  ui.group("Watering", "Automatic watering by soil humidity.");
  threshold = &ui.addSlider("threshold", "Humidity threshold", 45, 10, 90).unit("%");
  ui.addSelect("mode", "Schedule", "day", {{"always", "Any time"}, {"day", "Daytime only"}});

  temp = &ui.addValue("temp", "Temperature").unit("°C").decimals(1).range(0, 40).icon("temp");
  led  = &ui.addLed("status", "Status LED");
  fan  = &ui.addPwm("fan", "Fan", 1023);

  ui.addAction("pump", "Run pump").button("Start").primary().run([] { /* ... */ });
  ui.begin();  // returns immediately
}

// anywhere, from any task:
temp->set(23.4);
led->setLed(true, "#3fb950", 70);   // on, color, dim %
fan->set(512);                      // raw duty → shown as 50 %
if (threshold->asInt() > 50) { /* ... */ }
```

**4. Flash**

```sh
pio run -t upload      # firmware
pio run -t uploadfs    # web UI
```

The library's build script copies the UI into the project's `data/` folder (gzipped) on every build, so
`uploadfs` always ships the UI matching the library version. Add `data/` to `.gitignore` if you have no own
files there. Files you put in `data/` yourself are left alone.

Without WiFi credentials (or when the network is unreachable for 20 s) the device opens the hotspot
`<hostname>`. Join it and the setup page opens.

A complete example is in [`examples/Greenhouse`](examples/Greenhouse).

## API

### Settings

| Method | Control | Notes |
|---|---|---|
| `addBool(id, label, def)` | switch | |
| `addInt(id, label, def, min, max)` | number | `.unit()` `.step()` |
| `addFloat(id, label, def, min, max)` | number | `.step(0.1)` |
| `addSlider(id, label, def, min, max)` | range slider | |
| `addText(id, label, def)` | text | `.maxLength(n)` (default 64) `.placeholder()` `.multiline(rows)` (text box that keeps line breaks; NVS stores up to ~4000 bytes) |
| `addPassword(id, label, def)` | password | shown masked, never sent to the browser; included in export |
| `addSelect(id, label, def, {{value, label}, ...})` | dropdown | |
| `addColor(id, label, "#rrggbb")` | color picker | |

`ui.group(title, help)` starts a section; settings added after it belong to it.
Modifiers: `.help(text)`, `.requiresRestart()`, `.onChange([](Setting& s) { ... })`.
Read with `asBool()`, `asInt()`, `asFloat()`, `asString()`; write with `set(...)`.
IDs are NVS keys: at most 15 characters, unique. Values are loaded when the setting is added, so they are
usable in `setup()` right away. `onChange` runs on the worker task after a change from the UI, an import, a
reset or `set()`.

The library adds a **Network** group (hostname, WiFi, IPv6, setup hotspot) and a **Security** group (login).

### Values

| Method | Shown as | Set with |
|---|---|---|
| `addValue(id, label)` | number + unit, meter with `.range(min, max)` | `set(double)` |
| `addLabel(id, label)` | text | `set("text")` |
| `addState(id, label)` | on / off | `set(bool)` |
| `addLed(id, label)` | LED; `.color("#hex")` sets the default color | `set(bool)` or `setLed(on, color, dim)` |
| `addPwm(id, label, maxRaw)` | duty in % with meter (+ raw/max) | `set(raw)` |
| `addUptime(id, label)` | `3d 4h 17m` | `set(seconds)` |

Modifiers: `.unit()`, `.icon()`, `.decimals()`. Setters are thread-safe and cheap; only changed values are
pushed to the browser (default every second).

Icons: `temp`, `hum`, `wifi`, `chip`, `clock`, `bolt`, `gauge`, `power`, `play`, `refresh`, `fan`, `led`,
`bell`, `dot`, `download`, `upload`, `copy`, `trash`.

### Actions

`addAction(id, label)` with `.help()`, `.button("Start")`, `.icon()`, `.primary()` / `.danger()`, `.confirm()`
(two presses), `.group("Device")`, `.run(fn)`. Actions run one after another on the worker task, which also
saves settings; start your own task for anything that takes longer than a few seconds.
`ui.notify("text", isError)` shows a message in all open browsers.

### Logging

`ui.log('I', "pump on for %d s", n)` (levels E, W, I, D) or `ui.log().println(...)` writes to Serial (CRLF)
and the web console. ESP-IDF `ESP_LOGx` output is captured as well.

### Options

```cpp
webdash::Config cfg;
cfg.port = 80;
cfg.pushIntervalMs = 1000;   // value updates to the browser
cfg.apFallbackMs = 20000;    // open the setup hotspot after this long without WiFi
cfg.logLines = 200;          // log history for the web console
cfg.workerStack = 6144;      // stack of the worker task (onChange, actions)
ui.config(cfg);              // before begin()
```

## HTTP API

| Method | Path | |
|---|---|---|
| GET | `/api/schema` | UI description (values, actions, settings groups) |
| GET / PATCH | `/api/settings` | current values / change several (all-or-nothing, 400 with message on error) |
| GET | `/api/settings/export` | all settings incl. passwords |
| POST | `/api/settings/import` | `{"settings":{...}}`; unknown or invalid keys are skipped and reported |
| POST | `/api/settings/reset` | defaults (WiFi included) |
| POST | `/api/action/{id}` | queue an action (202) |
| GET | `/api/system` | firmware, chip, network, memory, filesystem |
| POST | `/api/system/restart` | |
| POST | `/api/ota/firmware`, `/api/ota/fs` | multipart upload of `firmware.bin` / `littlefs.bin` |
| WS | `/ws` | `{"values":{...}}`, `{"log":[...]}`, `{"settings":{...}}`, `{"toast":"..."}` |

Rolling one configuration out to many devices:
`curl -X POST -H 'Content-Type: application/json' --data @settings.json http://<device>/api/settings/import`.

## Customizing the UI

Put files into a `web/` folder in your project; they are merged over the library's UI when building:

- `web/theme.css`: override design tokens, e.g. `:root{--acc:#c2410c}` `:root[data-theme=light]{--acc:#9a3412}`
- `web/logo.svg` + `ui.logo("logo.svg")`: your logo in the top bar
- any other file (images, extra pages) is added; a file with the same name as a library file replaces it

The main tokens (defined in `web/core.css`, dark values on `:root`, light ones on `:root[data-theme=light]`):

| Token | Role |
|---|---|
| `--acc` | accent color (markers, focus, sliders, active tab) |
| `--bg` / `--card` / `--card2` | page / box fill / hover surface |
| `--fg` / `--dim` / `--faint` | text / secondary text / inactive values |
| `--line` / `--line2` | dividers / row separators |
| `--rule` | 1px line on top of every box |
| `--pri` / `--onpri` | primary buttons and active switches / text on them |
| `--meter` | value bars and progress |
| `--warn` / `--err` | warnings / errors |
| `--r` / `--rs` | box / control corner radius |
| `--font` / `--mono` | UI font / numbers and logs |

## Security notes

The login uses HTTP Basic auth over plain HTTP: it keeps casual users out of a trusted network, nothing more.
The setup hotspot is open unless a hotspot password is set. Settings exports contain passwords in plain text.
IPv6 is off by default: with it on, the device gets a global address (SLAAC) and is reachable from outside
the local network unless the router's firewall blocks incoming connections. The setup hotspot stays IPv4-only.
