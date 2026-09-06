'use strict';

const net = require('net');
const assert = require('assert');

const args = new Set(process.argv.slice(2));
const portArg = process.argv.find((arg) => arg.startsWith('--port='));
const scenarioArg = process.argv.find((arg) => arg.startsWith('--scenario='));
const PORT = Number(portArg ? portArg.split('=')[1] : 1502);
const SCENARIO = String(scenarioArg ? scenarioArg.split('=')[1] : 'normal').toLowerCase();
const SELF_TEST = args.has('--self-test');

const EM500_SOURCE_ADDRESS = 0x2160;
// 58, not 57: confirmed on four independent sources -- live hardware probing
// (docs/METER_COMMISSIONING_2026-07-29.md sign-off), the SolTrix EMS
// driver-registry (server/runtime/soltrix/driver-registry/drivers/
// em500-energy-meter.json, "activePowerTotal" @58, "verified live"), and
// SolTrix's own real-device reference doc
// (server/docs/planning_docs/EM500_WM15_METER_REFERENCE.md). The firmware's
// config default is still 57 (components/config_manager/config_manager.c) --
// meter_manager.c tolerates either value in config, but a real EM500 answers
// activePowerTotal at 58, so the simulator must emulate the real meter here,
// not the firmware's uncorrected default. Do not "fix" this back to 57.
const EM500_POWER_ADDRESS = 58;
const EM500_TARIFF1_IMPORT_ADDRESS = 0x1B48;
const EM500_TARIFF2_IMPORT_ADDRESS = 0x1B5C;

const DEVICES = new Map([
    [21, { kind: 'inverter', name: 'Huawei SUN2000', identityAddress: 40000, identity: 0xA021, powerAddress: 40010, powerW: 31000, limitAddress: 40125, limitRaw: 620 }],
    [22, { kind: 'inverter', name: 'GoodWe Commercial', identityAddress: 41000, identity: 0xA022, powerAddress: 41010, powerW: 27500, limitAddress: 41125, limitRaw: 550 }],
    [23, { kind: 'inverter', name: 'Solis Commercial', identityAddress: 42000, identity: 0xA023, powerAddress: 42010, powerW: 18500, limitAddress: 42125, limitRaw: 370 }],
]);

const EM500_DEVICES = new Map([
    [31, { kind: 'em500', name: 'EM500 Grid or Common Bus', role: 'grid' }],
    [32, { kind: 'em500', name: 'EM500 Generator', role: 'generator' }],
]);

// Carlo Gavazzi WM15 (bench unit WM1596AV53XOSX, fw 1.3) -- register map and
// quirks per server/docs/planning_docs/EM500_WM15_METER_REFERENCE.md and
// server/runtime/soltrix/driver-registry/drivers/carlo-gavazzi-wm15.json in
// the SolTrix repo (both independently verified against the real bench
// meter). Unlike the EM500 there is NO address-1 offset quirk here -- these
// are the real wire addresses. INT32 values are LSW-first (CDAB), not the
// ABCD/MSW-first order the EM500 and the inverters use, and an unmeasured
// phase answers the 0x7FFFFFFF sentinel rather than zero.
const WM15_IDENTITY_ADDRESS = 0x000B;
const WM15_VLN_L1_ADDRESS = 0x00;
const WM15_VLN_L2_ADDRESS = 0x02;
const WM15_VLN_L3_ADDRESS = 0x04;
const WM15_W_L1_ADDRESS = 0x12;
const WM15_HZ_ADDRESS = 0x33;
const WM15_KWH_IMPORT_ADDRESS = 0x34;
const WM15_KWH_EXPORT_ADDRESS = 0x4E;
const WM15_NA_INT32 = 0x7FFFFFFF;

