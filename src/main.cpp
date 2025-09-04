/***********************************************************************
 *  Master Board ESP32-S3 – spin-lock-safe version
 *  Last edit: 2025-07-28 (UART changed to 2B: [slaveType, cmd4])
 ******************************************************************************/
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <WiFiClient.h>
#include <vector>
#include <algorithm>
#include "PeripheralFactory.h"
#include <SPI.h>
#include <MFRC522.h>
#include <NFCBuildingRegistry.h>

#include "board_config.h"
#include "power_plant_config.h"
#include "GameManager.h"
#include "robust_uart.h"

/* ------------------------------------------------------------------ */
/*                             PIN MAP                                */
/* ------------------------------------------------------------------ */
#define BUZZER_PIN 35

#define CLOCK_PIN 18 // Shift register clock
#define LATCH_PIN 16 // Shift register latch
#define DATA_PIN 17  // Shift register data

// Original logical encoder numbering changed to match new physical layout:
// User mapping provided:
//   logical 1 -> old physical encoder 3 (pins 13,14)
//   logical 2 -> old physical encoder 1 (pins 4,5)
//   logical 3 -> old physical encoder 4 (pins 7,8)
//   logical 4 -> old physical encoder 2 (pins 10,11)
//   logical 5 (new) -> pins 15,6 for most boards, pins 12,9 for masterboard-005
hw_timer_t *displayTimer = nullptr;
#define ENCODER1_PIN_A 14
#define ENCODER1_PIN_B 13
#define ENCODER2_PIN_A 5
#define ENCODER2_PIN_B 4
#define ENCODER3_PIN_A 8
#define ENCODER3_PIN_B 7
#define ENCODER4_PIN_A 11
#define ENCODER4_PIN_B 10
// Encoder 5 pins - different for masterboard-005
#define ENCODER5_PIN_A 6
#define ENCODER5_PIN_B 15

// We disable encoder button functionality by using sentinel value 255 for all
#define ENCODER_NO_BUTTON 255

/* Unused pins for future expansion */
#define NFC_SCK_PIN 40
#define NFC_MISO_PIN 41
#define NFC_MOSI_PIN 39
#define NFC_RST_PIN 42
#define NFC_SS_PIN 21
#define COMPROT_PIN 19

/* UART Communication with Retranslation Station */
// For BOARD_ID 5 hardware the intended RX pin was miswired: design expected GPIO19 but
// working connection is on GPIO48. Automatically switch and warn so firmware still works.
#if defined(BOARD_ID) && (BOARD_ID == 5)
#define UART_RX_PIN 48
#define UART_RX_PIN_STR "48"
#warning "Compiling board 5: assuming bad pin 19 switching to 48"
#else
#define UART_RX_PIN 19
#define UART_RX_PIN_STR "19"
#endif
#define UART_TX_PIN 47

/* ------------------------------------------------------------------ */
/*                     SERVER IP DISCOVERY                            */
/* ------------------------------------------------------------------ */
// MACs to search for
static const uint8_t SERVER_MAC_1[6] = {0x74, 0x3A, 0xF4, 0x10, 0xD5, 0x7E};
static const uint8_t SERVER_MAC_2[6] = {0x00, 0xD8, 0x61, 0x31, 0x29, 0xC5};

WiFiUDP udp;

