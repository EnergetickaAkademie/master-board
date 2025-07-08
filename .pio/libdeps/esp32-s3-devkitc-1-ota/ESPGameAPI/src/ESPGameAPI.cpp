#include "ESPGameAPI.h"

// Constructor
ESPGameAPI::ESPGameAPI(const String& serverUrl, uint32_t id, const String& name, BoardType type) 
    : baseUrl(serverUrl), boardId(id), boardName(name), boardType(type), 
      isLoggedIn(false), isRegistered(false), lastRound(0) {
}

// Network byte order conversion functions (renamed to avoid conflicts)
uint32_t ESPGameAPI::hostToNetworkLong(uint32_t hostlong) {
    return ((hostlong & 0xFF000000) >> 24) |
           ((hostlong & 0x00FF0000) >> 8)  |
           ((hostlong & 0x0000FF00) << 8)  |
           ((hostlong & 0x000000FF) << 24);
}

uint64_t ESPGameAPI::hostToNetworkLongLong(uint64_t hostlonglong) {
    return ((uint64_t)hostToNetworkLong((uint32_t)hostlonglong) << 32) | hostToNetworkLong((uint32_t)(hostlonglong >> 32));
}

uint32_t ESPGameAPI::networkToHostLong(uint32_t netlong) {
    return hostToNetworkLong(netlong); // Same operation for conversion back
}

uint64_t ESPGameAPI::networkToHostLongLong(uint64_t netlonglong) {
    return hostToNetworkLongLong(netlonglong); // Same operation for conversion back
}

uint16_t ESPGameAPI::networkToHostShort(uint16_t netshort) {
    return ((netshort & 0xFF00) >> 8) | ((netshort & 0x00FF) << 8);
}

// Helper function to convert board type to string
String ESPGameAPI::boardTypeToString(BoardType type) const {
    switch (type) {
        case BOARD_SOLAR: return "solar";
        case BOARD_WIND: return "wind";
        case BOARD_BATTERY: return "battery";
        case BOARD_GENERIC: 
        default: return "generic";
    }
}

// Get current Unix timestamp
uint64_t ESPGameAPI::getCurrentTimestamp() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec;
}

// Authentication
bool ESPGameAPI::login(const String& user, const String& pass) {
    username = user;
    password = pass;
    
    http.begin(baseUrl + "/coreapi/login");
    http.addHeader("Content-Type", "application/json");
    
    JsonDocument doc;
    doc["username"] = username;
    doc["password"] = password;
    
    String jsonString;
    serializeJson(doc, jsonString);
    
    int httpCode = http.POST(jsonString);
    
    if (httpCode == 200) {
        String response = http.getString();
        JsonDocument responseDoc;
        
        if (deserializeJson(responseDoc, response) == DeserializationError::Ok) {
            if (responseDoc["token"].is<String>()) {
                token = responseDoc["token"].as<String>();
                isLoggedIn = true;
                Serial.println("🔐 Successfully logged in");
                http.end();
                return true;
            }
        }
    }
    
    Serial.println("❌ Login failed: " + String(httpCode));
    http.end();
    return false;
}

// Make HTTP request with binary data
bool ESPGameAPI::makeHttpRequest(const String& endpoint, const uint8_t* data, size_t dataSize, uint8_t* response, size_t& responseSize) {
    if (!isLoggedIn) {
        Serial.println("❌ Not logged in");
        return false;
    }
    
    http.begin(baseUrl + endpoint);
    http.addHeader("Authorization", "Bearer " + token);
    http.addHeader("Content-Type", "application/octet-stream");
    
    int httpCode = http.POST((uint8_t*)data, dataSize);
    
    if (httpCode == 200) {
        WiFiClient* stream = http.getStreamPtr();
        responseSize = stream->available();
        if (responseSize > 0 && response) {
            stream->readBytes(response, responseSize);
        }
        http.end();
        return true;
    } else {
        Serial.println("❌ HTTP request failed: " + String(httpCode));
        String errorResponse = http.getString();
        Serial.println("Error: " + errorResponse);
        http.end();
        return false;
    }
}

// Make HTTP GET request
bool ESPGameAPI::makeHttpGetRequest(const String& endpoint, uint8_t* response, size_t& responseSize) {
    if (!isLoggedIn) {
        Serial.println("❌ Not logged in");
        return false;
    }
    
    http.begin(baseUrl + endpoint);
    http.addHeader("Authorization", "Bearer " + token);
    
    int httpCode = http.GET();
    
    if (httpCode == 200) {
        WiFiClient* stream = http.getStreamPtr();
        responseSize = stream->available();
        if (responseSize > 0 && response) {
            stream->readBytes(response, responseSize);
        }
        http.end();
        return true;
    } else {
        Serial.println("❌ HTTP GET failed: " + String(httpCode));
        http.end();
        return false;
    }
}

