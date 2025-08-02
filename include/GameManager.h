#ifndef GAME_MANAGER_H
#define GAME_MANAGER_H

#include <atomic>
#include <vector>
#include <array>
#include <ESPGameAPI.h>
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
    SegmentDisplay* coeffDisplay;
    Bargraph* powerBargraph;
    Bargraph* coeffBargraph;
    
    // State variables
    std::atomic<float> powerSetting;
    std::atomic<float> coefficient;
    std::atomic<float> powerPercentage;
    
    PowerPlant() : 
        plantId(0), sourceType(0), minWatts(0.0f), maxWatts(0.0f),
        encoder(nullptr), powerDisplay(nullptr), coeffDisplay(nullptr),
        powerBargraph(nullptr), coeffBargraph(nullptr),
        powerSetting(0.0f), coefficient(1.0f), powerPercentage(0.0f) {}
        
    // Copy constructor and assignment operator
    PowerPlant(const PowerPlant& other) :
        plantId(other.plantId), sourceType(other.sourceType), 
        minWatts(other.minWatts), maxWatts(other.maxWatts),
        encoder(other.encoder), powerDisplay(other.powerDisplay), 
        coeffDisplay(other.coeffDisplay), powerBargraph(other.powerBargraph), 
        coeffBargraph(other.coeffBargraph),
        powerSetting(other.powerSetting.load()), 
        coefficient(other.coefficient.load()), 
        powerPercentage(other.powerPercentage.load()) {}
        
    PowerPlant& operator=(const PowerPlant& other) {
        if (this != &other) {
            plantId = other.plantId;
            sourceType = other.sourceType;
            minWatts = other.minWatts;
            maxWatts = other.maxWatts;
            encoder = other.encoder;
            powerDisplay = other.powerDisplay;
            coeffDisplay = other.coeffDisplay;
            powerBargraph = other.powerBargraph;
            coeffBargraph = other.coeffBargraph;
            powerSetting = other.powerSetting.load();
            coefficient = other.coefficient.load();
            powerPercentage = other.powerPercentage.load();
        }
        return *this;
    }
        
    PowerPlant(uint16_t id, uint8_t type, float min, float max) :
        plantId(id), sourceType(type), minWatts(min), maxWatts(max),
        encoder(nullptr), powerDisplay(nullptr), coeffDisplay(nullptr),
        powerBargraph(nullptr), coeffBargraph(nullptr),
        powerSetting(0.0f), coefficient(1.0f), powerPercentage(0.0f) {}
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

    // Private constructor for singleton
    GameManager() :
        powerPlantCount(0),
        espApi(nullptr) {
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
        espApi->setConsumptionCallback([]() { return 0.0f; });
        espApi->setPowerPlantsCallback([this]() { return getConnectedPowerPlants(); });
        espApi->setConsumersCallback([]() { return std::vector<ConnectedConsumer>{}; });
        
        // Set intervals
        espApi->setUpdateInterval(500);
        espApi->setPollInterval(2000);
        
        // Login and register
        if (espApi->login(username, password) && espApi->registerBoard()) {
            espApi->printStatus();
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
    
    // Update coefficients from game server
    void updateCoefficientsFromGame() {
        if (!espApi) return;
        
        for (const auto &c : espApi->getProductionCoefficients()) {
            setCoefficient(c.source_id, c.coefficient);
        }
    }
    
    // Check if game is active
    bool isGameActive() const {
        return espApi ? espApi->isGameActive() : false;
    }
    
    // Update ESP-API (call this in main loop)
    bool updateEspApi() {
        return espApi ? espApi->update() : false;
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
    int addPowerPlant(uint16_t plantId, uint8_t sourceType, float minWatts, float maxWatts,
                      Encoder* encoder, SegmentDisplay* powerDisplay, SegmentDisplay* coeffDisplay,
                      Bargraph* powerBargraph, Bargraph* coeffBargraph) {
        if (powerPlantCount >= MAX_POWER_PLANTS) {
            return -1; // Array is full
        }
        
        auto& plant = powerPlants[powerPlantCount];
        plant.plantId = plantId;
        plant.sourceType = sourceType;
        plant.minWatts = minWatts;
        plant.maxWatts = maxWatts;
        plant.encoder = encoder;
        plant.powerDisplay = powerDisplay;
        plant.coeffDisplay = coeffDisplay;
        plant.powerBargraph = powerBargraph;
        plant.coeffBargraph = coeffBargraph;
        plant.powerSetting = 0.0f;
        plant.coefficient = 1.0f;
        plant.powerPercentage = 0.0f;
        
        // Set initial encoder value to 50%
        if (encoder) {
            encoder->setValue(500); // 50% of 1000
        }
        
        return powerPlantCount++;
    }

    // Update game state from hardware
    void update() {
        for (size_t i = 0; i < powerPlantCount; i++) {
            auto& plant = powerPlants[i];
            if (!plant.encoder) continue;

            // Read encoder value and convert to percentage
            plant.powerPercentage = plant.encoder->getValue() / 1000.0f;

            // Calculate power setting based on plant capacity and coefficient
            plant.powerSetting = (plant.minWatts +
                                 (plant.powerPercentage.load()) *
                                 (plant.maxWatts - plant.minWatts)) * plant.coefficient.load();
        }
    }

    // Method for IO task to update displays
    static void updateDisplays() {
        getInstance().updateDisplaysImpl();
    }

    // Update displays (private implementation)
private:
    void updateDisplaysImpl() {
        bool gameActive = isGameActive();
        
        for (size_t i = 0; i < powerPlantCount; i++) {
            auto& plant = powerPlants[i];
            
            // Update power displays
            if (plant.powerDisplay) {
                plant.powerDisplay->displayNumber(plant.powerSetting.load());
            }
            if (plant.powerBargraph) {
                plant.powerBargraph->setValue(static_cast<uint8_t>(plant.powerPercentage.load() * 10));
            }

            // Update coefficient displays
            if (gameActive) {
                if (plant.coeffDisplay) {
                    plant.coeffDisplay->displayNumber(plant.coefficient.load() * 100);
                }
                if (plant.coeffBargraph) {
                    plant.coeffBargraph->setValue(static_cast<uint8_t>(plant.coefficient.load() * 10));
                }
            } else {
                if (plant.coeffDisplay) {
                    plant.coeffDisplay->displayNumber(0L);
                }
                if (plant.coeffBargraph) {
                    plant.coeffBargraph->setValue(0);
                }
            }
        }
    }

public:

    // Setters for game coefficients (called from API callbacks)
    void setCoefficient(uint16_t plantId, float coeff) {
        for (size_t i = 0; i < powerPlantCount; i++) {
            if (powerPlants[i].plantId == plantId) {
                powerPlants[i].coefficient = coeff;
                break;
            }
        }
    }

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

    float getCoefficientByPlantId(uint16_t plantId) const {
        for (size_t i = 0; i < powerPlantCount; i++) {
            if (powerPlants[i].plantId == plantId) {
                return powerPlants[i].coefficient.load();
            }
        }
        return 1.0f;
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
        Serial.printf("[PLANTS] Total: %.1fW | Game %s | Plants: %zu\n",
                      getTotalProduction(), gameActive ? "ON" : "OFF", powerPlantCount);
        
        for (size_t i = 0; i < powerPlantCount; i++) {
            const auto& plant = powerPlants[i];
            Serial.printf("  [%zu] ID:%u %.0f%% → %.1fW (c=%.2f)\n",
                          i, plant.plantId, plant.powerPercentage.load() * 100,
                          plant.powerSetting.load(), plant.coefficient.load());
        }
    }
};

#endif // GAME_MANAGER_H
