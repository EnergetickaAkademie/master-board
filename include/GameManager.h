#ifndef GAME_MANAGER_H
#define GAME_MANAGER_H

#include <atomic>
#include <vector>
#include <array>
#include <ESPGameAPI.h>
#include <NFCBuildingRegistry.h>
#include "power_plant_config.h"
#include "PeripheralFactory.h"

// Forward declarations for peripheral classes
class Encoder;
class SegmentDisplay;
class Bargraph;

// Power plant data structure
struct PowerPlant {
    uint16_t plantId;           // Physical plant ID for API
    uint8_t sourceType;         // Source type (coal, gas, etc.)
    float minWatts;             // Minimum production capacity
    float maxWatts;             // Maximum production capacity
    
    // Hardware references
    Encoder* encoder;
    SegmentDisplay* powerDisplay;
    Bargraph* powerBargraph;
    
    // State variables
    std::atomic<float> powerSetting;
    std::atomic<float> powerPercentage;
    std::atomic<float> frozenPercentage;  // Store value when frozen
    
    PowerPlant() : 
        plantId(0), sourceType(0), minWatts(0.0f), maxWatts(0.0f),
        encoder(nullptr), powerDisplay(nullptr), powerBargraph(nullptr),
        powerSetting(0.0f), powerPercentage(0.0f), frozenPercentage(0.0f) {}
        
    // Copy constructor and assignment operator
    PowerPlant(const PowerPlant& other) :
        plantId(other.plantId), sourceType(other.sourceType), 
        minWatts(other.minWatts), maxWatts(other.maxWatts),
        encoder(other.encoder), powerDisplay(other.powerDisplay), 
        powerBargraph(other.powerBargraph),
        powerSetting(other.powerSetting.load()), 
        powerPercentage(other.powerPercentage.load()),
        frozenPercentage(other.frozenPercentage.load()) {}
        
    PowerPlant& operator=(const PowerPlant& other) {
        if (this != &other) {
            plantId = other.plantId;
            sourceType = other.sourceType;
            minWatts = other.minWatts;
            maxWatts = other.maxWatts;
            encoder = other.encoder;
            powerDisplay = other.powerDisplay;
            powerBargraph = other.powerBargraph;
            powerSetting = other.powerSetting.load();
            powerPercentage = other.powerPercentage.load();
            frozenPercentage = other.frozenPercentage.load();
        }
        return *this;
    }
        
    PowerPlant(uint16_t id, uint8_t type, float min, float max) :
        plantId(id), sourceType(type), minWatts(min), maxWatts(max),
        encoder(nullptr), powerDisplay(nullptr), powerBargraph(nullptr),
        powerSetting(0.0f), powerPercentage(0.0f), frozenPercentage(0.0f) {}
};

class GameManager {
private:
    // Maximum number of power plants supported
    static constexpr size_t MAX_POWER_PLANTS = 6;
    
    // Array of power plants
    std::array<PowerPlant, MAX_POWER_PLANTS> powerPlants;
    size_t powerPlantCount;
    
    // ESP-API instance (owned by GameManager)
    ESPGameAPI* espApi;
    
    // NFC Building Registry for consumption tracking
    NFCBuildingRegistry* nfcRegistry;
    
    // Consumption tracking
    std::atomic<float> totalConsumption;
    unsigned long lastConsumptionUpdate;

    // Private constructor for singleton
    GameManager() :
        powerPlantCount(0),
        espApi(nullptr),
        nfcRegistry(nullptr),
        totalConsumption(0.0f),
        lastConsumptionUpdate(0) {
        // Initialize power plants array
        for (auto& plant : powerPlants) {
            plant = PowerPlant();
        }
    }

public:
    // ESP-API integration and initialization
    void initEspApi(const char* serverUrl, const char* boardName, const char* username, const char* password) {
        if (espApi) {
            delete espApi;
        }
        
        espApi = new ESPGameAPI(serverUrl, boardName, BOARD_GENERIC, 500, 2000);
        
        // Set up callbacks
        espApi->setProductionCallback([this]() { return getTotalProduction(); });
        espApi->setConsumptionCallback([this]() { return getTotalConsumption(); });
        espApi->setPowerPlantsCallback([this]() { return getConnectedPowerPlants(); });
        espApi->setConsumersCallback([this]() { return getConnectedConsumers(); });
        
        // Set intervals
        espApi->setUpdateInterval(500);
        espApi->setPollInterval(2000);
        
        // Login and register
        if (espApi->login(username, password) && espApi->registerBoard()) {
            espApi->printStatus();
            
            // Request initial production ranges
            Serial.println("[GameManager] Requesting initial production ranges...");
            requestProductionRanges();
        } else {
            Serial.println("[GameManager] ESP-API login or registration failed");
        }
    }
    