// Register board with the game
bool ESPGameAPI::registerBoard() {
    if (!isLoggedIn) {
        Serial.println("❌ Cannot register: not logged in");
        return false;
    }
    
    RegistrationRequest req;
    req.board_id = hostToNetworkLong(boardId);
    strncpy(req.board_name, boardName.c_str(), 31);
    req.board_name[31] = '\0';
    
    String typeStr = boardTypeToString(boardType);
    strncpy(req.board_type, typeStr.c_str(), 15);
    req.board_type[15] = '\0';
    
    uint8_t responseBuffer[67];
    size_t responseSize = 0;
    
    bool success = makeHttpRequest("/coreapi/register_binary", (uint8_t*)&req, sizeof(req), responseBuffer, responseSize);
    
    if (success && responseSize >= 3) {
        RegistrationResponse* resp = (RegistrationResponse*)responseBuffer;
        if (resp->version == PROTOCOL_VERSION && resp->success == 0x01) {
            isRegistered = true;
            Serial.println("📋 Successfully registered board: " + boardName);
            return true;
        } else {
            // Print error message if available
            if (resp->message_length > 0 && responseSize >= (3 + resp->message_length)) {
                String errorMsg = String(resp->message).substring(0, resp->message_length);
                Serial.println("❌ Registration failed: " + errorMsg);
            } else {
                Serial.println("❌ Registration failed: unknown error");
            }
        }
    } else {
        Serial.println("❌ Registration request failed");
    }
    
    return false;
}

// Submit power data
bool ESPGameAPI::submitPowerData(float generation, float consumption) {
    uint8_t flags = 0;
    if (generation >= 0) flags |= FLAG_GENERATION_PRESENT;
    if (consumption >= 0) flags |= FLAG_CONSUMPTION_PRESENT;
    return submitPowerData(generation, consumption, flags);
}

bool ESPGameAPI::submitPowerData(float generation, float consumption, uint8_t flags) {
    if (!isRegistered) {
        Serial.println("❌ Cannot submit data: board not registered");
        return false;
    }
    
    PowerDataRequest req;
    req.board_id = hostToNetworkLong(boardId);
    req.timestamp = hostToNetworkLongLong(getCurrentTimestamp());
    
    // Convert watts to centi-watts (watts * 100) and handle null values
    if (flags & FLAG_GENERATION_PRESENT && generation >= 0) {
        req.generation = hostToNetworkLong((int32_t)(generation * 100));
    } else {
        req.generation = hostToNetworkLong(POWER_NULL_VALUE);
    }
    
    if (flags & FLAG_CONSUMPTION_PRESENT && consumption >= 0) {
        req.consumption = hostToNetworkLong((int32_t)(consumption * 100));
    } else {
        req.consumption = hostToNetworkLong(POWER_NULL_VALUE);
    }
    
    req.flags = flags;
    
    uint8_t responseBuffer[100];
    size_t responseSize = 0;
    
    bool success = makeHttpRequest("/coreapi/power_data_binary", (uint8_t*)&req, sizeof(req), responseBuffer, responseSize);
    
    if (success) {
        Serial.println("⚡ Power data submitted - Gen: " + String(generation, 1) + "W, Cons: " + String(consumption, 1) + "W");
        return true;
    }
    
    return false;
}

// Poll board status
bool ESPGameAPI::pollStatus(uint64_t& timestamp, uint16_t& round, uint32_t& score, 
                           float& generation, float& consumption, uint8_t& statusFlags) {
    if (!isRegistered) {
        Serial.println("❌ Cannot poll: board not registered");
        return false;
    }
    
    String endpoint = "/coreapi/poll_binary/" + String(boardId);
    uint8_t responseBuffer[24];
    size_t responseSize = 0;
    
    bool success = makeHttpGetRequest(endpoint, responseBuffer, responseSize);
    
    if (success && responseSize >= 24) {
        PollResponse* resp = (PollResponse*)responseBuffer;
        
        if (resp->version == PROTOCOL_VERSION) {
            timestamp = networkToHostLongLong(resp->timestamp);
            round = networkToHostShort(resp->round);
            score = networkToHostLong(resp->score);
            statusFlags = resp->flags;
            
            // Convert power values from centi-watts to watts
            int32_t genRaw = networkToHostLong(resp->generation);
            int32_t consRaw = networkToHostLong(resp->consumption);
            
            generation = (genRaw == POWER_NULL_VALUE) ? -1.0 : (float)genRaw / 100.0;
            consumption = (consRaw == POWER_NULL_VALUE) ? -1.0 : (float)consRaw / 100.0;
            
            // Update last round if this is a new round
            if (round > lastRound) {
                lastRound = round;
                String roundType = isRoundTypeDay(statusFlags) ? "day" : "night";
                Serial.println("🔄 New round detected: " + String(round) + " (" + roundType + ")");
            }
            
            return true;
        }
    }
    
    return false;
}

// Status flag helpers
bool ESPGameAPI::isGameActive(uint8_t statusFlags) const {
    return (statusFlags & 0x02) != 0; // Bit 1
}

bool ESPGameAPI::isExpectingData(uint8_t statusFlags) const {
    return (statusFlags & 0x04) != 0; // Bit 2
}

bool ESPGameAPI::isRoundTypeDay(uint8_t statusFlags) const {
    return (statusFlags & 0x01) != 0; // Bit 0: 0=night, 1=day
}

// Debug function
void ESPGameAPI::printStatus() const {
    Serial.println("=== ESP Game API Status ===");
    Serial.println("Board ID: " + String(boardId));
    Serial.println("Board Name: " + boardName);
    Serial.println("Board Type: " + boardTypeToString(boardType));
    Serial.println("Logged In: " + String(isLoggedIn ? "Yes" : "No"));
    Serial.println("Registered: " + String(isRegistered ? "Yes" : "No"));
    Serial.println("Last Round: " + String(lastRound));
    Serial.println("WiFi Connected: " + String(WiFi.status() == WL_CONNECTED ? "Yes" : "No"));
    Serial.println("===========================");
}
