# Changelog

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
