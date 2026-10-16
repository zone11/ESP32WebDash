'use strict';
// ESP32WebDash generic UI. Renders everything from GET /api/schema.
(() => {
const $ = (s, r = document) => r.querySelector(s);

function el(tag, attrs, ...kids) {
  const e = document.createElement(tag);
  for (const [k, v] of Object.entries(attrs || {})) {
    if (v == null || v === false) continue;
    if (k.startsWith('on')) e.addEventListener(k.slice(2), v);
    else if (k === 'html') e.innerHTML = v;
    else e.setAttribute(k, v === true ? '' : v);
  }
  for (const k of kids.flat()) if (k != null && k !== false) e.append(k.nodeType ? k : String(k));
  return e;
}
const SVGNS = 'http://www.w3.org/2000/svg';
function icon(name, cls = 'ic') {
  const s = document.createElementNS(SVGNS, 'svg');
  s.setAttribute('class', cls); s.setAttribute('aria-hidden', 'true');
  const u = document.createElementNS(SVGNS, 'use'); u.setAttribute('href', '#i-' + name);
  s.append(u); return s;
}

const ICONS = {
  logo: '<rect x="3" y="3" width="18" height="18" rx="4"/><path d="M8 12h8M12 8v8"/>',
  sun: '<circle cx="12" cy="12" r="4"/><path d="M12 3v2M12 19v2M3 12h2M19 12h2M5.6 5.6 7 7M17 17l1.4 1.4M18.4 5.6 17 7M7 17l-1.4 1.4"/>',
  moon: '<path d="M20 15.5A8.5 8.5 0 0 1 8.5 4a8.5 8.5 0 1 0 11.5 11.5z"/>',
  temp: '<path d="M10 13.5V5a2 2 0 1 1 4 0v8.5a4 4 0 1 1-4 0z"/>',
  hum: '<path d="M12 3l5 6.5a6 6 0 1 1-10 0z"/>',
  wifi: '<path d="M5 10.5a10 10 0 0 1 14 0M8.5 14a6 6 0 0 1 7 0"/><path d="M12 17.5h.01"/>',
  chip: '<rect x="6" y="6" width="12" height="12" rx="2"/><path d="M9 2v4M15 2v4M9 18v4M15 18v4M2 9h4M2 15h4M18 9h4M18 15h4"/>',
  clock: '<circle cx="12" cy="12" r="9"/><path d="M12 7v5l3 2"/>',
  bolt: '<path d="M13 2 4 14h7l-1 8 9-12h-7z"/>',
  gauge: '<path d="M4 15a8 8 0 1 1 16 0"/><path d="M12 15l4-5"/>',
  power: '<path d="M12 3v8"/><path d="M6.3 7a8 8 0 1 0 11.4 0"/>',
  play: '<path d="M7 5v14l12-7z"/>',
  refresh: '<path d="M20 11a8 8 0 0 0-14.3-4.9L4 8"/><path d="M4 4v4h4"/><path d="M4 13a8 8 0 0 0 14.3 4.9L20 16"/><path d="M20 20v-4h-4"/>',
  download: '<path d="M12 4v11"/><path d="M8 11l4 4 4-4"/><path d="M5 19h14"/>',
  upload: '<path d="M12 15V4"/><path d="M8 8l4-4 4 4"/><path d="M5 19h14"/>',
  copy: '<rect x="9" y="9" width="11" height="11" rx="2"/><path d="M15 5.5A1.5 1.5 0 0 0 13.5 4h-8A1.5 1.5 0 0 0 4 5.5v8A1.5 1.5 0 0 0 5.5 15"/>',
  trash: '<path d="M4 7h16M10 4h4M6 7l1 13h10l1-13"/>',
  dot: '<circle cx="12" cy="12" r="3"/>',
  bell: '<path d="M6 16V11a6 6 0 0 1 12 0v5l2 2H4z"/><path d="M10 21h4"/>',
  fan: '<circle cx="12" cy="12" r="1.5"/><path d="M12 10.5c0-4 1-7 4-7s3 4-2.5 7.2M13.5 12c4 0 7 1 7 4s-4 3-7.2-2.5M12 13.5c0 4-1 7-4 7s-3-4 2.5-7.2M10.5 12c-4 0-7-1-7-4s4-3 7.2 2.5"/>',
  led: '<circle cx="12" cy="10" r="5"/><path d="M10 15v6M14 15v6"/>',
  wave: '<path d="M2 12c1.5-4 3.5-4 5 0s3.5 4 5 0 3.5-4 5 0 3.5 4 5 0"/>',
  antenna: '<circle cx="12" cy="8" r="1.5"/><path d="M12 10v11M9 21h6"/><path d="M8 4.5a5 5 0 0 0 0 7M16 4.5a5 5 0 0 1 0 7M5 2a9 9 0 0 0 0 12M19 2a9 9 0 0 1 0 12"/>',
  sliders: '<path d="M5 4v16M12 4v16M19 4v16"/><path d="M3 15h4M10 8h4M17 13h4"/>',
  plug: '<path d="M9 2v5M15 2v5"/><path d="M6 7h12v4a6 6 0 0 1-12 0z"/><path d="M12 17v5"/>',
  server: '<rect x="4" y="3" width="16" height="7" rx="1.5"/><rect x="4" y="14" width="16" height="7" rx="1.5"/><path d="M8 6.5h.01M8 17.5h.01"/>',
};
function sprite() {
  const syms = Object.entries(ICONS).map(([k, p]) => `<symbol id="i-${k}" viewBox="0 0 24 24">${p}</symbol>`).join('');
  document.body.insertAdjacentHTML('afterbegin',
    `<svg width="0" height="0" style="position:absolute" aria-hidden="true"><defs>${syms}</defs></svg>`);
}

// ---------- transport ----------
// window.WEBDASH_API can replace this (e.g. a mock API for previews or tests).
const api = window.WEBDASH_API || {
  async req(method, path, body) {
    const opt = { method, headers: {} };
    if (body !== undefined) {
      if (body instanceof FormData || body instanceof Blob) opt.body = body;
      else { opt.body = JSON.stringify(body); opt.headers['Content-Type'] = 'application/json'; }
    }
    const r = await fetch(path, opt);
    const ct = r.headers.get('content-type') || '';
    const data = ct.includes('json') ? await r.json() : await r.text();
    if (!r.ok) throw new Error((data && data.error) || r.status + ' ' + r.statusText);
    return data;
  },
  connect(onMsg, onState) {
    let ws, retry = 1000;
    const open = () => {
      ws = new WebSocket((location.protocol === 'https:' ? 'wss://' : 'ws://') + location.host + '/ws');
      ws.onopen = () => { retry = 1000; onState(true); };
      ws.onmessage = e => { try { onMsg(JSON.parse(e.data)); } catch (_) {} };
      ws.onclose = () => { onState(false); setTimeout(open, retry); retry = Math.min(retry * 2, 10000); };
    };
    open();
  },
};

// ---------- state ----------
const S = { schema: null, settings: {}, values: {}, online: false, log: [], logListeners: new Set(), valueListeners: new Set(), settingListeners: new Set() };

function toast(msg, err) {
  const t = el('div', { class: 'toast' + (err ? ' err' : '') }, msg);
  $('#toasts').append(t);
  setTimeout(() => t.remove(), err ? 6000 : 3000);
}
const toastErr = e => toast(e.message || String(e), true);

// two-press confirm for dangerous buttons
function armed(btn, fn) {
  let timer;
  btn.addEventListener('click', () => {
    if (!btn.classList.contains('armed')) {
      btn.classList.add('armed'); btn.dataset.label = btn.textContent; btn.lastChild.textContent = ' Confirm';
      timer = setTimeout(() => { btn.classList.remove('armed'); btn.lastChild.textContent = btn.dataset.label; }, 3000);
      return;
    }
    clearTimeout(timer); btn.classList.remove('armed'); btn.lastChild.textContent = btn.dataset.label; fn();
  });
  return btn;
}

const fmtUptime = s => {
  const d = Math.floor(s / 86400), h = Math.floor(s % 86400 / 3600), m = Math.floor(s % 3600 / 60);
  if (!d && !h) return m + 'm ' + Math.floor(s % 60) + 's';
  return (d ? d + 'd ' : '') + h + 'h ' + m + 'm';
};
function fmtValue(def, v) {
  if (v == null) return '–';
  if (def.type === 'uptime') return fmtUptime(v);
  if (typeof v === 'number' && def.decimals != null) return v.toFixed(def.decimals);
  if (typeof v === 'boolean') return v ? 'on' : 'off';
  return String(v);
}

// ---------- shell ----------
const PAGES = [
  { id: '', label: 'Dashboard', render: pageDashboard },
  { id: 'settings', label: 'Settings', render: pageSettings },
  { id: 'system', label: 'System', render: pageSystem },
  { id: 'log', label: 'Log', render: pageLog },
];

function shell() {
  sprite();
  document.body.append(
    el('a', { class: 'skip', href: '#view' }, 'Skip to content'),
    el('div', { class: 'topbar' },
      el('header', null,
        S.schema.device.logo ? el('img', { class: 'logo', src: S.schema.device.logo, alt: '' }) : icon('logo', 'ic logo'),
        el('h1', { id: 'brand' }, S.schema.device.name),
        el('span', { class: 'conn', id: 'conn', title: 'Connection' }),
        el('span', { class: 'grow' }),
        el('button', { class: 'icon ghost', id: 'themebtn', title: 'Theme', onclick: toggleTheme }, icon(themeIcon()))),
      el('nav', { id: 'nav' })),
    el('main', { id: 'view', tabindex: '-1' }),
    el('div', { id: 'toasts', 'aria-live': 'polite' }));
  document.title = S.schema.device.name;
}
const themeIcon = () => document.documentElement.dataset.theme === 'light' ? 'moon' : 'sun';
function toggleTheme() {
  const t = document.documentElement.dataset.theme === 'light' ? 'dark' : 'light';
  document.documentElement.dataset.theme = t;
  try { localStorage.webdashTheme = t; } catch (_) {}
  $('#themebtn').replaceChildren(icon(themeIcon()));
}

let cleanup = [];
function route() {
  cleanup.forEach(f => f()); cleanup = [];
  const id = location.hash.replace(/^#\/?/, '');
  const page = PAGES.find(p => p.id === id) || PAGES[0];
  $('#nav').replaceChildren(...PAGES.map(p =>
    el('a', { href: '#/' + p.id, class: p === page ? 'on' : null }, p.label)));
  const view = $('#view');
  view.replaceChildren();
  view.className = '';
  if (S.sys && S.sys.mode === 'ap')
    view.append(el('div', { class: 'banner' }, el('span', null,
      el('b', null, 'Setup mode. '), 'Enter your WiFi under Settings → Network, save, then restart the device.')));
  page.render(view);
}

function setOnline(on) {
  S.online = on;
  $('#conn').classList.toggle('off', !on);
  $('#brand').classList.toggle('off', !on);
  $('#view').classList.toggle('stale', !on);
}

// ---------- dashboard ----------
// LED value: true/false, a dim level 0..100 (0 = off), or {on, color, dim}.
function ledState(def, x) {
  if (x == null) return { on: false };
  if (typeof x === 'boolean') return { on: x, color: def.color };
  if (typeof x === 'number') return { on: x > 0, color: def.color, dim: x };
  return { on: x.on ?? (x.dim == null || x.dim > 0), color: x.color || def.color, dim: x.dim };
}
const pct = (x, min, max) => Math.max(0, Math.min(100, (x - min) * 100 / (max - min)));

function vitalTile(def) {
  const v = el('span', null, '–');
  const unit = el('u', null, def.unit || '');
  const valRow = el('div', { class: 'v' });
  const sub = el('div', { class: 's' });
  let bar = null, led = null;
  if (def.type === 'led') {
    led = el('i', { class: 'led', 'aria-hidden': 'true' });
    valRow.append(led, v, unit);
  } else {
    valRow.append(v, unit);
    if (def.type === 'pwm' || def.max != null) bar = el('i');
  }
  const tile = el('div', { class: 'vit' },
    el('div', { class: 'k' }, def.icon ? icon(def.icon) : null, el('span', null, def.label)),
    valRow, bar ? el('div', { class: 'b' }, bar) : null, sub);

  const upd = vals => {
    if (!(def.id in vals)) return;
    const x = vals[def.id];
    let p = null, text = fmtValue(def, x), u = def.unit || '', s = '', off = false;
    if (def.type === 'led') {
      const st = ledState(def, x);
      off = !st.on;
      text = st.on ? 'on' : 'off';
      u = st.on && st.dim != null ? Math.round(st.dim) + ' %' : '';
      led.classList.toggle('on', st.on);
      led.style.setProperty('--led', st.color || 'var(--acc)');
      led.style.setProperty('--lv', st.dim != null ? Math.max(0, Math.min(100, st.dim)) / 100 : 1);
      s = st.on && st.color ? st.color : '';
    } else if (def.type === 'pwm' && typeof x === 'number') {
      const max = def.max ?? 100;
      p = pct(x, def.min ?? 0, max);
      text = p.toFixed(def.decimals ?? 0); u = '%';
      off = x <= (def.min ?? 0);
      if (max !== 100) s = x + ' / ' + max;
    } else if (bar && typeof x === 'number') {
      p = pct(x, def.min ?? 0, def.max);
    } else if (typeof x === 'boolean') {
      off = !x;
    }
    v.textContent = text; unit.textContent = u;
    valRow.classList.toggle('off', off);
    if (bar && p != null) bar.style.width = p.toFixed(1) + '%';
    sub.textContent = s; sub.hidden = !s;
  };
  sub.hidden = true;
  upd(S.values);
  return { tile, upd };
}

function pageDashboard(view) {
  const sc = S.schema;
  const tiles = (sc.values || []).map(vitalTile);
  const onVals = vals => tiles.forEach(t => t.upd(vals));
  S.valueListeners.add(onVals); cleanup.push(() => S.valueListeners.delete(onVals));

  view.append(el('div', { class: 'vitals' }, tiles.map(t => t.tile)));

  const groups = {};
  for (const a of sc.actions || []) (groups[a.group || 'Actions'] ||= []).push(a);
  for (const [g, list] of Object.entries(groups)) {
    view.append(el('section', { class: 'card' },
      el('header', null, el('h2', null, g)),
      el('div', { class: 'actions' }, list.map(actionRow))));
  }

  const info = el('dl', { class: 'kv' });
  const sys = () => api.req('GET', '/api/system').then(s => info.replaceChildren(
    ...[['Firmware', s.firmware], ['IP', s.ip], ...ip6(s), ['WiFi', s.ssid + ' (' + s.rssi + ' dBm)'],
        ['Uptime', fmtUptime(s.uptime)], ['Free heap', Math.round(s.heap / 1024) + ' KB']]
      .flatMap(([k, v]) => [el('dt', null, k), el('dd', null, v)]))).catch(() => {});
  sys();
  const t = setInterval(sys, 10000); cleanup.push(() => clearInterval(t));
  view.append(el('section', { class: 'card' }, el('header', null, el('h2', null, 'Status')), info));
}

function actionRow(a) {
  const run = async () => {
    try { const r = await api.req('POST', '/api/action/' + encodeURIComponent(a.id)); toast(r && r.message || a.label + ': done'); }
    catch (e) { toastErr(e); }
  };
  const btn = el('button', { class: a.style || null }, icon(a.icon || 'play'), el('span', null, a.button || 'Run'));
  if (a.confirm) armed(btn, run); else btn.addEventListener('click', run);
  return el('div', { class: 'act' },
    el('div', null, el('b', null, a.label), a.help ? el('div', { class: 'help' }, a.help) : null), btn);
}

// ---------- settings ----------
function control(def, value, onChange) {
  const set = v => onChange(v);
  switch (def.type) {
    case 'bool': {
      const i = el('input', { type: 'checkbox', 'aria-label': def.label });
      i.checked = !!value; i.addEventListener('change', () => set(i.checked));
      return { node: el('label', { class: 'switch' }, i, el('i')), get: () => i.checked, put: v => i.checked = !!v };
    }
    case 'select': {
      const s = el('select', { 'aria-label': def.label }, def.options.map(([v, l]) => el('option', { value: v }, l)));
      s.value = String(value); s.addEventListener('change', () => set(s.value));
      return { node: s, put: v => s.value = String(v) };
    }
    case 'range': {
      const r = el('input', { type: 'range', min: def.min, max: def.max, step: def.step || 1, 'aria-label': def.label });
      const val = el('span', { class: 'val' });
      const show = () => val.textContent = r.value + (def.unit || '');
      r.value = value; show(); r.addEventListener('input', () => { show(); set(+r.value); });
      return { node: el('span', { class: 'row' }, r, val), put: v => { r.value = v; show(); } };
    }
    case 'int': case 'float': {
      const n = el('input', { type: 'number', min: def.min, max: def.max, step: def.step || (def.type === 'int' ? 1 : 'any'), 'aria-label': def.label });
      n.value = value; n.addEventListener('input', () => n.value !== '' && set(+n.value));
      return { node: def.unit ? el('span', { class: 'unitwrap' }, n, el('span', { class: 'unit' }, def.unit)) : n, put: v => n.value = v };
    }
    case 'color': {
      const c = el('input', { type: 'color', 'aria-label': def.label }); const hex = el('span');
      c.value = value; hex.textContent = value; c.addEventListener('input', () => { hex.textContent = c.value; set(c.value); });
      return { node: el('span', { class: 'cpick' }, c, hex), put: v => { c.value = v; hex.textContent = v; } };
    }
    default: {
      if (def.rows) {
        const a = el('textarea', { rows: def.rows, maxlength: def.max, placeholder: def.placeholder, spellcheck: 'false', 'aria-label': def.label });
        a.value = value ?? ''; a.addEventListener('input', () => set(a.value));
        return { node: a, put: v => a.value = v ?? '' };
      }
      const t = el('input', { type: def.type === 'password' ? 'password' : 'text', maxlength: def.max, placeholder: def.placeholder,
        autocomplete: def.type === 'password' ? 'new-password' : 'off', 'aria-label': def.label });
      t.value = value ?? ''; t.addEventListener('input', () => set(t.value));
      return { node: t, put: v => t.value = v ?? '' };
    }
  }
}

function pageSettings(view) {
  const groups = S.schema.settings || [];
  const dirty = new Map();
  const rows = new Map();
  const count = el('span', { class: 'n' });
  const savebar = el('div', { id: 'savebar', hidden: true },
    el('span', null, count, ' unsaved'), el('span', { class: 'grow' }),
    el('button', { class: 'ghost', onclick: discard }, 'Discard'),
    el('button', { class: 'pri', onclick: save }, 'Save'));
  const upd = () => { count.textContent = dirty.size; savebar.hidden = !dirty.size; applyWhen(); };
  // groups with "when" are shown only while that setting (incl. an unsaved change) has the given value
  const conditional = [];
  const applyWhen = () => conditional.forEach(({ when, sec, link }) => {
    const v = dirty.has(when.id) ? dirty.get(when.id) : S.settings[when.id];
    sec.hidden = link.hidden = String(v) !== String(when.value);
  });

  function discard() {
    for (const [id] of dirty) { const r = rows.get(id); r.ctl.put(S.settings[id]); r.row.classList.remove('dirty'); }
    dirty.clear(); upd();
  }
  async function save() {
    const patch = Object.fromEntries(dirty);
    try {
      const r = await api.req('PATCH', '/api/settings', patch);
      Object.assign(S.settings, patch);
      for (const id of dirty.keys()) rows.get(id).row.classList.remove('dirty');
      dirty.clear(); upd();
      toast(r && r.reboot ? 'Saved. Restart required to apply.' : 'Saved');
    } catch (e) { toastErr(e); }
  }

  const sub = el('nav', { class: 'subnav', 'aria-label': 'Sections' });
  const panel = el('div', { class: 'panel' });
  groups.forEach((g, gi) => {
    const sid = 'sec-' + gi;
    const link = el('a', { onclick: () => document.getElementById(sid).scrollIntoView({ behavior: 'smooth' }) }, g.title);
    sub.append(link);
    const list = el('div', { class: 'rows' });
    for (const def of g.items) {
      const row = el('div', { class: def.rows ? 'frow wide' : 'frow' });
      const ctl = control(def, S.settings[def.id], v => {
        if (v === S.settings[def.id]) dirty.delete(def.id); else dirty.set(def.id, v);
        row.classList.toggle('dirty', dirty.has(def.id)); upd();
      });
      row.append(
        el('div', { class: 'lab' }, el('b', null, def.label, def.reboot ? el('span', { class: 'tag' }, 'restart') : null),
          def.help ? el('div', { class: 'help' }, def.help) : null),
        el('div', { class: 'ctl' }, ctl.node));
      rows.set(def.id, { row, ctl });
      list.append(row);
    }
    const sec = el('section', { class: 'section', id: sid },
      el('header', null, el('h2', null, g.title), g.help ? el('div', { class: 'help' }, g.help) : null), list);
    if (g.when) conditional.push({ when: g.when, sec, link });
    panel.append(sec);
  });
  applyWhen();

  // highlight current section in subnav
  const io = new IntersectionObserver(es => es.forEach(e => {
    if (!e.isIntersecting) return;
    const i = [...panel.children].indexOf(e.target);
    [...sub.children].forEach((a, j) => a.classList.toggle('on', i === j));
  }), { rootMargin: '-120px 0px -60% 0px' });
  [...panel.children].forEach(s => io.observe(s));
  sub.firstChild && sub.firstChild.classList.add('on');

  const onSettings = changed => {
    for (const [id, v] of Object.entries(changed)) if (rows.has(id) && !dirty.has(id)) rows.get(id).ctl.put(v);
    applyWhen();
  };
  S.settingListeners.add(onSettings);
  cleanup.push(() => S.settingListeners.delete(onSettings));

  const beforeUnload = e => { if (dirty.size) { e.preventDefault(); e.returnValue = ''; } };
  addEventListener('beforeunload', beforeUnload);
  cleanup.push(() => { io.disconnect(); savebar.remove(); removeEventListener('beforeunload', beforeUnload); });

  view.append(el('div', { class: 'settings' }, sub, panel));
  document.body.append(savebar);
}

// ---------- system ----------
const ip6 = s => (s.ipv6 || []).map((a, i) => [i ? '' : 'IPv6', a]);

function pageSystem(view) {
  const info = el('dl', { class: 'kv' });
  api.req('GET', '/api/system').then(s => info.replaceChildren(
    ...[['Device', s.name], ['Firmware', s.firmware], ['Chip', s.chip], ['MAC', s.mac], ['IP', s.ip], ...ip6(s),
        ['WiFi', s.ssid + ' (' + s.rssi + ' dBm)'], ['Uptime', fmtUptime(s.uptime)],
        ['Heap', Math.round(s.heap / 1024) + ' KB free'], ['PSRAM', Math.round(s.psram / 1024) + ' KB free'],
        ['Filesystem', Math.round(s.fsUsed / 1024) + ' / ' + Math.round(s.fsTotal / 1024) + ' KB']]
      .flatMap(([k, v]) => [el('dt', null, k), el('dd', null, v ?? '–')]))).catch(toastErr);

  // backup
  const fileIn = el('input', { type: 'file', accept: '.json,application/json' });
  fileIn.addEventListener('change', async () => {
    const f = fileIn.files[0]; fileIn.value = '';
    if (!f) return;
    try {
      const r = await api.req('POST', '/api/settings/import', JSON.parse(await f.text()));
      toast('Imported ' + (r.applied ?? '') + ' settings' + (r.skipped ? ', ' + r.skipped + ' skipped' : ''));
      S.settings = await api.req('GET', '/api/settings');
    } catch (e) { toastErr(e); }
  });
  const exportBtn = el('button', { onclick: async () => {
    try {
      const data = await api.req('GET', '/api/settings/export');
      const a = el('a', { href: URL.createObjectURL(new Blob([JSON.stringify(data, null, 2)], { type: 'application/json' })),
        download: (S.schema.device.name || 'device').replace(/\W+/g, '-').toLowerCase() + '-settings.json' });
      a.click(); URL.revokeObjectURL(a.href);
    } catch (e) { toastErr(e); }
  } }, icon('download'), el('span', null, 'Export'));

  // ota
  const prog = el('i');
  const otaState = el('div', { class: 'help' }, 'Upload firmware.bin or littlefs.bin.');
  const otaIn = el('input', { type: 'file', accept: '.bin' });
  const drop = el('div', { class: 'drop', tabindex: '0', onclick: () => otaIn.click() }, icon('upload'), ' Drop a .bin file or click to choose');
  const upload = f => {
    const kind = /littlefs|spiffs/i.test(f.name) ? 'fs' : 'firmware';
    otaState.textContent = 'Uploading ' + f.name + ' as ' + kind + '…';
    const fd = new FormData(); fd.append('file', f, f.name);
    const x = new XMLHttpRequest();
    x.open('POST', '/api/ota/' + kind);
    x.upload.onprogress = e => prog.style.width = (e.loaded * 100 / e.total).toFixed(1) + '%';
    x.onload = () => { otaState.textContent = x.status === 200 ? 'Done. The device is restarting…' : 'Failed: ' + x.responseText; };
    x.onerror = () => otaState.textContent = 'Upload failed';
    x.send(fd);
  };
  otaIn.addEventListener('change', () => otaIn.files[0] && upload(otaIn.files[0]));
  drop.addEventListener('dragover', e => { e.preventDefault(); drop.classList.add('over'); });
  drop.addEventListener('dragleave', () => drop.classList.remove('over'));
  drop.addEventListener('drop', e => { e.preventDefault(); drop.classList.remove('over'); e.dataTransfer.files[0] && upload(e.dataTransfer.files[0]); });

  const post = (path, msg) => () => api.req('POST', path).then(() => toast(msg)).catch(toastErr);
  view.append(
    el('section', { class: 'card' }, el('header', null, el('h2', null, 'Device')), info),
    el('section', { class: 'card' },
      el('header', null, el('h2', null, 'Backup'), el('div', { class: 'help' }, 'Settings as a JSON file, for backups or rolling one configuration out to many devices. Passwords are included.')),
      el('div', { class: 'row' }, exportBtn, el('button', { onclick: () => fileIn.click() }, icon('upload'), el('span', null, 'Import')), fileIn)),
    el('section', { class: 'card' },
      el('header', null, el('h2', null, 'Update'), otaState), drop, el('div', { class: 'bar' }, prog), otaIn),
    el('section', { class: 'card' },
      el('header', null, el('h2', null, 'Maintenance')),
      el('div', { class: 'actions' },
        el('div', { class: 'act' }, el('div', null, el('b', null, 'Restart'), el('div', { class: 'help' }, 'Reboots the device. Settings are kept.')),
          armed(el('button', null, icon('refresh'), el('span', null, 'Restart')), post('/api/system/restart', 'Restarting…'))),
        el('div', { class: 'act' }, el('div', null, el('b', null, 'Reset settings'), el('div', { class: 'help' }, 'All settings back to their defaults, WiFi included.')),
          armed(el('button', { class: 'danger' }, icon('trash'), el('span', null, 'Reset')), post('/api/settings/reset', 'Settings reset')))))
  );
}

// ---------- log ----------
function pageLog(view) {
  const con = el('div', { class: 'console', role: 'log' });
  let follow = true;
  con.addEventListener('scroll', () => follow = con.scrollTop + con.clientHeight >= con.scrollHeight - 8);
  const line = l => {
    const m = /^\[\s*([\d.]+)\]\s*([EWID])?/.exec(l);
    return el('div', { class: m && m[2] === 'E' ? 'e' : m && m[2] === 'W' ? 'w' : null }, l);
  };
  con.append(...S.log.map(line));
  const onLine = l => { con.append(line(l)); while (con.childNodes.length > 500) con.firstChild.remove(); if (follow) con.scrollTop = con.scrollHeight; };
  S.logListeners.add(onLine); cleanup.push(() => S.logListeners.delete(onLine));
  view.append(el('section', { class: 'card wide' },
    el('div', { class: 'row' }, el('h2', null, 'Log'), el('span', { class: 'grow' }),
      el('button', { onclick: () => navigator.clipboard.writeText(S.log.join('\n')).then(() => toast('Copied')) }, icon('copy'), el('span', null, 'Copy')),
      el('button', { onclick: () => { S.log = []; con.replaceChildren(); } }, icon('trash'), el('span', null, 'Clear'))),
    con));
  requestAnimationFrame(() => con.scrollTop = con.scrollHeight);
}

// ---------- boot ----------
function onMessage(m) {
  if (m.values) { Object.assign(S.values, m.values); S.valueListeners.forEach(f => f(m.values)); }
  if (m.log) for (const l of [].concat(m.log)) { S.log.push(l); if (S.log.length > 500) S.log.shift(); S.logListeners.forEach(f => f(l)); }
  if (m.settings) { Object.assign(S.settings, m.settings); S.settingListeners.forEach(f => f(m.settings)); }
  if (m.toast) toast(m.toast, m.error);
}

async function boot() {
  try {
    [S.schema, S.settings, S.sys] = await Promise.all([api.req('GET', '/api/schema'), api.req('GET', '/api/settings'),
      api.req('GET', '/api/system').catch(() => null)]);
  } catch (e) {
    document.body.append(el('div', { class: 'empty', style: 'margin:40px' }, 'Could not load the device schema: ' + e.message));
    return;
  }
  shell();
  setOnline(false);
  addEventListener('hashchange', route);
  route();
  api.connect(onMessage, setOnline);
}
window.WEBDASH = { el, icon, toast, api, state: S, pages: PAGES };
document.addEventListener('DOMContentLoaded', boot);
})();