// Find HTTP server by connecting and then checking if we can find the expected MAC
IPAddress findHttpServer()
{
    IPAddress myIp = WiFi.localIP();
    IPAddress mask = WiFi.subnetMask();

    WiFiClient client;

    Serial.printf("[Server Discovery] Scanning network %s with mask %s\n",
                  myIp.toString().c_str(), mask.toString().c_str());
    Serial.printf("[Server Discovery] Looking for HTTP servers with known MACs:\n");
    Serial.printf("   MAC 1: %02X:%02X:%02X:%02X:%02X:%02X\n",
                  SERVER_MAC_1[0], SERVER_MAC_1[1], SERVER_MAC_1[2],
                  SERVER_MAC_1[3], SERVER_MAC_1[4], SERVER_MAC_1[5]);
    Serial.printf("   MAC 2: %02X:%02X:%02X:%02X:%02X:%02X\n",
                  SERVER_MAC_2[0], SERVER_MAC_2[1], SERVER_MAC_2[2],
                  SERVER_MAC_2[3], SERVER_MAC_2[4], SERVER_MAC_2[5]);

    // Calculate subnet base address
    uint8_t subnet[4] = {(uint8_t)(myIp[0] & mask[0]),
                         (uint8_t)(myIp[1] & mask[1]),
                         (uint8_t)(myIp[2] & mask[2]),
                         0};

    Serial.printf("[Server Discovery] Subnet base: %d.%d.%d.0\n", subnet[0], subnet[1], subnet[2]);

    uint32_t hostsScanned = 0;

    // First try common server IPs
    uint8_t commonHosts[] = {2, 5, 6, 210, 11, 100, 105, 105, 106, 106, 101, 106, 200, 201, 4, 5, 7, 8, 9, 10, 12, 13, 14, 105, 15, 3, 106, 106, 106};
    for (uint8_t host : commonHosts)
    {
        IPAddress target(subnet[0], subnet[1], subnet[2], host);
        if (target == myIp)
            continue;

        hostsScanned++;
        Serial.printf("[Server Discovery] Testing common host %s...", target.toString().c_str());

        if (client.connect(target, 80, 600))
        {                  // 150 ms timeout
            client.stop(); // handshake OK
            Serial.println(" ✓ HTTP server found!");
            return target;
        }
        Serial.println(" ✗");
    }

    // If no common hosts work, try full scan
    Serial.println("[Server Discovery] No common hosts found, trying full scan...");

    for (uint16_t host = 2; host < 255; ++host)
    {
        // Skip our own address and already-tested common hosts
        if (host == myIp[3])
            continue;

        bool alreadyTested = false;
        for (uint8_t commonHost : commonHosts)
        {
            if (host == commonHost)
            {
                alreadyTested = true;
                break;
            }
        }
        if (alreadyTested)
            continue;

        IPAddress target(subnet[0], subnet[1], subnet[2], host);

        hostsScanned++;
        if (hostsScanned % 50 == 0)
        {
            Serial.printf("[Server Discovery] Scanned %lu hosts...\n", hostsScanned);
        }
        // testing mac address
        if (client.connect(target, 80, 150) && (client.localIP() == target))
        {                  // 150 ms timeout
            client.stop(); // handshake OK
            Serial.printf("✅ HTTP server found at %s\n", target.toString().c_str());
            return target;
        }
        else
        {
            Serial.printf("✗ %s and mac %02X:%02X:%02X:%02X:%02X:%02X\n",
                          target.toString().c_str(),
                          client.localIP()[0], client.localIP()[1],
                          client.localIP()[2], client.localIP()[3],
                          client.localIP()[4], client.localIP()[5]);
        }
    }

    Serial.printf("[Server Discovery] Scanned %lu hosts total, no HTTP server found\n", hostsScanned);
    return IPAddress(); // 0.0.0.0 = not found
}

// UDP broadcast discovery (fallback method)
IPAddress findServerByBroadcast()
{
    const char *DISCOVERY_MSG = "DISCOVER-POWERPLANT";
    const int DISCOVERY_PORT = 80;
    const int LISTEN_PORT = 80;

    Serial.println("[Server Discovery] Trying UDP broadcast discovery...");

    if (!udp.begin(LISTEN_PORT))
    {
        Serial.println("[Server Discovery] Failed to start UDP");
        return INADDR_NONE;
    }

    // Send broadcast
    IPAddress broadcastIP = WiFi.localIP();
    broadcastIP[3] = 255; // Assume /24 network

    udp.beginPacket(broadcastIP, DISCOVERY_PORT);
    udp.write((const uint8_t *)DISCOVERY_MSG, strlen(DISCOVERY_MSG));
    udp.endPacket();

    Serial.printf("[Server Discovery] Sent broadcast to %s:%d\n", broadcastIP.toString().c_str(), DISCOVERY_PORT);

    // Wait for response
    unsigned long startTime = millis();
    while (millis() - startTime < 3000)
    { // 3 second timeout
        int packetSize = udp.parsePacket();
        if (packetSize)
        {
            IPAddress serverIP = udp.remoteIP();
            udp.stop();
            Serial.printf("[Server Discovery] Server responded from %s\n", ("http://" + serverIP.toString()).c_str());
            return serverIP;
        }
        delay(100);
    }

    udp.stop();
    Serial.println("[Server Discovery] No UDP response received");
    return INADDR_NONE;
}

