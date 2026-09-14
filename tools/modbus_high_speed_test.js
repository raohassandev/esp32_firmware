'use strict';

/**
 * High-speed Modbus TCP communication test against the standalone SolTrix
 * simulator (tools/soltrix_modbus_simulator.js).
 *
 * IMPORTANT SCOPE / HONESTY NOTE (read before trusting these numbers):
 * This measures the round-trip cost of the SAME request/response pattern the
 * firmware's Modbus client uses (one persistent TCP connection, one
 * outstanding transaction at a time, wait for the reply before sending the
 * next request) against a Node.js TCP server on loopback. It proves the
 * *code path* -- framing, parsing, serialization -- can drive many
 * transactions per second when the transport underneath it is fast.
 *
 * It does NOT measure the real ZLAN gateway or the real RS485/EM500/WM15
 * bus. That path is bounded by Modbus RTU's own physics (t3.5 inter-frame
 * silence + byte time at the configured baud rate -- see
 * docs/MODBUS_ARCHITECTURE_PROMPT.md for the derived floor, e.g. ~23ms per
 * single-value transaction at 9600 baud, ~3.4ms at 115200). A loopback
 * TCP test will always be far faster than a real RS485 bus and must never
 * be reported as "the achieved field transaction rate" -- only physical
 * hardware evidence (docs/ZLAN_GATEWAY_FIX.md acceptance test) proves that.
 *
 * Every response is also matched strictly to the outstanding MBAP
 * transaction id and unit id before it contributes a latency sample. This
 * mirrors the firmware's receive-resync safety rule: stale/cross-delivered
 * responses are errors, never measurements.
 *
 * Usage:
 *   node tools/modbus_high_speed_test.js [--port=1502] [--transactions=500]
 */

const net = require('net');
const { createServer } = require('./soltrix_modbus_simulator.js');

const portArg = process.argv.find((a) => a.startsWith('--port='));
const countArg = process.argv.find((a) => a.startsWith('--transactions='));
const PORT = Number(portArg ? portArg.split('=')[1] : 0); // 0 = ephemeral, own server
const TRANSACTION_COUNT = Number(countArg ? countArg.split('=')[1] : 500);
const USE_OWN_SERVER = PORT === 0;

function percentile(sortedMs, p) {
    const idx = Math.min(sortedMs.length - 1, Math.floor((p / 100) * sortedMs.length));
    return sortedMs[idx];
}

function buildReadRequest(transactionId, unitId, address, count) {
    const frame = Buffer.alloc(12);
    frame.writeUInt16BE(transactionId, 0);
    frame.writeUInt16BE(0, 2);
    frame.writeUInt16BE(6, 4);
    frame.writeUInt8(unitId, 6);
    frame.writeUInt8(3, 7);
    frame.writeUInt16BE(address, 8);
    frame.writeUInt16BE(count, 10);
    return frame;
}

function validateReadResponse(frame, expectedTransactionId, expectedUnitId, expectedCount) {
    if (!Buffer.isBuffer(frame) || frame.length < 9) {
        throw new Error('short Modbus TCP response');
    }

    const transactionId = frame.readUInt16BE(0);
    const protocolId = frame.readUInt16BE(2);
    const mbapLength = frame.readUInt16BE(4);
    const unitId = frame.readUInt8(6);
    const functionCode = frame.readUInt8(7);

    if (frame.length !== 6 + mbapLength) {
        throw new Error(`MBAP length mismatch: header=${mbapLength}, frame=${frame.length}`);
    }
    if (protocolId !== 0) {
        throw new Error(`unexpected Modbus protocol id ${protocolId}`);
    }
    if (transactionId !== expectedTransactionId) {
        throw new Error(
            `transaction id mismatch: expected ${expectedTransactionId}, got ${transactionId}`,
        );
    }
    if (unitId !== expectedUnitId) {
        throw new Error(`unit id mismatch: expected ${expectedUnitId}, got ${unitId}`);
    }
    if ((functionCode & 0x80) !== 0) {
        const exceptionCode = frame.length > 8 ? frame.readUInt8(8) : -1;
        throw new Error(
            `Modbus exception response: function=0x${functionCode.toString(16)}, code=${exceptionCode}`,
        );
    }
    if (functionCode !== 3) {
        throw new Error(`function mismatch: expected 3, got ${functionCode}`);
    }

    const byteCount = frame.readUInt8(8);
    const expectedBytes = expectedCount * 2;
    if (byteCount !== expectedBytes || frame.length !== 9 + byteCount) {
        throw new Error(
            `read payload size mismatch: expected ${expectedBytes} bytes, got ${byteCount}`,
        );
    }
}

/**
 * Runs N serialized (one-at-a-time, wait-for-reply) transactions over a
 * single persistent connection -- exactly the pattern
 * components/modbus_tcp/modbus_tcp.c uses -- and returns latency stats.
 */
