'use strict';

const net = require('net');
const assert = require('assert');

const args = new Set(process.argv.slice(2));
const portArg = process.argv.find((arg) => arg.startsWith('--port='));
const scenarioArg = process.argv.find((arg) => arg.startsWith('--scenario='));
const PORT = Number(portArg ? portArg.split('=')[1] : 1503);
const SCENARIO = String(scenarioArg ? scenarioArg.split('=')[1] : 'normal').toLowerCase();
const SELF_TEST = args.has('--self-test');

/*
 * The tariff / source-detection digital input, and the register the firmware
 * actually polls: SOURCE_DETECTION_SINGLE_REGISTER_DEFAULT in
 * components/source_detection/include/source_detection_config.h. Zero is grid,
 * any energised input is generator.
 *
 * This was 0x2160 and that was wrong in the way that matters. The firmware's
 * default was corrected to 0x2100 on 2026-07-29 after the installed meters
 * returned exception 0x02, illegal data address, for a single-register read at
 * 0x2160 (docs/METER_COMMISSIONING_2026-07-29.md). A lab rig serving the tariff
 * somewhere the firmware never looks reads a permanent zero, reports GRID for
 * ever, and never once exercises the curtailment path the product exists for --
 * a green lab run proving nothing. soltrix_modbus_simulator_test.js now pins
 * this constant against the firmware header so the two cannot drift again.
 */
const EM500_SOURCE_ADDRESS = 0x2100;
/* Superseded. Deliberately left unmapped so a read here returns zero rather
 * than the tariff, which is the closest this simulator gets to the real meters'
 * refusal and stops the old address from silently appearing to work. */
const EM500_SUPERSEDED_SOURCE_ADDRESS = 0x2160;
/*
 * "Eqv. Active Power", Table 1. This was 57 and the firmware asks for 0x003A,
 * which is 58 -- so every EM500 run against this simulator read a register the
 * firmware never requests, and the one address the control loop depends on was
 * never actually exercised. An off-by-one that a green lab run would not show.
 * soltrix_modbus_simulator_test.js now pins it against meter_profiles.c, the same
 * way EM500_SOURCE_ADDRESS is pinned, so the two cannot drift again.
 */
const EM500_POWER_ADDRESS = 0x003A;
const EM500_TARIFF1_IMPORT_ADDRESS = 0x1B48;
const EM500_TARIFF2_IMPORT_ADDRESS = 0x1B5C;

/*
 * THE FULL TABLE 1 BLOCK AND THE TABLE 3 COUNTERS.
 *
 * The firmware now reads 0002H..0049H and 1B20H..1B47H in one transaction each.
 * Serving only the three registers this simulator used to know meant those
 * blocks came back as zeros, which decodes cleanly to a plant at 0 V -- the
 * failure that looks like data rather than like an absence.
 *
 * The numbers below are one physically coherent site, not noise: 400 V, roughly
 * balanced, with L2 exporting so the SIGNED path is exercised, and counters
 * above 2^32 so the 64-bit path is exercised. A simulator whose values are all
 * small and all positive cannot fail the two decodes that actually go wrong.
 */
const EM500_BLOCK_START = 0x0002;
const EM500_ENERGY_START = 0x1B20;


const DEVICES = new Map([
    [21, { kind: 'inverter', name: 'Huawei SUN2000', identityAddress: 40000, identity: 0xA021, powerAddress: 40010, powerW: 32000, limitAddress: 40125, limitRaw: 620, telemetryLayout: 'huawei.v3' }],
    [22, { kind: 'inverter', name: 'GoodWe Commercial', identityAddress: 41000, identity: 0xA022, powerAddress: 41010, powerW: 27500, limitAddress: 41125, limitRaw: 550 }],
    [23, { kind: 'inverter', name: 'Solis Commercial', identityAddress: 42000, identity: 0xA023, powerAddress: 42010, powerW: 18500, limitAddress: 42125, limitRaw: 370 }],
]);

const EM500_DEVICES = new Map([
    [31, { kind: 'em500', name: 'EM500 Grid or Common Bus', role: 'grid' }],
    [32, { kind: 'em500', name: 'EM500 Generator', role: 'generator' }],
]);

function writeWords(words, requestAddress, base, values) {
    values.forEach((value, index) => {
        const offset = base + index - requestAddress;
        if (offset >= 0 && offset < words.length) words[offset] = value & 0xFFFF;
    });
}

