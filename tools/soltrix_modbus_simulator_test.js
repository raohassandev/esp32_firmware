'use strict';

const assert = require('assert');
const {
    DEVICES,
    EM500_SOURCE_ADDRESS,
    EM500_POWER_ADDRESS,
    createServer,
    request,
} = require('./soltrix_modbus_simulator');

async function withServer(scenario, fn) {
    const server = createServer(scenario);
    await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve));
    try {
        await fn(server.address().port);
    } finally {
        await new Promise((resolve) => server.close(resolve));
    }
}

async function readU16(port, unitId, address) {
    const response = await request(port, unitId, 3, address, 1);
    return response.readUInt16BE(9);
}

async function readI32(port, unitId, address) {
    const response = await request(port, unitId, 3, address, 2);
    return response.readInt32BE(9);
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

    await withServer('em500-single-grid', async (port) => {
        assert.strictEqual(await readU16(port, 31, EM500_SOURCE_ADDRESS), 0);
    });

    await withServer('em500-single-generator', async (port) => {
        assert.strictEqual(await readU16(port, 31, EM500_SOURCE_ADDRESS), 1);
    });

    await withServer('em500-single-toggle', async (port) => {
        assert.strictEqual(await readU16(port, 31, EM500_SOURCE_ADDRESS), 0);
        assert.strictEqual(await readU16(port, 31, EM500_SOURCE_ADDRESS), 1);
        assert.strictEqual(await readU16(port, 31, EM500_SOURCE_ADDRESS), 0);
    });

    await withServer('em500-dual-grid', async (port) => {
        assert.strictEqual(await readI32(port, 31, EM500_POWER_ADDRESS), 5000);
        assert.strictEqual(await readI32(port, 32, EM500_POWER_ADDRESS), 200);
    });

    await withServer('em500-dual-generator', async (port) => {
        assert.strictEqual(await readI32(port, 31, EM500_POWER_ADDRESS), 200);
        assert.strictEqual(await readI32(port, 32, EM500_POWER_ADDRESS), 5000);
    });

    await withServer('em500-dual-conflict', async (port) => {
        assert.strictEqual(await readI32(port, 31, EM500_POWER_ADDRESS), 5000);
        assert.strictEqual(await readI32(port, 32, EM500_POWER_ADDRESS), 5000);
    });

    await withServer('em500-dual-none', async (port) => {
        assert.strictEqual(await readI32(port, 31, EM500_POWER_ADDRESS), 200);
        assert.strictEqual(await readI32(port, 32, EM500_POWER_ADDRESS), 200);
    });

    await withServer('em500-threshold-hover', async (port) => {
        assert.strictEqual(await readI32(port, 32, EM500_POWER_ADDRESS), 900);
        assert.strictEqual(await readI32(port, 32, EM500_POWER_ADDRESS), 1100);
        assert.strictEqual(await readI32(port, 32, EM500_POWER_ADDRESS), 900);
    });

    console.log(JSON.stringify({
        result: 'PASS',
        testedUnits: [21, 22, 23, 31, 32],
        testedScenarios: [
            'normal', 'rollback', 'timeout', 'comm-lost',
            'em500-single-grid', 'em500-single-generator', 'em500-single-toggle',
            'em500-dual-grid', 'em500-dual-generator', 'em500-dual-conflict',
            'em500-dual-none', 'em500-threshold-hover'
        ],
        staleHandledByFirmwareAgeGate: true,
        productionEvidence: false,
    }));
}

main().catch((error) => {
    console.error(error);
    process.exitCode = 1;
});
