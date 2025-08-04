/***********************************************************************
 *  Master Board ESP32‑S3 – spin‑lock‑safe version
 *  Last edit: 2025‑07‑28
 ******************************************************************************/
#include <Arduino.h>
#include <WiFi.h>
#include "PeripheralFactory.h"
#include <SPI.h>
#include <MFRC522.h>
#include <NFCBuildingRegistry.h>
#include <com-prot.h>

#include "board_config.h"
#include "power_plant_config.h"
#include "GameManager.h"

/* ------------------------------------------------------------------ */
/*                             PIN MAP                                */
/* ------------------------------------------------------------------ */
#define BUZZER_PIN        35

#define CLOCK_PIN         18            // Shift register clock
#define LATCH_PIN         16            // Shift register latch  
#define DATA_PIN          17            // Shift register data

#define ENCODER1_PIN_A     4
#define ENCODER1_PIN_B     5
#define ENCODER1_PIN_SW    6
#define ENCODER2_PIN_A    10
#define ENCODER2_PIN_B    11
#define ENCODER2_PIN_SW   12

/* Future power plant encoders - pins reserved but not currently used */
// #define ENCODER3_PIN_A    13
// #define ENCODER3_PIN_B    14
// #define ENCODER3_PIN_SW   15
// #define ENCODER4_PIN_A     7
// #define ENCODER4_PIN_B     8
// #define ENCODER4_PIN_SW    9

/* Unused pins for future expansion */
#define NFC_SCK_PIN       40
#define NFC_MISO_PIN      41
#define NFC_MOSI_PIN      39
#define NFC_RST_PIN       42
#define NFC_SS_PIN         21
#define COMPROT_PIN       19

/* ------------------------------------------------------------------ */
/*                        COM-PROT CONFIGURATION                      */
/* ------------------------------------------------------------------ */
// Create master instance - Master ID 1, pin 19 (COMPROT_PIN)
ComProtMaster master(1, COMPROT_PIN);

/* ------------------------------------------------------------------ */
/*                    GLOBAL STATE & FORWARD DECLS                    */
/* ------------------------------------------------------------------ */
// Game state is now managed by GameManager singleton
unsigned long lastUpdateTime = 0;
unsigned long lastDebugTime  = 0;

/* ---------------- Hardware helper singletons --------------------- */
PeripheralFactory  factory;
ShiftRegisterChain *shiftChain  = nullptr;

Bargraph        *bargraph1 = nullptr, *bargraph2 = nullptr,
                *bargraph3 = nullptr, *bargraph4 = nullptr,
                *bargraph5 = nullptr, *bargraph6 = nullptr;
SegmentDisplay  *display1  = nullptr, *display2  = nullptr,
                *display3  = nullptr, *display4  = nullptr,
                *display5  = nullptr, *display6  = nullptr;
Encoder         *encoder1  = nullptr, *encoder2  = nullptr;


MFRC522 mfrc522(NFC_SS_PIN, NFC_RST_PIN);
NFCBuildingRegistry nfcRegistry(&mfrc522);

/* ------------------------------------------------------------------ */
/*                      COM-PROT INTERRUPT HANDLER                    */
/* ------------------------------------------------------------------ */
// This ISR is called on a rising edge on the COM-PROT pin.
// It immediately calls receive() to handle the incoming message.
// NOTE: The receive() function must be safe to call from an ISR.
void IRAM_ATTR onComProtRise() {
    master.receive();
}

/* ------------------------------------------------------------------ */
/*                           COM-PROT TASK                            */
/* ------------------------------------------------------------------ */
TaskHandle_t comProtTaskHandle = nullptr;