function runSerializedBurst(host, port, unitId, address, count, transactions) {
    return new Promise((resolve, reject) => {
        const client = net.createConnection({ host, port });
        const latenciesMs = [];
        let sent = 0;
        let transactionId = 1;
        let inFlightTransactionId = 0;
        let pending = Buffer.alloc(0);
        let inFlightSentAt = 0n;
        let settled = false;
        const overallStart = process.hrtime.bigint();

        function fail(error) {
            if (settled) return;
            settled = true;
            client.destroy();
            reject(error instanceof Error ? error : new Error(String(error)));
        }

        function sendNext() {
            if (settled) return;
            if (sent >= transactions) {
                const overallMs = Number(process.hrtime.bigint() - overallStart) / 1e6;
                settled = true;
                client.end();
                resolve({ latenciesMs, overallMs });
                return;
            }
            transactionId = (transactionId + 1) & 0xFFFF;
            inFlightTransactionId = transactionId;
            inFlightSentAt = process.hrtime.bigint();
            client.write(buildReadRequest(inFlightTransactionId, unitId, address, count));
        }

        client.on('connect', () => sendNext());
        client.on('data', (chunk) => {
            if (settled) return;
            pending = Buffer.concat([pending, chunk]);
            while (pending.length >= 7) {
                const pduLength = pending.readUInt16BE(4);
                const frameLength = 6 + pduLength;
                if (frameLength < 9) {
                    fail(new Error(`invalid MBAP length ${pduLength}`));
                    return;
                }
                if (pending.length < frameLength) break;
                const frame = pending.subarray(0, frameLength);
                pending = pending.subarray(frameLength);

                try {
                    validateReadResponse(frame, inFlightTransactionId, unitId, count);
                } catch (error) {
                    fail(error);
                    return;
                }

                const elapsedMs = Number(process.hrtime.bigint() - inFlightSentAt) / 1e6;
                latenciesMs.push(elapsedMs);
                sent += 1;
                sendNext();
                if (settled) return;
            }
        });
        client.on('error', fail);
        client.on('close', () => {
            if (!settled && sent < transactions) {
                fail(new Error(`connection closed after ${sent}/${transactions} transactions`));
            }
        });
    });
}

async function main() {
    let server = null;
    const host = '127.0.0.1';
    let port = PORT;

    if (USE_OWN_SERVER) {
        server = createServer('normal');
        await new Promise((resolve) => server.listen(0, host, resolve));
        port = server.address().port;
    }

    console.log(`Target: ${host}:${port} (${USE_OWN_SERVER ? 'in-process loopback simulator' : 'external server'})`);
    console.log(`Serialized (one-outstanding-transaction) pattern, ${TRANSACTION_COUNT} transactions per case.`);
    console.log('Strict MBAP matching: transaction-id + unit-id + function + payload size.\n');

    const cases = [
        { label: 'EM500 single value (2 words: activePowerTotal)', unitId: 31, address: 58, count: 2 },
        { label: 'EM500 block read (fast_power_loop, 62 words)', unitId: 31, address: 2, count: 62 },
        { label: 'WM15 single value (2 words: W L1)', unitId: 41, address: 0x12, count: 2 },
    ];

    const results = [];
    for (const c of cases) {
        const { latenciesMs, overallMs } = await runSerializedBurst(
            host, port, c.unitId, c.address, c.count, TRANSACTION_COUNT,
        );
        const sorted = [...latenciesMs].sort((a, b) => a - b);
        const txnPerSec = (TRANSACTION_COUNT / overallMs) * 1000;
        const row = {
            case: c.label,
            transactions: TRANSACTION_COUNT,
            overallMs: Number(overallMs.toFixed(1)),
            txnPerSec: Number(txnPerSec.toFixed(1)),
            latencyMs: {
                min: Number(sorted[0].toFixed(3)),
                p50: Number(percentile(sorted, 50).toFixed(3)),
                p95: Number(percentile(sorted, 95).toFixed(3)),
                max: Number(sorted[sorted.length - 1].toFixed(3)),
            },
        };
        results.push(row);
        console.log(JSON.stringify(row));
    }

    console.log('\nScope reminder: these numbers are the TCP-loopback ceiling of the');
    console.log('firmware\'s own request/response code path -- they show the transport');
    console.log('CODE is not the bottleneck. The real bottleneck is Modbus RTU physics');
    console.log('on the RS485 leg past the ZLAN gateway (t3.5 + byte time at the');
    console.log('configured baud rate). Compare against docs/MODBUS_ARCHITECTURE_PROMPT.md');
    console.log('and only accept a field transaction rate that is confirmed on real');
    console.log('hardware (docs/ZLAN_GATEWAY_FIX.md acceptance test).');

    if (server) await new Promise((resolve) => server.close(resolve));
}

main().catch((error) => {
    console.error(error);
    process.exitCode = 1;
});
