(function (root, factory) {
    const api = factory();
    if (typeof module === 'object' && module.exports) module.exports = api;
    else root.PvdgDeviceUtils = api;
})(typeof globalThis !== 'undefined' ? globalThis : this, function () {
    'use strict';

    function finite(value) {
        if (value == null || value === '' || typeof value === 'boolean') return null;
        const number = Number(value);
        return Number.isFinite(number) ? number : null;
    }

    function formatPower(value) {
        const number = finite(value);
        return number == null ? 'Unavailable' : `${number.toFixed(2)} kW`;
    }

    function formatPercent(value) {
        const number = finite(value);
        return number == null ? 'Unavailable' : `${number.toFixed(1)}%`;
    }

    function formatAge(value) {
        const milliseconds = finite(value);
        if (milliseconds == null || milliseconds < 0) return 'Never';
        if (milliseconds < 1000) return `${Math.round(milliseconds)} ms ago`;
        if (milliseconds < 60000) return `${(milliseconds / 1000).toFixed(1)} s ago`;
        if (milliseconds < 3600000) return `${(milliseconds / 60000).toFixed(1)} min ago`;
        return `${(milliseconds / 3600000).toFixed(1)} h ago`;
    }

    function meterState(meter) {
        const runtime = meter && meter.runtime ? meter.runtime : {};
        if (!meter || !meter.enabled) {
            return { label: 'Disabled', tone: 'neutral', detail: 'Polling is disabled by configuration.' };
        }
        if (runtime.initialization_failed) {
            return { label: 'Initialization failed', tone: 'bad', detail: 'The Modbus runtime could not be initialized. Review the endpoint and system resources.' };
        }
        if (runtime.online && !runtime.stale) {
            return { label: 'Online', tone: 'good', detail: 'Latest Modbus sample is current.' };
        }
        if (runtime.has_data && runtime.stale) {
            return { label: 'Stale', tone: 'warning', detail: 'Last valid value is retained but is not current.' };
        }
        return { label: 'Unavailable', tone: 'bad', detail: 'No current valid Modbus sample is available.' };
    }

    function inverterState(inverter) {
        const runtime = inverter && inverter.runtime ? inverter.runtime : {};
        if (!inverter || !inverter.enabled) {
            return { label: 'Disabled', tone: 'neutral', detail: 'Command channel is disabled by configuration.' };
        }
        if (runtime.initialization_failed) {
            return { label: 'Initialization failed', tone: 'bad', detail: 'The Modbus command channel could not be initialized.' };
        }
        if (!runtime.has_command) {
            return { label: 'Not tested', tone: 'neutral', detail: 'No command has been issued since this boot.' };
        }
        if (runtime.last_write_ok) {
            return { label: 'Last write OK', tone: 'good', detail: 'This confirms only the last command transaction, not inverter telemetry.' };
        }
        return { label: 'Last write failed', tone: 'bad', detail: 'The most recent command transaction failed.' };
    }

    function endpointLabel(endpoint) {
        if (!endpoint) return 'Unavailable';
        const host = endpoint.host || '--';
        const port = finite(endpoint.port);
        return `${host}:${port == null ? '--' : port}`;
    }

    return {
        finite,
        formatPower,
        formatPercent,
        formatAge,
        meterState,
        inverterState,
        endpointLabel
    };
});
