/*
  MQTT TCP→TLS Proxy
  - Listens locally on plain TCP (e.g., 1883)
  - Dials remote broker with or without TLS (e.g., 8883 with TLS)
  - Pipes data bidirectionally without speaking MQTT itself

  Configure via environment variables or .env (if you add one):
    LOCAL_HOST=127.0.0.1
    LOCAL_PORT=1883
    REMOTE_HOST=your-broker.example.com
    REMOTE_PORT=8883
    REMOTE_TLS=true            # true|false
    REMOTE_SNI=your-broker.example.com  # optional SNI override
    ALLOW_SELF_SIGNED=false    # true disables TLS verification
    CA_FILE=./ca.crt           # optional path to CA bundle
    CERT_FILE=./client.crt     # optional client cert
    KEY_FILE=./client.key      # optional client key
*/

const net = require('net');
const tls = require('tls');
const fs = require('fs');
const path = require('path');

function getBool(name, def) {
  const v = process.env[name];
  if (v == null) return def;
  return /^(1|true|yes|on)$/i.test(String(v).trim());
}

function getStr(name, def) {
  const v = process.env[name];
  return v == null || v === '' ? def : v;
}

const LOCAL_HOST = getStr('LOCAL_HOST', '127.0.0.1');
const LOCAL_PORT = Number(getStr('LOCAL_PORT', '1883'));
const REMOTE_HOST = getStr('REMOTE_HOST', '');
const REMOTE_PORT = Number(getStr('REMOTE_PORT', '8883'));
const REMOTE_TLS = getBool('REMOTE_TLS', true);
const REMOTE_SNI = getStr('REMOTE_SNI', '') || REMOTE_HOST;
const ALLOW_SELF_SIGNED = getBool('ALLOW_SELF_SIGNED', false);
const CA_FILE = getStr('CA_FILE', '');
const CERT_FILE = getStr('CERT_FILE', '');
const KEY_FILE = getStr('KEY_FILE', '');

if (!REMOTE_HOST) {
  console.error('[config] REMOTE_HOST is required');
  process.exit(1);
}

function readIfFile(p) {
  if (!p) return undefined;
  const abs = path.resolve(p);
  if (!fs.existsSync(abs)) {
    console.error(`[config] File not found: ${abs}`);
    process.exit(1);
  }
  return fs.readFileSync(abs);
}

const tlsOptions = REMOTE_TLS
  ? {
      host: REMOTE_HOST,
      port: REMOTE_PORT,
      servername: REMOTE_SNI || REMOTE_HOST,
      rejectUnauthorized: !ALLOW_SELF_SIGNED,
      ca: CA_FILE ? [readIfFile(CA_FILE)] : undefined,
      cert: CERT_FILE ? readIfFile(CERT_FILE) : undefined,
      key: KEY_FILE ? readIfFile(KEY_FILE) : undefined,
    }
  : null;

function connectRemote() {
  return REMOTE_TLS
    ? tls.connect(tlsOptions)
    : net.connect({ host: REMOTE_HOST, port: REMOTE_PORT });
}

const server = net.createServer((local) => {
  local.setNoDelay(true);
  local.setKeepAlive(true, 15_000);

  const peer = `${local.remoteAddress}:${local.remotePort}`;
  console.log(`[local] client connected ${peer}`);

  const remote = connectRemote();

  const onLocalClose = (hadError) => {
    console.log(`[local] client closed ${peer}${hadError ? ' (error)' : ''}`);
    remote.destroy();
  };
  const onRemoteClose = () => {
    console.log(`[remote] closed for ${peer}`);
    local.destroy();
  };

  local.once('close', onLocalClose);
  remote.once('close', onRemoteClose);

  local.on('error', (err) => {
    console.warn(`[local] error ${peer}:`, err.message);
  });
  remote.on('error', (err) => {
    console.warn(`[remote] error for ${peer}:`, err.message);
    // If remote fails before piping, end local
    if (!local.destroyed) local.destroy();
  });

  remote.once('secureConnect', () => {
    // TLS secure established
    console.log(`[remote] TLS connected ${REMOTE_HOST}:${REMOTE_PORT}`);
  });

  remote.once('connect', () => {
    console.log(`[remote] connected ${REMOTE_HOST}:${REMOTE_PORT} (tls=${REMOTE_TLS})`);
    // Pipe traffic both ways
    local.pipe(remote);
    remote.pipe(local);
  });
});

server.on('error', (err) => {
  console.error('[server] error:', err.message);
});

server.listen(LOCAL_PORT, LOCAL_HOST, () => {
  console.log('[proxy] listening', { LOCAL_HOST, LOCAL_PORT });
  console.log('[proxy] forwarding to', {
    REMOTE_HOST,
    REMOTE_PORT,
    REMOTE_TLS,
    REMOTE_SNI,
    ALLOW_SELF_SIGNED,
    CA_FILE,
  });
});

function shutdown() {
  console.log('\n[proxy] shutting down...');
  server.close(() => process.exit(0));
  // In case of hanging sockets, force exit after grace
  setTimeout(() => process.exit(0), 2000).unref();
}

process.on('SIGINT', shutdown);
process.on('SIGTERM', shutdown);

