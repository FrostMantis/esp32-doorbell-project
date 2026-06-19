#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// ── WiFi ──────────────────────────────────────────────────────────────────────
const char*    ssid              = "xx";
const char*    password          = "xx";

// ── MQTT ──────────────────────────────────────────────────────────────────────
const char*    mqtt_host         = "172.27.27.xx";
const uint16_t mqtt_port         = 1883;
const char*    mqtt_client_id    = "esp32-doorbell";
const char*    mqtt_topic        = "home/doorbell";

// ── Pin / timing ──────────────────────────────────────────────────────────────
const int            DOORBELL_PIN        = 34;
const unsigned long  DEBOUNCE_DELAY      = 50;
const unsigned long  MIN_PRESS_INTERVAL  = 2000;
const unsigned long  RECONNECT_INTERVAL  = 5000;
const unsigned long  HEARTBEAT_INTERVAL  = 60000;

WiFiClient   wifiClient;
PubSubClient mqtt(wifiClient);

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n[SYSTEM] Starting Doorbell Monitor");

  pinMode(DOORBELL_PIN, INPUT_PULLUP);

  connectWiFi();
  setupMQTT();

  Serial.println("[SYSTEM] Ready");
}

void loop() {
  maintainConnections();
  mqtt.loop();
  checkDoorbell();
  sendHeartbeat();
  delay(10);
}

// ── WiFi ──────────────────────────────────────────────────────────────────────

void connectWiFi() {
  Serial.print("[WIFI] Connecting to ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  unsigned long startTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startTime < 20000) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\n[WIFI] Failed — restarting");
    logWiFiStatus();
    ESP.restart();
  }

  Serial.print("\n[WIFI] Connected — IP: ");
  Serial.println(WiFi.localIP());
}

void logWiFiStatus() {
  Serial.print("[WIFI] Status: ");
  switch (WiFi.status()) {
    case WL_NO_SHIELD:      Serial.println("No shield");        break;
    case WL_IDLE_STATUS:    Serial.println("Idle");             break;
    case WL_NO_SSID_AVAIL:  Serial.println("SSID unavailable"); break;
    case WL_SCAN_COMPLETED: Serial.println("Scan completed");   break;
    case WL_CONNECTED:      Serial.println("Connected");        break;
    case WL_CONNECT_FAILED: Serial.println("Connect failed");   break;
    case WL_CONNECTION_LOST:Serial.println("Connection lost");  break;
    case WL_DISCONNECTED:   Serial.println("Disconnected");     break;
    default:                Serial.println("Unknown");          break;
  }
}

// ── MQTT ──────────────────────────────────────────────────────────────────────

void setupMQTT() {
  mqtt.setServer(mqtt_host, mqtt_port);
  Serial.print("[MQTT] Connecting to ");
  Serial.print(mqtt_host);
  Serial.print(":");
  Serial.println(mqtt_port);
  connectMQTT();
}

bool connectMQTT() {
  if (mqtt.connect(mqtt_client_id)) {
    Serial.println("[MQTT] Connected");
    return true;
  }
  Serial.print("[MQTT] Failed, state=");
  Serial.println(mqtt.state());
  logMQTTError();
  return false;
}

void logMQTTError() {
  Serial.print("[MQTT] Error: ");
  switch (mqtt.state()) {
    case -4: Serial.println("Timeout");           break;
    case -3: Serial.println("Connection lost");   break;
    case -2: Serial.println("Connect failed");    break;
    case -1: Serial.println("Disconnected");      break;
    case  1: Serial.println("Bad protocol");      break;
    case  2: Serial.println("Bad client ID");     break;
    case  3: Serial.println("Server unavailable");break;
    case  4: Serial.println("Bad credentials");   break;
    case  5: Serial.println("Unauthorized");      break;
    default: Serial.println("Unknown error");     break;
  }
}

void maintainConnections() {
  static unsigned long lastAttempt = 0;

  if (!mqtt.connected() && millis() - lastAttempt > RECONNECT_INTERVAL) {
    lastAttempt = millis();
    Serial.println("[MQTT] Reconnecting...");
    connectMQTT();
  }
}

void publishEvent(const char* event) {
  if (!mqtt.connected()) {
    Serial.println("[MQTT] Cannot publish — not connected");
    return;
  }

  StaticJsonDocument<128> doc;
  doc["event"] = event;
  char buffer[128];
  serializeJson(doc, buffer);

  if (mqtt.publish(mqtt_topic, buffer)) {
    Serial.print("[MQTT] Published: ");
    Serial.println(buffer);
  } else {
    Serial.println("[MQTT] Publish failed");
  }
}

// ── Heartbeat ─────────────────────────────────────────────────────────────────

void sendHeartbeat() {
  static unsigned long lastHeartbeat = 0;

  if (millis() - lastHeartbeat >= HEARTBEAT_INTERVAL) {
    lastHeartbeat = millis();
    publishEvent("heartbeat");
  }
}

// ── Doorbell ──────────────────────────────────────────────────────────────────

void checkDoorbell() {
  static bool          lastState      = HIGH;
  static unsigned long lastChangeTime = 0;

  int currentState = digitalRead(DOORBELL_PIN);

  if (currentState != lastState) {
    lastChangeTime = millis();
    lastState      = currentState;
  }

  if (millis() - lastChangeTime > DEBOUNCE_DELAY && currentState == LOW) {
    handleDoorbellPress();
  }
}

void handleDoorbellPress() {
  static unsigned long lastPressTime = 0;

  if (millis() - lastPressTime > MIN_PRESS_INTERVAL) {
    lastPressTime = millis();
    Serial.println("[EVENT] Doorbell pressed");
    publishEvent("doorbell_ring");
  }
}