function signed32Words(value) {
    const raw = value | 0;
    return [(raw >>> 16) & 0xFFFF, raw & 0xFFFF];
}

function unsigned32Words(value) {
    const raw = value >>> 0;
    return [(raw >>> 16) & 0xFFFF, raw & 0xFFFF];
}

function unsigned64Words(value) {
    /* Table 3 counters are four words. BigInt because the whole point of the
     * field is that it exceeds what a 32-bit read -- or a JS bitwise operator,
     * which truncates to 32 bits -- can carry. */
    const raw = BigInt(value);
    return [
        Number((raw >> 48n) & 0xFFFFn),
        Number((raw >> 32n) & 0xFFFFn),
        Number((raw >> 16n) & 0xFFFFn),
        Number(raw & 0xFFFFn),
    ];
}

/* One coherent three-phase site, in the manual's raw units. Grid meters carry
 * the load and export on L2; generator meters sit near idle. */
function em500BlockWords(words, requestAddress, device) {
    const grid = device.role === 'grid';
    const put = (address, value, signed) => writeWords(words, requestAddress, address,
        signed ? signed32Words(value) : unsigned32Words(value));

    /* Phase voltage, V/100. */
    put(0x0002, 23012); put(0x0004, 22987); put(0x0006, 23105);
    /* Current, A/10000. */
    put(0x0008, grid ? 3567890 : 120000);
    put(0x000A, grid ? 1200000 : 118000);
    put(0x000C, grid ? 2000000 : 121000);
    /* Line to line, V/100. */
    put(0x000E, 39876); put(0x0010, 39912); put(0x0012, 40001);
    /* Active power, W/100, SIGNED. L2 exports on the grid meter. */
    put(0x0014, grid ? 12266000 : 800000, true);
    put(0x0016, grid ? -2000000 : 790000, true);
    put(0x0018, grid ? 12388000 : 810000, true);
    /* Reactive, var/100, signed. */
    put(0x001A, 500000, true); put(0x001C, -250000, true); put(0x001E, 750000, true);
    /* Apparent, VA/100, unsigned. */
    put(0x0020, 12300000); put(0x0022, 2100000); put(0x0024, 12400000);
    /* Power factor, /10000, signed -- L2 leads. */
    put(0x0026, 9970, true); put(0x0028, -9520, true); put(0x002A, 9990, true);
    /* Whole installation. */
    put(0x0032, 49985);                         /* frequency, Hz/1000 */
    put(0x0034, 23034); put(0x0036, 39930);     /* eqv phase and line voltage */
    put(0x0038, grid ? 2255963 : 119667);       /* eqv current */
    put(0x003C, 1000000, true);                 /* eqv reactive */
    put(0x003E, grid ? 26800000 : 2400000);     /* eqv apparent */
    put(0x0040, 9850, true);                    /* eqv power factor */
    put(0x0042, 120); put(0x0044, 95); put(0x0046, 4310);  /* asymmetry, %/100 */
    put(0x0048, grid ? 812345 : 40000);         /* neutral current */
}

/* Table 3, kWh/100, four words each. Deliberately above 2^32: a counter that
 * fits in 32 bits cannot show whether the firmware decoded four words or two. */
function em500EnergyWords(words, requestAddress, device) {
    const grid = device.role === 'grid';
    const put = (address, value) => writeWords(words, requestAddress, address,
        unsigned64Words(value));
    put(0x1B20, grid ? 500000000123n : 12345678n);   /* total imported */
    put(0x1B24, grid ? 8765432100n : 0n);            /* total exported */
    put(0x1B28, 98765n);
    put(0x1B2C, 4321n);
    put(0x1B30, 222222n);
    put(0x1B34, 5000n);
    put(0x1B38, 2500n);
    put(0x1B3C, 1000n);
    put(0x1B40, 300n);
    put(0x1B44, 7777n);
}

function em500ScenarioValues(device, scenario, context) {
    let sourceValue = 0;
    let powerRaw = device.role === 'grid' ? 5000 : 200;

    switch (scenario) {
    case 'em500-single-generator':
        sourceValue = 1;
        break;
    case 'em500-single-toggle':
        sourceValue = (context.sourceReads++ % 2) === 0 ? 0 : 1;
        break;
    case 'em500-dual-generator':
        powerRaw = device.role === 'generator' ? 5000 : 200;
        break;
    case 'em500-dual-conflict':
        powerRaw = 5000;
        break;
    case 'em500-dual-none':
        powerRaw = 200;
        break;
    case 'em500-threshold-hover':
        powerRaw = device.role === 'generator'
            ? ((context.generatorPowerReads++ % 2) === 0 ? 900 : 1100)
            : 200;
        break;
    case 'em500-single-grid':
    case 'em500-dual-grid':
    case 'normal':
    default:
        break;
    }

    return {
        sourceValue,
        powerRaw,
        tariff1ImportRaw: device.role === 'grid' ? 12345 : 100,
        tariff2ImportRaw: device.role === 'generator' ? 6789 : 200,
    };
}

