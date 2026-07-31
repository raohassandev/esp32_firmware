'use strict';

const net = require('net');
const assert = require('assert');

const args = new Set(process.argv.slice(2));
const portArg = process.argv.find((arg) => arg.startsWith('--port='));
const scenarioArg = process.argv.find((arg) => arg.startsWith('--scenario='));
const PORT = Number(portArg ? portArg.split('=')[1] : 1504);
const SCENARIO = String(scenarioArg ? scenarioArg.split('=')[1] : 'normal').toLowerCase();
const SELF_TEST = args.has('--self-test');

/*
 * Register addresses below are the real Huawei SUN2000 Modbus addresses from
 * "Solar Inverter Modbus Interface Definitions (V3.0)" (docs in
 * D:/Working/FUXA SADA/FUXA-1.3.2/Manuals/Inverter/Huawei), not made-up test
 * addresses. This lets the simulator stand in for the actual meter/inverter
 * a commissioning engineer would point a SCADA/FUXA client at.
 */
const REG = {
    // Signal 1 "Model", RO STR, 15 registers (30 characters), null-padded.
    //
    // This is a DIFFERENT signal from MODEL_ID below and both are real: 30000 is
    // the nameplate NAME as a string, 30070 is a numeric model code. The
    // simulator implemented only 30070, so a controller probing identity the way
    // the manufacturer manual describes read address 30000, got zero, and
    // rejected the device -- while every other register on it answered
    // correctly. The firmware profile huawei.sun2000.pending probes the first
    // word of this string, which is "SU" (0x5355) on every SUN2000.
    //
    // Transcribed from the same source as the firmware profile: "Huawei Inverter
    // Modbus Interface Definitions (V3.0)", Issue 01 (2023-01-17). Nothing here
    // is invented; the register was simply missing.
    MODEL_NAME: 30000,        // STR, 15 regs
    MODEL_ID: 30070,          // U16
    RATED_POWER: 30073,       // U32, gain 1000, kW (Pn) -- 2 regs
    GRID_VOLTAGE_AB: 32066,   // U16, gain 10, V
    GRID_VOLTAGE_BC: 32067,   // U16, gain 10, V
    GRID_VOLTAGE_CA: 32068,   // U16, gain 10, V
    GRID_VOLTAGE_A: 32069,    // U16, gain 10, V (L-N)
    GRID_VOLTAGE_B: 32070,    // U16, gain 10, V (L-N)
    GRID_VOLTAGE_C: 32071,    // U16, gain 10, V (L-N)
    GRID_CURRENT_A: 32072,    // I32, gain 1000, A -- 2 regs
    GRID_CURRENT_B: 32074,    // I32, gain 1000, A -- 2 regs
    GRID_CURRENT_C: 32076,    // I32, gain 1000, A -- 2 regs
    ACTIVE_POWER: 32080,      // I32, gain 1000, kW -- 2 regs
    GRID_FREQUENCY: 32085,    // U16, gain 100, Hz
    ACTIVE_POWER_PERCENT: 40125, // I16, gain 10, %  (RW, FC6) -- "Active Power Percentage Derating"
    ACTIVE_POWER_FIXED: 40126,  // U32, gain 1, W (RW, FC16) -- "Active power fixed value derating", range [0, Pmax]
};

const NOMINAL_VOLTAGE_LN = 230; // V, three-phase four-wire nominal phase voltage
const NOMINAL_VOLTAGE_LL = 400; // V, nominal line-to-line voltage
const NOMINAL_FREQUENCY = 50;   // Hz
const SQRT3 = Math.sqrt(3);

// Small per-read jitter so repeated polls show the meter "living" instead of
// returning a frozen snapshot. Disabled under the 'no-jitter' scenario so the
// self-test can assert exact values.
function jitter(base, fraction) {
    return base * (1 + (Math.random() * 2 - 1) * fraction);
}

