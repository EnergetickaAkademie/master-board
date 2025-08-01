/***********************************************************************
 *  Master Board ESP32‑S3 – spin‑lock‑safe version
 *  Last edit: 2025‑07‑28
 *********************    */
#include <Arduino.h>
#include <WiFi.h>

#include <esp_wifi.h>
#include <esp_log.h>
#include "PeripheralFactory.h"


#include <SPI.h>
#include <MFRC522.h>
#include <NFCBuildingRegistry.h>
#include <com-prot.h>
#include <ESPGameAPI.h>
#include <power_tracker.h>

#include "board_config.h"
#include "power_plant_config.h"
#include "wifi_config.h"
#include <atomic>

/* ------------------------------------------------------------------ */
/*                             PIN MAP                                */
/* ------------------------------------------------------------------ */
#define NFC_SCK_PIN       40
#define NFC_MISO_PIN      41
#define NFC_MOSI_PIN      39
#define NFC_RST_PIN       42
#define NFC_SS_PIN         5
#define BUZZER_PIN        35

#define COMPROT_PIN       19            // ← keeps 18
#define CLOCK_PIN         18            // ← moved here, avoids clash
#define LATCH_PIN         16
#define DATA_PIN          17

#define ENCODER4_PIN_A     7
#define ENCODER4_PIN_B     8
#define ENCODER4_PIN_SW    9
#define ENCODER1_PIN_A     4
#define ENCODER1_PIN_B     5
#define ENCODER1_PIN_SW    6
#define ENCODER3_PIN_A    13
#define ENCODER3_PIN_B    14
#define ENCODER3_PIN_SW   15
#define ENCODER2_PIN_A    10
#define ENCODER2_PIN_B    11
#define ENCODER2_PIN_SW   12

/* ------------------------------------------------------------------ */
/*                     ESP‑API / BACKEND SETTINGS                     */
/* ------------------------------------------------------------------ */
#define SERVER_URL   "http://192.168.50.201"
#define API_USERNAME "board1"
#define API_PASSWORD "board123"

/* ------------------------------------------------------------------ */
/*                    GLOBAL STATE & FORWARD DECLS                    */
/* ------------------------------------------------------------------ */
std::atomic<float> coalPowerSetting(50.0f);   // %
std::atomic<float> gasPowerSetting(50.0f);   // %
std::atomic<float> coalCoefficient(1.0f);
std::atomic<float> gasCoefficient(1.0f);
std::atomic<float> coalPowerPercentage(0.5f); // 50%
std::atomic<float> gasPowerPercentage(0.5f);  // 50%
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
Encoder         *encoder1  = nullptr, *encoder2  = nullptr,
                *encoder3  = nullptr, *encoder4  = nullptr;

/* ---------------- Game API instance ------------------------------ */
ESPGameAPI espApi(SERVER_URL, BOARD_NAME, BOARD_GENERIC);

/* ------------------------------------------------------------------ */
/*                     GAME / POWER‑PLANT CALLBACKS                   */
/* ------------------------------------------------------------------ */
float getProductionValue()
{
    return gasPowerSetting + coalPowerSetting;
}

float getConsumptionValue() { return 0.0f; }

std::vector<ConnectedPowerPlant> getConnectedPowerPlants()
{
    std::vector<ConnectedPowerPlant> plants;
    plants.push_back({COAL_PLANT_ID,
       coalPowerSetting});
    plants.push_back({GAS_PLANT_ID,gasPowerSetting});
    return plants;
}

std::vector<ConnectedConsumer> getConnectedConsumers() { return {}; }



void updateCoefficientsFromGame()
{
    for (const auto &c : espApi.getProductionCoefficients()) {
        if (c.source_id == COAL_PLANT_ID) coalCoefficient = c.coefficient;
        else if (c.source_id == GAS_PLANT_ID) gasCoefficient  = c.coefficient;
    }
}




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
    encoder1 = factory.createEncoder(ENCODER1_PIN_A, ENCODER1_PIN_B,
                                     ENCODER1_PIN_SW, 0, 1000, 1);
Serial.println("[Peripherals] Encoder 1 created");
    encoder2 = factory.createEncoder(ENCODER2_PIN_A, ENCODER2_PIN_B,
                                     ENCODER2_PIN_SW, 0, 1000, 1);
    encoder3 = factory.createEncoder(ENCODER3_PIN_A, ENCODER3_PIN_B,
                                     ENCODER3_PIN_SW, 0, 255, 1);
    encoder4 = factory.createEncoder(ENCODER4_PIN_A, ENCODER4_PIN_B,
                                     ENCODER4_PIN_SW, 0, 255, 1);