    // Destructor to clean up ESP-API
    ~GameManager() {
        if (espApi) {
            delete espApi;
        }
    }
    
    // Update coefficients and ranges from game server
    void updateCoefficientsFromGame() {
        if (!espApi) return;
        
        // First, reset all power plants to (0,0) - disabled by default
        for (size_t i = 0; i < powerPlantCount; i++) {
            powerPlants[i].minWatts = 0.0f;
            powerPlants[i].maxWatts = 0.0f;
        }
        
        // Update production ranges (these are now pre-multiplied by the server)
        // Only power plants that receive ranges from server will be enabled
        for (const auto &r : espApi->getProductionRanges()) {
            for (size_t i = 0; i < powerPlantCount; i++) {
                if (powerPlants[i].sourceType == r.source_id) {
                    Serial.printf("🔄 Updated power plant %u ranges: %.1fW - %.1fW\n", 
                                  powerPlants[i].plantId, r.min_power, r.max_power);
                    powerPlants[i].minWatts = r.min_power;
                    powerPlants[i].maxWatts = r.max_power;
                    break; // Found the matching plant, no need to continue
                }
            }
        }
        
        // Log any power plants that remain disabled (0,0)
        for (size_t i = 0; i < powerPlantCount; i++) {
            if (powerPlants[i].minWatts == 0.0f && powerPlants[i].maxWatts == 0.0f) {
                Serial.printf("⚠️  Power plant %u (type %u) disabled - no ranges from server\n", 
                              powerPlants[i].plantId, powerPlants[i].sourceType);
            }
        }
    }
    
    // Update consumption from connected buildings
    void updateConsumptionFromBuildings() {
        if (!nfcRegistry || !espApi) return;
        
        float consumption = 0.0f;
        
        // Get consumption coefficients from server
        const auto& consumptionCoeffs = espApi->getConsumptionCoefficients();
        
        // Get all connected buildings from NFC registry
        auto connectedBuildings = nfcRegistry->getAllBuildings();
        
        // Calculate total consumption based on connected buildings
        for (const auto& building : connectedBuildings) {
            uint8_t buildingType = building.second.buildingType;
            
            // Find consumption for this building type
            for (const auto& coeff : consumptionCoeffs) {
                if (coeff.building_id == buildingType) {
                    consumption += coeff.consumption;
                    break;
                }
            }
        }
        
        totalConsumption = consumption;
        lastConsumptionUpdate = millis();
    }
    
    // Check if game is active
    bool isGameActive() const {
        return espApi ? espApi->isGameActive() : false;
    }
    
    // Update ESP-API (call this in main loop)
    bool updateEspApi() {
        bool result = espApi ? espApi->update() : false;
        
        // If coefficients were updated, also request production ranges
        if (result && espApi) {
            requestProductionRanges();
        }
        
        return result;
    }
    
    // Request production ranges from server
    void requestProductionRanges() {
        if (!espApi) return;
        
        espApi->getProductionRanges([this](bool success, const std::vector<ProductionRange>& ranges, const std::string& error) {
            if (success) {
                Serial.println("📊 Production ranges received from server");
                updateCoefficientsFromGame(); // This will update both coefficients and ranges
            } else {
                Serial.println("❌ Failed to get production ranges: " + String(error.c_str()));
            }
        });
    }

    // Delete copy/move constructors and assignment operators
    GameManager(const GameManager&) = delete;
    GameManager& operator=(const GameManager&) = delete;
    GameManager(GameManager&&) = delete;
    GameManager& operator=(GameManager&&) = delete;

    // Singleton instance accessor
    static GameManager& getInstance() {
        static GameManager instance;
        return instance;
    }

    // Add a new power plant (returns index or -1 if full)
    // Min/max watts will be automatically set from server production ranges
    int addPowerPlant(uint16_t plantId, uint8_t sourceType,
                      Encoder* encoder, SegmentDisplay* powerDisplay, Bargraph* powerBargraph) {
        if (powerPlantCount >= MAX_POWER_PLANTS) {
            return -1; // Array is full
        }
        
        auto& plant = powerPlants[powerPlantCount];
        plant.plantId = plantId;
        plant.sourceType = sourceType;
        plant.minWatts = 0.0f;  // Will be updated from server
        plant.maxWatts = 1000.0f;  // Default max, will be updated from server
        plant.encoder = encoder;
        plant.powerDisplay = powerDisplay;
        plant.powerBargraph = powerBargraph;
        plant.powerSetting = 0.0f;
        plant.powerPercentage = 0.0f;
        plant.frozenPercentage = 0.0f;
        
        // Set initial encoder value to 50%
        if (encoder) {
            encoder->setValue(500); // 50% of 1000
        }
        
        return powerPlantCount++;
    }