const DEVICES = new Map([
    [11, {
        name: 'Huawei SUN2000-100KTL-M2',
        // The NAMEPLATE string served at 30000, which is the model alone: the
        // manual describes register 1 as the "nameplate name of machine" and
        // every SUN2000 nameplate begins "SU". Deliberately not `name` above --
        // that carries a "Huawei " prefix for the simulator's own logging, and
        // serving it here would put "Hu" in the first word and fail an identity
        // probe that is behaving correctly.
        modelName: 'SUN2000-100KTL-M2',
        modelId: 150,
        ratedPowerW: 100000,
        // "available" solar generation before any operator-imposed limit is applied.
        // A real inverter's active power tracks irradiance; this simulator fixes
        // it so tests are deterministic and can assert on the effect of writes.
        availableGenerationW: 82000,
        activePowerPercent: 1000,   // gain 10 => 100.0%, i.e. unlimited by default
        activePowerFixedW: 100000,  // defaults to Pmax, i.e. unlimited by default
    }],
]);

function writeWords(words, requestAddress, base, values) {
    values.forEach((value, index) => {
        const offset = base + index - requestAddress;
        if (offset >= 0 && offset < words.length) words[offset] = value & 0xFFFF;
    });
}

// Modbus packs a string two characters to a register, first character in the
// high byte. Fixed at `registers` words and null-padded, because the manual
// gives this field a fixed length and a client reading all 15 must not see
// whatever happened to follow.
function stringWords(text, registers) {
    const words = new Array(registers).fill(0);
    for (let index = 0; index < registers; index += 1) {
        const high = text.charCodeAt(index * 2);
        const low = text.charCodeAt(index * 2 + 1);
        words[index] = (((Number.isNaN(high) ? 0 : high) & 0xFF) << 8) | ((Number.isNaN(low) ? 0 : low) & 0xFF);
    }
    return words;
}

function signed32Words(value) {
    const raw = value | 0;
    return [(raw >>> 16) & 0xFFFF, raw & 0xFFFF];
}

function unsigned32Words(value) {
    const raw = value >>> 0;
    return [(raw >>> 16) & 0xFFFF, raw & 0xFFFF];
}

function effectiveActivePowerW(device, availableGenerationW = device.availableGenerationW) {
    // activePowerPercent has gain 10 (raw 1000 == 100.0%), so the fraction of
    // rated power is raw / 1000 -- not raw / 1000 / 100, which would silently
    // clamp every inverter to ~1% of its rated power.
    const percentLimitW = device.ratedPowerW * (device.activePowerPercent / 1000);
    const fixedLimitW = device.activePowerFixedW;
    return Math.max(0, Math.min(availableGenerationW, percentLimitW, fixedLimitW));
}

