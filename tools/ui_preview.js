/*
 * ui_preview.js — serves the real interface on a laptop, with invented data.
 *
 * WHY. The board is not always reachable, and looking at a screen is the only
 * way to judge one. This serves the SAME files in the SAME order the firmware
 * does, so what appears in the browser is what appears on the controller.
 *
 * The order is not retyped here. tools/ui_preview_order.json is extracted from
 * the asset arrays in components/web_server/web_server.c, which are the real
 * source of truth -- CMakeLists.txt lists the same files in a DIFFERENT order
 * and following that one would produce a preview whose CSS cascade does not
 * match the product. Re-extract it whenever an asset is added.
 *
 * THE DATA IS FABRICATED, AND THAT IS THE POINT AND THE LIMIT. Every endpoint
 * below returns a plausible plant so the layout can be judged with realistic
 * numbers, long device names and non-round values. Nothing here is evidence
 * about firmware behaviour: a screen that renders correctly against this proves
 * the markup and the styling, and proves nothing about acquisition, control or
 * safety. Scenarios exist precisely so the ugly states get looked at too.
 *
 *   node tools/ui_preview.js                 healthy plant
 *   node tools/ui_preview.js --scenario=fault      a meter is dead
 *   node tools/ui_preview.js --scenario=empty      nothing commissioned
 *   node tools/ui_preview.js --port=8080
 */
'use strict';

const http = require('http');
const fs = require('fs');
const path = require('path');

const ROOT = path.join(__dirname, '..');
const WEB = path.join(ROOT, 'web');
const ORDER = JSON.parse(fs.readFileSync(path.join(__dirname, 'ui_preview_order.json'), 'utf8'));

const args = process.argv.slice(2);
const arg = (name, fallback) => {
    const hit = args.find((a) => a.startsWith(`--${name}=`));
    return hit ? hit.split('=')[1] : fallback;
};
const PORT = Number(arg('port', 8099));
const SCENARIO = String(arg('scenario', 'healthy'));

/* Concatenated fresh on every request, so editing a file and reloading the page
 * is the whole loop. A preview that needs restarting is a preview nobody uses. */
function bundle(list) {
    return list
        .map((file) => {
            const full = path.join(WEB, file);
            const body = fs.existsSync(full) ? fs.readFileSync(full, 'utf8') : `/* MISSING: ${file} */`;
            return `/* ---- ${file} ---- */\n${body}`;
        })
        .join('\n');
}

/* A plant with awkward numbers on purpose: a long device name, a value that is
 * not round, and a phase that is exporting while the total imports. Tidy sample
 * data hides exactly the layout faults a preview exists to expose. */
