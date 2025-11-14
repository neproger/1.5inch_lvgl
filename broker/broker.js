/*
  Local MQTT Test Broker (Aedes)
  - Listens on plain TCP and logs all published messages to console
  - Optional username/password auth via .env

  Environment (from .env in this folder):
    BROKER_HOST=127.0.0.1
    BROKER_PORT=1884
    BROKER_USERNAME=   # optional
    BROKER_PASSWORD=   # optional
    BROKER_LOG_HEX=true          # log payload as hex (default true)
    BROKER_LOG_MAX_BYTES=256     # preview length
*/

import { createServer } from 'net';
import { existsSync, readFileSync } from 'fs';
import { dirname, join } from 'path';
import { fileURLToPath } from 'url';
import aedes from 'aedes';

const __dirname = dirname(fileURLToPath(import.meta.url));

function loadEnvFile(envPath) {
    try {
        if (!existsSync(envPath)) return;
        const content = readFileSync(envPath, 'utf8');
        for (const rawLine of content.split(/\r?\n/)) {
            const line = rawLine.trim();
            if (!line || line.startsWith('#')) continue;
            const eq = line.indexOf('=');
            if (eq <= 0) continue;
            const key = line.slice(0, eq).trim();
            let val = line.slice(eq + 1).trim();
            if ((val.startsWith('"') && val.endsWith('"')) || (val.startsWith("'") && val.endsWith("'"))) {
                val = val.slice(1, -1);
            }
            if (process.env[key] === undefined) process.env[key] = val;
        }
    } catch (e) {
        console.warn('[env] failed to load .env:', e.message);
    }
}

loadEnvFile(join(__dirname, '.env'));

function getBool(name, def) {
    const v = process.env[name];
    if (v == null) return def;
    return /^(1|true|yes|on)$/i.test(String(v).trim());
}
function getStr(name, def) {
    const v = process.env[name];
    return v == null || v === '' ? def : v;
}

const BROKER_HOST = getStr('BROKER_HOST', '127.0.0.1');
const BROKER_PORT = Number(getStr('BROKER_PORT', '1884'));
const BROKER_USERNAME = getStr('BROKER_USERNAME', '');
const BROKER_PASSWORD = getStr('BROKER_PASSWORD', '');
const BROKER_LOG_HEX = getBool('BROKER_LOG_HEX', true);
const BROKER_LOG_MAX_BYTES = Number(getStr('BROKER_LOG_MAX_BYTES', '256'));

const broker = aedes();

// Optional auth
if (BROKER_USERNAME || BROKER_PASSWORD) {
    broker.authenticate = (client, username, password, done) => {
        console.log("Authenticate: ", username, password)
        const pwd = password ? password.toString('utf8') : '';
        const ok = username === BROKER_USERNAME && pwd === BROKER_PASSWORD;
        if (!ok) return done(new Error('Auth failed'), false);
        done(null, true);
    };
}

function preview(buf) {
    const b = buf.length > BROKER_LOG_MAX_BYTES ? buf.slice(0, BROKER_LOG_MAX_BYTES) : buf;
    return BROKER_LOG_HEX ? b.toString('hex') : b.toString('utf8');
}

// Простое хранилище состояний наших двух тестовых свитчей
const entityStates = {
    'switch.wifi_breaker_t_switch_1': 'OFF',
    'switch.wifi_breaker_t_switch_2': 'OFF',
};

function toggleState(id) {
    const cur = entityStates[id] || 'OFF';
    const next = cur === 'ON' ? 'OFF' : 'ON';
    entityStates[id] = next;
    return next;
}

// Logging hooks
broker.on('clientReady', (client) => {
    console.log(`[broker] client connected id=${client ? client.id : '?'} addr=${client && client.conn ? client.conn.remoteAddress : '?'}:`);
});
broker.on('clientDisconnect', (client) => {
    console.log(`[broker] client disconnected id=${client ? client.id : '?'}:`);
});
broker.on('clientError', (client, err) => {
    console.warn(`[broker] client error id=${client ? client.id : '?'}: ${err && err.message}`);
});
broker.on('subscribe', (subs, client) => {
    const topics = subs.map((s) => `${s.topic}(q${s.qos})`).join(', ');
    console.log(`[broker] subscribe id=${client ? client.id : '?'} -> ${topics}`);
});
broker.on('unsubscribe', (subs, client) => {
    console.log(`[broker] unsubscribe id=${client ? client.id : '?'} -> ${subs.join(', ')}`);
});
broker.on('publish', (packet, client) => {
    // Skip $SYS noise
    if (!packet || !packet.topic || packet.topic.startsWith('$SYS')) return;

    const from = client ? `id=${client.id}` : 'broker';
    const plBuf = packet.payload
        ? (Buffer.isBuffer(packet.payload) ? packet.payload : Buffer.from(String(packet.payload)))
        : Buffer.alloc(0);
    const pl = preview(plBuf);

    console.log(`[broker] publish ${from} -> topic=${packet.topic} qos=${packet.qos} retain=${packet.retain} bytes=${plBuf.length} :: ${pl}`);

    // --- ЛОГИКА ТЕСТОВЫХ СВИТЧЕЙ ---

    // Реагируем только на команды от клиента, а не на собственные публикации брокера
    if (client && packet.topic === 'ha/cmd/toggle') {
        const entityId = plBuf.toString('utf8').trim();
        // console.log(`[logic] toggle requested for "${entityId}"`);

        // если такой entity отслеживается – переключаем его самого
        if (entityStates[entityId] !== undefined) {
            const prev = entityStates[entityId];
            const newState = toggleState(entityId);
            const stateTopic = `ha/state/${entityId}`;
            const statePayload = Buffer.from(newState, 'utf8');

            // console.log(
            //     `[logic] ${entityId}: ${prev} -> ${newState}, publishing to ${stateTopic}`
            // );

            broker.publish({
                topic: stateTopic,
                payload: statePayload,
                qos: 1,
                retain: true,
            });
        }
    }
});

const server = createServer(broker.handle);
server.listen(BROKER_PORT, BROKER_HOST, () => {
    console.log('[broker] listening', { BROKER_HOST, BROKER_PORT, auth: !!(BROKER_USERNAME || BROKER_PASSWORD) });
});

function shutdown() {
    console.log('\n[broker] shutting down...');
    try { server.close(); } catch (_) { }
    try { broker.close(() => process.exit(0)); } catch (_) { process.exit(0); }
}
process.on('SIGINT', shutdown);
process.on('SIGTERM', shutdown);
