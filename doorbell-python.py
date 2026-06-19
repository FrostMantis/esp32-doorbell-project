# doorbell_monitor.py
#
# Systemd service tip — drop this in /etc/systemd/system/doorbell.service:
#
#   [Unit]
#   Description=Doorbell Monitor
#   After=network.target
#
#   [Service]
#   ExecStart=/usr/bin/python3 /opt/doorbell/doorbell_monitor.py
#   Restart=always
#   RestartSec=10
#
#   [Install]
#   WantedBy=multi-user.target

import json
import time
import threading
import requests
import paho.mqtt.client as mqtt
from rich import print

# ── MQTT ──────────────────────────────────────────────────────────────────────
MQTT_BROKER_HOST     = "172.27.27.xx"
MQTT_BROKER_PORT     = 1883
MQTT_TOPIC           = "home/doorbell"

# ── Notifications ─────────────────────────────────────────────────────────────
NTFY_URL             = "https://ntfy.sh/xxxxx"
NOTIFICATION_COOLDOWN = 120          # seconds between doorbell alerts

# ── Heartbeat / health ────────────────────────────────────────────────────────
HEARTBEAT_TIMEOUT    = 60 * 60      # 1 hour  — declare offline after this
ALERT_REPEAT_AFTER   = 60 * 60 * 72 # 72 hours — re-alert if still offline
HEALTH_CHECK_INTERVAL = 60          # how often the watcher thread runs (seconds)

# ── State ─────────────────────────────────────────────────────────────────────
last_notification_time  = 0
first_message_received  = False
last_heartbeat_time     = time.time()  # assume online at startup
offline_alerted_at      = None         # when we last sent an offline alert
is_online               = True


# ── ntfy helper ───────────────────────────────────────────────────────────────

def send_ntfy(message, title="Doorbell", priority="high"):
    try:
        r = requests.post(
            NTFY_URL,
            data=message.encode("utf-8"),
            headers={"Title": title, "Priority": priority},
            timeout=5,
        )
        if r.ok:
            print(f"[green]Notification sent: {message}[/green]")
        else:
            print(f"[red]Notification failed: {r.status_code}[/red]")
    except Exception as e:
        print(f"[red]Error sending notification: {e}[/red]")


# ── MQTT callbacks ────────────────────────────────────────────────────────────

def on_connect(client, userdata, flags, reason_code, properties=None):
    print(f"[bright_blue]MQTT connected: {reason_code}[/bright_blue]")
    client.subscribe(MQTT_TOPIC)


def on_message(client, userdata, msg):
    global last_notification_time, first_message_received
    global last_heartbeat_time, is_online, offline_alerted_at

    try:
        data = json.loads(msg.payload.decode())
        print(f"[bright_blue]Message on {msg.topic}: {data}[/bright_blue]")

        # ── Ignore the very first message after startup ────────────────────
        if not first_message_received:
            first_message_received = True
            print("[yellow]Ignoring first message after startup[/yellow]")
            # Still counts as a heartbeat — the ESP32 is clearly alive
            last_heartbeat_time = time.time()
            return

        event = data.get("event")

        # ── Heartbeat ──────────────────────────────────────────────────────
        if event == "heartbeat":
            last_heartbeat_time = time.time()
            print("[dim]Heartbeat received[/dim]")

            # If we previously declared it offline, announce recovery
            if not is_online:
                is_online = True
                offline_alerted_at = None
                print("[green]ESP32 back online[/green]")
                send_ntfy("✅ Doorbell ESP32 is back online.", title="Doorbell Health", priority="default")
            return

        # ── Doorbell ring ──────────────────────────────────────────────────
        if event == "doorbell_ring":
            now = time.time()
            if now - last_notification_time >= NOTIFICATION_COOLDOWN:
                send_ntfy("Someone rang the doorbell!")
                last_notification_time = now
            else:
                print("[yellow]Doorbell ignored — cooldown active[/yellow]")

    except json.JSONDecodeError:
        print("[red]Invalid JSON received[/red]")


# ── Health watcher thread ─────────────────────────────────────────────────────

def health_watcher():
    """Runs in the background, checks heartbeat age every HEALTH_CHECK_INTERVAL seconds."""
    global is_online, offline_alerted_at

    while True:
        time.sleep(HEALTH_CHECK_INTERVAL)

        silence = time.time() - last_heartbeat_time

        if silence < HEARTBEAT_TIMEOUT:
            # All good — nothing to do
            continue

        # We're past the timeout threshold
        if is_online:
            # Transition: just went offline
            is_online = False
            print(f"[red]No heartbeat for {silence/60:.0f} min — ESP32 appears offline[/red]")
            send_ntfy("⚠️ Doorbell ESP32 has been offline for over an hour.", title="Doorbell Health", priority="high")
            offline_alerted_at = time.time()

        else:
            # Already offline — check if it's time to repeat the alert
            if offline_alerted_at and (time.time() - offline_alerted_at >= ALERT_REPEAT_AFTER):
                print("[red]ESP32 still offline — sending repeat alert[/red]")
                send_ntfy("⚠️ Doorbell ESP32 is still offline.", title="Doorbell Health", priority="high")
                offline_alerted_at = time.time()


# ── Entry point ───────────────────────────────────────────────────────────────

watcher = threading.Thread(target=health_watcher, daemon=True)
watcher.start()

client = mqtt.Client()
client.on_connect = on_connect
client.on_message = on_message
client.connect(MQTT_BROKER_HOST, MQTT_BROKER_PORT, 60)
client.loop_forever()
