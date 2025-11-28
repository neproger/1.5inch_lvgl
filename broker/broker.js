/*
  Local MQTT + HTTP Test Server
  - MQTT broker (Aedes) on BROKER_HOST:BROKER_PORT
  - HTTP endpoint emulating HA /api/template on BROKER_HOST:HTTP_PORT
    * For bootstrap template: returns CSV with areas/entities
    * For weather template (screensaver): returns "Temperature,Condition" CSV
*/

const net = require('net');
const fs = require('fs');
const path = require('path');
const http = require('http');
const aedes = require('aedes')();

function loadEnvFile(envPath) {
    try {
        if (!fs.existsSync(envPath)) return;
        const content = fs.readFileSync(envPath, 'utf8');
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

loadEnvFile(path.join(__dirname, '.env'));

function getBool(name, defVal) {
    const v = process.env[name];
    if (v == null) return defVal;
    return /^(1|true|yes|on)$/i.test(String(v).trim());
}

function getStr(name, defVal) {
    const v = process.env[name];
    return v == null || v === '' ? defVal : v;
}

const BROKER_HOST = getStr('BROKER_HOST', '0.0.0.0');
const BROKER_PORT = Number(getStr('BROKER_PORT', '1884'));
const BROKER_USERNAME = getStr('BROKER_USERNAME', '');
const BROKER_PASSWORD = getStr('BROKER_PASSWORD', '');
const BROKER_LOG_HEX = getBool('BROKER_LOG_HEX', true);
const BROKER_LOG_MAX_BYTES = Number(getStr('BROKER_LOG_MAX_BYTES', '256'));
const HTTP_PORT = Number(getStr('HTTP_PORT', '8123'));

// Optional auth
if (BROKER_USERNAME || BROKER_PASSWORD) {
    aedes.authenticate = (client, username, password, done) => {
        const pwd = password ? password.toString('utf8') : '';
        const ok = username === BROKER_USERNAME && pwd === BROKER_PASSWORD;
        if (!ok) return done(new Error('Auth failed'), false);
        done(null, true);
    };
}

function preview(buf) {
    const b = buf.length > BROKER_LOG_MAX_BYTES ? buf.slice(0, BROKER_LOG_MAX_BYTES) : buf;
    return BROKER_LOG_HEX ? b.toString('hex') : buf.toString('utf8');
}

// Simple in-memory entity states used by MQTT emulation
const entityStates = {
    'switch.wifi_breaker_t_switch_1': 'OFF',
    'switch.wifi_breaker_t_switch_2': 'OFF',
    'switch.wifi_breaker_t_switch_3': 'OFF',
    'switch.wifi_breaker_t_switch_4': 'OFF',
    'switch.wifi_breaker_t_switch_5': 'OFF',
    'switch.wifi_breaker_t_switch_6': 'OFF',
    'switch.wifi_breaker_t_switch_7': 'OFF',
    'switch.wifi_breaker_t_switch_8': 'OFF',
};

function toggleState(id) {
    const cur = entityStates[id] || 'OFF';
    const next = cur === 'ON' ? 'OFF' : 'ON';
    entityStates[id] = next;
    return next;
}

// Logging hooks
aedes.on('clientReady', (client) => {
    console.log('[broker] client connected', {
        id: client ? client.id : '?',
        addr: client && client.conn ? client.conn.remoteAddress : '?',
    });
});

aedes.on('clientDisconnect', (client) => {
    console.log('[broker] client disconnected', { id: client ? client.id : '?' });
});

aedes.on('clientError', (client, err) => {
    console.warn('[broker] client error', {
        id: client ? client.id : '?',
        error: err && err.message,
    });
});

aedes.on('subscribe', (subs, client) => {
    const topics = subs.map((s) => `${s.topic}(q${s.qos})`).join(', ');
    console.log('[broker] subscribe', { id: client ? client.id : '?', topics });
});

aedes.on('unsubscribe', (subs, client) => {
    console.log('[broker] unsubscribe', { id: client ? client.id : '?', topics: subs });
});

aedes.on('publish', (packet, client) => {
    if (!packet || !packet.topic || packet.topic.startsWith('$SYS')) return;

    const from = client ? `id=${client.id}` : 'broker';
    const plBuf = packet.payload
        ? (Buffer.isBuffer(packet.payload) ? packet.payload : Buffer.from(String(packet.payload)))
        : Buffer.alloc(0);
    const pl = preview(plBuf);

    console.log('[broker] publish', {
        from,
        topic: packet.topic,
        qos: packet.qos,
        retain: packet.retain,
        bytes: plBuf.length,
        payload: pl,
    });

    // Handle toggle command from device: topic "ha/cmd/toggle", payload = entity_id
    if (client && packet.topic === 'ha/cmd/toggle') {
        const entityId = plBuf.toString('utf8').trim();
        if (entityStates[entityId] !== undefined) {
            const prev = entityStates[entityId];
            const next = toggleState(entityId);
            const stateTopic = `ha/state/${entityId}`;
            const statePayload = Buffer.from(next, 'utf8');

            console.log('[logic] toggle', { entityId, prev, next, stateTopic });

            aedes.publish({
                topic: stateTopic,
                payload: statePayload,
                qos: 1,
                retain: true,
            });
        }
    }
});

// MQTT TCP server
const server = net.createServer(aedes.handle);
server.listen(BROKER_PORT, BROKER_HOST, () => {
    console.log('[broker] listening', {
        host: BROKER_HOST,
        port: BROKER_PORT,
        auth: !!(BROKER_USERNAME || BROKER_PASSWORD),
    });
});

// Simple HTTP endpoint emulating HA /api/template
const httpServer = http.createServer((req, res) => {
    if (req.method === 'POST' && req.url === '/api/template') {
        let body = '';
        req.on('data', (chunk) => {
            body += chunk;
            if (body.length > 10240) {
                // prevent abuse
                req.socket.destroy();
            }
        });
        req.on('end', () => {
            const isWeatherTemplate = body.includes('Temperature,Condition');
            res.statusCode = 200;
            res.setHeader('Content-Type', 'text/plain; charset=utf-8');

            if (isWeatherTemplate) {
                const now = new Date();

                const year = now.getFullYear();
                const month = now.getMonth() + 1;      // 0–11 -> +1
                const day = now.getDate();
                const weekday = now.getDay();          // 0–6 (вс-пн)
                const hour = String(now.getHours()).padStart(2, '0');
                const minute = String(now.getMinutes()).padStart(2, '0');
                const second = String(now.getSeconds()).padStart(2, '0');

                res.end(
                    'Temperature,Condition,Year,Month,Day,Weekday,Hour,Minute,Second\n' +
                    `14.2,cloudy,${year},${month},${day},${weekday},${hour},${minute},${second}\n`
                );
            } else {
                // Default bootstrap CSV with areas/entities, using current in-memory states
                res.end(
                    'AREA_ID,AREA_NAME,ENTITY_ID,ENTITY_NAME,STATE\n' +
                    '\n' +
                    `kukhnia,Кухня,switch.wifi_breaker_t_switch_1,Освещение,${entityStates['switch.wifi_breaker_t_switch_1']}\n` +
                    `kukhnia,Кухня,switch.wifi_breaker_t_switch_2,Розетки_кухня,${entityStates['switch.wifi_breaker_t_switch_2']}\n` +
                    `kukhnia,Кухня,switch.wifi_breaker_t_switch_3,Розетки_бар,${entityStates['switch.wifi_breaker_t_switch_3']}\n` +
                    `kukhnia,Кухня,switch.wifi_breaker_t_switch_4,Посудомойка,${entityStates['switch.wifi_breaker_t_switch_4']}\n` +
                    `koridor,Коридор,switch.wifi_breaker_t_switch_5,Освещение,${entityStates['switch.wifi_breaker_t_switch_5']}\n` +
                    `spalnia,Спальня,switch.wifi_breaker_t_switch_6,Освещение,${entityStates['switch.wifi_breaker_t_switch_6']}\n` +
                    `spalnia,Спальня,switch.wifi_breaker_t_switch_7,Посудомойка,${entityStates['switch.wifi_breaker_t_switch_7']}\n` +
                    `spalnia,Спальня,switch.wifi_breaker_t_switch_8,Розетки_спальня,${entityStates['switch.wifi_breaker_t_switch_8']}\n`
                );
            }
        });
        return;
    }

    res.statusCode = 404;
    res.setHeader('Content-Type', 'text/plain; charset=utf-8');
    res.end('Not found');
});

httpServer.listen(HTTP_PORT, BROKER_HOST, () => {
    console.log('[http] listening', { host: BROKER_HOST, port: HTTP_PORT, path: '/api/template' });
});

function shutdown() {
    console.log('\n[broker] shutting down...');
    try { server.close(); } catch (_) { }
    try { httpServer.close(); } catch (_) { }
    try { aedes.close(() => process.exit(0)); } catch (_) { process.exit(0); }
}

process.on('SIGINT', shutdown);
process.on('SIGTERM', shutdown);

