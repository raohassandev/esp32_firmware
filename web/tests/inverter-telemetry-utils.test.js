'use strict';

const assert = require('assert');
const utils = require('../inverter-telemetry-utils.js');

const valid = {
    index: 0,
    enabled: true,
    active_power: {
        function: 3,
        pdu_address: 32080,
        data_type: 3,
        word_order: 0,
        scale: 0.001,
        offset: 0,
        poll_ms: 1000
    }
};
assert.deepStrictEqual(utils.validateProfile(valid), []);
assert.ok(utils.validateProfile({ ...valid, active_power: { ...valid.active_power, scale: 0 } }).length > 0);
assert.ok(utils.validateProfile({ ...valid, active_power: { ...valid.active_power, function: 6 } }).length > 0);
assert.ok(utils.validateProfile({ ...valid, active_power: { ...valid.active_power, poll_ms: 50 } }).length > 0);
assert.ok(utils.validateProfile({ ...valid, active_power: { ...valid.active_power, pdu_address: 65536 } }).length > 0);

const huawei = utils.huaweiV3Example(2, {
    active_power: { pdu_address: 1, scale: 9 }
});
assert.strictEqual(huawei.index, 2);
assert.strictEqual(huawei.enabled, true);
assert.strictEqual(huawei.active_power.function, 3);
assert.strictEqual(huawei.active_power.pdu_address, 32080);
assert.strictEqual(huawei.active_power.data_type, 3);
assert.strictEqual(huawei.active_power.word_order, 0);
assert.strictEqual(huawei.active_power.scale, 0.001);
assert.strictEqual(huawei.active_power.poll_ms, 1000);

assert.strictEqual(utils.telemetryState({ enabled: false }).label, 'Device disabled');
assert.strictEqual(utils.telemetryState({ enabled: true, telemetry: { enabled: false } }).label, 'Profile disabled');
assert.strictEqual(utils.telemetryState({ enabled: true, telemetry: { enabled: true }, telemetry_runtime: { online: true, stale: false } }).label, 'Online');
assert.strictEqual(utils.telemetryState({ enabled: true, telemetry: { enabled: true }, telemetry_runtime: { has_data: true, stale: true } }).label, 'Stale');
assert.strictEqual(utils.telemetryState({ enabled: true, telemetry: { enabled: true }, telemetry_runtime: { state: 'initialization_failed' } }).label, 'Initialization failed');

assert.strictEqual(utils.freshMeasuredTotal([{ measured_power_kw: null, telemetry_runtime: { online: true, stale: false } }]), null);
assert.deepStrictEqual(
    utils.freshMeasuredTotal([
        { measured_power_kw: 40, telemetry_runtime: { online: true, stale: false } },
        { measured_power_kw: 10.5, telemetry_runtime: { online: true, stale: false } },
        { measured_power_kw: 99, telemetry_runtime: { online: false, stale: true } }
    ]),
    { total: 50.5, count: 2 }
);

console.log('inverter-telemetry-utils test passed');
