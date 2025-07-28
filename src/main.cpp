#include <Arduino.h>
#include <WiFi.h>
#include <SPI.h>
#include <MFRC522.h>
#include "PeripheralFactory.h"
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
#define BUZZER_PIN    35
// Communication Protocol pin
#define COMPROT_PIN   18

#define LATCH_PIN 16
#define DATA_PIN  17
#define CLOCK_PIN 18
#define ENCODER1_PIN_A 7
#define ENCODER1_PIN_B 8
#define ENCODER1_PIN_SW 9
#define ENCODER2_PIN_A 4 
#define ENCODER2_PIN_B 5
#define ENCODER2_PIN_SW 6
#define ENCODER3_PIN_A 13
#define ENCODER3_PIN_B 14
#define ENCODER3_PIN_SW 15

#define ENCODER4_PIN_A 10
#define ENCODER4_PIN_B 11
#define ENCODER4_PIN_SW 12
// ESP-API configuration
#define ESP_API_IP    "192.168.2.131"
#define ESP_API_NAME  "board1"
#define ESP_API_PASS  "board123"
#define BOARD_ID      1
unsigned long lastUpdateTime = 0;
// Global objects
//MFRC522 mfrc522(NFC_SS_PIN, NFC_RST_PIN);
//NFCBuildingRegistry nfcRegistry(&mfrc522);
//ComProtMaster comProt(1, COMPROT_PIN);
//ESPGameAPI espApi("http://192.168.2.131", BOARD_ID, ESP_API_NAME, BOARD_GENERIC);

// Callback functions for NFC building events

PeripheralFactory factory;
ShiftRegisterChain* shiftChain = nullptr;



Bargraph* bargraph6 = nullptr;
SegmentDisplay* display6 = nullptr;
Bargraph* bargraph5 = nullptr;
SegmentDisplay* display5 = nullptr;
Bargraph* bargraph4 = nullptr;
SegmentDisplay* display4 = nullptr;
Bargraph* bargraph3 = nullptr;
SegmentDisplay* display3 = nullptr;
Bargraph* bargraph2 = nullptr;
SegmentDisplay* display2 = nullptr;
Bargraph* bargraph1 = nullptr;
SegmentDisplay* display1 = nullptr;
Encoder* encoder1 = nullptr;
Encoder* encoder2 = nullptr;
Encoder* encoder3 = nullptr;
Encoder* encoder4 = nullptr;
//SegmentDisplay* display2 = factory.createSegmentDisplay(shiftChain, 8);



// WiFi connection function
bool connectToWiFi() {
    Serial.print("Connecting to WiFi: ");
    Serial.println(WIFI_SSID);
    //setup Buzzer for output
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
bool reversed = false;
void setup() {

    
    Serial.begin(115200);
    Serial.println("Master Board ESP32-S3 Starting...");
    digitalWrite(BUZZER_PIN, LOW);

    pinMode(BUZZER_PIN, OUTPUT);
    // disable Buzzer
    digitalWrite(BUZZER_PIN, LOW);
    // Initialize custom SPI pins
    //SPI.begin(NFC_SCK_PIN, NFC_MISO_PIN, NFC_MOSI_PIN, NFC_SS_PIN);
    
    // Initialize NFC/MFRC522
    Serial.println("Initializing NFC/MFRC522...");
    /*mfrc522.PCD_Init();
    mfrc522.PCD_DumpVersionToSerial();
    
    // Set up NFC building registry callbacks
    nfcRegistry.setOnNewBuildingCallback(PowerTracker::onNewBuilding);
    nfcRegistry.setOnDeleteBuildingCallback(PowerTracker::onDeleteBuilding)*/;
    
    Serial.println("NFC Building Registry initialized successfully");

    // Initialize Communication Protocol
    Serial.println("Initializing Communication Protocol...");
    // The ComProtMaster is already initialized in the constructor
    Serial.println("Communication Protocol initialized successfully");

    // Connect to WiFi
    /*if (connectToWiFi()) {
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
    }*/

    Serial.println("Setup complete!");
    encoder1 = factory.createEncoder(ENCODER1_PIN_A, ENCODER1_PIN_B, ENCODER1_PIN_SW, 0, 255, 1);
    encoder2 = factory.createEncoder(ENCODER2_PIN_A, ENCODER2_PIN_B, ENCODER2_PIN_SW, 0, 255, 1);
    encoder3 = factory.createEncoder(ENCODER3_PIN_A, ENCODER3_PIN_B, ENCODER3_PIN_SW, 0, 255, 1);
    encoder4 = factory.createEncoder(ENCODER4_PIN_A, ENCODER4_PIN_B, ENCODER4_PIN_SW, 0, 255, 1);




    shiftChain = factory.createShiftRegisterChain(LATCH_PIN, DATA_PIN, CLOCK_PIN);

 bargraph6 = factory.createBargraph(shiftChain, 10);
 display6 = factory.createSegmentDisplay(shiftChain, 4);
 bargraph5 = factory.createBargraph(shiftChain, 10);
 display5 = factory.createSegmentDisplay(shiftChain, 4);
 bargraph4 = factory.createBargraph(shiftChain, 10);
 display4 = factory.createSegmentDisplay(shiftChain, 4);
 bargraph3 = factory.createBargraph(shiftChain, 10);
 display3 = factory.createSegmentDisplay(shiftChain, 4);
 bargraph2 = factory.createBargraph(shiftChain, 10);
 display2 = factory.createSegmentDisplay(shiftChain, 4);
 bargraph1 = factory.createBargraph(shiftChain, 10);
 display1 = factory.createSegmentDisplay(shiftChain, 4);
    
}

void loop() {
    // Scan for NFC cards
    /*if (nfcRegistry.scanForCards()) {
        // Card event was processed by the callbacks
        // The registry automatically handles card detection and database management
    }*/
    factory.update();
	
	
	if(millis() % 100 == 0) {
		
		float timesec = (float)millis();

		
		uint8_t value1 = encoder1->getValue();
		uint8_t value2 = encoder2->getValue();
		uint8_t value3 = encoder3->getValue();
		uint8_t value4 = encoder4->getValue();
        Serial.printf("Encoder Values: %d, %d, %d, %d\n", value1, value2, value3, value4);

        bargraph1->setValue((int)(((float)value3 / 255) * 10) % 11);
        bargraph2->setValue((int)(((float)value4 / 255) * 10) % 11);
        display1->displayNumber((float)value1);
        display2->displayNumber((float)value2);
	}
	
    // Handle communication protocol messages
    // The ComProtMaster automatically handles incoming messages
    // You can add command handlers if needed
    
    // Process any API calls or game updates
    // Note: ESP-API operations require WiFi connection
    
    // Small delay to prevent overwhelming the system
}