void comProtTask(void* pvParameters) {
    unsigned long lastDebugTime = 0;
    Serial.println("[COM-PROT Task] Running on core " + String(xPortGetCoreID()));

    for (;;) {
        master.update();

        if (millis() - lastDebugTime >= POWER_PLANT_DEBUG_INTERVAL) {
            auto allSlaves = master.getConnectedSlaves();
            Serial.printf("[COM-PROT] Active power plants: %d\n", allSlaves.size());
            
            for (const auto& slave : allSlaves) {
                Serial.printf("[COM-PROT]   Power Plant ID=%d, Type=%d\n", slave.id, slave.type);
            }
            
            // Example: Send LED toggle command to type 1 slaves every debug cycle
            if (master.getSlavesByType(1).size() > 0) {
                uint8_t ledState = (millis() / 10000) % 2; // Toggle every 10 seconds
                master.sendCommandToSlaveType(1, 0x10, &ledState, 1);
                Serial.printf("[COM-PROT] Sent LED command (state=%d) to type 1 power plants\n", ledState);
            }
            lastDebugTime = millis();
        }
        // master.receive(); // This is now handled by the ISR
        taskYIELD(); // Yield to other tasks, allows for very high update rate
    }
}


/* ------------------------------------------------------------------ */
/*                           NFC CONNECTION TEST                      */
/* ------------------------------------------------------------------ */


/* ------------------------------------------------------------------ */
/*                        COM-PROT DEBUG HANDLER                      */
/* ------------------------------------------------------------------ */

// Debug receive handler - called for every received message
void debugReceiveHandler(uint8_t* payload, uint16_t length, uint8_t senderId, uint8_t messageType) {
    // Only log non-heartbeat messages to avoid spam
    if (messageType != 0x03) { // Skip heartbeat messages
        Serial.printf("[COM-PROT] RX from slave %d: type=0x%02X, len=%d\n", senderId, messageType, length);
    }
    
    // Log heartbeat messages with less detail
    if (messageType == 0x03) {
        static unsigned long lastHeartbeatLog = 0;
        if (millis() - lastHeartbeatLog > 5000) { // Log every 5 seconds
            Serial.printf("[COM-PROT] Heartbeats active from %d slaves\n", master.getSlaveCount());
            lastHeartbeatLog = millis();
        }
    }
}


/* ------------------------------------------------------------------ */
/*                        WIFI CONNECTION                             */
/* ------------------------------------------------------------------ */

void printWiFiStatusCode(wl_status_t status) {
    switch (status) {
        case WL_IDLE_STATUS:
            Serial.print("Idle");
            break;
        case WL_NO_SSID_AVAIL:
            Serial.print("No SSID Available");
            break;
        case WL_SCAN_COMPLETED:
            Serial.print("Scan Completed");
            break;
        case WL_CONNECTED:
            Serial.print("Connected");
            break;
        case WL_CONNECT_FAILED:
            Serial.print("Connection Failed");
            break;
        case WL_CONNECTION_LOST:
            Serial.print("Connection Lost");
            break;
        case WL_DISCONNECTED:
            Serial.print("Disconnected");
            break;
        default:
            Serial.print("Unknown Status");
            break;
    }
}

bool connectToWiFi()
{
    const char* ssid = "PotkaniNora";
    const char* password = "PrimaryPapikTarget";
    const int max_connection_attempts = 10;

    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(100);
    Serial.println("\n🔄 Connecting to WiFi...");
    Serial.print("SSID: ");
    Serial.println(ssid);
    
    WiFi.begin(ssid, password);
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < max_connection_attempts) {
        delay(1000);
        Serial.print(".");
        attempts++;
        
        // Print connection status
        switch (WiFi.status()) {
            case WL_NO_SSID_AVAIL:
                Serial.print(" [SSID not found]");
                break;
            case WL_CONNECT_FAILED:
                Serial.print(" [Connection failed]");
                break;
            case WL_CONNECTION_LOST:
                Serial.print(" [Connection lost]");
                break;
            case WL_DISCONNECTED:
                Serial.print(" [Disconnected]");
                break;
        }
    }
    
    Serial.println();
    
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("✅ WiFi connected successfully!");
        return true;
    } else {
        Serial.println("❌ WiFi connection failed!");
        Serial.print("Final status: ");
        printWiFiStatusCode(WiFi.status());
        Serial.println();
        return false;
    }
}