/* ------------------------------------------------------------------ */
/*                    UART COMMUNICATION PROTOCOL                     */
/* ------------------------------------------------------------------ */
/*
   New UART master<->retranslation protocol:
   - TX from this ESP32-S3: exactly 2 bytes: [slaveType, cmd4]
     * cmd4 is 4-bit command (0x01=ON, 0x02=OFF, ...)

   - RX from retranslation: packed pairs [slaveType, amount] repeating
     (parsed in parseSlaveInfo())
*/

// 4-bit command constants (optional helpers)
static const uint8_t CMD_ON = 0x01;
static const uint8_t CMD_OFF = 0x02;

// UART communication variables
HardwareSerial uartComm(1); // Use UART1
unsigned long lastUartReceive = 0;
RobustUart robustUart; // Robust UART protocol handler

// Storage for connected slaves from retranslation station
std::vector<UartSlaveInfo> connectedSlaves; // struct defined in GameManager.h

// Function prototypes
void processUartData();
void uartWriteFunction(const uint8_t *data, size_t len);

/* ------------------------------------------------------------------ */
/*                    GLOBAL STATE & FORWARD DECLS                    */
/* ------------------------------------------------------------------ */
unsigned long lastUpdateTime = 0;
unsigned long lastDebugTime = 0;

/* ---------------- Hardware helper singletons --------------------- */
PeripheralFactory factory;
ShiftRegisterChain *shiftChain = nullptr;

Bargraph *bargraph1 = nullptr, *bargraph2 = nullptr,
         *bargraph3 = nullptr, *bargraph4 = nullptr,
         *bargraph5 = nullptr, *bargraph6 = nullptr,
         *bargraph7 = nullptr;
SegmentDisplay *display1 = nullptr, *display2 = nullptr,
               *display3 = nullptr, *display4 = nullptr,
               *display5 = nullptr, *display6 = nullptr,
               *display7 = nullptr;
// Double displays for totals
SegmentDisplay *productionTotalDisplay = nullptr;
SegmentDisplay *consumptionTotalDisplay = nullptr;
Encoder *encoder1 = nullptr, *encoder2 = nullptr,
        *encoder3 = nullptr, *encoder4 = nullptr,
        *encoder5 = nullptr; // Newly added encoder

MFRC522 mfrc522(NFC_SS_PIN, NFC_RST_PIN);
NFCBuildingRegistry nfcRegistry(&mfrc522);

/* ------------------------------------------------------------------ */
/*                        WIFI CONNECTION                             */
/* ------------------------------------------------------------------ */