function wordsFor(device, address, count, scenario = 'normal') {
    const words = new Array(count).fill(0);
    const live = scenario !== 'no-jitter';

    // Irradiance wanders a few percent poll-to-poll; voltage/frequency wander
    // much less, matching how a real grid-tied inverter's readings never sit
    // perfectly still even under a constant operator-imposed power limit.
    const availableGenerationW = live ? jitter(device.availableGenerationW, 0.03) : device.availableGenerationW;
    const voltageLL = live ? jitter(NOMINAL_VOLTAGE_LL, 0.01) : NOMINAL_VOLTAGE_LL;
    const voltageLN = live ? jitter(NOMINAL_VOLTAGE_LN, 0.01) : NOMINAL_VOLTAGE_LN;
    const frequency = live ? jitter(NOMINAL_FREQUENCY, 0.001) : NOMINAL_FREQUENCY;
    const activePowerW = effectiveActivePowerW(device, availableGenerationW);

    // Balance current across three phases from the effective active power,
    // assuming unity power factor: P = sqrt(3) * V_LL * I.
    const currentA = activePowerW / (SQRT3 * voltageLL);

    writeWords(words, address, REG.MODEL_NAME, stringWords(device.modelName || device.name, 15));
    writeWords(words, address, REG.MODEL_ID, [device.modelId]);
    writeWords(words, address, REG.RATED_POWER, unsigned32Words(Math.round(device.ratedPowerW / 1000 * 1000)));

    writeWords(words, address, REG.GRID_VOLTAGE_AB, [Math.round(voltageLL * 10)]);
    writeWords(words, address, REG.GRID_VOLTAGE_BC, [Math.round(voltageLL * 10)]);
    writeWords(words, address, REG.GRID_VOLTAGE_CA, [Math.round(voltageLL * 10)]);
    writeWords(words, address, REG.GRID_VOLTAGE_A, [Math.round(voltageLN * 10)]);
    writeWords(words, address, REG.GRID_VOLTAGE_B, [Math.round(voltageLN * 10)]);
    writeWords(words, address, REG.GRID_VOLTAGE_C, [Math.round(voltageLN * 10)]);

    writeWords(words, address, REG.GRID_CURRENT_A, signed32Words(Math.round(currentA * 1000)));
    writeWords(words, address, REG.GRID_CURRENT_B, signed32Words(Math.round(currentA * 1000)));
    writeWords(words, address, REG.GRID_CURRENT_C, signed32Words(Math.round(currentA * 1000)));

    writeWords(words, address, REG.ACTIVE_POWER, signed32Words(Math.round(activePowerW / 1000 * 1000)));
    writeWords(words, address, REG.GRID_FREQUENCY, [Math.round(frequency * 100)]);

    writeWords(words, address, REG.ACTIVE_POWER_PERCENT, [device.activePowerPercent & 0xFFFF]);
    writeWords(words, address, REG.ACTIVE_POWER_FIXED, unsigned32Words(device.activePowerFixedW));

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

function handleRequest(socket, frame, scenario) {
    if (frame.length < 8) return;
    const transactionId = frame.readUInt16BE(0);
    const unitId = frame.readUInt8(6);
    const functionCode = frame.readUInt8(7);
    const device = DEVICES.get(unitId);

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
        const words = wordsFor(device, address, count, scenario);
        const pdu = Buffer.alloc(2 + count * 2);
        pdu.writeUInt8(functionCode, 0);
        pdu.writeUInt8(count * 2, 1);
        words.forEach((word, index) => pdu.writeUInt16BE(word, 2 + index * 2));
        socket.write(response(transactionId, unitId, pdu));
        return;
    }

    if (functionCode === 6) {
        if (frame.length < 12) return;
        const address = frame.readUInt16BE(8);
        const value = frame.readUInt16BE(10);
        if (address !== REG.ACTIVE_POWER_PERCENT || value > 1000) {
            socket.write(exception(transactionId, unitId, functionCode, 2));
            return;
        }
        // A percentage write actually re-derates the reported active power --
        // that is the whole point of the test: writing the limit must move 32080.
        device.activePowerPercent = value;
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
        if (address !== REG.ACTIVE_POWER_FIXED || count !== 2 || byteCount !== 4) {
            socket.write(exception(transactionId, unitId, functionCode, 2));
            return;
        }
        const value = frame.readUInt32BE(13);
        if (value > device.ratedPowerW) {
            socket.write(exception(transactionId, unitId, functionCode, 3));
            return;
        }
        device.activePowerFixedW = value;
        const pdu = Buffer.alloc(5);
        pdu.writeUInt8(16, 0);
        pdu.writeUInt16BE(address, 1);
        pdu.writeUInt16BE(count, 3);
        socket.write(response(transactionId, unitId, pdu));
        return;
    }

    socket.write(exception(transactionId, unitId, functionCode, 1));
}

function createServer(scenario = SCENARIO) {
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
                handleRequest(socket, frame, scenario);
            }
        });
    });
}

