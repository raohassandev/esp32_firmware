'use strict';

const assert = require('assert');
const utils = require('../em500-utils.js');

assert.strictEqual(utils.formatValue({ value: null, unit: 'V' }), 'Unavailable');
assert.match(utils.formatValue({ value: 257.66, unit: 'V' }), /257\.66 V/);
assert.strictEqual(utils.formatValue({ value: true }), 'Enabled');
assert.strictEqual(utils.humanize('ct_primary_a'), 'Ct Primary A');
assert.strictEqual(utils.WIRING_LABELS.length, 6);
assert.strictEqual(utils.MENU_LABELS.M18, 'Power quality');

const profiles = [
    {
        enabled: true,
        name: 'Grid EM500',
        host: '192.168.0.200',
        port: 502,
        unit_id: 1,
        timeout_ms: 500,
        function: 3,
        active_power_address: 58,
        data_type: 3,
        word_order: 0,
        scale: 0.00001,
        poll_ms: 250
    },
    {
        enabled: true,
        name: 'Generator EM500',
        host: '192.168.0.200',
        port: 502,
        unit_id: 2,
        timeout_ms: 500,
        function: 3,
        active_power_address: 58,
        data_type: 3,
        word_order: 0,
        scale: 0.00001,
        poll_ms: 250
    }
];

const payload = utils.buildMeterPayload(profiles);
assert.strictEqual(payload.meters.length, 2);
assert.strictEqual(payload.meters[1].unit_id, 2);

const duplicate = profiles.map((profile) => ({ ...profile, unit_id: 1 }));
assert.throws(() => utils.buildMeterPayload(duplicate), /same enabled endpoint/);
assert.throws(() => utils.buildMeterPayload([{ ...profiles[0], scale: 0 }]), /scale must be non-zero/);

const m01 = utils.buildM01Changes({
    ct_primary_a: 1000,
    ct_secondary_a: 5,
    rated_voltage_v: 400,
    use_vt: false,
    vt_primary_v: 400,
    vt_secondary_v: 100,
    wiring: 0
});
assert.deepStrictEqual(m01, {
    ct_primary_a: 1000,
    ct_secondary_a: 5,
    rated_voltage_v: 400,
    use_vt: false,
    vt_primary_v: 400,
    vt_secondary_v: 100,
    wiring: 0
});
assert.throws(() => utils.buildM01Changes({
    ct_primary_a: 0,
    ct_secondary_a: 5,
    rated_voltage_v: 400,
    use_vt: false,
    vt_primary_v: 400,
    vt_secondary_v: 100,
    wiring: 0
}), /CT Primary A must be 1–10000/i);

console.log('EM500 browser utility tests passed');
