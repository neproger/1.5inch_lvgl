HA Mock Server (server_test)

Minimal Node.js server that emulates a small subset of the Home Assistant REST API used by this firmware for local testing.

Implemented endpoints
- `GET /api/` — returns 200 and text "API running."
- `GET /api/states/:entityId` — returns JSON state for the entity (404 if missing)
- `POST /api/services/:domain/:service` — supports `toggle`, `turn_on`, `turn_off`; accepts JSON body `{ "entity_id": "..." }` or `{ "entity_id": ["..."] }` or `{ "target": { "entity_id": ["..."] } }`. Returns an array of affected entities like HA.

Auth
- Accepts any Bearer token by default.
- To require a specific token, set env var `HA_TOKEN` and the server will require `Authorization: Bearer <HA_TOKEN>`.

Port
- Listens on `8123` by default (same as HA). Override with env var `PORT` if needed.

Seeded entities
- `switch.wifi_breaker_t_switch_1` → off
- `switch.wifi_breaker_t_switch_2` → off

Quick start
1) Install deps:
   - From `server_test/`: `npm install`
2) Run:
   - `npm start`
   - Optional with token: `set HA_TOKEN=YOURTOKEN && npm start` (PowerShell/cmd) or `HA_TOKEN=YOURTOKEN npm start` (bash)
3) Point the firmware to this server:
   - Adjust `main/ha_client_config.h` `HA_DEFAULT_BASE_URL` to your PC’s IP: `"http://<YOUR_PC_IP>:8123/"` (note the trailing slash) or call `ha_client_init()` accordingly.

Examples
- Health: `curl -i http://localhost:8123/api/`
- Get state: `curl -i http://localhost:8123/api/states/switch.wifi_breaker_t_switch_1`
- Toggle: `curl -i -X POST http://localhost:8123/api/services/switch/toggle -H "Content-Type: application/json" -d "{\"entity_id\":\"switch.wifi_breaker_t_switch_1\"}"`

