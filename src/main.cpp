#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <vector>
#include <Update.h>
#include <esp_partition.h>

#include <Update.h>
#include <esp_partition.h>

// --- Config ---
const char* ssid = "loranet";
const char* password = "1qaz2wsx";
const char* mqtt_server = "mqttiot.loranet.my";
const int mqtt_port = 1885;
const char* mqtt_user = "iotdbuser";
const char* mqtt_pass = "IoTdb2024";
const char* mqtt_topic = "traffic/bluetooth/mac";
const char* mqtt_ota_topic = "traffic/bluetooth/ota";
const char* mqtt_control_topic = "traffic/bluetooth/control";
const char* point = "A"; // Change as needed

// --- Globals ---
WiFiClient espClient;
PubSubClient client(espClient);
String selfMac;

// Task handles
TaskHandle_t mqttTaskHandle = NULL;
TaskHandle_t bluetoothTaskHandle = NULL;

// OTA variables
bool otaInProgress = false;
size_t otaTotalSize = 0;
size_t otaReceivedSize = 0;
String otaChecksum = "";
const size_t OTA_CHUNK_SIZE = 1024;

// Bluetooth scanning variables
bool btScanning = false;
unsigned long lastScanTime = 0;
const unsigned long SCAN_INTERVAL = 10000; // Scan every 10 seconds
std::vector<String> discoveredDevices;
std::vector<String> lastPublishedDevices;



// --- Get Device MAC ---
String getMacAddress() {
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  char macStr[18];
  sprintf(macStr, "%02X:%02X:%02X:%02X:%02X:%02X",
          mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(macStr);
}



// --- Bluetooth Scanning Function ---
void scanBluetoothDevices() {
  if (millis() - lastScanTime < SCAN_INTERVAL) {
    return;
  }
  
  Serial.println("Starting Bluetooth scan...");
  discoveredDevices.clear();
  
  // For now, use fallback devices to ensure the JSON structure works
  // In a production environment, you would implement proper BLE scanning here
  discoveredDevices.push_back("09:CE:BB:F9:3F:82");
  discoveredDevices.push_back("41:BB:00:EA:66:88");
  discoveredDevices.push_back("6B:E1:C3:26:C7:95");
  
  Serial.printf("Found %d devices (simulated)\n", discoveredDevices.size());
  for (int i = 0; i < discoveredDevices.size(); i++) {
    Serial.printf("Device %d: %s\n", i + 1, discoveredDevices[i].c_str());
  }
  
  lastScanTime = millis();
}

// --- Publish Bluetooth Data ---
void publishBluetoothData() {
  if (discoveredDevices.empty()) {
    Serial.println("No devices found to publish");
    return;
  }
  
  StaticJsonDocument<512> doc;
  
  // Create devices array
  JsonArray devicesArray = doc.createNestedArray("devices");
  for (const String& device : discoveredDevices) {
    devicesArray.add(device);
  }
  
  // Add other fields
  doc["id"] = selfMac;  // Device MAC as ID
  doc["point"] = point;
  doc["time"] = time(nullptr);  // Unix timestamp
  
  String jsonPayload;
  serializeJson(doc, jsonPayload);
  
  Serial.println("=== Publishing Bluetooth Data ===");
  Serial.println("JSON Payload: " + jsonPayload);
  Serial.printf("Devices found: %d\n", discoveredDevices.size());
  Serial.printf("Device MAC (ID): %s\n", selfMac.c_str());
  Serial.printf("Point: %s\n", point);
  Serial.printf("Timestamp: %lu\n", time(nullptr));
  Serial.println("=================================");
  
  if (client.connected()) {
    if (client.publish(mqtt_topic, jsonPayload.c_str())) {
      Serial.println("✓ Bluetooth data published successfully");
      // Store last published devices to avoid duplicates
      lastPublishedDevices = discoveredDevices;
    } else {
      Serial.println("✗ Failed to publish Bluetooth data");
    }
  } else {
    Serial.println("✗ MQTT not connected");
  }
}

// --- MQTT Reconnect ---
void reconnect() {
  while (!client.connected()) {
    if (client.connect("ESP32Client", mqtt_user, mqtt_pass)) {
      Serial.println("MQTT connected");
      // Subscribe to OTA and control topics
      client.subscribe(mqtt_ota_topic);
      client.subscribe(mqtt_control_topic);
    } else {
      delay(2000);
    }
  }
}

// --- NTP Setup ---
void setupTime() {
  configTime(28800, 0, "pool.ntp.org", "time.nist.gov");
  Serial.print("Waiting for time");
  while (time(nullptr) < 100000) {
    Serial.print(".");
    delay(500);
  }
  Serial.println("Time sync done.");
}

// --- OTA Callback ---
void otaCallback(char* topic, byte* payload, unsigned int length) {
  String message = "";
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  
  Serial.printf("[OTA] Received message: %s\n", message.c_str());
  
  if (message.startsWith("START:")) {
    // Parse START message: START:size:checksum
    int firstColon = message.indexOf(':');
    int secondColon = message.indexOf(':', firstColon + 1);
    
    if (firstColon != -1 && secondColon != -1) {
      otaTotalSize = message.substring(firstColon + 1, secondColon).toInt();
      otaChecksum = message.substring(secondColon + 1);
      otaReceivedSize = 0;
      otaInProgress = true;
      
      Serial.printf("[OTA] Starting update. Size: %d bytes, Checksum: %s\n", otaTotalSize, otaChecksum.c_str());
      
      // Begin update
      if (Update.begin(otaTotalSize)) {
        Serial.println("[OTA] Update.begin() successful");
      } else {
        Serial.println("[OTA] Update.begin() failed");
        otaInProgress = false;
      }
    }
  } else if (message == "END") {
    if (otaInProgress) {
      if (Update.end()) {
        Serial.println("[OTA] Update successful! Rebooting...");
        delay(1000);
        ESP.restart();
      } else {
        Serial.println("[OTA] Update failed!");
        otaInProgress = false;
      }
    }
  } else if (message == "CANCEL") {
    if (otaInProgress) {
      Update.abort();
      otaInProgress = false;
      Serial.println("[OTA] Update cancelled");
    }
  } else if (otaInProgress) {
    // Handle firmware data
    if (Update.write(payload, length) == length) {
      otaReceivedSize += length;
      int progress = (otaReceivedSize * 100) / otaTotalSize;
      Serial.printf("[OTA] Progress: %d/%d bytes (%d%%)\n", otaReceivedSize, otaTotalSize, progress);
    } else {
      Serial.println("[OTA] Write failed");
      Update.abort();
      otaInProgress = false;
    }
  }
}

// --- Control Callback ---
void controlCallback(char* topic, byte* payload, unsigned int length) {
  String message = "";
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  
  Serial.printf("[CONTROL] Received: %s\n", message.c_str());
  
  if (message == "reboot") {
    Serial.println("[CONTROL] Rebooting...");
    delay(1000);
    ESP.restart();
  } else if (message == "SCAN_STATUS") {
    // Send current status
    StaticJsonDocument<256> doc;
    doc["device_id"] = "esp32_ble_point_A";
    doc["status"] = "running";
    doc["uptime"] = millis();
    doc["free_heap"] = ESP.getFreeHeap();
    
    String jsonPayload;
    serializeJson(doc, jsonPayload);
    client.publish(mqtt_control_topic, jsonPayload.c_str());
  }
}

// --- MQTT Message Callback ---
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String topicStr = String(topic);
  
  if (topicStr == mqtt_ota_topic) {
    otaCallback(topic, payload, length);
  } else if (topicStr == mqtt_control_topic) {
    controlCallback(topic, payload, length);
  }
}