/* ------------------------------------------------------------------ */
/*                               SETUP                                */
/* ------------------------------------------------------------------ */

void initPeripherals()
{
    /* ---------- Peripherals ---------- */
    encoder1 = factory.createEncoder(ENCODER1_PIN_B, ENCODER1_PIN_A,
                                     ENCODER1_PIN_SW, 0, 1000, 1);
    Serial.println("[Peripherals] Encoder 1 created");
    encoder2 = factory.createEncoder(ENCODER2_PIN_B, ENCODER2_PIN_A,
                                     ENCODER2_PIN_SW, 0, 1000, 1);
    Serial.println("[Peripherals] Encoder 2 created");
    encoder1->setValue(500);  // 50%
    encoder2->setValue(500);  // 50%
    Serial.println("[Peripherals] Encoders initialized");

    shiftChain = factory.createShiftRegisterChain(LATCH_PIN, DATA_PIN, CLOCK_PIN);
    bargraph6 = factory.createBargraph(shiftChain, 10);
    display6  = factory.createSegmentDisplay(shiftChain, 4);
    bargraph5 = factory.createBargraph(shiftChain, 10);
    display5  = factory.createSegmentDisplay(shiftChain, 4);
    bargraph4 = factory.createBargraph(shiftChain, 10);
    display4  = factory.createSegmentDisplay(shiftChain, 4);
    bargraph3 = factory.createBargraph(shiftChain, 10);
    display3  = factory.createSegmentDisplay(shiftChain, 4);
    bargraph2 = factory.createBargraph(shiftChain, 10);
    display2  = factory.createSegmentDisplay(shiftChain, 4);
    bargraph1 = factory.createBargraph(shiftChain, 10);
    display1  = factory.createSegmentDisplay(shiftChain, 4);

    display1->displayNumber(8878.0,1); // display test pattern
    display2->displayNumber(8878.0,1);
    display3->displayNumber(8878.0,1);
    display4->displayNumber(8878.0,1);
    display5->displayNumber(8878.0,1);
    display6->displayNumber(8878.0,1);
    bargraph1->setValue(4);
    bargraph2->setValue(5);
    bargraph3->setValue(4);
    bargraph4->setValue(5);
    bargraph5->setValue(4);
    bargraph6->setValue(4);
    // Initialize the GameManager with power plants
    // Note: Last added in factory is first in chain, so we add in reverse order
    auto& gameManager = GameManager::getInstance();
    
    // Add Power Plant 1 (Coal) - using display1, bargraph1, encoder1
    gameManager.addPowerPlant(0, SOURCE_COAL, encoder1, display1, bargraph1);
    
    // Add Power Plant 2 (Gas) - using display2, bargraph2, encoder2  
    gameManager.addPowerPlant(1, SOURCE_GAS, encoder2, display2, bargraph2);

}

TaskHandle_t ioTaskHandle = nullptr;
void ioTask(void*){
  for (;;){
    // Update displays using GameManager
    GameManager::updateDisplays();
        // master.receive(); // This is now handled by the ISR

    factory.update();          // all the SPI/GPIO work
    // master.receive(); // This is now handled by the ISR

    vTaskDelay(1);             // 1 ms = 1 kHz; tune as you like
  }
}