/*
 * THE HUAWEI TELEMETRY BLOCK, 32016..32117.
 *
 * The firmware reads this in one FC03 to render the inverter page. Served with a
 * physically coherent 60 kW machine on a 400 V grid, scaled by the device's own
 * rating so the three simulated inverters do not all read identically -- a rig
 * where every machine reports the same numbers cannot show a page that has
 * mixed two of them up.
 *
 * Source for every address, unit and gain: "Solar Inverter Modbus Interface
 * Definitions (V3.0)", Huawei, Issue 01 (2023-01-17), section 3. Gains are
 * DIVISORS, so the raw words below are the value times the gain.
 */
function huaweiTelemetryWords(words, requestAddress, device) {
    const ratedW = device.powerW || 32000;
    const k = ratedW / 60000;   /* against the 60 kW reference machine */
    const put16 = (address, value) =>
        writeWords(words, requestAddress, address, [Math.round(value) & 0xFFFF]);
    const put32 = (address, value) =>
        writeWords(words, requestAddress, address, signed32Words(Math.round(value)));

    /* PV strings: V gain 10, A gain 100. Four distinct strings. */
    for (let string = 0; string < 4; string += 1) {
        put16(32016 + 2 * string, 6000 + string * 40);          /* 600.0..612.0 V */
        put16(32017 + 2 * string, Math.round(1200 * k) + string * 20);
    }
    put32(32064, ratedW * 1.02);          /* DC power, kW gain 1000 => raw W */

    put16(32066, 3987); put16(32067, 3991); put16(32068, 4001);  /* line, V gain 10 */
    put16(32069, 2301); put16(32070, 2298); put16(32071, 2311);  /* phase, V gain 10 */
    put32(32072, Math.round(84120 * k));  /* A gain 1000, TWO registers apart */
    put32(32074, Math.round(83550 * k));
    put32(32076, Math.round(85010 * k));

    put32(32078, ratedW * 1.04);          /* peak today */
    put32(32080, ratedW);                 /* active power, matches powerAddress */
    put32(32082, -1250);                  /* reactive, signed */
    put16(32084, 998);                    /* power factor, gain 1000 */
    put16(32085, 4998);                   /* frequency, gain 100 */
    put16(32086, 9862);                   /* efficiency, gain 100 */
    put16(32087, 412);                    /* internal temperature, gain 10 */
    put16(32088, 30000);                  /* insulation, gain 1000 */
    put16(32089, 512);                    /* device status, raw */
    put16(32090, 0);                      /* fault code, raw: healthy */

    /* Energy, U32, kWh gain 100. */
    const put32u = (address, value) =>
        writeWords(words, requestAddress, address, unsigned32Words(Math.round(value)));
    put32u(32106, 12345678 * k);          /* lifetime */
    put32u(32108, 12999999 * k);          /* lifetime DC input */
    put32u(32114, 24567 * k);             /* today */
    put32u(32116, 567890 * k);            /* this month */
}

function wordsFor(device, address, count, scenario = 'normal', context = {}) {
    const words = new Array(count).fill(0);
    if (device.kind === 'em500') {
        const values = em500ScenarioValues(device, scenario, context);
        writeWords(words, address, EM500_SOURCE_ADDRESS, [values.sourceValue]);
        writeWords(words, address, EM500_POWER_ADDRESS, signed32Words(values.powerRaw));
        writeWords(words, address, EM500_TARIFF1_IMPORT_ADDRESS,
            unsigned32Words(values.tariff1ImportRaw));
        writeWords(words, address, EM500_TARIFF2_IMPORT_ADDRESS,
            unsigned32Words(values.tariff2ImportRaw));
        /* Written after the scenario power so a scenario that moves the total
         * still wins at 0x003A: the scenarios exist to drive source detection
         * and the block must not quietly override them. */
        em500BlockWords(words, address, device);
        writeWords(words, address, EM500_POWER_ADDRESS, signed32Words(values.powerRaw));
        em500EnergyWords(words, address, device);
        return words;
    }

    writeWords(words, address, device.identityAddress, [device.identity]);
    /* The telemetry block first, then the discrete registers, so a device whose
     * power address falls inside the block still reports the scenario's own
     * value there rather than the block's. */
    if (device.telemetryLayout === 'huawei.v3') huaweiTelemetryWords(words, address, device);
    writeWords(words, address, device.powerAddress, signed32Words(device.powerW));
    writeWords(words, address, device.limitAddress, [device.limitRaw]);
    return words;
}

