# Changelog

## 0.1.9

- LEDs can blink: `setLed(on, color, dim, blink)`. The browser blinks the LED 500 ms on and 500 ms off and shows
  `blinking` instead of `on`. With reduced motion the LED stays lit and the text still says `blinking`. In the
  JSON it is `{on, color, dim, blink}`.
- The LED tile no longer shows the color as `#rrggbb` text under the value.

## 0.1.8

- The System page shows PSRAM only when the board has it and the build enables it, as free / total. It used to
  read "0 KB free" without PSRAM. `/api/system` has the new field `psramTotal` (0 without PSRAM).

## 0.1.7

- Dashboard tiles keep the order they were added in (no more reordering to fill gaps). Empty cells at the end of a
  row are filled, so the lines between rows are complete.
- `.wrap()` text takes the rest of its row instead of always a new full row.
- The dashboard status box shows the library version ("WebDash", also `webdash` in `/api/system`,
  `ESP32WEBDASH_VERSION` in code).
- Fix: the setup-mode banner stayed on the page after the device had reconnected to WiFi; it now follows the
  device state.

## 0.1.6

- Dashboard layout for values: `.group(title)` (titled strips), `.span(cols)` (2–4 columns, 0 = full row),
  `.size(ValueSize::…)` and `.wrap()` for longer text.
- Fix: long text values no longer overflow into the neighbouring tiles; they end in an ellipsis.
- **Look:** one sans font throughout (monospace only in the log console and multi-line fields), a four-step type
  scale, plain dimmed box titles without the accent square, tabs aligned with the boxes, no placeholder logo
  when `logo()` isn't set. Ungrouped values now sit under a "Values" title.

## 0.1.5

- **Behaviour change:** IPv6 is now a build option, off by default. Without `-DWEBDASH_IPV6=1` there is no
  `ipv6` setting (`ui.setting("ipv6")` returns `nullptr`) and IPv6 is never enabled. Projects that use the
  setting must add the flag. A stored `ipv6` value is ignored, and importing it is reported as skipped.
- Fix: with IPv6 on, DNS servers from Router Advertisements no longer replace the DHCP DNS server, which
  broke all name resolution when those resolvers were unreachable. The DHCP server stays first.

## 0.1.4

- `showWhen(settingId, value)`: show a settings group only while another setting has a given value.
- `addSelect()` also takes the options as a `std::vector` (for lists built at runtime).

## 0.1.3

- New icons: `wave`, `antenna`, `sliders`, `plug`, `server`.

## 0.1.2

- Multi-line text settings: `addText(...).multiline(rows)` shows a full-width text box that keeps line
  breaks, e.g. for pasting a PEM certificate.

## 0.1.1

- Optional IPv6 (setting `ipv6` in the Network group, off by default): link-local and global addresses are
  logged and shown on the dashboard and System page.

## 0.1.0

First release: schema-driven web UI with settings in NVS (JSON export/import), live values (number, text,
on/off, LED, PWM, uptime), actions, OTA for firmware and UI, setup hotspot with captive portal, optional login,
log console, light/dark theme.
