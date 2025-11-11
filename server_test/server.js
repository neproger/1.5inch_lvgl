/*
  Minimal Home Assistant REST API mock for local testing.
  Endpoints implemented (subset used by firmware):
    - GET  /api/                             -> 200, "API running."
    - GET  /api/states/:entityId             -> 200 with entity state JSON (or 404)
    - POST /api/services/:domain/:service    -> 200 and updates/toggles state

  Auth: Accepts any Bearer token by default. If HA_TOKEN env is set,
        requires exact match of Authorization: Bearer <HA_TOKEN>.

  Port: default 8123 (to mimic HA), set PORT env to override.
*/

const express = require('express');

const app = express();
app.use(express.json());

// Simple request logger: method, url, optional body, status, duration, size, ip
app.use((req, res, next) => {
  const start = process.hrtime.bigint();
  const ip = req.ip || (req.connection && req.connection.remoteAddress) || '-';
  const { method } = req;
  const url = req.originalUrl || req.url;
  res.on('finish', () => {
    const durMs = Number(process.hrtime.bigint() - start) / 1e6;
    const len = res.getHeader('content-length');
    const size = len ? `${len}b` : '-';
    let bodyStr = '';
    try {
      if (method !== 'GET' && req.body && typeof req.body === 'object') {
        const json = JSON.stringify(req.body);
        // Show up to 512 chars of the body for readability
        bodyStr = ` body=${json.length > 512 ? json.slice(0, 512) + '…' : json}`;
      }
    } catch (_) {
      // ignore body stringify errors
    }
    console.log(`[req] ${method} ${url}${bodyStr} -> ${res.statusCode} ${durMs.toFixed(1)}ms ${size} from ${ip}`);
  });
  next();
});

const PORT = process.env.PORT ? parseInt(process.env.PORT, 10) : 8123;
const REQUIRED_TOKEN = process.env.HA_TOKEN || null;

// Simple middleware to emulate HA Bearer token check
app.use((req, res, next) => {
  const auth = req.header('Authorization') || '';
  if (REQUIRED_TOKEN) {
    const expected = `Bearer ${REQUIRED_TOKEN}`;
    if (auth !== expected) {
      console.warn('[auth] Unauthorized request');
      return res.status(401).json({ message: 'Unauthorized: invalid token' });
    }
  }
  next();
});

// In-memory state store: entity_id -> { entity_id, state, attributes, ... }
const store = new Map();

// Helper to now() as ISO string
function nowISO() {
    return new Date().toISOString();
}

// Seed some demo entities used by the firmware UI (names inferred from code)
function seed() {
    const initial = [
        {
            entity_id: 'switch.wifi_breaker_t_switch_1',
            friendly_name: 'Wifi Breaker T1',
            state: 'off',
        },
        {
            entity_id: 'switch.wifi_breaker_t_switch_2',
            friendly_name: 'Wifi Breaker T2',
            state: 'off',
        },
    ];
    for (const e of initial) {
        const obj = {
            entity_id: e.entity_id,
            state: e.state,
            attributes: {
                friendly_name: e.friendly_name,
            },
            last_changed: nowISO(),
            last_updated: nowISO(),
            context: { id: null, parent_id: null, user_id: null },
        };
        store.set(e.entity_id, obj);
    }
}

seed();

// Health endpoint: Home Assistant returns 200 and text "API running."
app.get('/api/', (req, res) => {
    res.type('text/plain').send('API running.');
});

// Get entity state
app.get('/api/states/:entityId', (req, res) => {
    const entityId = req.params.entityId;
    const obj = store.get(entityId);
    if (!obj) {
        return res.status(404).json({ message: `Entity ${entityId} not found` });
    }
    res.json(obj);
});

// POST service call (toggle, turn_on, turn_off etc.)
app.post('/api/services/:domain/:service', (req, res) => {
    const { domain, service } = req.params;
    const body = req.body || {};

    // Accept { entity_id: string | string[] } like HA
    // Also accept { target: { entity_id: [...] } }
    let ids = [];
    if (typeof body.entity_id === 'string') ids = [body.entity_id];
    else if (Array.isArray(body.entity_id)) ids = body.entity_id;
    else if (body.target && Array.isArray(body.target.entity_id)) ids = body.target.entity_id;

    // Fallback: if no ids provided, apply to all entities in the given domain
    if (ids.length === 0) {
        for (const key of store.keys()) {
            if (key.startsWith(`${domain}.`)) ids.push(key);
        }
    }

    const updated = [];
    for (const id of ids) {
        let obj = store.get(id);
        if (!obj) {
            // Auto-create unknown entity in this domain
            obj = {
                entity_id: id,
                state: 'off',
                attributes: { friendly_name: id },
                last_changed: nowISO(),
                last_updated: nowISO(),
                context: { id: null, parent_id: null, user_id: null },
            };
            store.set(id, obj);
        }

        // Apply service semantics
        const svc = String(service || '').toLowerCase();
        let newState = obj.state;
        if (svc === 'toggle') newState = obj.state === 'on' ? 'off' : 'on';
        else if (svc === 'turn_on') newState = 'on';
        else if (svc === 'turn_off') newState = 'off';

        if (newState !== obj.state) {
            obj.state = newState;
            obj.last_changed = nowISO();
        }
        obj.last_updated = nowISO();
        updated.push(obj);
    }

    // HA returns an array of affected states; 200 on success
    res.status(200).json(updated);
});

// Basic 404 for other routes
app.use((req, res) => {
    res.status(404).json({ message: 'Not Found' });
});

app.listen(PORT, () => {
    console.log(`[ha-mock] Listening on http://localhost:${PORT}`);
    if (REQUIRED_TOKEN) {
        console.log('[ha-mock] Token required');
    } else {
        console.log('[ha-mock] Token check disabled');
    }
});
