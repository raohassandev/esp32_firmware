'use strict';

const assert = require('assert');
const utils = require('../devices-utils.js');

assert.strictEqual(utils.finite(null), null);
assert.strictEqual(utils.finite(undefined), null);
assert.strictEqual(utils.finite(''), null);
assert.strictEqual(utils.finite(false), null);
assert.strictEqual(utils.finite('0'), 0);
assert.strictEqual(utils.finite('12.5'), 12.5);

assert.strictEqual(utils.formatPower(null), 'Unavailable');
assert.strictEqual(utils.formatPower(undefined), 'Unavailable');
assert.strictEqual(utils.formatPower(''), 'Unavailable');
assert.strictEqual(utils.formatPower(false), 'Unavailable');
assert.strictEqual(utils.formatPower(0), '0.00 kW');
assert.strictEqual(utils.formatPower(-4.901), '-4.90 kW');
assert.strictEqual(utils.formatPercent(50), '50.0%');
assert.strictEqual(utils.formatAge(null), 'Never');
assert.strictEqual(utils.formatAge(-1), 'Never');
assert.strictEqual(utils.formatAge(250), '250 ms ago');
assert.strictEqual(utils.formatAge(2500), '2.5 s ago');
assert.strictEqual(utils.formatAge(120000), '2.0 min ago');
assert.strictEqual(utils.endpointLabel({ host: '192.168.0.200', port: 502 }), '192.168.0.200:502');
assert.strictEqual(utils.endpointLabel(null), 'Unavailable');

assert.deepStrictEqual(
    utils.meterState({ enabled: false, runtime: {} }),
    { label: 'Disabled', tone: 'neutral', detail: 'Polling is disabled by configuration.' }
);
assert.strictEqual(utils.meterState({ enabled: true, runtime: { initialization_failed: true } }).label, 'Initialization failed');
assert.strictEqual(utils.meterState({ enabled: true, runtime: { online: true, stale: false } }).label, 'Online');
assert.strictEqual(utils.meterState({ enabled: true, runtime: { has_data: true, stale: true } }).label, 'Stale');
assert.strictEqual(utils.meterState({ enabled: true, runtime: { has_data: false } }).label, 'Unavailable');

assert.strictEqual(utils.inverterState({ enabled: false, runtime: {} }).label, 'Disabled');
assert.strictEqual(utils.inverterState({ enabled: true, runtime: { initialization_failed: true } }).label, 'Initialization failed');
assert.strictEqual(utils.inverterState({ enabled: true, runtime: { has_command: false } }).label, 'Not tested');
assert.strictEqual(utils.inverterState({ enabled: true, runtime: { has_command: true, last_write_ok: true } }).label, 'Last write OK');
assert.strictEqual(utils.inverterState({ enabled: true, runtime: { has_command: true, last_write_ok: false } }).label, 'Last write failed');

assert.strictEqual(utils.readinessTone(true), 'good');
assert.strictEqual(utils.readinessTone(false), 'bad');
assert.strictEqual(utils.readinessTone(null), 'neutral');

/* ---------------------------------------------------------- inverter diagnosis
 *
 * The case that prompted this: Unit ID 1 configured against a simulator serving
 * units 21, 22 and 23. The page reported "Unavailable" and "Commandable 0" and
 * pointed at nothing. The diagnosis must name the endpoint, name the configured
 * unit id, and say that a gateway which does not host that unit stays silent -
 * without claiming to know which of the two possible causes it is. */
const LAB_ENDPOINT = { index: 0, enabled: true, host: '192.168.100.11', port: 1503, unit_id: 1, rated_kw: 100 };

const silent = utils.diagnoseInverter(
    { index: 0, connection_initialized: true, online: false, telemetry_stale: true,
      read_successes: 0, read_errors: 57, consecutive_read_failures: 57,
      last_error_name: 'ESP_ERR_TIMEOUT' },
    LAB_ENDPOINT);
assert.strictEqual(silent.tone, 'bad');
assert.ok(silent.text.includes('192.168.100.11:1503 unit 1'), 'the endpoint must be named');
assert.ok(silent.text.includes('Unit ID 1 is configured'), 'the configured unit id must be named');
assert.ok(silent.text.includes('does not serve that unit id'), 'the unit-id cause must be offered');
assert.ok(silent.text.includes('Nothing is listening'), 'the address cause must be offered too');
/* Both causes, never one: the evidence does not distinguish them. */
assert.ok(silent.text.includes(' or '), 'a timeout must not claim a single cause');