function exception(transactionId, unitId, functionCode, code) {
    const frame = Buffer.alloc(9);
    frame.writeUInt16BE(transactionId, 0);
    frame.writeUInt16BE(0, 2);
    frame.writeUInt16BE(3, 4);
    frame.writeUInt8(unitId, 6);
    frame.writeUInt8(functionCode | 0x80, 7);
    frame.writeUInt8(code, 8);
    return frame;
}

function response(transactionId, unitId, pdu) {
    const frame = Buffer.alloc(7 + pdu.length);
    frame.writeUInt16BE(transactionId, 0);
    frame.writeUInt16BE(0, 2);
    frame.writeUInt16BE(1 + pdu.length, 4);
    frame.writeUInt8(unitId, 6);
    pdu.copy(frame, 7);
    return frame;
}

function handleRequest(socket, frame, scenario, context) {
    if (frame.length < 8) return;
    const transactionId = frame.readUInt16BE(0);
    const unitId = frame.readUInt8(6);
    const functionCode = frame.readUInt8(7);
    const device = DEVICES.get(unitId) || EM500_DEVICES.get(unitId);

    if (scenario === 'comm-lost') {
        socket.destroy();
        return;
    }
    if (scenario === 'timeout') return;
    if (!device) {
        socket.write(exception(transactionId, unitId, functionCode, 0x0B));
        return;
    }

    if (functionCode === 3 || functionCode === 4) {
        if (frame.length < 12) return;
        const address = frame.readUInt16BE(8);
        const count = frame.readUInt16BE(10);
        if (count < 1 || count > 125) {
            socket.write(exception(transactionId, unitId, functionCode, 3));
            return;
        }
        const words = wordsFor(device, address, count, scenario, context);
        const pdu = Buffer.alloc(2 + count * 2);
        pdu.writeUInt8(functionCode, 0);
        pdu.writeUInt8(count * 2, 1);
        words.forEach((word, index) => pdu.writeUInt16BE(word, 2 + index * 2));
        socket.write(response(transactionId, unitId, pdu));
        return;
    }

    if (device.kind !== 'inverter') {
        socket.write(exception(transactionId, unitId, functionCode, 1));
        return;
    }

    if (functionCode === 6) {
        if (frame.length < 12) return;
        const address = frame.readUInt16BE(8);
        const value = frame.readUInt16BE(10);
        if (address !== device.limitAddress || value > 1000) {
            socket.write(exception(transactionId, unitId, functionCode, 2));
            return;
        }
        device.limitRaw = scenario === 'rollback' ? 0 : value;
        const pdu = Buffer.alloc(5);
        pdu.writeUInt8(6, 0);
        pdu.writeUInt16BE(address, 1);
        pdu.writeUInt16BE(value, 3);
        socket.write(response(transactionId, unitId, pdu));
        return;
    }

    if (functionCode === 16) {
        if (frame.length < 13) return;
        const address = frame.readUInt16BE(8);
        const count = frame.readUInt16BE(10);
        const byteCount = frame.readUInt8(12);
        if (address !== device.limitAddress || count !== 1 || byteCount !== 2) {
            socket.write(exception(transactionId, unitId, functionCode, 2));
            return;
        }
        const value = frame.readUInt16BE(13);
        if (value > 1000) {
            socket.write(exception(transactionId, unitId, functionCode, 3));
            return;
        }
        device.limitRaw = scenario === 'rollback' ? 0 : value;
        const pdu = Buffer.alloc(5);
        pdu.writeUInt8(16, 0);
        pdu.writeUInt16BE(address, 1);
        pdu.writeUInt16BE(1, 3);
        socket.write(response(transactionId, unitId, pdu));
        return;
    }

    socket.write(exception(transactionId, unitId, functionCode, 1));
}

