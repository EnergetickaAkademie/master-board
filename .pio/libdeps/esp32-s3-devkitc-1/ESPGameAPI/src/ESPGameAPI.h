#ifndef ESP_GAME_API_H
#define ESP_GAME_API_H

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// Protocol version
#define PROTOCOL_VERSION 0x01

// Special values for power data
#define POWER_NULL_VALUE 0x7FFFFFFF

// Data flags for power submission
#define FLAG_GENERATION_PRESENT 0x01
#define FLAG_CONSUMPTION_PRESENT 0x02

// Board types
enum BoardType {
    BOARD_SOLAR,
    BOARD_WIND,
    BOARD_BATTERY,
    BOARD_GENERIC
};

// Binary protocol structures
struct __attribute__((packed)) RegistrationRequest {
    uint8_t version = PROTOCOL_VERSION;
    uint32_t board_id;
    char board_name[32];
    char board_type[16];
};

struct __attribute__((packed)) RegistrationResponse {
    uint8_t version;
    uint8_t success;
    uint8_t message_length;
    char message[64];
};

struct __attribute__((packed)) PowerDataRequest {
    uint8_t version = PROTOCOL_VERSION;
    uint32_t board_id;
    uint64_t timestamp;
    int32_t generation;
    int32_t consumption;
    uint8_t flags;
};

struct __attribute__((packed)) PollResponse {
    uint8_t version;
    uint64_t timestamp;
    uint16_t round;
    uint32_t score;
    int32_t generation;
    int32_t consumption;
    uint8_t flags;
};

class ESPGameAPI {
private:
    String baseUrl;
    String username;
    String password;
    String token;
    uint32_t boardId;
    String boardName;
    BoardType boardType;
    bool isLoggedIn;
    bool isRegistered;
    uint16_t lastRound;
    
    HTTPClient http;
    
    // Network byte order conversion functions (renamed to avoid conflicts)
    uint32_t hostToNetworkLong(uint32_t hostlong);
    uint64_t hostToNetworkLongLong(uint64_t hostlonglong);
    uint32_t networkToHostLong(uint32_t netlong);
    uint64_t networkToHostLongLong(uint64_t netlonglong);
    uint16_t networkToHostShort(uint16_t netshort);
    
    // Helper functions
    String boardTypeToString(BoardType type) const;
    uint64_t getCurrentTimestamp();
    bool makeHttpRequest(const String& endpoint, const uint8_t* data, size_t dataSize, uint8_t* response, size_t& responseSize);
    bool makeHttpGetRequest(const String& endpoint, uint8_t* response, size_t& responseSize);
    
public:
    // Constructor
    ESPGameAPI(const String& serverUrl, uint32_t id, const String& name, BoardType type);
    
    // Authentication
    bool login(const String& user, const String& pass);
    
    // Board management
    bool registerBoard();
    bool isGameRegistered() const { return isRegistered; }
    
    // Game operations
    bool submitPowerData(float generation, float consumption);
    bool submitPowerData(float generation, float consumption, uint8_t flags);
    bool pollStatus(uint64_t& timestamp, uint16_t& round, uint32_t& score, 
                   float& generation, float& consumption, uint8_t& statusFlags);
    
    // Utility functions
    bool isGameActive(uint8_t statusFlags) const;
    bool isExpectingData(uint8_t statusFlags) const;
    bool isRoundTypeDay(uint8_t statusFlags) const;
    uint16_t getLastRound() const { return lastRound; }
    
    // Network status
    bool isConnected() const { return WiFi.status() == WL_CONNECTED && isLoggedIn; }
    
    // Debug functions
    void printStatus() const;
};

#endif // ESP_GAME_API_H