Serial.println("[Peripherals] Encoders 2, 3, and 4 created");
    encoder1->setValue(50);
    encoder2->setValue(50);
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

    display1->displayNumber(8878.0,1); // display seconds since last update
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

}
TaskHandle_t ioTaskHandle = nullptr;
void ioTask(void*){
  for (;;){


    display1->displayNumber(coalPowerSetting.load());
    display2->displayNumber(gasPowerSetting.load());

    bargraph1->setValue(static_cast<uint8_t>(coalPowerPercentage.load() * 10));
    bargraph2->setValue(static_cast<uint8_t>(gasPowerPercentage.load() * 10));

    if (espApi.isGameActive()) {
        display5->displayNumber(coalCoefficient.load() * 100);
        display6->displayNumber(gasCoefficient.load() * 100);
        bargraph5->setValue(static_cast<uint8_t>(coalCoefficient.load() * 10));
        bargraph6->setValue(static_cast<uint8_t>(gasCoefficient.load() * 10));
    } else {
        display5->displayNumber(0L);
        display6->displayNumber(0L);
        bargraph5->setValue(0);
        bargraph6->setValue(0);
    }
    factory.update();          // all the SPI/GPIO work
    vTaskDelay(1);             // 1 ms = 1 kHz; tune as you like
  }
}
void setup()
{
    Serial.begin(115200);
    Serial.println("\nMaster Board ESP32‑S3 booting…");

    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW);

    /* ---------- Wi‑Fi & backend ---------- */
    if (connectToWiFi()) {
        Serial.println("[ESP-API] Initializing…");
        espApi.setProductionCallback (getProductionValue);
        Serial.println("[ESP-API] Registering callbacks…");
        espApi.setConsumptionCallback(getConsumptionValue);
        espApi.setPowerPlantsCallback(getConnectedPowerPlants);
        espApi.setConsumersCallback  (getConnectedConsumers);

        espApi.setUpdateInterval(500);
        espApi.setPollInterval  (2000);
        Serial.println("[ESP-API] Setting update intervals…");

        if (espApi.login(API_USERNAME, API_PASSWORD) &&
            espApi.registerBoard())
        {
            espApi.printStatus();
        } else {
            Serial.println("[ESP‑API] Login or registration failed");
        }
    }
    Serial.println("[ESP-API] Setup done ✓");
    initPeripherals();

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

void updateGame()
{

    coalPowerPercentage = encoder1->getValue()  / 1000.0f;
    gasPowerPercentage  = encoder2->getValue()  / 1000.0f;
    coalPowerSetting = (COAL_MIN_PRODUCTION_WATTS +
                        (coalPowerPercentage) *
                        (COAL_MAX_PRODUCTION_WATTS - COAL_MIN_PRODUCTION_WATTS)) * coalCoefficient;
    gasPowerSetting  = (GAS_MIN_PRODUCTION_WATTS  +
                        (gasPowerPercentage)  *
                        (GAS_MAX_PRODUCTION_WATTS  - GAS_MIN_PRODUCTION_WATTS)) * gasCoefficient;
    //Serial.printf("Coal Power Value: %d, Gas Power Value: %d\n", encoder1->getValue() , encoder2->getValue() );
    const uint8_t value3 = encoder3->getValue();
    const uint8_t value4 = encoder4->getValue();
}

void loop()
{/*

    if (millis() - lastUpdateTime >= 1000) {
        lastUpdateTime = millis();
        
        display1->displayNumber(8878.0,1); // display seconds since last update
        display2->displayNumber(8878.0,1);
        display3->displayNumber(8878.0,1);
        display4->displayNumber(8878.0,1);
        display5->displayNumber(8878.0,1);
        bargraph1->setValue(4);
        bargraph2->setValue(5);
        bargraph3->setValue(4);
        bargraph4->setValue(5);
        bargraph5->setValue(4);
        Serial.printf("[Peripherals] Updated displays and bargraphs at %lu ms\n", millis());

    }*/


    if (WiFi.status() == WL_CONNECTED && espApi.update())
        updateCoefficientsFromGame();

    long now = millis();
    updateGame(); // update displays, bargraphs, encoders, etc.
    long elapsed = millis() - now;


    if (millis() - lastDebugTime >= POWER_PLANT_DEBUG_INTERVAL) {
        Serial.printf("[PLANTS] Debug: %lu ms slong\n", elapsed);
        lastDebugTime = millis();
        Serial.printf("[PLANTS] Coal %.0f%% → %.1fW (c=%.2f) | "
                      "Gas %.0f%% → %.1fW (c=%.2f) | Game %s\n",
                      coalPowerSetting.load(), coalPowerSetting.load(), coalCoefficient.load(),
                      gasPowerSetting.load(),  gasPowerSetting.load(),  gasCoefficient.load(),
                      espApi.isGameActive() ? "ON" : "OFF");
        // check how much heap is free
        Serial.printf("[PLANTS] Free heap: %lu bytes\n", ESP.getFreeHeap());
    }
/*
    if (Serial.available()) {
        String cmd = Serial.readStringUntil('\n');
        cmd.trim();
        if (cmd == "scan")          WiFi.scanNetworks(true, true);
        else if (cmd == "status")   Serial.printf("Wi‑Fi %s\n",
                                    WiFi.status()==WL_CONNECTED?"OK":"DOWN");
        else if (cmd == "reconnect")
        {
            WiFi.disconnect();
            connectToWiFi();
        }
    }*/


}
