((root, factory) => {
    const api = factory();
    if (typeof module === 'object' && module.exports) module.exports = api;
    if (root) root.PvdgEm500Utils = api;
})(typeof window !== 'undefined' ? window : globalThis, () => {
    'use strict';

    const WIRING_LABELS = [
        '3 phase + neutral',
        '3 phase',
        '3 phase + neutral balanced',
        '3 phase balanced',
        '2 phase + neutral',
        '1 phase + neutral'
    ];

    const MENU_LABELS = {
        M01: 'General CT / PT / wiring',
        M02: 'Display and utility',
        M03: 'Password controls',
        M04: 'Integration',
        M05: 'Hour counters',
        M06: 'Trend graph',
        M07: 'Communication',
        M08: 'Limit thresholds',
        M09: 'Alarms',
        M10: 'Counters',
        M11: 'Energy pulse outputs',
        M12: 'Boolean logic',
        M13: 'Inputs',
        M14: 'Outputs',
        M15: 'User pages',
        M16: 'Analog inputs',
        M17: 'Analog outputs',
        M18: 'Power quality'
    };

    function finite(value) {
        const number = Number(value);
        return Number.isFinite(number) ? number : null;
    }

    function formatValue(entry, digits = 3) {
        if (!entry || entry.value == null) return 'Unavailable';
        if (typeof entry.value === 'boolean') return entry.value ? 'Enabled' : 'Disabled';
        if (typeof entry.value === 'string') return entry.value || '--';
        const value = finite(entry.value);
        if (value == null) return 'Unavailable';
        const absolute = Math.abs(value);
        let decimals = digits;
        if (absolute >= 1000) decimals = 1;
        else if (absolute >= 100) decimals = 2;
        const text = value.toLocaleString(undefined, { maximumFractionDigits: decimals });
        return entry.unit && entry.unit !== 'ratio' ? `${text} ${entry.unit}` : text;
    }

    function flattenValues(values) {
        if (!values || typeof values !== 'object') return [];
        return Object.entries(values).map(([key, entry]) => ({ key, ...(entry || {}) }));
    }

    function humanize(key) {
        return String(key || '')
            .replace(/_/g, ' ')
            .replace(/\b\w/g, (character) => character.toUpperCase());
    }

    function cloneMeter(profile = {}, index = 0) {
        return {
            enabled: profile.enabled !== false,
            name: String(profile.name || `Meter ${index + 1}`),
            host: String(profile.host || ''),
            port: Number(profile.port ?? 502),
            unit_id: Number(profile.unit_id ?? index + 1),
            timeout_ms: Number(profile.timeout_ms ?? 500),
            function: Number(profile.function ?? 3),
            active_power_address: Number(profile.active_power_address ?? 58),
            data_type: Number(profile.data_type ?? 3),
            word_order: Number(profile.word_order ?? 0),
            scale: Number(profile.scale ?? 0.00001),
            poll_ms: Number(profile.poll_ms ?? 1000)
        };
    }

    function validateMeter(profile, index = 0) {
        const prefix = `Meter ${index + 1}`;
        const errors = [];
        if (!String(profile.name || '').trim()) errors.push(`${prefix}: name is required`);
        if (profile.enabled && !String(profile.host || '').trim()) errors.push(`${prefix}: host is required`);
        const integer = (value, min, max) => Number.isInteger(Number(value)) && Number(value) >= min && Number(value) <= max;
        if (!integer(profile.port, 1, 65535)) errors.push(`${prefix}: port must be 1–65535`);
        if (!integer(profile.unit_id, 1, 247)) errors.push(`${prefix}: unit ID must be 1–247`);
        if (!integer(profile.timeout_ms, 100, 60000)) errors.push(`${prefix}: timeout must be 100–60000 ms`);
        if (![3, 4].includes(Number(profile.function))) errors.push(`${prefix}: function must be FC03 or FC04`);
        if (!integer(profile.active_power_address, 0, 65535)) errors.push(`${prefix}: active-power address must be 0–65535`);
        if (!integer(profile.data_type, 0, 4)) errors.push(`${prefix}: data type is invalid`);
        if (!integer(profile.word_order, 0, 3)) errors.push(`${prefix}: word order is invalid`);
        if (!Number.isFinite(Number(profile.scale)) || Number(profile.scale) === 0) errors.push(`${prefix}: scale must be non-zero`);
        if (!integer(profile.poll_ms, 100, 60000)) errors.push(`${prefix}: poll interval must be 100–60000 ms`);
        return errors;
    }

    function validateMeterList(profiles) {
        if (!Array.isArray(profiles) || profiles.length > 4) return ['A maximum of four meter profiles is supported'];
        const errors = profiles.flatMap((profile, index) => validateMeter(profile, index));
        const endpoints = new Map();
        profiles.forEach((profile, index) => {
            if (!profile.enabled) return;
            const key = `${String(profile.host).trim().toLowerCase()}:${Number(profile.port)}:${Number(profile.unit_id)}`;
            if (endpoints.has(key)) errors.push(`Meters ${endpoints.get(key) + 1} and ${index + 1} use the same enabled endpoint`);
            else endpoints.set(key, index);
        });
        return errors;
    }

    function buildMeterPayload(profiles) {
        const meters = (profiles || []).map((profile, index) => cloneMeter(profile, index));
        const errors = validateMeterList(meters);
        if (errors.length) throw new Error(errors.join('; '));
        return { meters };
    }

    function buildM01Changes(form) {
        const changes = {};
        const integer = (key, minimum, maximum) => {
            const value = Number(form[key]);
            if (!Number.isInteger(value) || value < minimum || value > maximum) {
                throw new Error(`${humanize(key)} must be ${minimum}–${maximum}`);
            }
            changes[key] = value;
        };
        integer('ct_primary_a', 1, 10000);
        integer('ct_secondary_a', 1, 5);
        if (![1, 5].includes(changes.ct_secondary_a)) throw new Error('CT secondary must be 1 A or 5 A');
        integer('rated_voltage_v', 49, 500000);
        changes.use_vt = Boolean(form.use_vt);
        integer('vt_primary_v', 50, 500000);
        integer('vt_secondary_v', 50, 500);
        integer('wiring', 0, 5);
        return changes;
    }

    return Object.freeze({
        WIRING_LABELS,
        MENU_LABELS,
        finite,
        formatValue,
        flattenValues,
        humanize,
        cloneMeter,
        validateMeter,
        validateMeterList,
        buildMeterPayload,
        buildM01Changes
    });
});
