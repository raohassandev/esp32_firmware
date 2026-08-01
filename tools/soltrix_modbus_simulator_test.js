'use strict';

const assert = require('assert');
const fs = require('fs');
const path = require('path');
const {
    DEVICES,
    EM500_SOURCE_ADDRESS,
    EM500_SUPERSEDED_SOURCE_ADDRESS,
    EM500_POWER_ADDRESS,
    EM500_BLOCK_START,
    EM500_ENERGY_START,
    createServer,
    request,
} = require('./soltrix_modbus_simulator');

const SOURCE_CONFIG_HEADER = path.join(
    __dirname, '..', 'components', 'source_detection', 'include',
    'source_detection_config.h');
const METER_PROFILES = path.join(
    __dirname, '..', 'components', 'meter_profiles', 'meter_profiles.c');
const EM500_BLOCK_HEADER = path.join(
    __dirname, '..', 'components', 'meter_profiles', 'include', 'em500_block.h');

/* Strips block and line comments so a register address quoted in prose can never
 * satisfy a contract about the code. */
function strippedSource(file) {
    return fs.readFileSync(file, 'utf8')
        .replace(/\/\*[\s\S]*?\*\//g, ' ')
        .replace(/\/\/[^\n]*/g, ' ');
}

/*
 * CROSS-ARTIFACT CONTRACT.
 *
 * The lab rig must serve the tariff at the register the firmware polls. When
 * these two drifted apart the simulator answered zero at the firmware's address
 * for ever, so every scenario resolved to GRID, the generator-curtailment path
 * was never entered, and the rig reported PASS while proving nothing about the
 * controller's primary function. Reading the address out of the firmware header
 * rather than restating it is the point: a future correction to the header moves
 * this assertion with it and cannot be satisfied by editing only one side.
 */
function assertSimulatorMatchesFirmwareSourceRegister() {
    const header = strippedSource(SOURCE_CONFIG_HEADER);
    const match = header.match(
        /#define\s+SOURCE_DETECTION_SINGLE_REGISTER_DEFAULT\s+(0[xX][0-9a-fA-F]+|\d+)u?/);
    assert.ok(match, 'SOURCE_DETECTION_SINGLE_REGISTER_DEFAULT not found in firmware header');
    const firmwareAddress = Number(match[1]);
    assert.ok(Number.isInteger(firmwareAddress) && firmwareAddress > 0);
    assert.strictEqual(
        EM500_SOURCE_ADDRESS, firmwareAddress,
        `simulator serves the tariff at 0x${EM500_SOURCE_ADDRESS.toString(16)} but the ` +
        `firmware polls 0x${firmwareAddress.toString(16)}`);
    assert.notStrictEqual(EM500_SOURCE_ADDRESS, EM500_SUPERSEDED_SOURCE_ADDRESS);
}

/*
 * THE SAME CONTRACT, FOR THE REGISTER THE CONTROL LOOP ACTUALLY REGULATES ON.
 *
 * The simulator served active power at 57 while the firmware profile asks for
 * 0x003A, which is 58. Nothing pinned them, so every EM500 run read a register
 * the firmware never requests: the tests passed because both sides used the
 * simulator's own constant, and the one address the whole control loop depends
 * on was never exercised. An off-by-one that a green lab run cannot show.
 *
 * As above, the firmware value is READ OUT of meter_profiles.c rather than
 * restated here, so correcting one side alone cannot satisfy this.
 */
function assertSimulatorMatchesFirmwarePowerRegister() {
    const source = strippedSource(METER_PROFILES);
    const match = source.match(
        /\.active_power_address\s*=\s*(0[xX][0-9a-fA-F]+|\d+)/);
    assert.ok(match, 'active_power_address not found in meter_profiles.c');
    const firmwareAddress = Number(match[1]);
    assert.strictEqual(
        EM500_POWER_ADDRESS, firmwareAddress,
        `simulator serves active power at 0x${EM500_POWER_ADDRESS.toString(16)} but the ` +
        `firmware profile reads 0x${firmwareAddress.toString(16)}`);
}

/* And the two block reads must start where the firmware starts them. A block
 * served one register off decodes into values of entirely plausible magnitude,
 * because neighbouring registers on this meter hold related quantities -- so
 * this is the drift least likely to be noticed by looking at a screen. */
function assertSimulatorMatchesFirmwareBlockStarts() {
    const header = strippedSource(EM500_BLOCK_HEADER);
    const block = header.match(/#define\s+EM500_BLOCK_START\s+(0[xX][0-9a-fA-F]+|\d+)u?/);
    const energy = header.match(/#define\s+EM500_ENERGY_START\s+(0[xX][0-9a-fA-F]+|\d+)u?/);
    assert.ok(block && energy, 'block start constants not found in em500_block.h');
    assert.strictEqual(EM500_BLOCK_START, Number(block[1]));
    assert.strictEqual(EM500_ENERGY_START, Number(energy[1]));
}

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
    assertSimulatorMatchesFirmwareSourceRegister();
    assertSimulatorMatchesFirmwarePowerRegister();
    assertSimulatorMatchesFirmwareBlockStarts();

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

    /* The superseded address must not carry the tariff. If it did, a rig could
     * pass while the firmware polled the other register and saw nothing. */
    await withServer('em500-single-generator', async (port) => {
        assert.strictEqual(await readU16(port, 31, EM500_SOURCE_ADDRESS), 1);
        assert.strictEqual(await readU16(port, 31, EM500_SUPERSEDED_SOURCE_ADDRESS), 0);
    });

    /*
     * THE TWO BLOCK READS, on the wire, in one transaction each.
     *
     * Asserted as raw words rather than decoded values: the decode is the
     * firmware's job and em500_block_test.c executes it. What this proves is
     * that a 72-register request from 0x0002 and a 40-register request from
     * 0x1B20 both answer, and that the two values a wrong decode would destroy
     * survive the wire -- L2 exporting (a negative signed long) and a counter
     * above 2^32 (which a two-word read reports as zero).
     */
    await withServer('normal', async (port) => {
        /* Byte 8 is the PDU's byte count; the registers start at 9. */
        const block = await request(port, 31, 3, EM500_BLOCK_START, 72);
        assert.strictEqual(block.readUInt8(8), 144,
            'measurement block must answer 72 registers in ONE read');
        const word = (frame, index) => frame.readUInt16BE(9 + index * 2);

        /* L2 active power at 0x0016 is two words at register offset 20. */
        const l2 = block.readInt32BE(9 + 20 * 2);
        assert.ok(l2 < 0, 'L2 must export, so the signed path is exercised');
        assert.strictEqual(l2, -2000000);

        /* L1 at 0x0014 imports, so the two are not the same number and a map
         * shifted by one register cannot satisfy both. */
        const l1 = block.readInt32BE(9 + 18 * 2);
        assert.ok(l1 > 0 && l1 !== l2);

        const energy = await request(port, 31, 3, EM500_ENERGY_START, 40);
        assert.strictEqual(energy.readUInt8(8), 80,
            'energy block must answer 40 registers in ONE read');
        /* Total imported at 0x1B20, four words at offset 0. A 32-bit decode
         * reads the first two, which must be non-zero here -- otherwise this
         * fixture could not tell a 64-bit decode from a 32-bit one. */
        assert.ok(word(energy, 0) !== 0 || word(energy, 1) !== 0,
            'fixture must exceed 2^32 or it cannot catch a 32-bit decode');
        const total = (BigInt(word(energy, 0)) << 48n) | (BigInt(word(energy, 1)) << 32n) |
                      (BigInt(word(energy, 2)) << 16n) | BigInt(word(energy, 3));
        assert.strictEqual(total, 500000000123n);
    });

    /*
     * THE WIRE, END TO END: tariff 0 -> 1 -> 0 with the curtailment command the
     * generator phase requires, asserted as the exact register value.
     *
     * A 45 % limit on the Huawei x10 register at 40125 is the word 450. The
     * assertions below refuse 45 explicitly, because 45 on the wire commands
     * 4.5 % -- the inverter would be told to stop, the readback would echo 45,
     * decode with the same wrong scale, agree with the request and report the
     * command CONFIRMED. Nothing downstream can catch that; only the word can.
     */
    await withServer('em500-single-toggle', async (port) => {
        const huawei = DEVICES.get(21);
        assert.strictEqual(huawei.limitAddress, 40125);

        /* GRID: tariff reads 0, and full output is 1000, never 100. */
        assert.strictEqual(await readU16(port, 31, EM500_SOURCE_ADDRESS), 0);
        await request(port, 21, 6, huawei.limitAddress, 1000);
        assert.strictEqual(await readU16(port, 21, huawei.limitAddress), 1000);
        assert.notStrictEqual(await readU16(port, 21, huawei.limitAddress), 100);

        /* GENERATOR: tariff reads non-zero, PV is curtailed to 45 % = 450. */
        assert.strictEqual(await readU16(port, 31, EM500_SOURCE_ADDRESS), 1);
        const write = await request(port, 21, 6, huawei.limitAddress, 450);
        assert.strictEqual(write.readUInt8(7), 6, 'FC06 was refused, not echoed');
        assert.strictEqual(write.readUInt16BE(8), huawei.limitAddress);
        assert.strictEqual(write.readUInt16BE(10), 450);
        const curtailed = await readU16(port, 21, huawei.limitAddress);
        assert.strictEqual(curtailed, 450);
        assert.notStrictEqual(curtailed, 45);
        assert.notStrictEqual(curtailed, 4500);
        assert.ok(curtailed < 1000, 'the generator phase must actually curtail');

        /* GRID again: the limit is released back to full output. */
        assert.strictEqual(await readU16(port, 31, EM500_SOURCE_ADDRESS), 0);
        await request(port, 21, 6, huawei.limitAddress, 1000);
        assert.strictEqual(await readU16(port, 21, huawei.limitAddress), 1000);
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