function createServer(scenario = SCENARIO) {
    const context = { sourceReads: 0, generatorPowerReads: 0 };
    return net.createServer((socket) => {
        let pending = Buffer.alloc(0);
        socket.on('data', (chunk) => {
            pending = Buffer.concat([pending, chunk]);
            while (pending.length >= 7) {
                const pduLength = pending.readUInt16BE(4);
                const frameLength = 6 + pduLength;
                if (pending.length < frameLength) break;
                const frame = pending.subarray(0, frameLength);
                pending = pending.subarray(frameLength);
                handleRequest(socket, frame, scenario, context);
            }
        });
    });
}

function request(port, unitId, functionCode, address, valueOrCount, timeoutMs = 800) {
    return new Promise((resolve, reject) => {
        const client = net.createConnection({ host: '127.0.0.1', port });
        const transactionId = Math.floor(Math.random() * 0xFFFF);
        const frame = Buffer.alloc(12);
        frame.writeUInt16BE(transactionId, 0);
        frame.writeUInt16BE(0, 2);
        frame.writeUInt16BE(6, 4);
        frame.writeUInt8(unitId, 6);
        frame.writeUInt8(functionCode, 7);
        frame.writeUInt16BE(address, 8);
        frame.writeUInt16BE(valueOrCount, 10);
        const timer = setTimeout(() => {
            client.destroy();
            reject(new Error('timeout'));
        }, timeoutMs);
        client.on('connect', () => client.write(frame));
        client.on('data', (data) => {
            clearTimeout(timer);
            client.end();
            resolve(data);
        });
        client.on('error', (error) => {
            clearTimeout(timer);
            reject(error);
        });
    });
}

async function runSelfTest() {
    const server = createServer('normal');
    await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve));
    const port = server.address().port;
    try {
        for (const [unitId, device] of DEVICES) {
            const identity = await request(port, unitId, 3, device.identityAddress, 1);
            assert.strictEqual(identity.readUInt8(7), 3);
            assert.strictEqual(identity.readUInt16BE(9), device.identity);

            const power = await request(port, unitId, 3, device.powerAddress, 2);
            const powerW = power.readInt32BE(9);
            assert.strictEqual(powerW, device.powerW);

            const targetRaw = 430 + (unitId - 21) * 10;
            const write = await request(port, unitId, 6, device.limitAddress, targetRaw);
            assert.strictEqual(write.readUInt8(7), 6);
            const readback = await request(port, unitId, 3, device.limitAddress, 1);
            assert.strictEqual(readback.readUInt16BE(9), targetRaw);
        }

        const source = await request(port, 31, 3, EM500_SOURCE_ADDRESS, 1);
        assert.strictEqual(source.readUInt16BE(9), 0);
        const gridPower = await request(port, 31, 3, EM500_POWER_ADDRESS, 2);
        assert.strictEqual(gridPower.readInt32BE(9), 5000);
        const generatorPower = await request(port, 32, 3, EM500_POWER_ADDRESS, 2);
        assert.strictEqual(generatorPower.readInt32BE(9), 200);

        const unknown = await request(port, 99, 3, 40000, 1);
        assert.strictEqual(unknown.readUInt8(7), 0x83);

        console.log(JSON.stringify({
            result: 'PASS',
            simulator: 'SolTrix Modbus inverter and EM500 simulator',
            inverterUnits: [...DEVICES.keys()],
            em500Units: [...EM500_DEVICES.keys()],
            scenarios: [
                'normal', 'stale', 'comm-lost', 'timeout', 'rollback',
                'em500-single-grid', 'em500-single-generator', 'em500-single-toggle',
                'em500-dual-grid', 'em500-dual-generator', 'em500-dual-conflict',
                'em500-dual-none', 'em500-threshold-hover'
            ],
            productionEvidence: false,
        }));
    } finally {
        await new Promise((resolve) => server.close(resolve));
    }
}

if (require.main === module) {
    if (SELF_TEST) {
        runSelfTest().catch((error) => {
            console.error(error);
            process.exitCode = 1;
        });
    } else {
        const server = createServer();
        server.listen(PORT, '0.0.0.0', () => {
            console.log(`SolTrix Modbus simulator listening on 0.0.0.0:${PORT} scenario=${SCENARIO} inverter-units=21,22,23 em500-units=31,32`);
        });
    }
}

module.exports = {
    DEVICES,
    EM500_DEVICES,
    EM500_SOURCE_ADDRESS,
    EM500_SUPERSEDED_SOURCE_ADDRESS,
    EM500_POWER_ADDRESS,
    EM500_BLOCK_START,
    EM500_ENERGY_START,
    createServer,
    request,
    wordsFor,
    runSelfTest,
};
