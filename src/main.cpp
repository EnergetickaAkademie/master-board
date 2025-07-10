#include <Arduino.h>
#include <WiFi.h>
#include <SPI.h>
#include <MFRC522.h>

// NFC Building Registry Library
#include <NFCBuildingRegistry.h>

// Communication Protocol Library
#include <com-prot.h>

// ESP-API Library
#include <ESPGameAPI.h>

#include <power_tracker.h>

// WiFi configuration
#include "wifi_config.h"

// NFC SPI pin configuration
#define NFC_SCK_PIN   40
#define NFC_MISO_PIN  41
#define NFC_MOSI_PIN  39
#define NFC_RST_PIN   42
#define NFC_SS_PIN    5   // SS pin for SPI

// Communication Protocol pin
#define COMPROT_PIN   18

// ESP-API configuration
#define ESP_API_IP    "192.168.2.131"
#define ESP_API_NAME  "board1"
#define ESP_API_PASS  "board123"
#define BOARD_ID      1

// Global objects
MFRC522 mfrc522(NFC_SS_PIN, NFC_RST_PIN);
NFCBuildingRegistry nfcRegistry(&mfrc522);
ComProtMaster comProt(1, COMPROT_PIN);
ESPGameAPI espApi("http://192.168.2.131", BOARD_ID, ESP_API_NAME, BOARD_GENERIC);

// Callback functions for NFC building events


// WiFi connection function
bool connectToWiFi() {
    Serial.print("Connecting to WiFi: ");
    Serial.println(WIFI_SSID);
    
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    
    unsigned long startTime = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startTime < WIFI_TIMEOUT_MS) {
        delay(500);
        Serial.print(".");
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\nWiFi connected successfully!");
        Serial.print("IP address: ");
        Serial.println(WiFi.localIP());
        return true;
    } else {
        Serial.println("\nWiFi connection failed!");
        return false;
    }
}

void setup() {
    Serial.begin(115200);
    Serial.println("Master Board ESP32-S3 Starting...");

    // Initialize custom SPI pins
    SPI.begin(NFC_SCK_PIN, NFC_MISO_PIN, NFC_MOSI_PIN, NFC_SS_PIN);
    
    // Initialize NFC/MFRC522
    Serial.println("Initializing NFC/MFRC522...");
    mfrc522.PCD_Init();
    mfrc522.PCD_DumpVersionToSerial();
    
    // Set up NFC building registry callbacks
    nfcRegistry.setOnNewBuildingCallback(PowerTracker::onNewBuilding);
    nfcRegistry.setOnDeleteBuildingCallback(PowerTracker::onDeleteBuilding);
    
    Serial.println("NFC Building Registry initialized successfully");

    // Initialize Communication Protocol
    Serial.println("Initializing Communication Protocol...");
    // The ComProtMaster is already initialized in the constructor
    Serial.println("Communication Protocol initialized successfully");

    // Connect to WiFi
    if (connectToWiFi()) {
        // Initialize ESP-API
        Serial.println("Initializing ESP-API...");
        if (espApi.login(ESP_API_NAME, ESP_API_PASS)) {
            Serial.println("ESP-API login successful");
            if (espApi.registerBoard()) {
                Serial.println("ESP-API board registration successful");
            } else {
                Serial.println("ESP-API board registration failed");
            }
        } else {
            Serial.println("ESP-API login failed");
        }
    } else {
        Serial.println("ESP-API initialization skipped (no WiFi connection)");
    }

    Serial.println("Setup complete!");
}

void loop() {
    // Scan for NFC cards
    if (nfcRegistry.scanForCards()) {
        // Card event was processed by the callbacks
        // The registry automatically handles card detection and database management
    }

    // Handle communication protocol messages
    // The ComProtMaster automatically handles incoming messages
    // You can add command handlers if needed
    
    // Process any API calls or game updates
    // Note: ESP-API operations require WiFi connection
    
    // Small delay to prevent overwhelming the system
}
