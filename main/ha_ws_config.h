#pragma once

// Default Home Assistant WebSocket endpoint.
// Example: "ws://homeassistant.local:8123/api/websocket"
#ifndef HA_WS_URI
#define HA_WS_URI "wss://ha.kulbaev.keenetic.pro:8123/api/websocket"
#endif

// Long-lived access token from HA user profile.
// IMPORTANT: replace placeholder with a valid token before connecting.
#ifndef HA_WS_ACCESS_TOKEN
#define HA_WS_ACCESS_TOKEN "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJiYjc3MzQ3MGZjNDM0NGE4OWEzZjNlMjU1OWZhNmNjYyIsImlhdCI6MTc2MzA5OTYzMCwiZXhwIjoyMDc4NDU5NjMwfQ.nUl6LPFmwPpE3PbKsGvN1EGHrja15v_ZeLMdLGfv7Fk"
#endif

