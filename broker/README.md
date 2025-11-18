MQTT TCP→TLS Proxy
===================

Purpose
 - Expose a local plain TCP MQTT endpoint (e.g., `127.0.0.1:1883`).
 - Bridge it to a remote MQTT broker, optionally over TLS (e.g., `host:8883`).
 - Useful when devices can’t do TLS but the upstream requires it.

Quick Start
1) Edit env vars (PowerShell example):

   $env:LOCAL_HOST = "127.0.0.1"
   $env:LOCAL_PORT = "1883"
   $env:REMOTE_HOST = "your-broker.example.com"
   $env:REMOTE_PORT = "8883"
   $env:REMOTE_TLS = "true"
   # Inject/override credentials (optional)
   $env:MQTT_USERNAME = "proxy_user"
   $env:MQTT_PASSWORD = "proxy_pass"
   # Optional
   # $env:REMOTE_SNI = "your-broker.example.com"
   # $env:ALLOW_SELF_SIGNED = "false"
   # $env:CA_FILE = ".\\ca.crt"
   # $env:CERT_FILE = ".\\client.crt"
   # $env:KEY_FILE = ".\\client.key"

2) Run:

   cd mqtt_proxy
   npm start

3) Point your device to the local proxy (`LOCAL_HOST:LOCAL_PORT`).

Local Test Broker
- Install deps once: `cd mqtt_proxy && npm install`
- Start broker (logs all publishes): `npm run broker`
- Broker env (in `mqtt_proxy/.env`):
  - `BROKER_HOST=127.0.0.1` (default)
  - `BROKER_PORT=1884` (default)
  - Optional auth: `BROKER_USERNAME=...`, `BROKER_PASSWORD=...`
  - Logging controls: `BROKER_LOG_HEX=true`, `BROKER_LOG_MAX_BYTES=256`
- Test from another shell:
  - Publish: `mosquitto_pub -h 127.0.0.1 -p 1884 -t test/hello -m "hi"`
  - Subscribe: `mosquitto_sub -h 127.0.0.1 -p 1884 -t test/# -v`
  - If auth enabled: add `-u user -P pass` to both commands.

Config Reference (env)
- LOCAL_HOST: Bind address for local TCP listener (default `127.0.0.1`).
- LOCAL_PORT: Local TCP port (default `1883`).
- REMOTE_HOST: Remote MQTT broker hostname (required).
- REMOTE_PORT: Remote MQTT broker port (default `8883`).
- REMOTE_TLS: `true` to use TLS to remote (default `true`). Set `false` for plain TCP remote.
- REMOTE_SNI: SNI servername for TLS (default `REMOTE_HOST`).
- ALLOW_SELF_SIGNED: `true` disables TLS verification (default `false`).
- CA_FILE: Path to a CA bundle/file (optional).
- CERT_FILE / KEY_FILE: Client cert/key if the broker requires mTLS (optional).
- MQTT_USERNAME / MQTT_PASSWORD: If set, the proxy parses the first MQTT CONNECT packet and injects/overrides username/password before forwarding. Works with MQTT 3.1/3.1.1/5.0 common cases.

Notes
- This is a transparent TCP proxy; it doesn’t parse MQTT. It creates one remote connection per local client and pipes bytes both ways.
  - Exception: if `MQTT_USERNAME`/`MQTT_PASSWORD` are set, it minimally parses the initial CONNECT packet to inject credentials.
- For production, keep the proxy on a trusted host/network because local side is unencrypted.
- If remote requires hostname verification, ensure `REMOTE_SNI` matches the broker’s certificate CN/SAN.

Logging
- Set LOG_TRAFFIC=true to print direction, size, and a preview of bytes for each chunk. Use LOG_HEX and LOG_MAX_BYTES to control format and preview length.

Config tips
- Use a hostname only for REMOTE_HOST (no http://). For standard TLS MQTT use REMOTE_PORT=8883 and REMOTE_TLS=true. For plain MQTT use REMOTE_PORT=1883 and REMOTE_TLS=false.