const answered = utils.diagnoseInverter(
    { index: 2, connection_initialized: true, online: false, read_successes: 0,
      read_errors: 12, last_error_name: 'ESP_ERR_INVALID_RESPONSE' },
    { ...LAB_ENDPOINT, index: 2, unit_id: 21 });
assert.ok(answered.text.startsWith('Inverter 3:'), 'channels are named one-based');
assert.ok(answered.text.includes('answered, but not to this request'));
assert.ok(answered.text.includes('Unit ID 21 is configured'));

assert.ok(utils.diagnoseInverter(
    { index: 0, connection_initialized: true, online: false, read_successes: 0, read_errors: 1,
      last_error_name: 'ESP_ERR_NOT_FOUND' }, LAB_ENDPOINT).text.includes('does not resolve'));

/* Ordered like recompute_commandable_capacity(): the first unmet condition wins. */
assert.strictEqual(utils.diagnoseInverter({ index: 0 }, { ...LAB_ENDPOINT, enabled: false }).tone, 'neutral');
assert.ok(utils.diagnoseInverter({ index: 0, connection_initialized: false }, LAB_ENDPOINT)
    .text.includes('never initialised'));
assert.ok(utils.diagnoseInverter(
    { index: 0, connection_initialized: true, read_successes: 0, read_errors: 0 }, LAB_ENDPOINT)
    .text.includes('no read has completed yet'));
assert.ok(utils.diagnoseInverter(
    { index: 0, connection_initialized: true, online: false, read_successes: 900, read_errors: 4,
      consecutive_read_failures: 4, last_error_name: 'ESP_ERR_TIMEOUT' }, LAB_ENDPOINT)
    .text.includes('answered before and has stopped'));
assert.ok(utils.diagnoseInverter(
    { index: 0, connection_initialized: true, online: true, read_successes: 5,
      identity_supported: true, identity_verified: false }, LAB_ENDPOINT)
    .text.includes('identity register did not match'));
assert.ok(utils.diagnoseInverter(
    { index: 0, connection_initialized: true, online: true, read_successes: 5,
      telemetry_supported: false }, LAB_ENDPOINT)
    .text.includes('no active-power register'));
assert.ok(utils.diagnoseInverter(
    { index: 0, connection_initialized: true, online: true, read_successes: 5,
      telemetry_supported: true, telemetry_valid: true }, { ...LAB_ENDPOINT, rated_kw: 0 })
    .text.includes('rated power is not set'));

/* A healthy, commandable channel produces no finding at all. A page that always
 * has something to say trains an engineer to stop reading it. */
assert.strictEqual(utils.diagnoseInverter(
    { index: 0, connection_initialized: true, online: true, read_successes: 5, read_errors: 0,
      telemetry_supported: true, telemetry_valid: true }, LAB_ENDPOINT), null);

/* Missing endpoint data must degrade, never invent. */
const noEndpoint = utils.diagnoseInverter(
    { index: 0, connection_initialized: true, online: false, read_successes: 0, read_errors: 3,
      last_error_name: 'ESP_ERR_TIMEOUT' }, null);
assert.ok(noEndpoint.text.includes('its configured endpoint'));
assert.ok(!noEndpoint.text.includes('Unit ID'), 'a unit id must not be invented when none is known');

assert.ok(utils.diagnoseInverterFleet({ count: 0, summary: {} }, 0)
    .includes('No inverter channel is configured'));
assert.ok(utils.diagnoseInverterFleet({ count: 3, summary: { commandable_rated_kw: 0 } }, 2)
    .includes('2 of 3 channels have a finding'));
assert.ok(utils.diagnoseInverterFleet({ count: 1, summary: { commandable_rated_kw: 0 } }, 1)
    .includes('1 of 1 channel has a finding'));
/* Zero capacity with everything answering points at the panels that own the
 * remaining conditions rather than guessing at them. */
assert.ok(utils.diagnoseInverterFleet({ count: 1, summary: { commandable_rated_kw: 0 } }, 0)
    .includes('write permission'));
assert.strictEqual(utils.diagnoseInverterFleet({ count: 1, summary: { commandable_rated_kw: 250 } }, 0), '');

console.log('devices-utils test passed');