void printWiFiStatusCode(wl_status_t status)
{
    switch (status)
    {
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
    // const char *ssid = "PotkaniNora";
    // const char *password = "PrimaryPapikTarget";
    const char *ssid = "Bagr";
    const char *password = "bagroviste";

    const int max_connection_attempts = 10;

    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(100);
    Serial.println("\n🔄 Connecting to WiFi...");
    Serial.print("SSID: ");
    Serial.println(ssid);

    WiFi.begin(ssid, password);

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < max_connection_attempts)
    {
        delay(1000);
        Serial.print(".");
        attempts++;

        // Print connection status
        switch (WiFi.status())
        {
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

    if (WiFi.status() == WL_CONNECTED)
    {
        Serial.println("✅ WiFi connected successfully!");
        Serial.printf("📶 Connected to: %s\n", ssid);
        Serial.printf("🌐 IP Address: %s\n", WiFi.localIP().toString().c_str());
        Serial.printf("🔗 Gateway: %s\n", WiFi.gatewayIP().toString().c_str());
        Serial.printf("🎭 Subnet Mask: %s\n", WiFi.subnetMask().toString().c_str());
        return true;
    }
    else
    {
        Serial.println("❌ WiFi connection failed!");
        Serial.print("Final status: ");
        printWiFiStatusCode(WiFi.status());
        Serial.println();
        return false;
    }
}

/* ------------------------------------------------------------------ */
/*                    UART COMMUNICATION FUNCTIONS                    */
/* ------------------------------------------------------------------ */

void initUartCommunication()
{
    uartComm.begin(9600, SERIAL_8N1, UART_RX_PIN, UART_TX_PIN);
#if defined(BOARD_ID) && (BOARD_ID == 5)
    Serial.println(F("[UART][WARN] BOARD_ID=5: assuming bad pin 19, switching RX to 48"));
#endif
    Serial.print(F("[UART] Robust communication initialized on pins RX="));
    Serial.print(UART_RX_PIN_STR);
    Serial.println(F(", TX=47, baud=9600"));
}

// Write function for robust UART
void uartWriteFunction(const uint8_t *data, size_t len)
{
    uartComm.write(data, len);
}

void processUartData()
{
    // Process incoming bytes through robust UART protocol
    while (uartComm.available())
    {
        uint8_t byte = uartComm.read();

        if (robustUart.processByte(byte))
        {
            // Complete frame received
            const uint8_t *payload = robustUart.getPayload();
            uint8_t length = robustUart.getPayloadLength();

            Serial.printf("[RobustUART] Received frame: %u bytes\n", length);
            RobustUartHelpers::parseSlaveInfo(payload, length, connectedSlaves);

            lastUartReceive = millis();
            robustUart.resetRx(); // Ready for next frame
        }
    }

    // Print stats periodically for debugging
    static unsigned long lastStats = 0;
    if (millis() - lastStats >= 10000)
    { // Every 10 seconds
        robustUart.printStats();
        lastStats = millis();
    }
}

/* ------------------------------------------------------------------ */
/*                               SETUP                                */
/* ------------------------------------------------------------------ */

void initPeripherals()
{
    /* ---------- Peripherals ---------- */
    // Create encoders with remapped physical positions; button disabled (255)
    encoder1 = factory.createEncoder(ENCODER1_PIN_B, ENCODER1_PIN_A,
                                     ENCODER_NO_BUTTON, 0, 1000, 1);
    Serial.println("[Peripherals] Encoder 1 (phys old 3) created");
    encoder2 = factory.createEncoder(ENCODER2_PIN_B, ENCODER2_PIN_A,
                                     ENCODER_NO_BUTTON, 0, 1000, 1);
    encoder3 = factory.createEncoder(ENCODER3_PIN_B, ENCODER3_PIN_A,
                                     ENCODER_NO_BUTTON, 0, 1000, 1);
    encoder4 = factory.createEncoder(ENCODER4_PIN_B, ENCODER4_PIN_A,
                                     ENCODER_NO_BUTTON, 0, 1000, 1);
    encoder5 = factory.createEncoder(ENCODER5_PIN_B, ENCODER5_PIN_A,
                                     ENCODER_NO_BUTTON, 0, 1000, 1);
    Serial.println("[Peripherals] Encoder 2 (phys old 1) created");
    Serial.println("[Peripherals] Encoder 3 (phys old 4) created");
    Serial.println("[Peripherals] Encoder 4 (phys old 2) created");
    Serial.println("[Peripherals] Encoder 5 (new) created");
    encoder1->setValue(500); // 50%
    encoder2->setValue(500); // 50%
    encoder3->setValue(500); // 50%
    encoder4->setValue(500); // 50%
    encoder5->setValue(500); // 50%
    Serial.println("[Peripherals] Encoders initialized");

    shiftChain = factory.createShiftRegisterChain(LATCH_PIN, DATA_PIN, CLOCK_PIN);

    // Create double displays for production and consumption totals (8 digits each)
    productionTotalDisplay = factory.createSegmentDisplay(shiftChain, 8);
    consumptionTotalDisplay = factory.createSegmentDisplay(shiftChain, 8);
    bargraph7 = factory.createBargraph(shiftChain, 10);
    display7 = factory.createSegmentDisplay(shiftChain, 4);
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

    display1->displayNumber(8878.0, 1); // display test pattern
    display2->displayNumber(8878.0, 1);
    display3->displayNumber(8878.0, 1);
    display4->displayNumber(8878.0, 1);
    display5->displayNumber(8878.0, 1);
    display6->displayNumber(8878.0, 1);
    display7->displayNumber(8878.0, 1);
    // Initialize double displays with test pattern (8 digits: 88888878)
    productionTotalDisplay->displayNumber(88888878.0, 1);
    consumptionTotalDisplay->displayNumber(88888878.0, 1);
    bargraph1->setValue(10);
    bargraph2->setValue(10);
    bargraph3->setValue(10);
    bargraph4->setValue(10);
    bargraph5->setValue(10);
    bargraph6->setValue(10);
    bargraph7->setValue(10);
    factory.update();

    auto &gameManager = GameManager::getInstance();

    gameManager.registerPowerPlantTypeControl(COAL, encoder1, display1, bargraph1);
    gameManager.registerPowerPlantTypeControl(GAS, encoder2, display2, bargraph2);
    gameManager.registerPowerPlantTypeControl(NUCLEAR, encoder3, display3, bargraph3);
    gameManager.registerPowerPlantTypeControl(BATTERY, encoder4, display4, bargraph4);
    gameManager.registerPowerPlantTypeControl(HYDRO_STORAGE, encoder4, display4, bargraph4); // Share with battery
    gameManager.registerPowerPlantTypeControl(HYDRO, encoder5, display5, bargraph5);
    gameManager.registerPowerPlantTypeControl(WIND, nullptr, display6, bargraph6);
    gameManager.registerPowerPlantTypeControl(PHOTOVOLTAIC, nullptr, display7, bargraph7);
    // Set total displays for production and consumption
    gameManager.setTotalDisplays(productionTotalDisplay, consumptionTotalDisplay);
    Serial.println("[Peripherals] Total displays for production and consumption initialized");
    // Note: updateAttractionStates() moved to main loop to avoid interrupt context issues
}

TaskHandle_t ioTaskHandle = nullptr;

void IRAM_ATTR onDisplayTimer()
{
    factory.update();
}

void initDisplayTimer()
{

    displayTimer = timerBegin(0, 80, true);                     // 1 MHz
    timerAttachInterrupt(displayTimer, &onDisplayTimer, false); // EDGE nepodporováno
    timerAlarmWrite(displayTimer, 1000, true);                  // 1 kHz
    timerAlarmEnable(displayTimer);
}


void setup()
{
    Serial.begin(115200);
    Serial.println("\nMaster Board ESP32-S3 booting…");
    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW);
    initPeripherals();

    if (connectToWiFi()) {
#ifdef PRODUCTION_SERVER_URL
        // Production mode: directly use fixed server URL, no discovery.
        Serial.println("\n🌐 Production mode: using fixed server URL");
        const char *serverUrl = PRODUCTION_SERVER_URL;
        auto &gameManager = GameManager::getInstance();
        gameManager.initEspApi(serverUrl, BOARD_NAME, API_USERNAME, API_PASSWORD);
        Serial.printf("[ESP-API] Initialized with %s\n", serverUrl);
#else
        Serial.println("\n🔍 Discovering server...");
        IPAddress serverIp = findHttpServer();
        if (!serverIp) {
            Serial.println("[Server Discovery] MAC discovery failed, trying UDP broadcast discovery...");
            serverIp = findServerByBroadcast();
        }
        if (serverIp) {
            Serial.printf("🎯 Server discovered at: %s\n", serverIp.toString().c_str());
            Serial.println("[ESP-API] Initializing via GameManager…");
            auto &gameManager = GameManager::getInstance();
            gameManager.initEspApi(("http://" + serverIp.toString()).c_str(), BOARD_NAME, API_USERNAME, API_PASSWORD);
            Serial.println("[ESP-API] Setup done ✓");
        } else {
            Serial.println("⚠️  Server not found on local network");
            Serial.println("[ESP-API] Using fallback server URL from config…");
            auto &gameManager = GameManager::getInstance();
            gameManager.initEspApi("http://192.168.50.201", BOARD_NAME, API_USERNAME, API_PASSWORD);
            Serial.println("[ESP-API] Setup done with fallback URL ✓");
        }
#endif
    } else {
        Serial.println("[ESP-API] Skipped due to WiFi connection failure");
#ifdef PRODUCTION_SERVER_URL
        Serial.println("[SYSTEM] Restarting in 5s to retry WiFi...");
        delay(5000);
        esp_restart();
#endif
    }

    // ---------- UART Communication Initialization ----------
    initUartCommunication();

    // ---------- NFC Testing & Initialization ----------
    Serial.println("\n🔧 Testing NFC hardware...");
    SPI.begin(NFC_SCK_PIN, NFC_MISO_PIN, NFC_MOSI_PIN, NFC_SS_PIN);
    mfrc522.PCD_Init();
    Serial.printf("[NFC] Using pins: SCK=%d, MISO=%d, MOSI=%d, SS=%d, RST=%d\n",
                  NFC_SCK_PIN, NFC_MISO_PIN, NFC_MOSI_PIN, NFC_SS_PIN, NFC_RST_PIN);

    // Initialize MFRC522
    mfrc522.PCD_DumpVersionToSerial();

    auto &gameManager = GameManager::getInstance();
    gameManager.initNfcRegistry(&nfcRegistry);

    Serial.printf("[COM-PROT] Master (via retranslation) UART on RX=%d, TX=%d\n", UART_RX_PIN, UART_TX_PIN);
    Serial.println("Setup done ✓");
#ifdef PRODUCTION_SERVER_URL
    // In production mode: do NOT restart just because the game is not active yet.
    // A 404 or inactive game state simply means we wait until the server starts a game.
    // Restarting is only appropriate if WiFi/server were unreachable (handled earlier).
    if (!GameManager::getInstance().isGameActive()) {
        Serial.println("[ESP-API][INFO] Game not active after init. Waiting for game start… (no restart)");
    }
#endif
    initDisplayTimer();
    //xTaskCreatePinnedToCore(displayTask, "DisplayTask", 2048, NULL, 1, &ioTaskHandle, 1);
}

/* ------------------------------------------------------------------ */
/*                                LOOP                                */
/* ------------------------------------------------------------------ */
unsigned long last_update_time = 0;
void loop()
{
    // Update ESP API if connected
    if (WiFi.status() == WL_CONNECTED)
    {
        GameManager::getInstance().updateEspApi();
    }

    GameManager::getInstance().update(); // update game logic

    // UART Communication processing (receive slave info)
    processUartData();
    
    // Update retranslation station status (handled inside display update too, but keep here for timely state)
    GameManager::getInstance().updateRetranslationStatus();
    if(millis() - last_update_time > 30) {
                GameManager::updateDisplays();
    }

    if (millis() - lastDebugTime >= POWER_PLANT_DEBUG_INTERVAL)
    {
        lastDebugTime = millis();
        static unsigned long lastNfcScan = 0;
        if (millis() - lastNfcScan >= 100)
        { // Scan every 100ms
            bool cardFound = nfcRegistry.scanForCards();
            if (cardFound)
            {
                Serial.println("📱 [NFC] Card detected and processed!");
            }
            lastNfcScan = millis();
        }
        nfcRegistry.printDatabase(); // Print NFC registry info

        GameManager::printDebugInfo();

        // Print coefficient debug info every 10 seconds
        static unsigned long lastCoefficientDebug = 0;
        if (millis() - lastCoefficientDebug >= 10000)
        {
            GameManager::printCoefficientDebugInfo();
            lastCoefficientDebug = millis();
        }
        
        // Print retranslation status
        static bool lastRetranslationStatus = true;
    bool currentStatus = GameManager::getInstance().isRetranslationStationAlive();
        if (lastRetranslationStatus != currentStatus) {
            Serial.printf("[RETRANSLATION] Status changed: %s\n", 
                         currentStatus ? "CONNECTED" : "DISCONNECTED");
            lastRetranslationStatus = currentStatus;
        }
    }
}
