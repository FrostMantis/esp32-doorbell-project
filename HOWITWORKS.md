# How It Works

## Architecture Overview

```
[Physical doorbell button]
        │ GPIO34
        ▼
  [ESP32 firmware]
        │ MQTT publish  →  home/doorbell
        ▼
  [MQTT broker]  (172.27.27.xx:1883)
        │ MQTT subscribe
        ▼
  [Python monitor]
        │ HTTP POST
        ▼
  [ntfy.sh]  →  push notification on your phone
```

Two components: an ESP32 running Arduino firmware, and a Python process running on a server (as a systemd service).

---

## ESP32 Firmware (`esp32_doorbel.ino`)

### Startup

1. Connects to WiFi. If the connection times out (20 s), the ESP32 hard-restarts.
2. Connects to the MQTT broker.
3. Configures GPIO 34 as `INPUT_PULLUP` — the doorbell button pulls this pin LOW when pressed.

### Main loop (runs every 10 ms)

- **`maintainConnections()`** — if MQTT is disconnected and 5 s have elapsed since the last attempt, tries to reconnect.
- **`mqtt.loop()`** — services the MQTT client (keepalive, incoming packets).
- **`checkDoorbell()`** — reads GPIO 34 and detects button presses.
- **`sendHeartbeat()`** — publishes a heartbeat every 60 s.

### Button debounce & deduplication

`checkDoorbell()` uses a two-stage guard:

1. **Debounce (50 ms)** — the state must be stable for at least 50 ms before it is acted on. Eliminates electrical noise on the pin.
2. **Minimum press interval (2 000 ms)** — even after debounce, a second ring event cannot fire within 2 s of the previous one. Prevents a single long press from flooding the broker.

When both conditions are satisfied and the pin is LOW, `publishEvent("doorbell_ring")` is called.

### MQTT message format

All published messages are JSON:

```json
{ "event": "doorbell_ring" }
{ "event": "heartbeat" }
```

Published to the topic `home/doorbell`.

---

## Python Monitor (`doorbell-python.py`)

Runs as a long-lived process (optionally as a systemd service). It subscribes to `home/doorbell` and handles two event types.

### First-message suppression

The very first message received after startup is silently ignored and treated only as a heartbeat. This prevents a spurious notification if the ESP32 happened to publish just before the monitor connected.

### `doorbell_ring` event

- **Cooldown (120 s)** — if fewer than 120 seconds have elapsed since the last notification, the ring is logged but no push notification is sent. Prevents notification spam from rapid re-rings.
- **Daily ring counter** — tracks how many rings have produced a notification today. Resets at midnight. Included in the notification text: *"3 time(s) today"*.
- Sends a push notification via ntfy.sh with the day/time and the daily count.

### `heartbeat` event

- Updates `last_heartbeat_time` to now.
- If the device was previously marked offline, marks it back online and sends a recovery notification.

### Health watcher thread

A background daemon thread wakes every 60 s and computes how long it has been since the last heartbeat.

| Condition | Action |
|---|---|
| Silence < 1 hour | Nothing. |
| Silence ≥ 1 hour, `is_online = True` | Mark offline, send "ESP32 has been offline for over an hour" alert. |
| Already offline, last alert > 72 hours ago | Send repeat "still offline" alert. |
| Heartbeat received while offline | Mark online, send "back online" recovery notification. |

The 72-hour repeat prevents the alert from being sent once and then going silent during a prolonged outage.

---

## Notifications

All notifications go to ntfy.sh (`POST` to `NTFY_URL`). Three notification types:

| Trigger | Title | Priority |
|---|---|---|
| Doorbell ring | "Doorbell" | high |
| ESP32 went offline | "Doorbell Health" | high |
| ESP32 back online | "Doorbell Health" | default |
| ESP32 still offline (repeat) | "Doorbell Health" | high |

---

## Configuration Reference

### ESP32 (`esp32_doorbel.ino`)

| Constant | Default | Meaning |
|---|---|---|
| `DOORBELL_PIN` | 34 | GPIO pin wired to the button |
| `DEBOUNCE_DELAY` | 50 ms | Pin must be stable this long |
| `MIN_PRESS_INTERVAL` | 2 000 ms | Minimum time between ring events |
| `RECONNECT_INTERVAL` | 5 000 ms | MQTT reconnect back-off |
| `HEARTBEAT_INTERVAL` | 60 000 ms | How often heartbeat is published |

### Python (`doorbell-python.py`)

| Constant | Default | Meaning |
|---|---|---|
| `NOTIFICATION_COOLDOWN` | 120 s | Minimum gap between ring notifications |
| `HEARTBEAT_TIMEOUT` | 3 600 s (1 h) | Silence duration before declaring offline |
| `ALERT_REPEAT_AFTER` | 259 200 s (72 h) | How often to re-alert while still offline |
| `HEALTH_CHECK_INTERVAL` | 60 s | How often the watcher thread runs |

---

## Running as a systemd Service

```ini
[Unit]
Description=Doorbell Monitor
After=network.target

[Service]
ExecStart=/usr/bin/python3 /opt/doorbell/doorbell_monitor.py
Restart=always
RestartSec=10

[Install]
WantedBy=multi-user.target
```

Save to `/etc/systemd/system/doorbell.service`, then:

```sh
systemctl enable --now doorbell
```

## Dependencies

**ESP32 (Arduino libraries)**
- `WiFi.h` (bundled with esp32 board package)
- `PubSubClient` — MQTT client
- `ArduinoJson` — JSON serialisation

**Python**
- `paho-mqtt` — MQTT client
- `requests` — HTTP for ntfy
- `rich` — coloured terminal output
