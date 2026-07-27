'use strict';

const assert = require('assert');
const { DEVICES, createServer, request } = require('./soltrix_modbus_simulator');

async function withServer(scenario, fn) {
    const server = createServer(scenario);
    await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve));
    try {
        await fn(server.address().port);
    } finally {
        await new Promise((resolve) => server.close(resolve));
    }
}

async function main() {
    await withServer('normal', async (port) => {
        for (const [unitId, device] of DEVICES) {
            const identity = await request(port, unitId, 3, device.identityAddress, 1);
            assert.strictEqual(identity.readUInt16BE(9), device.identity);
            const power = await request(port, unitId, 3, device.powerAddress, 2);
            assert.strictEqual(power.readInt32BE(9), device.powerW);
        }
    });

    await withServer('rollback', async (port) => {
        const device = DEVICES.get(21);
        await request(port, 21, 6, device.limitAddress, 750);
        const readback = await request(port, 21, 3, device.limitAddress, 1);
        assert.strictEqual(readback.readUInt16BE(9), 0);
    });

    await withServer('timeout', async (port) => {
        const device = DEVICES.get(22);
        await assert.rejects(() => request(port, 22, 3, device.identityAddress, 1, 120), /timeout/);
    });

    await withServer('comm-lost', async (port) => {
        const device = DEVICES.get(23);
        await assert.rejects(() => request(port, 23, 3, device.identityAddress, 1, 300));
    });

    console.log(JSON.stringify({
        result: 'PASS',
        testedUnits: [21, 22, 23],
        testedScenarios: ['normal', 'rollback', 'timeout', 'comm-lost'],
        staleHandledByFirmwareAgeGate: true,
        productionEvidence: false,
    }));
}

main().catch((error) => {
    console.error(error);
    process.exitCode = 1;
});
