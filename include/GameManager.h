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

// Power plant type enumeration
enum PowerPlantType : uint8_t {
    PHOTOVOLTAIC = 1,
    WIND = 2,
    NUCLEAR = 3,
    GAS = 4,
    HYDRO = 5,
    HYDRO_STORAGE = 6,
    COAL = 7,
    BATTERY = 8
};

// UART Powerplant info structure (from UART protocol)
struct UartSlaveInfo {
    uint8_t slaveType;
    uint8_t amount;
};

// Power plant type controller data structure
// This represents a local controller (encoder + display) for a power plant type
// The actual number of powerplants is tracked via UART
struct PowerPlant {
    PowerPlantType plantType;   // Power plant type (enum)
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
        plantType(COAL), minWatts(0.0f), maxWatts(0.0f),
        encoder(nullptr), powerDisplay(nullptr), powerBargraph(nullptr),
        powerSetting(0.0f), powerPercentage(0.0f), frozenPercentage(0.0f) {}
        
    // Copy constructor and assignment operator
    PowerPlant(const PowerPlant& other) :
        plantType(other.plantType), 
        minWatts(other.minWatts), maxWatts(other.maxWatts),
        encoder(other.encoder), powerDisplay(other.powerDisplay), 
        powerBargraph(other.powerBargraph),
        powerSetting(other.powerSetting.load()), 
        powerPercentage(other.powerPercentage.load()),
        frozenPercentage(other.frozenPercentage.load()) {}
        
