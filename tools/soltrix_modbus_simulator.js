'use strict';

const net = require('net');
const assert = require('assert');

const args = new Set(process.argv.slice(2));
const portArg = process.argv.find((arg) => arg.startsWith('--port='));
const scenarioArg = process.argv.find((arg) => arg.startsWith('--scenario='));
const PORT = Number(portArg ? portArg.split('=')[1] : 1502);
const SCENARIO = String(scenarioArg ? scenarioArg.split('=')[1] : 'normal').toLowerCase();
const SELF_TEST = args.has('--self-test');

const DEVICES = new Map([
    [21, { name: 'Huawei SUN2000', identityAddress: 40000, identity: 0xA021, powerAddress: 40010, powerW: 31000, limitAddress: 40125, limitRaw: 620 }],
    [22, { name: 'GoodWe Commercial', identityAddress: 41000, identity: 0xA022, powerAddress: 41010, powerW: 27500, limitAddress: 41125, limitRaw: 550 }],
    [23, { name: 'Solis Commercial', identityAddress: 42000, identity: 0xA023, powerAddress: 42010, powerW: 18500, limitAddress: 42125, limitRaw: 370 }],
]);

function wordsFor(device, address, count) {
    const words = new Array(count).fill(0);
    const write = (base, values) => {
        values.forEach((value, index) => {
            const offset = base + index - address;
            if (offset >= 0 && offset < count) words[offset] = value & 0xFFFF;
        });
    };
    write(device.identityAddress, [device.identity]);
    const rawPower = device.powerW >>> 0;
    write(device.powerAddress, [(rawPower >>> 16) & 0xFFFF, rawPower & 0xFFFF]);
    write(device.limitAddress, [device.limitRaw]);
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
        const words = wordsFor(device, address, count);
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

        const unknown = await request(port, 99, 3, 40000, 1);
        assert.strictEqual(unknown.readUInt8(7), 0x83);

        console.log(JSON.stringify({
            result: 'PASS',
            simulator: 'SolTrix Modbus inverter simulator',
            units: [...DEVICES.keys()],
            scenarios: ['normal', 'stale', 'comm-lost', 'timeout', 'rollback'],
            huaweiRegister40125: true,
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
            console.log(`SolTrix Modbus simulator listening on 0.0.0.0:${PORT} scenario=${SCENARIO} units=21,22,23`);
        });
    }
}

module.exports = { DEVICES, createServer, request, wordsFor, runSelfTest };
