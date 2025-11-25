async function loadConfig() {
  const res = await fetch("/api/config");
  if (!res.ok) {
    throw new Error("Failed to load config");
  }
  return await res.json();
}

function createWifiRow(item) {
  const div = document.createElement("div");
  div.className = "row wifi-row";

  div.innerHTML = `
    <label>SSID
      <input type="text" name="ssid" value="${item?.ssid || ""}">
    </label>
    <label>Password
      <input type="text" name="password" value="${item?.password || ""}">
    </label>
    <button type="button" class="danger wifi-remove">Remove</button>
  `;

  div.querySelector(".wifi-remove").addEventListener("click", () => {
    div.remove();
  });

  return div;
}

function createHaRow(item) {
  const div = document.createElement("div");
  div.className = "row ha-row";

  div.innerHTML = `
    <label>Host
      <input type="text" name="host" value="${item?.host || ""}">
    </label>
    <label>HTTP port
      <input type="number" name="http_port" value="${item?.http_port || 8123}">
    </label>
    <label>MQTT port
      <input type="number" name="mqtt_port" value="${item?.mqtt_port || 1883}">
    </label>
    <label>MQTT username
      <input type="text" name="mqtt_username" value="${item?.mqtt_username || ""}">
    </label>
    <label>MQTT password
      <input type="text" name="mqtt_password" value="${item?.mqtt_password || ""}">
    </label>
    <label>HTTP token
      <input type="text" name="http_token" value="${item?.http_token || ""}">
    </label>
    <button type="button" class="danger ha-remove">Remove</button>
  `;

  div.querySelector(".ha-remove").addEventListener("click", () => {
    div.remove();
  });

  return div;
}

function collectConfig() {
  const wifi = [];
  document.querySelectorAll(".wifi-row").forEach((row) => {
    const ssid = row.querySelector('input[name="ssid"]').value.trim();
    const password = row.querySelector('input[name="password"]').value.trim();
    if (ssid) {
      wifi.push({ ssid, password });
    }
  });

  const ha = [];
  document.querySelectorAll(".ha-row").forEach((row) => {
    const host = row.querySelector('input[name="host"]').value.trim();
    const http_port = parseInt(row.querySelector('input[name="http_port"]').value, 10) || 8123;
    const mqtt_port = parseInt(row.querySelector('input[name="mqtt_port"]').value, 10) || 1883;
    const mqtt_username = row.querySelector('input[name="mqtt_username"]').value.trim();
    const mqtt_password = row.querySelector('input[name="mqtt_password"]').value.trim();
    const http_token = row.querySelector('input[name="http_token"]').value.trim();
    if (host) {
      ha.push({ host, http_port, mqtt_port, mqtt_username, mqtt_password, http_token });
    }
  });

  return { wifi, ha };
}

async function saveConfig() {
  const status = document.getElementById("status");
  status.textContent = "Saving…";
  const payload = collectConfig();
  const res = await fetch("/api/config", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(payload),
  });
  if (!res.ok) {
    status.textContent = "Save failed";
    return;
  }
  status.textContent = "Saved. You may reboot the device.";
}

async function main() {
  const wifiList = document.getElementById("wifi-list");
  const haList = document.getElementById("ha-list");

  document.getElementById("wifi-add").addEventListener("click", () => {
    wifiList.appendChild(createWifiRow());
  });
  document.getElementById("ha-add").addEventListener("click", () => {
    haList.appendChild(createHaRow());
  });
  document.getElementById("save").addEventListener("click", () => {
    saveConfig().catch((e) => {
      console.error(e);
      document.getElementById("status").textContent = "Save failed";
    });
  });

  try {
    const cfg = await loadConfig();
    (cfg.wifi || []).forEach((w) => wifiList.appendChild(createWifiRow(w)));
    (cfg.ha || []).forEach((h) => haList.appendChild(createHaRow(h)));
  } catch (e) {
    console.error(e);
    document.getElementById("status").textContent = "Failed to load config";
  }
}

window.addEventListener("DOMContentLoaded", () => {
  main().catch((e) => console.error(e));
});

