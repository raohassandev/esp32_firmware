(() => {
    'use strict';

    const utils = window.PvdgInverterTelemetryUtils;
    const deviceUtils = window.PvdgDeviceUtils;
    if (!utils || !deviceUtils) return;

    let latest = [];
    let loading = false;

    function meta(label, value) {
        const item = document.createElement('div');
        item.className = 'device-meta-item telemetry-meta-item';
        const name = document.createElement('span');
        name.textContent = label;
        const content = document.createElement('strong');
        content.textContent = String(value);
        content.title = String(value);
        item.append(name, content);
        return item;
    }

    function apply() {
        const cards = Array.from(document.querySelectorAll('#inverterRuntimeList .device-runtime-card'));
        cards.forEach((card, index) => {
            const inverter = latest[index];
            if (!inverter) return;
            const runtime = inverter.telemetry_runtime || {};
            const telemetry = inverter.telemetry || {};
            const state = utils.telemetryState(inverter);
            const value = card.querySelector('.device-reading-value');
            const note = card.querySelector('.device-reading-note');
            if (value && note) {
                if (runtime.online && !runtime.stale && inverter.measured_power_kw != null) {
                    value.textContent = deviceUtils.formatPower(inverter.measured_power_kw);
                    note.textContent = `Fresh measured active power · ${deviceUtils.formatAge(runtime.data_age_ms)}`;
                } else if (runtime.has_data && runtime.stale && inverter.measured_power_kw != null) {
                    value.textContent = `${deviceUtils.formatPower(inverter.measured_power_kw)} stale`;
                    note.textContent = `Retained measured value · ${deviceUtils.formatAge(runtime.data_age_ms)} · not current`;
                } else {
                    value.textContent = 'Unavailable';
                    note.textContent = state.label === 'Profile disabled'
                        ? 'Active-power telemetry profile is disabled.'
                        : state.label === 'Device disabled'
                            ? 'The inverter is disabled by configuration.'
                            : 'No valid measured inverter active-power sample is available.';
                }
            }

            const grid = card.querySelector('.device-meta-grid');
            if (!grid) return;
            grid.querySelectorAll('.telemetry-meta-item').forEach((item) => item.remove());
            const error = Number(runtime.last_error) === 0 && Number(runtime.error_count) === 0
                ? 'None'
                : runtime.last_error_name || `Error ${runtime.last_error}`;
            grid.append(
                meta('Telemetry state', state.label),
                meta('Telemetry mapping', `FC${telemetry.function ?? '--'} · PDU ${telemetry.pdu_address ?? '--'}`),
                meta('Telemetry timing', `${telemetry.poll_ms ?? '--'} ms poll · ${telemetry.stale_after_ms ?? '--'} ms stale`),
                meta('Telemetry results', `${runtime.success_count ?? 0} OK · ${runtime.error_count ?? 0} failed`),
                meta('Telemetry last attempt', deviceUtils.formatAge(runtime.last_attempt_age_ms)),
                meta('Telemetry last error', error)
            );
        });
    }

    async function refresh() {
        if (loading || !window.location.hash.includes('inverters')) return;
        loading = true;
        try {
            const response = await fetch('/api/inverters', { cache: 'no-store' });
            if (!response.ok) return;
            const payload = await response.json();
            latest = Array.isArray(payload.inverters) ? payload.inverters : [];
            apply();
        } finally {
            loading = false;
        }
    }

    function start() {
        const list = document.getElementById('inverterRuntimeList');
        if (list) new MutationObserver(apply).observe(list, { childList: true });
        window.addEventListener('hashchange', refresh);
        window.setInterval(refresh, 5000);
        refresh();
    }

    if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', start, { once: true });
    else start();
})();
