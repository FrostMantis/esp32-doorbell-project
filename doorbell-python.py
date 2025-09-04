import json, time, requests, paho.mqtt.client as mqtt
from rich import print

MQTT_BROKER_HOST = "172.27.27.xx"
MQTT_BROKER_PORT = 1883
MQTT_TOPIC = "home/doorbell"

NTFY_URL = "https://ntfy.sh/xxxxxx"
NOTIFICATION_COOLDOWN = 120

last_notification_time = 0
first_message_received = False


def send_ntfy(message):
    try:
        r = requests.post(
            NTFY_URL,
            data=message.encode("utf-8"),
            headers={"Title": "Doorbell", "Priority": "high"},
            timeout=5
        )
        print("[green]Notification sent[/green]" if r.ok else f"[red]Failed: {r.status_code}[/red]")
    except Exception as e:
        print(f"[red]Error sending notification: {e}[/red]")


def on_connect(client, userdata, flags, reason_code, properties=None):
    print(f"[bright_blue]Connected: {reason_code}[/bright_blue]")
    client.subscribe(MQTT_TOPIC)


def on_message(client, userdata, msg):
    global last_notification_time, first_message_received
    try:
        data = json.loads(msg.payload.decode())
        print(f"[bright_blue]Message on {msg.topic}: {data}[/bright_blue]")

        if not first_message_received:
            first_message_received = True
            print("[yellow]Ignoring first message after startup[/yellow]")
            return

        if data.get("event") == "doorbell_ring":
            now = time.time()
            if now - last_notification_time >= NOTIFICATION_COOLDOWN:
                send_ntfy("Someone rang the doorbell!")
                last_notification_time = now
            else:
                print("[yellow]Ignored due to cooldown[/yellow]")
    except json.JSONDecodeError:
        print("[red]Invalid JSON[/red]")


client = mqtt.Client()
client.on_connect = on_connect
client.on_message = on_message
client.connect(MQTT_BROKER_HOST, MQTT_BROKER_PORT, 60)
client.loop_forever()