function plant() {
    const now = Date.now();
    const healthy = SCENARIO !== 'fault';
    const empty = SCENARIO === 'empty';

    const meters = empty ? [] : [{
        index: 0,
        name: 'Automatrix EM500 — main incomer',
        enabled: true,
        runtime: {
            available: true,
            online: healthy,
            has_data: healthy,
            stale: !healthy,
            active_power_kw: healthy ? 243.7 : null,
            data_age_ms: healthy ? 420 : 265000,
            state: healthy ? 'online' : 'offline'
        }
    }];

    const inverters = empty ? [] : [
        { index: 0, telemetry_valid: true, telemetry_stale: false, measured_power_kw: 81.4, telemetry_age_ms: 900 },
        { index: 1, telemetry_valid: false, telemetry_stale: true, measured_power_kw: null, telemetry_age_ms: 138000 }
    ];

    return {
        '/api/status': {
            controller_online: true,
            network_online: true,
            wifi_state: 4,
            ssid: 'Rao',
            ip: '192.168.0.121',
            rssi: -60,
            fallback_ap_active: true,
            recovery_ap_ssid: 'Automatrix-PVDG-Setup',
            recovery_ap_channel: 2,
            control_enabled: healthy,
            meter_online: healthy,
            meter_stale: !healthy,
            meter_has_data: healthy,
            grid_measurement: { quality: healthy ? 'good' : 'unavailable' },
            control_authority: healthy
                ? { mode_label: 'Commanding', command_authority: true, control_enabled: true, inhibit_reason: '' }
                : { mode_label: 'Inhibited', command_authority: false, control_enabled: true,
                    inhibit_reason: 'The grid measurement is missing, stale or non-finite.' },
            timestamp_ms: now
        },
        '/api/meters': { operator_view: true, configured_count: meters.length, meters,
            summary: { enabled: meters.length, online: healthy ? meters.length : 0 } },
        '/api/inverters': {
            operator_view: true, configured_count: inverters.length, measured_power_supported: true,
            inverters: inverters.map((i) => ({
                index: i.index, name: `Inverter ${i.index + 1}`, enabled: true, rated_kw: 60,
                telemetry_supported: true, measured_power_kw: i.measured_power_kw,
                runtime: { available: true, online: i.telemetry_valid, telemetry_valid: i.telemetry_valid,
                    state: i.telemetry_valid ? 'online' : 'unavailable' }
            })),
            summary: { enabled: inverters.length, online: inverters.filter((i) => i.telemetry_valid).length,
                configured_rated_kw: 120, enabled_rated_kw: 120, commandable_rated_kw: healthy ? 60 : 0,
                command_tested: 1, last_write_ok: 1, initialization_failed: 0 }
        },
        '/api/inverter-telemetry': {
            operator_view: true, read_only_endpoint: true, writes_issued: false,
            count: inverters.length, inverters,
            summary: { online: inverters.filter((i) => i.telemetry_valid).length,
                telemetry_valid: inverters.filter((i) => i.telemetry_valid).length,
                stale: inverters.filter((i) => i.telemetry_stale).length,
                measured_total_kw: healthy ? 81.4 : 0,
                commandable_rated_kw: healthy ? 60 : 0 }
        },
        '/api/telemetry': { measures: { active_power: true, voltage: true, current: true } },
        '/api/config': { schema: 7, device_name: 'automatrix-pvdg', operator_view: true,
            meters: meters.map((m) => ({ name: m.name, enabled: true })),
            inverters: [{ name: 'Inverter 1', enabled: true, rated_kw: 60 }],
            control: { enabled: healthy },
            wifi: { primary: { enabled: true, ssid: 'Rao', password: '********', ip_mode: 0 },
                fallback: { enabled: false, ssid: '', password: '', ip_mode: 0 },
                fallback_ap_enabled: true, fallback_ap_ssid: 'Automatrix-PVDG-Setup',
                fallback_ap_password: '********', scan_before_connect: true,
                max_retries_per_profile: 5, reconnect_backoff_ms: 2000 } },
        '/api/operator/alarms': { alarms: healthy ? [] : [
            { code: 5, severity: 'critical', message: 'Meter offline', acknowledged: false },
            { code: 6, severity: 'warning', message: 'Meter data stale', acknowledged: false }
        ] },
        '/api/operator/events': { events: [] },
        '/api/operator/history': { samples: [] },
        '/api/network/scan': { state: 2, networks: [
            { ssid: 'Rao', rssi: -58, channel: 6, auth_mode: 3, security: 'WPA2', connected: true },
            { ssid: 'Automatrix-5G', rssi: -74, channel: 36, auth_mode: 3, security: 'WPA2' }
        ] }
    };
}

const ENGINEERING_ONLY = [
    '/api/engineering/', '/api/wifi/', '/api/meters/config', '/api/inverters/config',
    '/api/meter-profiles', '/api/inverter-profiles', '/api/inverter-probe',
    '/api/solar-grid/', '/api/commissioning/', '/api/source-detection', '/api/system/'
];

function send(response, status, type, body) {
    response.writeHead(status, { 'Content-Type': type, 'Cache-Control': 'no-store' });
    response.end(body);
}

const server = http.createServer((request, response) => {
    const url = request.url.split('?')[0];

    if (url === '/' || url === '/index.html') {
        return send(response, 200, 'text/html; charset=utf-8',
            fs.readFileSync(path.join(WEB, 'index.html'), 'utf8'));
    }
    if (url === '/app.css') return send(response, 200, 'text/css; charset=utf-8', bundle(ORDER.css));
    if (url === '/app.js') return send(response, 200, 'application/javascript; charset=utf-8', bundle(ORDER.js));

    const data = plant();
    if (Object.prototype.hasOwnProperty.call(data, url)) {
        return send(response, 200, 'application/json', JSON.stringify(data[url]));
    }
    /* Engineering routes answer 401 exactly as the firmware does for a signed-out
     * browser, so the preview shows the OPERATOR's view rather than a version of
     * the interface no customer will ever see. */
    if (ENGINEERING_ONLY.some((prefix) => url.startsWith(prefix))) {
        return send(response, 401, 'application/json',
            JSON.stringify({ error: 'engineering_authentication_required' }));
    }
    return send(response, 404, 'application/json', JSON.stringify({ error: 'not_found' }));
});

server.listen(PORT, () => {
    console.log(`UI preview on http://localhost:${PORT}  (scenario: ${SCENARIO})`);
    console.log(`  ${ORDER.css.length} stylesheets, ${ORDER.js.length} scripts, in the firmware's own order`);
    console.log('  scenarios: healthy | fault | empty      data is fabricated, not evidence');
});
