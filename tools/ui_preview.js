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

    /* The measurement and energy blocks, in EXACTLY the shape meter_json.c
     * emits -- nulls where the firmware sends null, arrays of three where it
     * sends three. A fixture that is merely similar produces a preview that
     * renders and a product that does not.
     *
     * L2 exports, so the signed rendering is on screen rather than only in a
     * unit test, and the phases are deliberately unbalanced: a fixture whose
     * three columns match cannot show whether the matrix reveals an imbalance,
     * which is the entire reason the matrix exists. */
    const measurements = healthy ? {
        available: true,
        age_ms: 640,
        phase_voltage_v: [230.1, 229.9, 231.0],
        line_voltage_v: [398.8, 399.1, 400.0],
        current_a: [356.79, 120.00, 200.00],
        active_power_kw: [122.66, -20.00, 123.88],
        reactive_power_kvar: [5.00, -2.50, 7.50],
        apparent_power_kva: [123.00, 21.00, 124.00],
        power_factor: [0.997, -0.952, 0.999],
        frequency_hz: 49.98,
        equivalent_phase_voltage_v: 230.3,
        equivalent_line_voltage_v: 399.3,
        equivalent_current_a: 225.59,
        total_active_power_kw: 226.54,
        total_reactive_power_kvar: 10.00,
        total_apparent_power_kva: 268.00,
        total_power_factor: 0.985,
        voltage_asymmetry_line_percent: 1.20,
        voltage_asymmetry_phase_percent: 0.95,
        current_asymmetry_percent: 43.10,
        neutral_current_a: 81.23
    } : {
        /* Not read. Every field null, never zero -- the fault scenario exists to
         * show what an unmeasured plant looks like, and zeros would show a
         * measured one that happens to read zero. */
        available: false, age_ms: null,
        phase_voltage_v: [null, null, null], line_voltage_v: [null, null, null],
        current_a: [null, null, null], active_power_kw: [null, null, null],
        reactive_power_kvar: [null, null, null], apparent_power_kva: [null, null, null],
        power_factor: [null, null, null],
        frequency_hz: null, equivalent_phase_voltage_v: null, equivalent_line_voltage_v: null,
        equivalent_current_a: null, total_active_power_kw: null, total_reactive_power_kvar: null,
        total_apparent_power_kva: null, total_power_factor: null,
        voltage_asymmetry_line_percent: null, voltage_asymmetry_phase_percent: null,
        current_asymmetry_percent: null, neutral_current_a: null
    };

    const meterEnergy = healthy ? {
        available: true, age_ms: 12400,
        total_import_active_kwh: 1234567.89,
        total_export_active_kwh: 45678.90,
        total_import_reactive_kvarh: 987.65,
        total_export_reactive_kvarh: 43.21,
        total_apparent_kvah: 2222.22,
        partial_import_active_kwh: 50.00,
        partial_export_active_kwh: 25.00,
        partial_import_reactive_kvarh: 10.00,
        partial_export_reactive_kvarh: 3.00,
        partial_apparent_kvah: 77.77
    } : { available: false, age_ms: null };

    const meters = empty ? [] : [{
        index: 0,
        name: 'Automatrix EM500 — main incomer',
        enabled: true,
        measurements,
        energy: meterEnergy,
        runtime: {
            available: true,
            online: healthy,
            has_data: healthy,
            stale: !healthy,
            active_power_kw: healthy ? 243.7 : null,
            data_age_ms: healthy ? 420 : 265000,
            phase_power_kw: healthy ? [122.66, -20.00, 123.88] : [null, null, null],
            state: healthy ? 'online' : 'offline'
        }
    }];

    /* The inverter block, in the shape inverter_json.c emits. The two machines
     * are scaled differently so a page that mixed them up would be visible. */
    const inverterMeasurements = (scale, online) => (online ? {
        available: true, age_ms: 880,
        dc: {
            string_voltage_v: [601.2, 598.7, 610.5, 587.6],
            string_current_a: [12.34 * scale, 11.98 * scale, 13.01 * scale, 11.50 * scale],
            power_kw: 58.75 * scale
        },
        ac: {
            line_voltage_v: [398.7, 399.1, 400.1],
            phase_voltage_v: [230.1, 229.8, 231.1],
            phase_current_a: [84.12 * scale, 83.55 * scale, 85.01 * scale],
            active_power_kw: 57.34 * scale,
            reactive_power_kvar: -1.25,
            peak_active_power_today_kw: 59.90 * scale,
            power_factor: 0.998,
            frequency_hz: 49.98
        },
        device: {
            efficiency_percent: 98.62,
            internal_temperature_c: 41.2,
            insulation_resistance_mohm: 30.0,
            status_raw: 512,
            fault_code_raw: 0
        },
        energy: {
            today_kwh: 245.67 * scale, month_kwh: 5678.90 * scale,
            total_kwh: 123456.78 * scale, total_dc_input_kwh: 129999.99 * scale
        }
    } : { available: false, age_ms: null });

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
                measured_age_ms: i.telemetry_age_ms,
                measurements: inverterMeasurements(i.index === 0 ? 1 : 0.86, i.telemetry_valid),
                runtime: { available: true, online: i.telemetry_valid, telemetry_valid: i.telemetry_valid,
                    has_command: true,
                    /* Deliberately BELOW the measured output on inverter 0, so the
                     * "above the limit" callout appears. That is the state worth
                     * looking at, and a fixture where the command always agrees can
                     * never show it. */
                    commanded_percent: 45, commanded_power_kw: 27.0,
                    last_command_age_ms: 4200, write_successes: 12, write_errors: 0,
                    last_error: 0, last_error_name: 'ESP_OK',
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
        /* The journal, in the shape alarms_journal_get emits -- including the
         * uptime time base, which is the thing the page must not dress up as a
         * calendar. Losses are non-zero in the fault scenario so the "this
         * history is incomplete" band gets looked at. */
        '/api/operator/alarms/journal': {
            generated_ms: now,
            time_base: 'uptime_ms',
            time_note: 'Times are milliseconds since the controller started, not calendar times: '
                + 'this controller has no real-time clock. A restart resets the time base; '
                + 'the sequence number does not, so records remain ordered across reboots.',
            storage_ready: true, storage_status: 'ready', persistent: true,
            capacity: 2048, stored: healthy ? 1089 : 1093, next_sequence: healthy ? 1090 : 1094,
            unreadable_skipped: healthy ? 0 : 3, write_failures: 0,
            staging_dropped: healthy ? 0 : 1,
            offset: 0, limit: 40, returned: healthy ? 4 : 6, has_more: true, next_offset: 40,
            entries: (healthy ? [] : [
                { sequence: 1093, code: 5, id: 'meter_offline', title: 'Meter offline',
                  transition: 'raised', uptime_ms: 4520000, age_ms: 21000 },
                { sequence: 1092, code: 6, id: 'meter_stale', title: 'Meter data stale',
                  transition: 'raised', uptime_ms: 4498000, age_ms: 43000 }
            ]).concat([
                { sequence: 1089, code: 12, id: 'grid_export', title: 'Export above the limit',
                  transition: 'cleared', uptime_ms: 3980000, age_ms: 561000 },
                { sequence: 1088, code: 12, id: 'grid_export', title: 'Export above the limit',
                  transition: 'acknowledged', uptime_ms: 3975000, age_ms: 566000,
                  actor: 'engineering' },
                { sequence: 1087, code: 12, id: 'grid_export', title: 'Export above the limit',
                  transition: 'raised', uptime_ms: 3960000, age_ms: 581000 },
                { sequence: 1086, code: 7, id: 'inverter_unconfirmed', title: 'Setpoint not confirmed',
                  transition: 'shelved', uptime_ms: 1200000, age_ms: 3341000,
                  actor: 'engineering', reason: 'awaiting Huawei bench qualification',
                  shelf_duration_ms: 3600000 }
            ])
        },
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
