(function (root, factory) {
    const api = factory();
    if (typeof module === 'object' && module.exports) module.exports = api;
    else root.PvdgInverterTelemetryUtils = api;
})(typeof globalThis !== 'undefined' ? globalThis : this, function () {
    'use strict';

    const DATA_TYPES = ['UInt16', 'Int16', 'UInt32', 'Int32', 'Float32'];
    const WORD_ORDERS = ['ABCD', 'CDAB', 'BADC', 'DCBA'];

    function finite(value) {
        if (value == null || value === '' || typeof value === 'boolean') return null;
        const number = Number(value);
        return Number.isFinite(number) ? number : null;
    }

    function integer(value, minimum, maximum) {
        const number = finite(value);
        return number != null && Number.isInteger(number) &&
            number >= minimum && number <= maximum ? number : null;
    }

    function validateProfile(profile) {
        const errors = [];
        if (!profile || typeof profile !== 'object') return ['Profile is required.'];
        if (typeof profile.enabled !== 'boolean') errors.push('Enabled state is required.');
        const point = profile.active_power;
        if (!point || typeof point !== 'object') return [...errors, 'Active-power mapping is required.'];
        const fn = integer(point.function, 3, 4);
        if (fn == null || (fn !== 3 && fn !== 4)) errors.push('Function must be FC03 or FC04.');
        if (integer(point.pdu_address, 0, 65535) == null) errors.push('PDU address must be 0–65535.');
        if (integer(point.data_type, 0, DATA_TYPES.length - 1) == null) errors.push('Data type is invalid.');
        if (integer(point.word_order, 0, WORD_ORDERS.length - 1) == null) errors.push('Word order is invalid.');
        const scale = finite(point.scale);
        if (scale == null || scale === 0) errors.push('Scale must be a non-zero number.');
        if (finite(point.offset) == null) errors.push('Offset must be a number.');
        if (integer(point.poll_ms, 100, 60000) == null) errors.push('Poll interval must be 100–60000 ms.');
        return errors;
    }

    function huaweiV3Example(index, current = {}) {
        return {
            index: Number(index),
            enabled: true,
            active_power: {
                function: 3,
                pdu_address: 32080,
                data_type: 3,
                word_order: 0,
                scale: 0.001,
                offset: 0,
                poll_ms: 1000,
                ...(current.active_power || {})
            }
        };
    }

    function telemetryState(inverter) {
        const telemetry = inverter && inverter.telemetry ? inverter.telemetry : {};
        const runtime = inverter && inverter.telemetry_runtime ? inverter.telemetry_runtime : {};
        if (!inverter || !inverter.enabled) return { label: 'Device disabled', tone: 'neutral' };
        if (!telemetry.enabled) return { label: 'Profile disabled', tone: 'neutral' };
        if (runtime.state === 'initialization_failed') return { label: 'Initialization failed', tone: 'bad' };
        if (runtime.online && !runtime.stale) return { label: 'Online', tone: 'good' };
        if (runtime.has_data && runtime.stale) return { label: 'Stale', tone: 'warning' };
        return { label: 'Unavailable', tone: 'bad' };
    }

    function freshMeasuredTotal(inverters) {
        if (!Array.isArray(inverters)) return null;
        let total = 0;
        let count = 0;
        inverters.forEach((inverter) => {
            const runtime = inverter && inverter.telemetry_runtime ? inverter.telemetry_runtime : {};
            const power = finite(inverter && inverter.measured_power_kw);
            if (runtime.online && !runtime.stale && power != null) {
                total += power;
                count++;
            }
        });
        return count ? { total, count } : null;
    }

    return {
        DATA_TYPES,
        WORD_ORDERS,
        finite,
        integer,
        validateProfile,
        huaweiV3Example,
        telemetryState,
        freshMeasuredTotal
    };
});
