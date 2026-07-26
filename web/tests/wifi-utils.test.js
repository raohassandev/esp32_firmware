'use strict';

const assert = require('assert');
const utils = require('../wifi-utils');

assert.strictEqual(utils.parseIpv4('192.168.0.1'), 0xc0a80001);
assert.strictEqual(utils.parseIpv4('255.255.255.255'), 0xffffffff);
assert.strictEqual(utils.parseIpv4('192.168.0.999'), null);
assert.strictEqual(utils.parseIpv4('192.168.0'), null);
assert.strictEqual(utils.parseIpv4('1.2.3.04'), 0x01020304);

assert.strictEqual(utils.isContiguousNetmask('255.255.255.0'), true);
assert.strictEqual(utils.isContiguousNetmask('255.255.0.0'), true);
assert.strictEqual(utils.isContiguousNetmask('255.0.255.0'), false);
assert.strictEqual(utils.isContiguousNetmask('0.0.0.0'), false);
assert.strictEqual(utils.isContiguousNetmask('255.255.255.255'), false);

assert.strictEqual(utils.sameSubnet('192.168.0.105', '192.168.0.1', '255.255.255.0'), true);
assert.strictEqual(utils.sameSubnet('192.168.1.105', '192.168.0.1', '255.255.255.0'), false);
assert.strictEqual(utils.isUsableHost('192.168.0.105', '255.255.255.0'), true);
assert.strictEqual(utils.isUsableHost('192.168.0.0', '255.255.255.0'), false);
assert.strictEqual(utils.isUsableHost('192.168.0.255', '255.255.255.0'), false);

assert.deepStrictEqual(utils.validateStaticProfile({
    ip_mode: 1,
    static_ip: '192.168.0.105',
    gateway: '192.168.0.1',
    netmask: '255.255.255.0',
    dns1: '8.8.8.8',
    dns2: ''
}), []);

assert(utils.validateStaticProfile({
    ip_mode: 1,
    static_ip: '192.168.1.105',
    gateway: '192.168.0.1',
    netmask: '255.255.255.0',
    dns1: '',
    dns2: ''
}).some(([field]) => field === 'gateway'));

assert.strictEqual(utils.authInfo(0).secure, false);
assert.strictEqual(utils.authInfo(3).supported, true);
assert.strictEqual(utils.authInfo(5).supported, false);
assert.strictEqual(utils.signalLevel(-50), 4);
assert.strictEqual(utils.signalLevel(-70), 2);
assert.strictEqual(utils.signalLevel(-90), 0);

console.log('wifi-utils test passed');
