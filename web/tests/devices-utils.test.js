'use strict';

const assert = require('assert');
const utils = require('../devices-utils.js');

assert.strictEqual(utils.formatPower(null), 'Unavailable');
assert.strictEqual(utils.formatPower(-4.901), '-4.90 kW');
assert.strictEqual(utils.formatPercent(50), '50.0%');
assert.strictEqual(utils.formatAge(null), 'Never');
assert.strictEqual(utils.formatAge(250), '250 ms ago');
assert.strictEqual(utils.formatAge(2500), '2.5 s ago');
assert.strictEqual(utils.endpointLabel({ host: '192.168.0.200', port: 502 }), '192.168.0.200:502');

assert.deepStrictEqual(
    utils.meterState({ enabled: false, runtime: {} }),
    { label: 'Disabled', tone: 'neutral', detail: 'Polling is disabled by configuration.' }
);
assert.strictEqual(utils.meterState({ enabled: true, runtime: { online: true, stale: false } }).label, 'Online');
assert.strictEqual(utils.meterState({ enabled: true, runtime: { has_data: true, stale: true } }).label, 'Stale');
assert.strictEqual(utils.meterState({ enabled: true, runtime: { has_data: false } }).label, 'Unavailable');

assert.strictEqual(utils.inverterState({ enabled: false, runtime: {} }).label, 'Disabled');
assert.strictEqual(utils.inverterState({ enabled: true, runtime: { has_command: false } }).label, 'Not tested');
assert.strictEqual(utils.inverterState({ enabled: true, runtime: { has_command: true, last_write_ok: true } }).label, 'Last write OK');
assert.strictEqual(utils.inverterState({ enabled: true, runtime: { has_command: true, last_write_ok: false } }).label, 'Last write failed');

console.log('devices-utils test passed');