const WM15_DEVICES = new Map([
    [41, { kind: 'wm15', name: 'Carlo Gavazzi WM15 Generator', measuringSystem: '1P' }],
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

// WM15 word order: LSW-first (CDAB) -- the reverse of signed32Words/
// unsigned32Words above, which are MSW-first (ABCD) for the EM500 and
// inverters. Getting this backwards silently produces a plausible-looking
// but wrong 32-bit value, which is exactly the class of bug this simulator
// exists to catch before it reaches real hardware.
function signed32WordsLswFirst(value) {
    const raw = value | 0;
    return [raw & 0xFFFF, (raw >>> 16) & 0xFFFF];
}

function unsigned32WordsLswFirst(value) {
    const raw = value >>> 0;
    return [raw & 0xFFFF, (raw >>> 16) & 0xFFFF];
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

function wm15ScenarioValues(scenario, context) {
    let wattsL1 = 3200;
    switch (scenario) {
    case 'wm15-zero-output':
        wattsL1 = 0;
        break;
    case 'wm15-fault':
        wattsL1 = -150;
        break;
    default:
        break;
    }
    return {
        vL1nRaw: 2312,
        wattsL1Raw: wattsL1 * 10,
        hzRaw: 501,
        kwhImportRaw: (context.wm15ImportKwhX10 = (context.wm15ImportKwhX10 || 41200) + 1),
        kwhExportRaw: 27520,
    };
}

function wordsFor(device, address, count, scenario = 'normal', context = {}) {
    const words = new Array(count).fill(0);
    if (device.kind === 'wm15') {
        const values = wm15ScenarioValues(scenario, context);
        writeWords(words, address, WM15_IDENTITY_ADDRESS, [0x0096]);
        writeWords(words, address, WM15_VLN_L1_ADDRESS, unsigned32WordsLswFirst(values.vL1nRaw));
        if (device.measuringSystem === '1P') {
            writeWords(words, address, WM15_VLN_L2_ADDRESS, unsigned32WordsLswFirst(WM15_NA_INT32));
            writeWords(words, address, WM15_VLN_L3_ADDRESS, unsigned32WordsLswFirst(WM15_NA_INT32));
        }
        writeWords(words, address, WM15_W_L1_ADDRESS, signed32WordsLswFirst(values.wattsL1Raw));
        writeWords(words, address, WM15_HZ_ADDRESS, [values.hzRaw & 0xFFFF]);
        writeWords(words, address, WM15_KWH_IMPORT_ADDRESS,
            unsigned32WordsLswFirst(values.kwhImportRaw));
        writeWords(words, address, WM15_KWH_EXPORT_ADDRESS,
            unsigned32WordsLswFirst(values.kwhExportRaw));
        return words;
    }
    if (device.kind === 'em500') {
        const values = em500ScenarioValues(device, scenario, context);
        writeWords(words, address, EM500_SOURCE_ADDRESS, [values.sourceValue]);
        writeWords(words, address, EM500_POWER_ADDRESS, signed32Words(values.powerRaw));
        writeWords(words, address, EM500_TARIFF1_IMPORT_ADDRESS,
            unsigned32Words(values.tariff1ImportRaw));
        writeWords(words, address, EM500_TARIFF2_IMPORT_ADDRESS,
            unsigned32Words(values.tariff2ImportRaw));
        return words;
    }

    writeWords(words, address, device.identityAddress, [device.identity]);
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
    const device = DEVICES.get(unitId) || EM500_DEVICES.get(unitId) || WM15_DEVICES.get(unitId);

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

        const wm15Voltage = await request(port, 41, 3, WM15_VLN_L1_ADDRESS, 2);
        // LSW-first: low word first in the wire order, so the 32-bit value
        // must be reassembled as (hiWordAtOffset2 << 16) | loWordAtOffset0.
        const wm15VRaw = (wm15Voltage.readUInt16BE(11) << 16) | wm15Voltage.readUInt16BE(9);
        assert.strictEqual(wm15VRaw, 2312);

        const wm15L2Na = await request(port, 41, 3, WM15_VLN_L2_ADDRESS, 2);
        const wm15L2Raw = (wm15L2Na.readUInt16BE(11) << 16) | wm15L2Na.readUInt16BE(9);
        assert.strictEqual(wm15L2Raw, WM15_NA_INT32);

        const wm15Watts = await request(port, 41, 3, WM15_W_L1_ADDRESS, 2);
        const wm15WRaw = (wm15Watts.readInt16BE(11) << 16) | wm15Watts.readUInt16BE(9);
        assert.strictEqual(wm15WRaw, 32000);

        const wm15Hz = await request(port, 41, 3, WM15_HZ_ADDRESS, 1);
        assert.strictEqual(wm15Hz.readUInt16BE(9), 501);

        const unknown = await request(port, 99, 3, 40000, 1);
        assert.strictEqual(unknown.readUInt8(7), 0x83);

        console.log(JSON.stringify({
            result: 'PASS',
            simulator: 'SolTrix Modbus inverter, EM500 and WM15 simulator',
            inverterUnits: [...DEVICES.keys()],
            em500Units: [...EM500_DEVICES.keys()],
            wm15Units: [...WM15_DEVICES.keys()],
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
            console.log(`SolTrix Modbus simulator listening on 0.0.0.0:${PORT} scenario=${SCENARIO} inverter-units=21,22,23 em500-units=31,32 wm15-units=41`);
        });
    }
}

module.exports = {
    DEVICES,
    EM500_DEVICES,
    WM15_DEVICES,
    EM500_SOURCE_ADDRESS,
    EM500_POWER_ADDRESS,
    WM15_VLN_L1_ADDRESS,
    WM15_W_L1_ADDRESS,
    WM15_HZ_ADDRESS,
    WM15_KWH_IMPORT_ADDRESS,
    WM15_NA_INT32,
    createServer,
    request,
    wordsFor,
    runSelfTest,
};
