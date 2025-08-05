#!/usr/bin/env python3
"""
ESP32 OTA Update Tool via MQTT
This script handles firmware updates for ESP32 devices over MQTT
"""

import paho.mqtt.client as mqtt
import time
import os
import sys
import argparse
import hashlib

# MQTT Settings
MQTT_BROKER = "mqttiot.loranet.my"
MQTT_PORT = 1885
MQTT_USER = "iotdbuser"
MQTT_PASSWORD = "IoTdb2024"
OTA_TOPIC = "traffic/bluetooth/ota"
CHUNK_SIZE = 1024  # Size of each firmware chunk

class OTAUpdater:
    def __init__(self, broker, port, username, password):
        self.client = mqtt.Client()
        self.client.username_pw_set(username, password)
        self.client.on_connect = self.on_connect
        self.client.on_message = self.on_message
        self.broker = broker
        self.port = port
        self.connected = False
        self.update_complete = False
        self.progress = 0

    def on_connect(self, client, userdata, flags, rc):
        if rc == 0:
            print(f"Connected to MQTT Broker: {self.broker}")
            self.connected = True
        else:
            print(f"Failed to connect, return code: {rc}")

    def on_message(self, client, userdata, msg):
        if msg.topic == OTA_TOPIC:
            try:
                message = msg.payload.decode()
                if "Progress" in message:
                    self.progress = int(message.split("(")[1].split("%")[0])
                    self.print_progress()
                elif "successful" in message:
                    print("\nUpdate successful!")
                    self.update_complete = True
            except Exception as e:
                print(f"Error processing message: {e}")

    def print_progress(self):
        sys.stdout.write(f"\rProgress: {self.progress}%")
        sys.stdout.flush()

    def connect(self):
        self.client.connect(self.broker, self.port, 60)
        self.client.loop_start()
        timeout = 5
        while not self.connected and timeout > 0:
            time.sleep(1)
            timeout -= 1
        return self.connected

    def calculate_checksum(self, firmware_path):
        """Calculate MD5 checksum of firmware file"""
        md5_hash = hashlib.md5()
        with open(firmware_path, "rb") as f:
            for byte_block in iter(lambda: f.read(4096), b""):
                md5_hash.update(byte_block)
        return md5_hash.hexdigest()

    def send_firmware(self, firmware_path, chunk_size=CHUNK_SIZE):
        if not os.path.exists(firmware_path):
            print(f"Error: Firmware file not found: {firmware_path}")
            return False

        # Get firmware size and checksum
        firmware_size = os.path.getsize(firmware_path)
        checksum = self.calculate_checksum(firmware_path)
        
        print(f"Firmware size: {firmware_size} bytes")
        print(f"MD5 Checksum: {checksum}")

        # Send start message with size and checksum
        start_message = f"START:{firmware_size}:{checksum}"
        self.client.publish(OTA_TOPIC, start_message)
        time.sleep(1)

        # Read and send firmware in chunks
        with open(firmware_path, "rb") as file:
            while True:
                chunk = file.read(chunk_size)
                if not chunk:
                    break
                self.client.publish(OTA_TOPIC, chunk)
                time.sleep(0.1)  # Small delay to prevent flooding

        # Send end message
        self.client.publish(OTA_TOPIC, "END")
        
        # Wait for completion or timeout
        timeout = 60  # 60 seconds timeout
        while not self.update_complete and timeout > 0:
            time.sleep(1)
            timeout -= 1

        return self.update_complete

    def disconnect(self):
        self.client.loop_stop()
        self.client.disconnect()

def main():
    parser = argparse.ArgumentParser(description='ESP32 OTA Update Tool')
    parser.add_argument('firmware', help='Path to firmware.bin file')
    parser.add_argument('--broker', default=MQTT_BROKER, help='MQTT broker address')
    parser.add_argument('--port', type=int, default=MQTT_PORT, help='MQTT broker port')
    parser.add_argument('--user', default=MQTT_USER, help='MQTT username')
    parser.add_argument('--password', default=MQTT_PASSWORD, help='MQTT password')
    args = parser.parse_args()

    updater = OTAUpdater(args.broker, args.port, args.user, args.password)
    
    try:
        if updater.connect():
            if updater.send_firmware(args.firmware):
                print("\nOTA update completed successfully!")
            else:
                print("\nOTA update failed or timed out!")
        else:
            print("Failed to connect to MQTT broker!")
    except KeyboardInterrupt:
        print("\nUpdate cancelled by user")
    finally:
        updater.disconnect()

if __name__ == "__main__":
    main()
