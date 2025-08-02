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
/*                           NFC CONNECTION TEST                      */
/* ------------------------------------------------------------------ */


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

    factory.update();          // all the SPI/GPIO work
    vTaskDelay(1);             // 1 ms = 1 kHz; tune as you like
  }
}

void setup()
{
    Serial.begin(115200);
    Serial.println("\nMaster Board ESP32-S3 booting…");

    pinMode(BUZZER_PIN, OUTPUT);
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

    /* ---------- NFC Testing & Initialization ---------- */
    Serial.println("\n🔧 Testing NFC hardware...");
    SPI.begin(NFC_SCK_PIN, NFC_MISO_PIN, NFC_MOSI_PIN, NFC_SS_PIN);
    mfrc522.PCD_Init();
    Serial.printf("[NFC] Using pins: SCK=%d, MISO=%d, MOSI=%d, SS=%d, RST=%d\n",
                  NFC_SCK_PIN, NFC_MISO_PIN, NFC_MOSI_PIN, NFC_SS_PIN, NFC_RST_PIN);
                    
  // Initialize MFRC522
    mfrc522.PCD_DumpVersionToSerial();
  
    

    auto& gameManager = GameManager::getInstance();
    gameManager.initNfcRegistry(&nfcRegistry);

    Serial.println("Setup done ✓");
    xTaskCreatePinnedToCore(
      ioTask,                 // task function
      "IO",                   // name
      4096,                   // stack bytes
      nullptr,                // param
      3,                      // priority (higher than default=1)
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
    
    // NFC scanning with basic status
    static unsigned long lastNfcScan = 0;
    if (millis() - lastNfcScan >= 100) {  // Scan every 100ms
        bool cardFound = nfcRegistry.scanForCards();
        if (cardFound) {
            Serial.println("📱 [NFC] Card detected and processed!");
        }
        lastNfcScan = millis();
    }

    long now = millis();
    GameManager::getInstance().update(); // update game logic
    long elapsed = millis() - now;

    if (millis() - lastDebugTime >= POWER_PLANT_DEBUG_INTERVAL) {
        auto& gameManager = GameManager::getInstance();
        Serial.printf("[PLANTS] Debug: %lu ms slong\n", elapsed);
        lastDebugTime = millis();
        gameManager.printDebugInfo();
        bool selfTestResult = mfrc522.PCD_PerformSelfTest();
        if (selfTestResult) {
            Serial.println("✅ PASSED");
        } else {
            Serial.println("❌ FAILED - Hardware may be faulty");
        }


    }
}
