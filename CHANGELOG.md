# Changelog

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