function request(port, unitId, functionCode, address, valueOrCount, extra, timeoutMs = 800) {
    return new Promise((resolve, reject) => {
        const client = net.createConnection({ host: '127.0.0.1', port });
        const transactionId = Math.floor(Math.random() * 0xFFFF);
        const hasExtra = Buffer.isBuffer(extra);
        const frame = Buffer.alloc(hasExtra ? 13 + extra.length : 12);
        frame.writeUInt16BE(transactionId, 0);
        frame.writeUInt16BE(0, 2);
        frame.writeUInt16BE(frame.length - 6, 4);
        frame.writeUInt8(unitId, 6);
        frame.writeUInt8(functionCode, 7);
        frame.writeUInt16BE(address, 8);
        frame.writeUInt16BE(valueOrCount, 10);
        if (hasExtra) {
            frame.writeUInt8(extra.length, 12);
            extra.copy(frame, 13);
        }
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
    const server = createServer('no-jitter');
    await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve));
    const port = server.address().port;
    try {
        const unitId = 11;

        // Identity. The controller probes the FIRST WORD of the nameplate
        // string, so that word is asserted directly rather than only the decoded
        // name: a client reading one register is the case that failed in the
        // field, and a test that reads all fifteen would not have caught it.
        const modelOneWord = await request(port, unitId, 3, REG.MODEL_NAME, 1);
        assert.strictEqual(modelOneWord.readUInt16BE(9), 0x5355, 'first word of the nameplate must be "SU"');

        const modelFull = await request(port, unitId, 3, REG.MODEL_NAME, 15);
        let decoded = '';
        for (let i = 0; i < 15; i += 1) decoded += String.fromCharCode(modelFull.readUInt16BE(9 + i * 2) >> 8, modelFull.readUInt16BE(9 + i * 2) & 0xFF);
        assert.strictEqual(decoded.replace(/\0+$/, ''), 'SUN2000-100KTL-M2');

        // 30000 and 30070 are different signals and both must answer.
        const modelId = await request(port, unitId, 3, REG.MODEL_ID, 1);
        assert.strictEqual(modelId.readUInt16BE(9), 150);

        const voltageAB = await request(port, unitId, 3, REG.GRID_VOLTAGE_AB, 1);
        assert.strictEqual(voltageAB.readUInt16BE(9), NOMINAL_VOLTAGE_LL * 10);

        const voltageA = await request(port, unitId, 3, REG.GRID_VOLTAGE_A, 1);
        assert.strictEqual(voltageA.readUInt16BE(9), NOMINAL_VOLTAGE_LN * 10);

        const frequency = await request(port, unitId, 3, REG.GRID_FREQUENCY, 1);
        assert.strictEqual(frequency.readUInt16BE(9), NOMINAL_FREQUENCY * 100);

        const powerBefore = await request(port, unitId, 3, REG.ACTIVE_POWER, 2);
        const powerBeforeKw = powerBefore.readInt32BE(9) / 1000;
        assert.strictEqual(powerBeforeKw, 82); // unlimited: reads the full available generation

        const currentBefore = await request(port, unitId, 3, REG.GRID_CURRENT_A, 2);
        const currentBeforeA = currentBefore.readInt32BE(9) / 1000;
        assert.ok(Math.abs(currentBeforeA - (82000 / (SQRT3 * NOMINAL_VOLTAGE_LL))) < 0.01);

        // Curtail to 50% via the percentage register (FC6) and verify active
        // power AND current actually drop -- this is the point of the exercise.
        const percentWrite = await request(port, unitId, 6, REG.ACTIVE_POWER_PERCENT, 500);
        assert.strictEqual(percentWrite.readUInt8(7), 6);

        const powerAfterPercent = await request(port, unitId, 3, REG.ACTIVE_POWER, 2);
        const powerAfterPercentKw = powerAfterPercent.readInt32BE(9) / 1000;
        assert.strictEqual(powerAfterPercentKw, 50); // min(82kW available, 50kW percent limit, 100kW fixed)

        const currentAfterPercent = await request(port, unitId, 3, REG.GRID_CURRENT_A, 2);
        const currentAfterPercentA = currentAfterPercent.readInt32BE(9) / 1000;
        assert.ok(currentAfterPercentA < currentBeforeA);

        // Reset percent limit, then curtail via the fixed-value register (FC16)
        // to a value below available generation and verify it wins.
        await request(port, unitId, 6, REG.ACTIVE_POWER_PERCENT, 1000);
        const fixedValue = Buffer.alloc(4);
        fixedValue.writeUInt32BE(30000, 0); // 30,000 W fixed limit
        const fixedWrite = await request(port, unitId, 16, REG.ACTIVE_POWER_FIXED, 2, fixedValue);
        assert.strictEqual(fixedWrite.readUInt8(7), 16);

        const powerAfterFixed = await request(port, unitId, 3, REG.ACTIVE_POWER, 2);
        const powerAfterFixedKw = powerAfterFixed.readInt32BE(9) / 1000;
        assert.strictEqual(powerAfterFixedKw, 30); // fixed limit now binds instead of the percent limit

        const unknown = await request(port, 99, 3, REG.ACTIVE_POWER, 2);
        assert.strictEqual(unknown.readUInt8(7), 0x83);

        console.log(JSON.stringify({
            result: 'PASS',
            simulator: 'Huawei SUN2000 Modbus inverter simulator',
            unit: unitId,
            registerMap: REG,
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
            console.log(`Huawei SUN2000 Modbus simulator listening on 0.0.0.0:${PORT} scenario=${SCENARIO} unit=11`);
        });
    }
}

module.exports = {
    DEVICES,
    REG,
    createServer,
    request,
    wordsFor,
    effectiveActivePowerW,
    runSelfTest,
};
