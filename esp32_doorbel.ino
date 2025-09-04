#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// Configuration
const char* ssid = "xx";
const char* password = "xx";
const char* mqtt_host = "172.27.27.xx";
const uint16_t mqtt_port = 1883;
const char* mqtt_client_id = "esp32-doorbell";
const int DOORBELL_PIN = 34;
const unsigned long DEBOUNCE_DELAY = 50;
const unsigned long MIN_PRESS_INTERVAL = 2000;
const unsigned long RECONNECT_INTERVAL = 5000;

WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);

void setup() {
  Serial.begin(115200);
  delay(1000); // Give serial monitor time to connect

  Serial.println("\n[SYSTEM] Starting Doorbell Monitor");

  pinMode(DOORBELL_PIN, INPUT_PULLUP);

  connectWiFi();
  setupMQTT();

  Serial.println("[SYSTEM] Ready for doorbell events");
}

void loop() {
  maintainConnections();
  mqtt.loop();
  checkDoorbell();
  delay(10); // Prevent watchdog triggers
}

void connectWiFi() {
  Serial.print("[WIFI] Connecting to ");
  Serial.print(ssid);

  WiFi.begin(ssid, password);

  unsigned long startTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startTime < 20000) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\n[WIFI] Connection failed!");
    logWiFiStatus();
    ESP.restart();
  }

  Serial.println("\n[WIFI] Connected!");
  Serial.print("[WIFI] IP: ");
  Serial.println(WiFi.localIP());
}

void logWiFiStatus() {
  Serial.print("[WIFI] Status: ");
  switch(WiFi.status()) {
    case WL_NO_SHIELD: Serial.println("No shield"); break;
    case WL_IDLE_STATUS: Serial.println("Idle"); break;
    case WL_NO_SSID_AVAIL: Serial.println("SSID unavailable"); break;
    case WL_SCAN_COMPLETED: Serial.println("Scan completed"); break;
    case WL_CONNECTED: Serial.println("Connected"); break;
    case WL_CONNECT_FAILED: Serial.println("Failed"); break;
    case WL_CONNECTION_LOST: Serial.println("Lost"); break;
    case WL_DISCONNECTED: Serial.println("Disconnected"); break;
  }
}

void setupMQTT() {
  mqtt.setServer(mqtt_host, mqtt_port);
  Serial.println("[MQTT] Server configured");
  Serial.print("[MQTT] Trying to connect to ");
  Serial.print(mqtt_host);
  Serial.print(":");
  Serial.println(mqtt_port);

  if (!testMQTTConnection()) {
    Serial.println("[MQTT] Initial connection failed");
  }
}

bool testMQTTConnection() {
  Serial.println("[MQTT] Attempting connection...");

  if (mqtt.connect(mqtt_client_id)) {
    Serial.println("[MQTT] Connected successfully!");
    return true;
  }

  Serial.print("[MQTT] Connection failed, state=");
  Serial.println(mqtt.state());
  logMQTTError();
  return false;
}

void logMQTTError() {
  Serial.println("[MQTT] Error details:");
  switch(mqtt.state()) {
    case -4: Serial.println("Timeout"); break;
    case -3: Serial.println("Connection lost"); break;
    case -2: Serial.println("Connect failed"); break;
    case -1: Serial.println("Disconnected"); break;
    case 1: Serial.println("Bad protocol"); break;
    case 2: Serial.println("Bad client ID"); break;
    case 3: Serial.println("Unavailable"); break;
    case 4: Serial.println("Bad credentials"); break;
    case 5: Serial.println("Unauthorized"); break;
    default: Serial.println("Unknown error"); break;
  }
}

void maintainConnections() {
  static unsigned long lastReconnectAttempt = 0;

  if (!mqtt.connected() && millis() - lastReconnectAttempt > RECONNECT_INTERVAL) {
    lastReconnectAttempt = millis();
    Serial.println("[MQTT] Reconnecting...");
    if (mqtt.connect(mqtt_client_id)) {
      Serial.println("[MQTT] Reconnected!");
    } else {
      Serial.print("[MQTT] Reconnect failed, state=");
      Serial.println(mqtt.state());
    }
  }
}

void checkDoorbell() {
  static bool lastState = HIGH;
  static unsigned long lastChangeTime = 0;

  int currentState = digitalRead(DOORBELL_PIN);

  if (currentState != lastState) {
    lastChangeTime = millis();
    lastState = currentState;
  }

  if (millis() - lastChangeTime > DEBOUNCE_DELAY) {
    if (currentState == LOW) { // Doorbell pressed
      handleDoorbellPress();
    }
  }
}

void handleDoorbellPress() {
  static unsigned long lastPressTime = 0;

  if (millis() - lastPressTime > MIN_PRESS_INTERVAL) {
    lastPressTime = millis();
    Serial.println("[EVENT] Doorbell pressed!");
    publishMQTTEvent("doorbell_ring");
  }
}

void publishMQTTEvent(const char* event) {
  if (!mqtt.connected()) {
    Serial.println("[MQTT] Can't publish - not connected");
    return;
  }

  StaticJsonDocument<128> doc;
  doc["event"] = event;
  char buffer[128];
  serializeJson(doc, buffer);

  Serial.print("[MQTT] Publishing: ");
  Serial.println(buffer);

  if (mqtt.publish("home/doorbell", buffer)) {
    Serial.println("[MQTT] Publish successful");
  } else {
    Serial.println("[MQTT] Publish failed");
  }
}