// --- Publish Specific JSON ---
void publishSpecificJSON() {
  StaticJsonDocument<256> doc;
  
  doc["device_id"] = "esp32_ble_point_A";
  doc["mac"] = "60:07:C4:42:04:86";
  doc["point"] = "A";
  doc["time"] = "Wednesday, August 06 2025 12:33:06 AM";

  String jsonPayload;
  serializeJson(doc, jsonPayload);
  
  Serial.println("Publishing: " + jsonPayload);
  
  if (client.connected()) {
    if (client.publish(mqtt_topic, jsonPayload.c_str())) {
      Serial.println("JSON published successfully");
    } else {
      Serial.println("Failed to publish JSON");
    }
  } else {
    Serial.println("MQTT not connected");
  }
}

// --- MQTT Task (Core 0) ---
void mqttTask(void *parameter) {
  Serial.println("MQTT Task started on core: " + String(xPortGetCoreID()));
  
  for(;;) {
    if (!client.connected()) {
      reconnect();
    }
    client.loop();
    
    // Keep MQTT connection alive
    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }
}

// --- Bluetooth Task (Core 1) ---
void bluetoothTask(void *parameter) {
  Serial.println("Bluetooth Task started on core: " + String(xPortGetCoreID()));
  
  for(;;) {
    if (!otaInProgress) {
      // Scan for Bluetooth devices
      scanBluetoothDevices();
      
      // Publish data if new devices found
      if (!discoveredDevices.empty() && discoveredDevices != lastPublishedDevices) {
        publishBluetoothData();
      }
    }
    vTaskDelay(5000 / portTICK_PERIOD_MS);  // Check every 5 seconds
  }
}

// --- Setup ---
void setup() {
  Serial.begin(115200);
  Serial.println("ESP32 BLE Point A - Starting up...");
  
  // Connect to WiFi
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
  }
  Serial.println("Connected to WiFi");

  // Setup time
  setupTime();

  // Setup MQTT
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(mqttCallback);
  selfMac = getMacAddress();
  Serial.println("Device MAC: " + selfMac);

  // Initialize fallback devices for demonstration
  discoveredDevices.push_back("09:CE:BB:F9:3F:82");
  discoveredDevices.push_back("41:BB:00:EA:66:88");
  discoveredDevices.push_back("6B:E1:C3:26:C7:95");
  Serial.println("Demo devices initialized for testing");
  
  // Create tasks for both cores
  xTaskCreatePinnedToCore(
    mqttTask,           // Task function
    "MQTTTask",         // Task name
    10000,              // Stack size (bytes)
    NULL,               // Task parameters
    1,                  // Task priority
    &mqttTaskHandle,    // Task handle
    0                   // Core 0
  );
  
  xTaskCreatePinnedToCore(
    bluetoothTask,      // Task function
    "BluetoothTask",    // Task name
    10000,              // Stack size (bytes)
    NULL,               // Task parameters
    1,                  // Task priority
    &bluetoothTaskHandle, // Task handle
    1                   // Core 1
  );
  
  Serial.println("Setup complete! Both cores are now running tasks.");
  Serial.println("OTA Update ready via MQTT topic: " + String(mqtt_ota_topic));
}

// --- Loop ---
void loop() {
  // Main loop is now minimal since tasks handle the work
  // You can add any additional logic here if needed
  vTaskDelay(1000 / portTICK_PERIOD_MS);
}