void setup()
{
    Serial.begin(115200);
    Serial.println("\nMaster Board ESP32-S3 booting…");

    /*pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW);

    /* ---------- Wi‑Fi & ESP-API initialization ---------- */
    if (connectToWiFi()) {
        Serial.println("[ESP-API] Initializing via GameManager…");
        auto& gameManager = GameManager::getInstance();
        gameManager.initEspApi(SERVER_URL, BOARD_NAME, API_USERNAME, API_PASSWORD);
        Serial.println("[ESP-API] Setup done ✓");
    } else {
        Serial.println("[ESP-API] Skipped due to WiFi connection failure");
    }
    initPeripherals();

    // ---------- NFC Testing & Initialization ----------
    Serial.println("\n🔧 Testing NFC hardware...");
    SPI.begin(NFC_SCK_PIN, NFC_MISO_PIN, NFC_MOSI_PIN, NFC_SS_PIN);
    mfrc522.PCD_Init();
    Serial.printf("[NFC] Using pins: SCK=%d, MISO=%d, MOSI=%d, SS=%d, RST=%d\n",
                  NFC_SCK_PIN, NFC_MISO_PIN, NFC_MOSI_PIN, NFC_SS_PIN, NFC_RST_PIN);
                    
  // Initialize MFRC522
    mfrc522.PCD_DumpVersionToSerial();
  
    

    auto& gameManager = GameManager::getInstance();
    gameManager.initNfcRegistry(&nfcRegistry);

    /* ---------- COM-PROT Master Initialization ---------- 
    Serial.println("\n🔧 Initializing COM-PROT Master...");*/
    
    // Set debug receive handler
    master.setDebugReceiveHandler(debugReceiveHandler);
    
    // Initialize the master
    master.begin();

    // Attach interrupt for COM-PROT pin
    pinMode(COMPROT_PIN, INPUT);
    attachInterrupt(digitalPinToInterrupt(COMPROT_PIN), onComProtRise, RISING);

    // Create ComProt task on core 0
    /*xTaskCreatePinnedToCore(
        comProtTask,          // Task function
        "ComProt",            // Task name
        4096,                 // Stack size
        nullptr,              // Task parameters
        2,                    // Priority
        &comProtTaskHandle,   // Task handle
        1                     // Core ID
    );*/

    // Increase priority of the loop task
    
    Serial.printf("[COM-PROT] Master initialized on pin %d\n", COMPROT_PIN);
    Serial.println("[COM-PROT] Ready to discover power plants...");

    Serial.println("Setup done ✓");
    xTaskCreatePinnedToCore(
      ioTask,                 // task function
      "IO",                   // name
      4096,                   // stack bytes
      nullptr,                // param
      1,                      // priority (higher than default=1)
      &ioTaskHandle,
      1);  
}

/* ------------------------------------------------------------------ */
/*                                LOOP                                */
/* ------------------------------------------------------------------ */

void loop()
{
    if (WiFi.status() == WL_CONNECTED && GameManager::getInstance().updateEspApi())
        GameManager::getInstance().updateCoefficientsFromGame();
    
    // COM-PROT logic is now in its own task (comProtTask)
    
    // NFC scanning with basic status
    /*tatic unsigned long lastNfcScan = 0;
    if (millis() - lastNfcScan >= 100) {  // Scan every 100ms
        bool cardFound = nfcRegistry.scanForCards();
        if (cardFound) {
            Serial.println("📱 [NFC] Card detected and processed!");
        }
        lastNfcScan = millis();
    }

    long now = millis();*/
    GameManager::getInstance().update(); // update game logic
        

    //long elapsed = millis() - now;
    
    //master.update();

    if (millis() - lastDebugTime >= POWER_PLANT_DEBUG_INTERVAL) {
        // master.receive(); // This is now handled by the ISR
        auto allSlaves = master.getConnectedSlaves();
        Serial.printf("[COM-PROT] Active power plants: %d\n", allSlaves.size());
        // master.receive(); // This is now handled by the ISR

        for (const auto& slave : allSlaves) {
            Serial.printf("[COM-PROT]   Power Plant ID=%d, Type=%d\n", slave.id, slave.type);
        }
        
        // Example: Send LED toggle command to type 1 slaves every debug cycle
        if (master.getSlavesByType(1).size() > 0) {
            uint8_t ledState = (millis() / 10000) % 2; // Toggle every 10 seconds
            master.sendCommandToSlaveType(1, 0x10, &ledState, 1);
            Serial.printf("[COM-PROT] Sent LED command (state=%d) to type 1 power plants\n", ledState);
        }
        lastDebugTime = millis();


        // list nfc cards aka consumers
        GameManager::getInstance().printDebugInfo();


    }
}
