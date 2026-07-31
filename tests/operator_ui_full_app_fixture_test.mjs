#!/usr/bin/env node
import { execFileSync, spawn } from 'node:child_process';
import { createServer } from 'node:http';
import { mkdirSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { createServer as createNetServer } from 'node:net';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const WEB = join(ROOT, 'web');
const OUTPUT = join(ROOT, 'artifacts', 'operator-ui-full-app');
mkdirSync(OUTPUT, { recursive: true });

const CSS_ASSETS = [
  'app.css', 'wifi.css', 'devices.css', 'em500.css', 'theme.css',
  'product-mode.css', 'operator-operations.css', 'operator-product-suite.css',
  'prelab-readiness.css', 'mobile-prelab-fixes.css', 'product-shell-v2.css',
  'product-experience-v2.css', 'commissioning-wizard-v2.css',
  'commissioning-release-v3.css', 'pvdg-chart.css'
];

const JS_ASSETS = [
  'theme.js', 'product-mode.js', 'app.js', 'wifi-utils.js', 'wifi-guard.js',
  'wifi.js', 'network-commissioning-fix.js', 'devices-utils.js', 'pvdg-chart.js',
  'devices.js', 'devices-refresh.js', 'inverter-profiles.js', 'inverter-config.js',
  'inverter-telemetry.js', 'em500-utils.js', 'em500-core.js', 'em500-quality.js',
  'em500-profiles.js', 'em500-plan.js', 'source-detection.js', 'solar-grid.js',
  'operator-view.js', 'operator-operations.js', 'operator-product-suite.js',
  'prelab-readiness.js', 'commissioning-wizard-v2.js', 'engineering-errors.js',
  'ui-enhancements.js', 'product-shell-v2.js', 'product-experience-v2.js',
  'commissioning-release-v3.js'
];

const INDEX = readFileSync(join(WEB, 'index.html'));
const CSS = Buffer.from(CSS_ASSETS.map((name) => readFileSync(join(WEB, name), 'utf8')).join('\n'));
const JS = Buffer.from(JS_ASSETS.map((name) => readFileSync(join(WEB, name), 'utf8')).join('\n'));
const requests = [];
const unknownApi = new Set();
let engineeringAuthenticated = false;

const statusFixture = {
  network_online: true,
  meter_has_data: true,
  meter_online: true,
  meter_stale: false,
  control_enabled: false,
  alarm_names: [],
  wifi_state: 4,
  using_fallback_sta: false,
  fallback_ap_active: false,
  mode: 0,
  ip: '192.168.10.50',
  ssid: 'Plant-Control',
  gateway: '192.168.10.1',
  netmask: '255.255.255.0',
  rssi: -52,
  grid_power_kw: 124.8,
  requested_pv_kw: 310.0,
  applied_pv_kw: 310.0,
  meter_age_ms: 140,
  meter_errors: 0,
  grid_measurement: {
    name: 'Grid EM500', source: 'EM500', unit_id: 1, age_ms: 140,
    quality: 'good', fresh: true
  },
  control_authority: {
    mode_label: 'Standby', inhibit_reason: 'Automatic control disabled'
  }
};

const meter = {
  index: 0,
  enabled: true,
  name: 'Grid EM500',
  role: 'grid',
  endpoint: { host: '192.168.10.60', port: 502, unit_id: 1, timeout_ms: 1000 },
  acquisition: {
    function: 3, pdu_address: 0, data_type: 4, word_order: 0,
    scale: 1, poll_ms: 1000
  },
  runtime: {
    online: true, has_data: true, stale: false, active_power_kw: 124.8,
    retained_active_power_kw: 124.8, data_age_ms: 140, last_attempt_age_ms: 140,
    success_count: 18420, error_count: 0, consecutive_failures: 0,
    last_error: 0, last_error_name: 'None', response_errors: 0
  }
};

const inverter = {
  index: 0,
  enabled: true,
  name: 'Solar Fleet 1',
  host: '192.168.10.70',
  port: 502,
  unit_id: 1,
  timeout_ms: 1000,
  rated_kw: 400,
  rated_power_kw: 400,
  endpoint: { host: '192.168.10.70', port: 502, unit_id: 1, timeout_ms: 1000 },
  command: {
    function: 6, limit_pdu_address: 10, minimum_percent: 0,
    maximum_percent: 100, raw_units_per_percent: 10
  },
  runtime: {
    initialized: true, initialization_failed: false, has_command: true,
    commanded_power_kw: 310, commanded_percent: 77.5, last_command_age_ms: 800,
    write_successes: 220, write_errors: 0, last_error: 0, last_error_name: 'None'
  }
};

const configFixture = {
  schema: 4,
  device_name: 'Automatrix PV-DG Controller',
  wifi: {
    primary: { enabled: true, ssid: 'Plant-Control', ip_mode: 0 },
    fallback: { enabled: true, ssid: 'Plant-Backup', ip_mode: 0 },
    scan_before_connect: true,
    fallback_ap_enabled: true,
    fallback_ap_ssid: 'Automatrix-Recovery',
    max_retries_per_profile: 5,
    reconnect_backoff_ms: 2000
  },
  meters: [{
    enabled: true, name: 'Grid EM500', role: 'grid', host: '192.168.10.60',
    port: 502, unit_id: 1, function: 3, active_power_address: 0,
    data_type: 4, word_order: 0, scale: 1, poll_ms: 1000, timeout_ms: 1000
  }],
  inverters: [{
    enabled: true, name: 'Solar Fleet 1', host: '192.168.10.70', port: 502,
    unit_id: 1, timeout_ms: 1000, rated_kw: 400, limit_address: 10,
    limit_function: 6, raw_units_per_percent: 10, min_percent: 0, max_percent: 100
  }],
  control: {
    grid_import_target_kw: 25, deadband_kw: 5, interval_ms: 250,
    meter_stale_timeout_ms: 3000
  }
};

function json(res, payload, status = 200) {
  const body = Buffer.from(JSON.stringify(payload));
  res.writeHead(status, {
    'Content-Type': 'application/json; charset=utf-8',
    'Content-Length': body.length,
    'Cache-Control': 'no-store'
  });
  res.end(body);
}

function apiFixture(pathname, method) {
  if (pathname === '/api/status') return statusFixture;
  if (pathname === '/api/config') return method === 'GET' ? configFixture : { saved: true, schema: 4 };
  if (pathname === '/api/telemetry') return {
    network: { online: true, ssid: 'Plant-Control', ip: '192.168.10.50', recovery_ap_active: false },
    grid_meter: { fresh: true, active_power_kw: 124.8, retained_active_power_kw: 124.8, data_age_ms: 140, state: 'online' },
    inverters: { enabled: 1, commandable_rated_kw: 400, initialization_failed: 0 },
    control: { enabled: false, mode: 0, last_cycle_age_ms: 250 },
    availability: { monitoring_ready: true, command_path_ready: true, automatic_control_active: false },
    capabilities: { grid_power: true, inverter_power: false, generator_power: false, facility_load: false }
  };
  if (pathname === '/api/meters') return {
    configured_count: 1,
    summary: { enabled: 1, online: 1, stale_or_unavailable: 0 },
    meters: [meter]
  };
  if (pathname === '/api/inverters') return {
    configured_count: 1,
    summary: { enabled: 1, enabled_rated_kw: 400, command_tested: 1, commandable_rated_kw: 400 },
    inverters: [inverter]
  };
  if (pathname === '/api/inverter-telemetry') return {
    configured_count: 1,
    summary: { online: 1, producing: 1, measured_power_kw: 302.4 },
    inverters: [{ index: 0, name: 'Solar Fleet 1', online: true, producing: true, active_power_kw: 302.4, data_age_ms: 180 }]
  };
  if (pathname === '/api/source-detection') return {
    status: {
      state: 'grid', candidate_state: 'grid', configured: true,
      fail_closed: false, transition_pending: false, conflict: false,
      evidence_fresh: true, detection_only: true, tariff: 1,
      successful_reads: 18420, failed_reads: 0,
      reason: 'Grid source confirmed by fresh meter evidence.', mode_a_limitation: ''
    }
  };
  if (pathname === '/api/operator/alarms') return {
    alarms: [],
    summary: { active: 0, unacknowledged: 0, shelved: 0, out_of_service: 0 },
    metrics: { alarms_last_10_minutes: 0, peak_alarms_per_10_minutes: 0, standing_alarm_count: 0 }
  };
  if (pathname === '/api/operator/events') return { events: [] };
  if (pathname === '/api/operator/history') return {
    range: '15m', sample_interval_ms: 30000,
    samples: [
      { timestamp_ms: Date.now() - 60000, grid_power_kw: 118.2, requested_pv_kw: 305, applied_pv_kw: 305, meter_online: true },
      { timestamp_ms: Date.now() - 30000, grid_power_kw: 121.7, requested_pv_kw: 308, applied_pv_kw: 308, meter_online: true },
      { timestamp_ms: Date.now(), grid_power_kw: 124.8, requested_pv_kw: 310, applied_pv_kw: 310, meter_online: true }
    ],
    events: []
  };
  if (pathname === '/api/engineering/session') return {
    authenticated: engineeringAuthenticated,
    session_timeout_minutes: 30,
    password_change_recommended: false
  };
  if (pathname === '/api/engineering/login' && method === 'POST') {
    engineeringAuthenticated = true;
    return { authenticated: true, session_timeout_minutes: 30, password_change_recommended: false };
  }
  if (pathname === '/api/engineering/logout' && method === 'POST') {
    engineeringAuthenticated = false;
    return { authenticated: false };
  }
  if (pathname === '/api/engineering/password' && method === 'POST') return { changed: true };
  if (pathname === '/api/inverter-profiles') return {
    profiles: [{ id: 'generic-modbus', profile_id: 'generic-modbus', manufacturer: 'Generic', model_family: 'Modbus TCP', name: 'Generic Modbus TCP', read_allowed: true }]
  };
  if (pathname === '/api/inverters/config') return method === 'GET' ? { inverters: [inverter] } : { saved: true };
  if (pathname === '/api/inverter-profile-assignment') return { saved: true, restart_required: true };
  if (pathname === '/api/inverter-probe') return { success: true, supported: true, reads: 3 };
  if (pathname === '/api/inverters/write-confirmation') return {
    summary: { configured: 1, confirmed: 1, unconfirmed: 0, failed: 0 },
    provenance: { demonstrated: 0, echoed: 1, unavailable: 0 },
    prerequisite: { confirmed: 1, unconfirmed: 0, unverifiable: 0 },
    inverters: [{ index: 0, name: 'Solar Fleet 1', state: 'confirmed', evidence: 'setpoint_readback', prerequisite_state: 'confirmed' }]
  };
  if (pathname === '/api/commissioning/gate') return {
    scope: 'production', satisfied: false,
    control_authority: { mode_label: 'Standby', inhibit_reason: 'Automatic control disabled' },
    write_confirmation: { state: 'confirmed' },
    limit_evidence: { state: 'accepted' },
    prerequisite_enable: { state: 'confirmed' },
    prerequisites: [
      { id: 'meter', title: 'Grid meter', satisfied: true, reason_code: 'ok', detail: 'Fresh grid measurement available.' },
      { id: 'inverter', title: 'Inverter command path', satisfied: true, reason_code: 'ok', detail: 'Command path initialized.' },
      { id: 'control', title: 'Automatic control', satisfied: false, reason_code: 'disabled', detail: 'Automatic control is intentionally disabled.' }
    ]
  };
  if (pathname === '/api/solar-grid/status') return {
    enabled: false, mode: 'disabled', commissioning_scope: 'production',
    lab_simulator_active: false, requested_pv_kw: 310, applied_pv_kw: 310,
    control_authority: { mode_label: 'Standby', inhibit_reason: 'Automatic control disabled' }
  };
  if (pathname === '/api/solar-grid/config') return {
    enabled: false, grid_import_target_kw: 25, generator_minimum_load_percent: 30,
    generator_rated_kw: 500, inverter_capacity_kw: 400
  };
  if (pathname === '/api/system/resources') return {
    resource_state: 'healthy', free_heap_bytes: 181000, minimum_free_heap_bytes: 162000,
    largest_free_block_bytes: 110000, task_stack_margin_bytes: 2900,
    http_open_sockets: 2, http_socket_capacity: 10
  };
  if (pathname === '/api/wifi/scan') return {
    state: 'complete', scanning: false,
    networks: [
      { ssid: 'Plant-Control', rssi: -52, auth: 'WPA2', channel: 6 },
      { ssid: 'Plant-Backup', rssi: -66, auth: 'WPA2', channel: 11 }
    ]
  };
  if (pathname === '/api/wifi/config') return method === 'GET' ? configFixture.wifi : { saved: true, restart_required: true };
  if (pathname === '/api/wifi/rescan') return { accepted: true };
  if (pathname === '/api/meters/config') return { saved: true };
  if (pathname === '/api/system/restart') return { accepted: true };
  if (pathname === '/api/meters/em500/cache') return { meters: [{ index: 0, state: 'fresh', age_ms: 140 }] };
  if (pathname === '/api/meters/em500/snapshot') return {
    meter_index: 0, scope: 'instantaneous', quality: 'good', age_ms: 140,
    values: { voltage_l1_v: 230.4, current_l1_a: 182.2, active_power_total_kw: 124.8, frequency_hz: 50.01, power_factor_total: 0.96 }
  };
  if (pathname === '/api/meters/em500/history') return { meter_index: 0, block: 'M01', samples: [] };
  if (pathname === '/api/meters/em500/settings') return { meter_index: 0, settings: [], values: {} };
  if (pathname === '/api/meters/em500/settings/plan') return { changes: [], warnings: [], restart_required: false };
  return null;
}

const server = createServer((req, res) => {
  const url = new URL(req.url || '/', 'http://127.0.0.1');
  requests.push(`${req.method} ${url.pathname}${url.search}`);
  if (url.pathname === '/' || url.pathname === '/index.html') {
    res.writeHead(200, { 'Content-Type': 'text/html; charset=utf-8', 'Cache-Control': 'no-store' });
    res.end(INDEX);
    return;
  }
  if (url.pathname === '/app.css') {
    res.writeHead(200, { 'Content-Type': 'text/css; charset=utf-8', 'Cache-Control': 'no-store' });
    res.end(CSS);
    return;
  }
  if (url.pathname === '/app.js') {
    res.writeHead(200, { 'Content-Type': 'application/javascript; charset=utf-8', 'Cache-Control': 'no-store' });
    res.end(JS);
    return;
  }
  if (url.pathname === '/favicon.ico') {
    res.writeHead(204, { 'Cache-Control': 'public, max-age=86400' });
    res.end();
    return;
  }
  if (url.pathname.startsWith('/api/')) {
    const fixture = apiFixture(url.pathname, req.method || 'GET');
    if (fixture != null) json(res, fixture);
    else {
      unknownApi.add(`${req.method} ${url.pathname}`);
      json(res, { error: `No fixture for ${req.method} ${url.pathname}` }, 404);
    }
    return;
  }
  res.writeHead(404, { 'Content-Type': 'text/plain' });
  res.end('Not found');
});

const listen = (instance) => new Promise((resolvePromise, reject) => {
  instance.once('error', reject);
  instance.listen(0, '127.0.0.1', () => resolvePromise(instance.address().port));
});
const closeServer = (instance) => new Promise((resolvePromise) => instance.close(resolvePromise));
const sleep = (ms) => new Promise((resolvePromise) => setTimeout(resolvePromise, ms));

async function reservePort() {
  const probe = createNetServer();
  const port = await listen(probe);
  await closeServer(probe);
  return port;
}

function findBrowser() {
  for (const candidate of ['google-chrome', 'google-chrome-stable', 'chromium', 'chromium-browser']) {
    try {
      const path = execFileSync('sh', ['-c', `command -v ${candidate}`], { encoding: 'utf8' }).trim();
      if (path) return path;
    } catch { /* try next */ }
  }
  throw new Error('No Chromium-family browser found');
}

class Cdp {
  constructor(url) {
    this.url = url;
    this.nextId = 0;
    this.pending = new Map();
    this.listeners = new Map();
  }
  async open() {
    this.ws = new WebSocket(this.url);
    await new Promise((resolvePromise, reject) => {
      this.ws.addEventListener('open', resolvePromise, { once: true });
      this.ws.addEventListener('error', reject, { once: true });
    });
    this.ws.addEventListener('message', (event) => {
      const message = JSON.parse(event.data);
      if (message.id) {
        const pending = this.pending.get(message.id);
        if (!pending) return;
        this.pending.delete(message.id);
        if (message.error) pending.reject(new Error(`${pending.method}: ${message.error.message}`));
        else pending.resolve(message.result || {});
        return;
      }
      for (const handler of this.listeners.get(message.method) || []) handler(message.params || {});
    });
  }
  on(method, handler) {
    if (!this.listeners.has(method)) this.listeners.set(method, []);
    this.listeners.get(method).push(handler);
  }
  call(method, params = {}) {
    const id = ++this.nextId;
    return new Promise((resolvePromise, reject) => {
      this.pending.set(id, { resolve: resolvePromise, reject, method });
      this.ws.send(JSON.stringify({ id, method, params }));
    });
  }
  close() { this.ws?.close(); }
}

const assert = (condition, message) => { if (!condition) throw new Error(message); };
const httpPort = await listen(server);
const browserPort = await reservePort();
const profile = mkdtempSync(join(tmpdir(), 'pvdg-full-app-'));
const browserLog = [];
const browser = spawn(findBrowser(), [
  '--headless', '--no-sandbox', '--disable-gpu', '--disable-dev-shm-usage',
  '--disable-crash-reporter', '--no-first-run', '--remote-allow-origins=*',
  `--remote-debugging-port=${browserPort}`, `--user-data-dir=${profile}`, 'about:blank'
], { stdio: ['ignore', 'ignore', 'pipe'] });
browser.stderr.on('data', (chunk) => browserLog.push(String(chunk)));

async function waitForBrowser() {
  for (let attempt = 0; attempt < 200; attempt += 1) {
    try {
      const response = await fetch(`http://127.0.0.1:${browserPort}/json/version`);
      if (response.ok) return;
    } catch { /* browser starting */ }
    if (browser.exitCode != null) throw new Error(`Browser exited before DevTools started (${browser.exitCode})\n${browserLog.join('')}`);
    await sleep(50);
  }
  throw new Error(`Timed out waiting for browser DevTools\n${browserLog.join('')}`);
}

let cdp;
let target;
const exceptions = [];
const consoleErrors = [];
const report = { screenshots: [], routes: [], requests: [], exceptions: [], consoleErrors: [], unknownApi: [] };

try {
  await waitForBrowser();
  const response = await fetch(`http://127.0.0.1:${browserPort}/json/new?${encodeURIComponent(`http://127.0.0.1:${httpPort}/#/dashboard`)}`, { method: 'PUT' });
  assert(response.ok, `Could not create browser target (${response.status})`);
  target = await response.json();
  cdp = new Cdp(target.webSocketDebuggerUrl);
  await cdp.open();
  cdp.on('Runtime.exceptionThrown', ({ exceptionDetails }) => {
    exceptions.push(exceptionDetails?.exception?.description || exceptionDetails?.text || 'Unknown runtime exception');
  });
  cdp.on('Runtime.consoleAPICalled', ({ type, args }) => {
    if (type !== 'error') return;
    consoleErrors.push(args.map((arg) => arg.value ?? arg.description ?? '').join(' '));
  });
  await cdp.call('Page.enable');
  await cdp.call('Runtime.enable');
  await cdp.call('Page.bringToFront');
  await sleep(1800);

  const evaluate = async (expression, awaitPromise = false) => {
    const result = await cdp.call('Runtime.evaluate', { expression, returnByValue: true, awaitPromise });
    if (result.exceptionDetails) throw new Error(result.exceptionDetails.exception?.description || result.exceptionDetails.text);
    return result.result?.value;
  };
  const settle = async (ms = 500) => {
    await sleep(ms);
    await evaluate(`new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve)))`, true);
  };

  async function setViewport(width, height) {
    await cdp.call('Emulation.setDeviceMetricsOverride', { width, height, deviceScaleFactor: 1, mobile: width <= 650 });
    await settle(120);
  }

  async function capture(name, width, height) {
    await setViewport(width, height);
    const shot = await cdp.call('Page.captureScreenshot', { format: 'png', captureBeyondViewport: false, fromSurface: true });
    const path = join(OUTPUT, `${name}-${width}x${height}.png`);
    writeFileSync(path, Buffer.from(shot.data, 'base64'));
    report.screenshots.push(path.slice(ROOT.length + 1));
  }

  async function visit(route, width = 1440, height = 900, screenshot = true) {
    await setViewport(width, height);
    await evaluate(`location.hash=${JSON.stringify(`#/${route}`)}`);
    await settle(800);
    const state = await evaluate(`(() => {
      const active=document.querySelector('.page.active');
      const content=document.querySelector('.content');
      return {
        route: location.hash,
        active: active?.dataset.page || '',
        title: document.getElementById('pageTitle')?.textContent || '',
        access: document.documentElement.dataset.access || '',
        overflow: document.documentElement.scrollWidth > innerWidth,
        contentWidth: Math.round(content?.getBoundingClientRect().width || 0),
        viewport: innerWidth
      };
    })()`);
    assert(!state.overflow, `${route} overflows horizontally at ${width}px`);
    assert(state.active === route, `${route} activated ${state.active || '<none>'}`);
    report.routes.push({ route, width, height, ...state });
    if (screenshot) await capture(route, width, height);
  }

  await visit('dashboard');
  const verdict = await evaluate(`document.getElementById('plantVerdictValue')?.textContent || ''`);
  assert(verdict === 'PLANT NORMAL', `Full app verdict is ${verdict || '<missing>'}`);
  await visit('meters');
  await visit('inverters');
  await visit('alarms');
  await visit('dashboard', 1024, 768);
  await visit('dashboard', 390, 844);

  await visit('engineering', 1440, 900, false);
  await evaluate(`(() => {
    const input=document.getElementById('engineeringPassword');
    input.value='fixture-password';
    document.getElementById('engineeringLoginForm').requestSubmit();
  })()`);
  await settle(700);
  const access = await evaluate(`document.documentElement.dataset.access`);
  assert(access === 'engineering', `Engineering login did not unlock the application (${access})`);
  await visit('control');
  await visit('system');

  assert(unknownApi.size === 0, `Missing API fixtures: ${[...unknownApi].join(', ')}`);
  assert(exceptions.length === 0, `Runtime exceptions:\n${exceptions.join('\n\n')}`);
  assert(consoleErrors.length === 0, `Console errors:\n${consoleErrors.join('\n')}`);

  report.requests = [...new Set(requests)].sort();
  report.exceptions = exceptions;
  report.consoleErrors = consoleErrors;
  report.unknownApi = [...unknownApi];
  writeFileSync(join(OUTPUT, 'report.json'), JSON.stringify(report, null, 2));
  console.log(`operator UI full application fixture: PASS (${report.screenshots.length} screenshots, ${report.requests.length} unique requests)`);
} catch (error) {
  report.requests = [...new Set(requests)].sort();
  report.exceptions = exceptions;
  report.consoleErrors = consoleErrors;
  report.unknownApi = [...unknownApi];
  report.failure = error.stack || String(error);
  writeFileSync(join(OUTPUT, 'report.json'), JSON.stringify(report, null, 2));
  throw error;
} finally {
  try { cdp?.close(); } catch { /* best effort */ }
  if (target?.id) {
    try { await fetch(`http://127.0.0.1:${browserPort}/json/close/${target.id}`); } catch { /* best effort */ }
  }
  if (browser.exitCode == null) browser.kill('SIGTERM');
  await sleep(100);
  if (browser.exitCode == null) browser.kill('SIGKILL');
  await closeServer(server);
  rmSync(profile, { recursive: true, force: true });
}