    PowerPlant& operator=(const PowerPlant& other) {
        if (this != &other) {
            plantType = other.plantType;
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
        
    PowerPlant(PowerPlantType type, float min, float max) :
        plantType(type), minWatts(min), maxWatts(max),
        encoder(nullptr), powerDisplay(nullptr), powerBargraph(nullptr),
        powerSetting(0.0f), powerPercentage(0.0f), frozenPercentage(0.0f) {}
};

class GameManager {
private:
    // Maximum number of power plant types we can control locally
    static constexpr size_t MAX_POWER_PLANTS = 8;
    static constexpr unsigned long ATTRACTION_UPDATE_MS = 200;
    
    // Array of local power plant type controllers (encoder + display)
    // The actual count of powerplants comes from UART
    std::array<PowerPlant, MAX_POWER_PLANTS> powerPlants;
    size_t powerPlantCount;
    
    // ESP-API instance (owned by GameManager)
    ESPGameAPI* espApi;
    
    // NFC Building Registry for consumption tracking
    NFCBuildingRegistry* nfcRegistry;
    
    // UART Powerplant tracking
    std::vector<UartSlaveInfo> uartPowerplants;
    unsigned long lastUartAttractionUpdate;
    
    // Consumption tracking
    std::atomic<float> totalConsumption;
    unsigned long lastConsumptionUpdate;

    // Throttling for server requests
    unsigned long lastRequestTime;
    static constexpr unsigned long REQUEST_INTERVAL_MS = 3000; // Request both coefficients and ranges every 3 seconds

    // Private constructor for singleton
    GameManager() :
        powerPlantCount(0),
        espApi(nullptr),
        nfcRegistry(nullptr),
        lastUartAttractionUpdate(0),
        totalConsumption(0.0f),
        lastConsumptionUpdate(0),
        lastRequestTime(0) {
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
            
            // Request initial production ranges and coefficients
            Serial.println("[GameManager] Requesting initial production ranges and coefficients...");
            requestProductionRanges();
            requestProductionCoefficients();
            
            // Initialize throttling timer
            unsigned long now = millis();
            lastRequestTime = now;
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
                if (static_cast<uint8_t>(powerPlants[i].plantType) == r.source_id) {
                    /*Serial.printf("🔄 Updated power plant type %u ranges: %.1fW - %.1fW\n", 
                                  static_cast<uint8_t>(powerPlants[i].plantType), r.min_power, r.max_power);*/
                    powerPlants[i].minWatts = r.min_power;
                    powerPlants[i].maxWatts = r.max_power;
                    break; // Found the matching plant, no need to continue
                }
            }
        }
        
        // Log any power plants that remain disabled (0,0)
        for (size_t i = 0; i < powerPlantCount; i++) {
            if (powerPlants[i].minWatts == 0.0f && powerPlants[i].maxWatts == 0.0f) {
                /*Serial.printf("⚠️  Power plant type %u disabled - no ranges from server\n", 
                              static_cast<uint8_t>(powerPlants[i].plantType));*/
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
        
        // If connected, request production ranges and coefficients
        // But throttle the requests to avoid spamming the server
        if (result && espApi) {
            unsigned long now = millis();
            
            // Request both ranges and coefficients every 3 seconds
            if (now - lastRequestTime >= REQUEST_INTERVAL_MS) {
                requestProductionRanges();
                requestProductionCoefficients();
                lastRequestTime = now;
            }
        }
        
        return result;
    }
    
    // Request production ranges from server
    void requestProductionRanges() {
        if (!espApi) return;
        
        espApi->getProductionRanges([this](bool success, const std::vector<ProductionRange>& ranges, const std::string& error) {
            if (success) {
                static unsigned long lastLogTime = 0;
                if (millis() - lastLogTime > 10000) { // Log only every 10 seconds
                    Serial.println("📊 Production ranges received from server");
                    lastLogTime = millis();
                }
                updateCoefficientsFromGame(); // This will update both coefficients and ranges
            } else {
                Serial.println("❌ Failed to get production ranges: " + String(error.c_str()));
            }
        });
    }

    // Request production coefficients from server
    void requestProductionCoefficients() {
        if (!espApi) return;
        
        espApi->pollCoefficients([this](bool success, const std::string& error) {
            if (success) {
                static unsigned long lastLogTime = 0;
                if (millis() - lastLogTime > 10000) { // Log only every 10 seconds
                    Serial.println("📊 Production coefficients received from server");
                    lastLogTime = millis();
                }
                // Coefficients are automatically stored in espApi and can be accessed via getProductionCoefficients()
            } else {
                Serial.println("❌ Failed to get production coefficients: " + String(error.c_str()));
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

    // Register a power plant type for local control (encoder + display)
    // This doesn't add actual power plants - those are tracked via UART
    // Returns index or -1 if full
    int registerPowerPlantTypeControl(PowerPlantType plantType,
                                      Encoder* encoder, SegmentDisplay* powerDisplay, Bargraph* powerBargraph) {
        if (powerPlantCount >= MAX_POWER_PLANTS) {
            return -1; // Array is full
        }
        
        auto& plant = powerPlants[powerPlantCount];
        plant.plantType = plantType;
        plant.minWatts = 0.0f;  // Will be updated from server
        plant.maxWatts = 1000.0f;  // Default max, will be updated from server
        plant.encoder = encoder;
        plant.powerDisplay = powerDisplay;
        plant.powerBargraph = powerBargraph;
        plant.powerSetting = 0.0f;
        
        if (encoder) {
            // Regulable source: set initial encoder value to 50%
            plant.powerPercentage = 0.5f;
            plant.frozenPercentage = 0.5f;
            encoder->setValue(500); // 50% of 1000
        } else {
            // Unregulable source: always at 100%
            plant.powerPercentage = 1.0f;
            plant.frozenPercentage = 1.0f;
        }
        
        return powerPlantCount++;
    }

    // Update game state from hardware
    void update() {
        // Update power plants
        nfcRegistry->scanForCards();
        for (size_t i = 0; i < powerPlantCount; i++) {
            auto& plant = powerPlants[i];
            
            if (plant.encoder) {
                // Regulable source: read encoder value and convert to percentage
                float newPercentage = plant.encoder->getValue() / 1000.0f;
                plant.powerPercentage = newPercentage;
                
                // For HYDRO_STORAGE sharing encoder with BATTERY, copy the same percentage
                if (plant.plantType == BATTERY) {
                    // Find hydro storage plant and sync its percentage
                    for (size_t j = 0; j < powerPlantCount; j++) {
                        if (powerPlants[j].plantType == HYDRO_STORAGE) {
                            powerPlants[j].powerPercentage = newPercentage;
                            break;
                        }
                    }
                }
                else if (plant.plantType == HYDRO_STORAGE) {
                    // Find battery plant and sync from it (battery should be registered first)
                    for (size_t j = 0; j < powerPlantCount; j++) {
                        if (powerPlants[j].plantType == BATTERY && powerPlants[j].encoder) {
                            plant.powerPercentage = powerPlants[j].encoder->getValue() / 1000.0f;
                            break;
                        }
                    }
                }
            } else {
                // Unregulable source: always run at maximum power (100%)
                plant.powerPercentage = 1.0f;
            }

            // Calculate power setting using helper with center snap
            plant.powerSetting = computePowerPerPlant(plant);
        }
        
        // Update consumption every 2 seconds
        if (millis() - lastConsumptionUpdate >= 2000) {
            updateConsumptionFromBuildings();
        }
        
        // Update attraction states based on power percentages
        updateAttractionStates();
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
            
            // Calculate total power for this type including UART powerplants
            float totalPowerForType = calculateTotalPowerForType(static_cast<uint8_t>(plant.plantType));
            
            // For battery display, also add hydro storage power (they share display)
            if (plant.plantType == BATTERY) {
                float hydroStoragePower = calculateTotalPowerForType(static_cast<uint8_t>(HYDRO_STORAGE));
                totalPowerForType += hydroStoragePower;
            }
            // For hydro storage, skip display update since it shares with battery
            else if (plant.plantType == HYDRO_STORAGE) {
                continue; // Skip individual display update, handled by battery
            }
            
            // Update power displays with total power (including count)
            if (plant.powerDisplay) {
                plant.powerDisplay->displayNumber(totalPowerForType);
            }
            
            // Update power bargraph based on encoder percentage
            if (plant.powerBargraph) {
                uint8_t desiredLEDs;

                // If powerplant type is disabled (max power = 0), show 0 bars
                if (plant.maxWatts <= 0.0f) {
                    desiredLEDs = 10; // Hardware inverted: 10 = no LEDs lit
                } else {
                    // Calculate bargraph value: 0% encoder = 0 LEDs, 100% encoder = 10 LEDs
                    // The hardware is inverted: setValue(0) = fully lit, setValue(10) = not lit
                    // So we invert: 0% → setValue(10), 100% → setValue(0)
                    desiredLEDs = static_cast<uint8_t>(plant.powerPercentage.load() * 10);
                }

                plant.powerBargraph->setValue(desiredLEDs);
            }
        }
    }

public:

    // Setters for game coefficients (called from API callbacks)
    
    // Get production coefficient for a specific plant type
    float getProductionCoefficientForType(uint8_t plantType) const {
        if (!espApi) return 0.0f;
        
        const auto& productionCoeffs = espApi->getProductionCoefficients();
        for (const auto& coeff : productionCoeffs) {
            if (coeff.source_id == plantType) {
                return coeff.coefficient;
            }
        }
        return 0.0f; // No coefficient found for this type
    }
    
    // Getters for specific plants by type
    PowerPlant* getPowerPlantByType(PowerPlantType plantType) {
        for (size_t i = 0; i < powerPlantCount; i++) {
            if (powerPlants[i].plantType == plantType) {
                return &powerPlants[i];
            }
        }
        return nullptr;
    }

    float getPowerByPlantType(PowerPlantType plantType) const {
        for (size_t i = 0; i < powerPlantCount; i++) {
            if (powerPlants[i].plantType == plantType) {
                return powerPlants[i].powerSetting.load();
            }
        }
        return 0.0f;
    }

    float getPercentageByPlantType(PowerPlantType plantType) const {
        for (size_t i = 0; i < powerPlantCount; i++) {
            if (powerPlants[i].plantType == plantType) {
                return powerPlants[i].powerPercentage.load();
            }
        }
        return 0.0f;
    }

    float getTotalProduction() const {
        float total = 0.0f;
        
        // Add power from all UART powerplant types
        for (const auto& uartPlant : uartPowerplants) {
            total += calculateTotalPowerForType(uartPlant.slaveType);
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
    
    // UART Powerplant management
    void updateUartPowerplants(const std::vector<UartSlaveInfo>& powerplants);
    void updateAttractionStates();
    float calculateTotalPowerForType(uint8_t slaveType) const;
    // Compute power per plant with center snap for symmetric ranges
    float computePowerPerPlant(const PowerPlant& plant) const;

private:
    // Per-type periodic update hooks (called from updateAttractionStates)
    void updatePhotovoltaic(uint8_t slaveType, const PowerPlant& plant);
    void updateWind(uint8_t slaveType, const PowerPlant& plant);
    void updateNuclear(uint8_t slaveType, const PowerPlant& plant);
    void updateGas(uint8_t slaveType, const PowerPlant& plant);
    void updateHydro(uint8_t slaveType, const PowerPlant& plant);
    void updateHydroStorage(uint8_t slaveType, const PowerPlant& plant);
    void updateCoal(uint8_t slaveType, const PowerPlant& plant);
    void updateBattery(uint8_t slaveType, const PowerPlant& plant);

    // Debug output (instance implementation)
    void printDebugInfoImpl() {
        bool gameActive = isGameActive();
        Serial.printf("[PLANTS] Total: %.1fW | Consumption: %.1fW | Game %s | Local: %zu | UART Types: %zu\n",
                      getTotalProduction(), getTotalConsumption(), gameActive ? "ON" : "OFF", 
                      powerPlantCount, uartPowerplants.size());
        
        // Print local encoder-controlled power plant types
        for (size_t i = 0; i < powerPlantCount; i++) {
            const auto& plant = powerPlants[i];
            float totalForType = calculateTotalPowerForType(static_cast<uint8_t>(plant.plantType));
            
            // Find UART count for this type
            uint8_t uartCount = 0;
            for (const auto& uartPlant : uartPowerplants) {
                if (uartPlant.slaveType == static_cast<uint8_t>(plant.plantType)) {
                    uartCount = uartPlant.amount;
                    break;
                }
            }
            
            float powerPerPlant = 0.0f;
            const char* status = "DISABLED";
            
            if (plant.maxWatts <= 0.0f) {
                status = "DISABLED";
                powerPerPlant = 0.0f;
            } else if (uartCount == 0) {
                status = "NO PLANTS";
                powerPerPlant = 0.0f;
            } else {
                status = "ACTIVE";
                powerPerPlant = computePowerPerPlant(plant);
            }
            
            Serial.printf("  [%zu] Type:%u %.0f%% → %.1fW×%u = %.1fW (%.1f-%.1fW) %s\n",
                          i, static_cast<uint8_t>(plant.plantType), plant.powerPercentage.load() * 100,
                          powerPerPlant, uartCount, totalForType,
                          plant.minWatts, plant.maxWatts, status);
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

    // Debug output for coefficients and power production (instance implementation)
    void printCoefficientDebugInfoImpl() {
        Serial.println("\n=== COEFFICIENT DEBUG INFO ===");
        
        if (!espApi) {
            Serial.println("[ERROR] No ESP API connection");
            return;
        }
        
        // Print production coefficients
        const auto& productionCoeffs = espApi->getProductionCoefficients();
        Serial.printf("[COEFFICIENTS] Production coefficients (%zu total):\n", productionCoeffs.size());
        for (const auto& coeff : productionCoeffs) {
            const char* typeName = "UNKNOWN";
            switch (coeff.source_id) {
                case SOURCE_PHOTOVOLTAIC: typeName = "SOLAR"; break;
                case SOURCE_WIND: typeName = "WIND"; break;
                case SOURCE_NUCLEAR: typeName = "NUCLEAR"; break;
                case SOURCE_GAS: typeName = "GAS"; break;
                case SOURCE_HYDRO: typeName = "HYDRO"; break;
                case SOURCE_HYDRO_STORAGE: typeName = "HYDRO_STORAGE"; break;
                case SOURCE_COAL: typeName = "COAL"; break;
                case SOURCE_BATTERY: typeName = "BATTERY"; break;
            }
            Serial.printf("  %s (ID:%u): %.3f\n", typeName, coeff.source_id, coeff.coefficient);
        }
        
        // Print power production for each type
        Serial.println("[POWER] Current power production:");
        for (size_t i = 0; i < powerPlantCount; i++) {
            const auto& plant = powerPlants[i];
            uint8_t slaveType = static_cast<uint8_t>(plant.plantType);
            float totalPower = calculateTotalPowerForType(slaveType);
            float coefficient = getProductionCoefficientForType(slaveType);
            
            const char* typeName = "UNKNOWN";
            switch (plant.plantType) {
                case PHOTOVOLTAIC: typeName = "SOLAR"; break;
                case WIND: typeName = "WIND"; break;
                case NUCLEAR: typeName = "NUCLEAR"; break;
                case GAS: typeName = "GAS"; break;
                case HYDRO: typeName = "HYDRO"; break;
                case HYDRO_STORAGE: typeName = "HYDRO_STORAGE"; break;
                case COAL: typeName = "COAL"; break;
                case BATTERY: typeName = "BATTERY"; break;
            }
            
            Serial.printf("  %s: %.1fW (coeff: %.3f, range: %.1f-%.1f, percent: %.1f%%)\n",
                         typeName, totalPower, coefficient, plant.minWatts, plant.maxWatts, 
                         plant.powerPercentage.load() * 100.0f);
        }
        
        Serial.println("=== END COEFFICIENT DEBUG ===\n");
    }

public:
    // Public static debug print that avoids exposing instance state externally
    static void printDebugInfo() {
        getInstance().printDebugInfoImpl();
    }
    
    // Print coefficient and power information for debugging
    static void printCoefficientDebugInfo() {
        getInstance().printCoefficientDebugInfoImpl();
    }
};

#endif // GAME_MANAGER_H