    // Update game state from hardware
    void update() {
        // Update power plants
        nfcRegistry->scanForCards();
        for (size_t i = 0; i < powerPlantCount; i++) {
            auto& plant = powerPlants[i];
            if (!plant.encoder) continue;

            // Read encoder value and convert to percentage
            float newPercentage = plant.encoder->getValue() / 1000.0f;
            plant.powerPercentage = newPercentage;

            // Calculate power setting based on plant capacity (ranges are now pre-multiplied by server)
            plant.powerSetting = plant.minWatts + 
                                (plant.powerPercentage.load()) * (plant.maxWatts - plant.minWatts);
        }
        
        // Update consumption every 2 seconds
        if (millis() - lastConsumptionUpdate >= 2000) {
            updateConsumptionFromBuildings();
        }
    }

    // Method for IO task to update displays
    static void updateDisplays() {
        getInstance().updateDisplaysImpl();
    }

    // Update displays (private implementation)
private:
    void updateDisplaysImpl() {
        for (size_t i = 0; i < powerPlantCount; i++) {
            auto& plant = powerPlants[i];
            
            // Update power displays
            if (plant.powerDisplay) {
                plant.powerDisplay->displayNumber(plant.powerSetting.load());
            }
            
            // Update power bargraph based on encoder percentage
            if (plant.powerBargraph) {
                // Calculate bargraph value: 0% encoder = 0 LEDs, 100% encoder = 10 LEDs
                // The hardware is inverted: setValue(0) = fully lit, setValue(10) = not lit
                // So we invert: 0% → setValue(10), 100% → setValue(0)
                uint8_t desiredLEDs = static_cast<uint8_t>(plant.powerPercentage.load() * 10);
                plant.powerBargraph->setValue(desiredLEDs);
            }
        }
    }

public:

    // Setters for game coefficients (called from API callbacks)
    // Getters for specific plants by ID
    PowerPlant* getPowerPlant(uint16_t plantId) {
        for (size_t i = 0; i < powerPlantCount; i++) {
            if (powerPlants[i].plantId == plantId) {
                return &powerPlants[i];
            }
        }
        return nullptr;
    }

    float getPowerByPlantId(uint16_t plantId) const {
        for (size_t i = 0; i < powerPlantCount; i++) {
            if (powerPlants[i].plantId == plantId) {
                return powerPlants[i].powerSetting.load();
            }
        }
        return 0.0f;
    }

    float getPercentageByPlantId(uint16_t plantId) const {
        for (size_t i = 0; i < powerPlantCount; i++) {
            if (powerPlants[i].plantId == plantId) {
                return powerPlants[i].powerPercentage.load();
            }
        }
        return 0.0f;
    }

    float getTotalProduction() const {
        float total = 0.0f;
        for (size_t i = 0; i < powerPlantCount; i++) {
            total += powerPlants[i].powerSetting.load();
        }
        return total;
    }
    
    // Get total consumption from connected buildings
    float getTotalConsumption() const {
        return totalConsumption.load();
    }
    
    // Initialize NFC Building Registry
    void initNfcRegistry(NFCBuildingRegistry* registry) {
        nfcRegistry = registry;
        Serial.println("[GameManager] NFC Building Registry initialized");
    }

    // Get count of power plants
    size_t getPowerPlantCount() const { return powerPlantCount; }

    // Get power plant by index
    const PowerPlant& getPowerPlantByIndex(size_t index) const {
        return powerPlants[index];
    }

    // ESP-API callback helpers
    std::vector<ConnectedPowerPlant> getConnectedPowerPlants();
    std::vector<ConnectedConsumer> getConnectedConsumers();

    // Debug output
    void printDebugInfo() {
        bool gameActive = isGameActive();
        Serial.printf("[PLANTS] Total: %.1fW | Consumption: %.1fW | Game %s | Plants: %zu\n",
                      getTotalProduction(), getTotalConsumption(), gameActive ? "ON" : "OFF", powerPlantCount);
        
        for (size_t i = 0; i < powerPlantCount; i++) {
            const auto& plant = powerPlants[i];
            Serial.printf("  [%zu] ID:%u %.0f%% → %.1fW (%.1f-%.1fW)\n",
                          i, plant.plantId, plant.powerPercentage.load() * 100,
                          plant.powerSetting.load(), plant.minWatts, plant.maxWatts);
        }
        
        // Print connected buildings info
        if (nfcRegistry) {
            auto buildings = nfcRegistry->getAllBuildings();
            Serial.printf("[BUILDINGS] Connected: %zu\n", buildings.size());
            for (const auto& building : buildings) {
                Serial.printf("  UID:%s Type:%u\n", 
                             building.second.uid.c_str(), building.second.buildingType);
            }
        }
    }
};

#endif // GAME_MANAGER_H
