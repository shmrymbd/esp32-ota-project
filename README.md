# esp32-ota-project
BLE + OTA + MQTT project for ESP32
#include <WiFi.h>
#include <WiFiClient.h>
#include <PubSubClient.h>
#include <ArduinoOTA.h>
#include "time.h"
#include <set>

// === MQTT Broker Configuration ===
#define WIFI_SSID     "loranet"
#define WIFI_PASSWORD "1qaz2wsx"

#define MQTT_HOST     "mqttiot.loranet.my"
#define MQTT_PORT     1885
#define MQTT_USERNAME "iotdbuser"
#define MQTT_PASSWORD "IoTdb2024"
#define MQTT_TOPIC    "traffic/bluetooth/mac"
#define MQTT_CONTROL_TOPIC "traffic/bluetooth/control"

#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_bt_api.h"

WiFiClient espClient;
PubSubClient client(espClient);

// Malaysia Time UTC+8
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 8 * 3600;
const int daylightOffset_sec = 0;

std::set<String> seenMacs; // ✅ Store unique MACs

// === Get formatted time ===
String getCurrentTimeString() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return "Unknown";
  char timeStr[64];
  strftime(timeStr, sizeof(timeStr), "%A, %B %d %Y %I:%M:%S %p", &timeinfo);
  return String(timeStr);
}

// === Bluetooth Callback ===
void btCallback(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param) {
  if (event == ESP_BT_GAP_DISC_RES_EVT) {
    char macStr[18];
    sprintf(macStr, "%02X:%02X:%02X:%02X:%02X:%02X",
            param->disc_res.bda[0], param->disc_res.bda[1], param->disc_res.bda[2],
            param->disc_res.bda[3], param->disc_res.bda[4], param->disc_res.bda[5]);
    String macString = String(macStr);

    if (seenMacs.find(macString) != seenMacs.end()) {
      // ✅ MAC already seen
      return;
    }

    seenMacs.insert(macString); // ✅ Store new MAC

    String jsonPayload = "{\"device_id\":\"esp32_ble_point_A\",";
    jsonPayload += "\"mac\":\"" + macString + "\",";
    jsonPayload += "\"point\":\"A\",";
    jsonPayload += "\"time\":\"" + getCurrentTimeString() + "\"}";

    Serial.println("[BLE] Found MAC: " + jsonPayload);

    if (client.connected()) {
      client.publish(MQTT_TOPIC, jsonPayload.c_str());
      Serial.println("[MQTT] Published to topic.");
    } else {
      Serial.println("[MQTT] Not connected. Skipping publish.");
    }
  }

  if (event == ESP_BT_GAP_DISC_STATE_CHANGED_EVT) {
    if (param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STOPPED) {
      Serial.println("[BLE] Scan stopped, restarting...");
      esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, 0x30, 0);
    }
  }
}

// === WiFi Connection ===
void connectToWiFi() {
  Serial.print("[WiFi] Connecting to WiFi...");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  int retries = 0;
  while (WiFi.status() != WL_CONNECTED && retries < 30) {
    delay(500);
    Serial.print(".");
    retries++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[WiFi] Connected!");
    Serial.print("[WiFi] IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\n[WiFi] ❌ Failed to connect.");
  }
}

// === Time Sync ===
void syncTime() {
  Serial.println("[TIME] Syncing...");
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  struct tm timeinfo;
  int retries = 0;
  while (!getLocalTime(&timeinfo) && retries < 20) {
    delay(500);
    Serial.print(".");
    retries++;
  }
  if (getLocalTime(&timeinfo)) {
    Serial.println("\n[TIME] Time synchronized!");
  } else {
    Serial.println("\n[TIME] ❌ Failed to sync time.");
    ESP.restart();
  }
}

// === MQTT Callback ===
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String message;
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }

  Serial.printf("[MQTT] Message received on topic %s: %s\n", topic, message.c_str());

  if (message == "reboot") {
    Serial.println("[MQTT] Reboot command received. Restarting...");
    delay(1000);
    ESP.restart();
  }
}

// === MQTT Connect ===
void connectToMQTT() {
  client.setServer(MQTT_HOST, MQTT_PORT);
  client.setCallback(mqttCallback);

  int retryCount = 0;
  const int maxRetries = 5;

  while (!client.connected() && retryCount < maxRetries) {
    Serial.print("[MQTT] Connecting...");
    String clientId = "ESP32Client-" + String(random(0xffff), HEX);
    if (client.connect(clientId.c_str(), MQTT_USERNAME, MQTT_PASSWORD)) {
      Serial.println("Connected to MQTT!");
      client.subscribe(MQTT_CONTROL_TOPIC);
      Serial.println("[MQTT] Subscribed to control topic.");
      return;
    } else {
      Serial.print("Failed, rc=");
      Serial.print(client.state());
      Serial.println("... retrying in 5 seconds");
      retryCount++;
      delay(5000);
    }
  }

  if (!client.connected()) {
    Serial.println("[MQTT] ❌ Unable to connect after several attempts. Rebooting...");
    delay(2000);
    ESP.restart();
  }
}

// === OTA Setup ===
void setupOTA() {
  ArduinoOTA.setHostname("esp32_ble_point_A");
  ArduinoOTA.setPassword("MyStrongOTAPassword");

  ArduinoOTA.onStart([]() {
    Serial.println("[OTA] Start updating...");
  });

  ArduinoOTA.onEnd([]() {
    Serial.println("\n[OTA] Update complete.");
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("[OTA] Progress: %u%%\r", (progress / (total / 100)));
  });

  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("[OTA] Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });

  ArduinoOTA.begin();
  Serial.println("[OTA] Ready for OTA updates.");
}

// === Bluetooth Setup ===
void setupBluetooth() {
  Serial.println("[BLE] Initializing Bluetooth...");
  if (!btStart()) {
    Serial.println("[BLE] Failed to start");
    return;
  }

  esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
  esp_bt_controller_init(&bt_cfg);
  esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT);
  esp_bluedroid_init();
  esp_bluedroid_enable();
  esp_bt_gap_register_callback(btCallback);
  esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);

  Serial.println("[BLE] Starting continuous scan...");
  esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, 0x30, 0);
}

// === Setup ===
void setup() {
  Serial.begin(115200);
  Serial.println("[BOOT] Starting ESP32 BLE scanner...");

  connectToWiFi();
  syncTime();
  setupOTA();
  Serial.print("[INFO] ESP32 MAC Address: ");
  Serial.println(WiFi.macAddress());

  connectToMQTT();
  setupBluetooth();
}

// === Loop ===
void loop() {
  ArduinoOTA.handle();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[WiFi] Lost connection. Reconnecting...");
    connectToWiFi();
  }

  if (!client.connected()) {
    connectToMQTT();
  }

  client.loop();
}



