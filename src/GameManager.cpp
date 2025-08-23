#include "GameManager.h"
#include "power_plant_config.h"
#include <ESPGameAPI.h>
#include <Arduino.h>
#include <math.h>

// --- New UART TX hooks provided by the ESP32-S3 main file ---
extern void sendAttractionCommand(uint8_t slaveType, uint8_t state); // maps to cmd4 0x01/0x02
extern void sendCmd2B(uint8_t slaveType, uint8_t cmd4);              // sends [type, cmd4]

// --- 4-bit command codes used on the wire (no payload) ---
static constexpr uint8_t CMD_ON               = 0x01; // generic ON
static constexpr uint8_t CMD_OFF              = 0x02; // generic OFF
static constexpr uint8_t CMD_BATTERY_IDLE     = 0x03; // battery -> idle
static constexpr uint8_t CMD_BATTERY_CHARGE   = 0x04; // battery -> charge/consume
static constexpr uint8_t CMD_BATTERY_DISCHARGE= 0x05; // battery -> discharge/produce

// Hydro storage command codes (5 levels)
static constexpr uint8_t CMD_HYDRO_STORAGE_LEVEL_1 = 0x0B; // 100% Full - Green (Discharging)
static constexpr uint8_t CMD_HYDRO_STORAGE_LEVEL_2 = 0x0C; // 75% Full - Light Green
static constexpr uint8_t CMD_HYDRO_STORAGE_LEVEL_3 = 0x0D; // 50% Full - Orange (Idle)
static constexpr uint8_t CMD_HYDRO_STORAGE_LEVEL_4 = 0x0E; // 25% Full - Light Red
static constexpr uint8_t CMD_HYDRO_STORAGE_LEVEL_5 = 0x0F; // 0% Empty - Red (Charging)

// Implementation of methods that need ESPGameAPI types

std::vector<ConnectedPowerPlant> GameManager::getConnectedPowerPlants() {
    std::vector<ConnectedPowerPlant> plants;
    // Only report powerplants that are actually connected via UART
    for (const auto& uartPlant : uartPowerplants) {
        if (uartPlant.amount > 0) {
            // Calculate individual power for each type (separate battery and hydro storage)
            float totalPower = calculateTotalPowerForType(uartPlant.slaveType);
            plants.push_back({static_cast<uint16_t>(uartPlant.slaveType), totalPower});
        }
    }
    return plants;
}

std::vector<ConnectedConsumer> GameManager::getConnectedConsumers() {
    std::vector<ConnectedConsumer> consumers;
    if (nfcRegistry) {
        auto buildings = nfcRegistry->getAllBuildings();
        for (const auto& building : buildings) {
            // use building type as consumer ID for now
            uint32_t consumerId = static_cast<uint32_t>(building.second.buildingType);
            consumers.push_back({consumerId});
        }
    }
    return consumers;
}

void GameManager::updateUartPowerplants(const std::vector<UartSlaveInfo>& powerplants) {
    uartPowerplants = powerplants;
    // (optionally trigger an immediate attraction refresh here)
}

void GameManager::updateAttractionStates() {
    // Throttle updates to avoid spam while keeping responsiveness
    if (millis() - lastUartAttractionUpdate < ATTRACTION_UPDATE_MS) {
        return;
    }

    // Track which types we already sent for (avoid duplicates)
    uint8_t sentTypes[16];
    size_t sentCount = 0;

    // Handle all types reported via UART first
    for (const auto& uartPlant : uartPowerplants) {
        if (uartPlant.amount == 0) continue;

        bool hasLocal = false;
        for (size_t i = 0; i < powerPlantCount; i++) {
            if (static_cast<uint8_t>(powerPlants[i].plantType) == uartPlant.slaveType) {
                const auto& plant = powerPlants[i];
                switch (plant.plantType) {
                    case PHOTOVOLTAIC: updatePhotovoltaic(uartPlant.slaveType, plant); break;
                    case WIND:         updateWind(uartPlant.slaveType, plant); break;
                    case NUCLEAR:      updateNuclear(uartPlant.slaveType, plant); break;
                    case GAS:          updateGas(uartPlant.slaveType, plant); break;
                    case HYDRO:        updateHydro(uartPlant.slaveType, plant); break;
                    case HYDRO_STORAGE:updateHydroStorage(uartPlant.slaveType, plant); break;
                    case COAL:         updateCoal(uartPlant.slaveType, plant); break;
                    case BATTERY:      updateBattery(uartPlant.slaveType, plant); break;
                    default: {
                        uint8_t attractionState = (plant.maxWatts > 0.0f && plant.powerPercentage.load() > 0.5f) ? 1 : 0;
                        sendAttractionCommand(uartPlant.slaveType, attractionState);
                    } break;
                }
                hasLocal = true;
                break;
            }
        }

        if (!hasLocal) {
            // No local control registered: ensure device goes OFF
            sendAttractionCommand(uartPlant.slaveType, 0);
        }

        if (sentCount < sizeof(sentTypes)) sentTypes[sentCount++] = uartPlant.slaveType;
    }

    lastUartAttractionUpdate = millis();
}

float GameManager::calculateTotalPowerForType(uint8_t slaveType) const {
    // Find the corresponding local powerplant controller for this type
    for (size_t i = 0; i < powerPlantCount; i++) {
        if (static_cast<uint8_t>(powerPlants[i].plantType) == slaveType) {
            const auto& plant = powerPlants[i];

            // Find the count of UART powerplants of this type
            for (const auto& uartPlant : uartPowerplants) {
                if (uartPlant.slaveType == slaveType) {
                    // If no powerplants connected or powerplant type disabled (max = 0), return 0
                    if (uartPlant.amount == 0 || plant.maxWatts <= 0.0f) {
                        // Debug logging for disabled plants
                        static unsigned long lastDisabledDebug = 0;
                        if (millis() - lastDisabledDebug > 5000) { // Debug every 5 seconds
                            if (uartPlant.amount == 0) {
                                Serial.printf("[POWER] Type %u: No plants connected via UART\n", slaveType);
                            } else if (plant.maxWatts <= 0.0f) {
                                Serial.printf("[POWER] Type %u: Plant disabled (maxWatts=%.1f)\n", slaveType, plant.maxWatts);
                            }
                            lastDisabledDebug = millis();
                        }
                        return 0.0f;
                    }

                    // Calculate power per plant with center snap
                    float powerPerPlant = computePowerPerPlant(plant);

                    // Total power = power per plant * number of connected plants
                    float totalPower = powerPerPlant * uartPlant.amount;
                    
                    // Debug logging for active plants
                    static unsigned long lastActiveDebug = 0;
                    if (millis() - lastActiveDebug > 2000) { // Debug every 2 seconds
                        if (totalPower != 0.0f) {
                            Serial.printf("[POWER] Type %u: %u plants, %.1fW per plant, %.1fW total\n", 
                                         slaveType, uartPlant.amount, powerPerPlant, totalPower);
                        }
                        lastActiveDebug = millis();
                    }
                    
                    return totalPower;
                }
            }
            // If no UART data for this type, return 0 (no powerplants connected)
            return 0.0f;
        }
    }
    // If no local controller for this type, return 0
    return 0.0f;
}

// ------- Per-type update helpers -------
static inline uint8_t onOffByPercent(const PowerPlant& plant) {
    if (plant.maxWatts <= 0.0f) return 0;
    return plant.powerPercentage.load() > 0.5f ? 1 : 0;
}

void GameManager::updatePhotovoltaic(uint8_t slaveType, const PowerPlant& plant) {
    sendAttractionCommand(slaveType, onOffByPercent(plant));
}

void GameManager::updateWind(uint8_t slaveType, const PowerPlant& plant) {
    // Wind powerplant control based on server-provided wind coefficient
    // 
    // The server sends production coefficients for wind that reflect current wind conditions.
    // When coefficient > WIND_COEFFICIENT_THRESHOLD, wind turbines should spin.
    // This approach allows the wind powerplant to respond to real-time wind data
    // from the game server rather than manual user control.
    
    uint8_t windState = 0;
    
    if (espApi) {
        float windCoefficient = getProductionCoefficientForType(SOURCE_WIND);
        windState = (windCoefficient > WIND_COEFFICIENT_THRESHOLD) ? 1 : 0;
        
        // Debug output to monitor wind conditions and help tune parameters
        static unsigned long lastWindDebug = 0;
        if (millis() - lastWindDebug > 5000) { // Debug every 5 seconds
            Serial.printf("[WIND] Server coefficient=%.2f, threshold=%.2f -> %s\n",
                         windCoefficient, WIND_COEFFICIENT_THRESHOLD, 
                         windState ? "SPINNING" : "STOPPED");
            lastWindDebug = millis();
        }
        
        if (windCoefficient == 0.0f) {
            // No wind coefficient found or zero coefficient
            static unsigned long lastNoDataDebug = 0;
            if (millis() - lastNoDataDebug > 10000) { // Debug every 10 seconds
                Serial.println("[WIND] No wind coefficient from server - turbines stopped");
                lastNoDataDebug = millis();
            }
        }
    } else {
        // No ESP API connection - keep turbines stopped
        static unsigned long lastNoApiDebug = 0;
        if (millis() - lastNoApiDebug > 10000) {
            Serial.println("[WIND] No ESP API connection - turbines stopped");
            lastNoApiDebug = millis();
        }
    }
    
    sendAttractionCommand(slaveType, windState);
}

void GameManager::updateNuclear(uint8_t slaveType, const PowerPlant& plant) {
    sendAttractionCommand(slaveType, onOffByPercent(plant));
}

void GameManager::updateGas(uint8_t slaveType, const PowerPlant& plant) {
    // Gas powerplant expanded to 10 levels (commands 0x06 - 0x0F)
    // Mapping encoder percentage -> level in 10% bands (with OFF below 5%)
    if (plant.maxWatts <= 0.0f) {
        sendCmd2B(slaveType, CMD_OFF);
        return;
    }

    float pct = plant.powerPercentage.load();
    uint8_t gasCommand = CMD_OFF;

    if (pct < 0.05f) {
        gasCommand = CMD_OFF; // below 5% off
    } else {
        // Determine level 1..10
        int level = (int)floorf(pct * 10.0f); // 0..10
        if (level == 0) level = 1;            // ensure at least level 1 for >=5%
        if (level > 10) level = 10;
        // Map level to command code: level1->0x06 ... level10->0x0F
        gasCommand = 0x05 + level; // 0x06..0x0F
    }

    sendCmd2B(slaveType, gasCommand);

    static unsigned long lastGasDebug = 0;
    if (millis() - lastGasDebug > 3000) {
        if (gasCommand == CMD_OFF) {
            Serial.printf("[GAS] pct=%.1f%% -> OFF (cmd=0x%02X)\n", pct * 100.0f, gasCommand);
        } else {
            int level = gasCommand - 0x05; // 1..10
            Serial.printf("[GAS] pct=%.1f%% -> Level %d (cmd=0x%02X)\n", pct * 100.0f, level, gasCommand);
        }
        lastGasDebug = millis();
    }
}

void GameManager::updateHydro(uint8_t slaveType, const PowerPlant& plant) {
    // Hydro powerplant control based on server-provided hydro coefficient
    // 
    // The server sends production coefficients for hydro that reflect current hydro conditions 
    // (water flow, reservoir levels, etc.). When coefficient > HYDRO_COEFFICIENT_THRESHOLD, 
    // hydro turbines should run. This allows the hydro powerplant to respond to real-time 
    // water conditions from the game server rather than manual user control.
    
    uint8_t hydroState = 0;
    
    if (espApi) {
        float hydroCoefficient = getProductionCoefficientForType(SOURCE_HYDRO);
        hydroState = (hydroCoefficient > HYDRO_COEFFICIENT_THRESHOLD) ? 1 : 0;
        
        // Debug output to monitor hydro conditions and help tune parameters
        static unsigned long lastHydroDebug = 0;
        if (millis() - lastHydroDebug > 5000) { // Debug every 5 seconds
            Serial.printf("[HYDRO] Server coefficient=%.2f, threshold=%.2f -> %s\n",
                         hydroCoefficient, HYDRO_COEFFICIENT_THRESHOLD, 
                         hydroState ? "RUNNING" : "STOPPED");
            lastHydroDebug = millis();
        }
        
        if (hydroCoefficient == 0.0f) {
            // No hydro coefficient found or zero coefficient
            static unsigned long lastNoDataDebug = 0;
            if (millis() - lastNoDataDebug > 10000) { // Debug every 10 seconds
                Serial.println("[HYDRO] No hydro coefficient from server - turbines stopped");
                lastNoDataDebug = millis();
            }
        }
    } else {
        // No ESP API connection - keep turbines stopped
        static unsigned long lastNoApiDebug = 0;
        if (millis() - lastNoApiDebug > 10000) {
            Serial.println("[HYDRO] No ESP API connection - turbines stopped");
            lastNoApiDebug = millis();
        }
    }
    
    sendAttractionCommand(slaveType, hydroState);
}

void GameManager::updateHydroStorage(uint8_t slaveType, const PowerPlant& plant) {
    // Hydro storage powerplant with 5-level control similar to battery
    // Maps encoder percentage to 5 storage levels with middle stages
    // Uses the same encoder control as battery but different command mapping
    // 
    // The hydro storage acts as a battery-like device but with 5 visual levels:
    // - Level 1 (100% Full): Heavy discharging (producing power)
    // - Level 2 (75% Full): Light discharging  
    // - Level 3 (50% Full): Idle state (neutral)
    // - Level 4 (25% Full): Light charging (consuming power)
    // - Level 5 (0% Empty): Heavy charging
    
    if (plant.maxWatts <= 0.0f) {
        sendCmd2B(slaveType, CMD_OFF);
        return;
    }
    
    float powerPerPlant = computePowerPerPlant(plant);
    uint8_t cmd;
    
    // Normalize power to percentage of range for level determination
    float range = plant.maxWatts - plant.minWatts;
    float normalizedPower = (range > 0) ? powerPerPlant / (range * 0.5f) : 0.0f; // Scale to ±1.0
    
    // Map normalized power to 5 hydro storage levels
    // More gradual transitions than battery for better visual feedback
    if (normalizedPower <= -0.6f) {
        cmd = CMD_HYDRO_STORAGE_LEVEL_5; // 0% Empty - Red (Heavy Charging)
    } else if (normalizedPower <= -0.2f) {
        cmd = CMD_HYDRO_STORAGE_LEVEL_4; // 25% - Light Red (Light Charging)
    } else if (normalizedPower >= 0.6f) {
        cmd = CMD_HYDRO_STORAGE_LEVEL_1; // 100% Full - Green (Heavy Discharging)
    } else if (normalizedPower >= 0.2f) {
        cmd = CMD_HYDRO_STORAGE_LEVEL_2; // 75% - Light Green (Light Discharging)
    } else {
        cmd = CMD_HYDRO_STORAGE_LEVEL_3; // 50% - Orange (Idle)
    }
    
    // Send as pure 2B command [type, cmd4]
    sendCmd2B(slaveType, cmd);
    
    // Debug output
    static unsigned long lastHydroStorageDebug = 0;
    if (millis() - lastHydroStorageDebug > 3000) { // Debug every 3 seconds
        const char* levelName = "UNKNOWN";
        switch (cmd) {
            case CMD_HYDRO_STORAGE_LEVEL_1: levelName = "100% Full (Heavy Discharging)"; break;
            case CMD_HYDRO_STORAGE_LEVEL_2: levelName = "75% (Light Discharging)"; break;
            case CMD_HYDRO_STORAGE_LEVEL_3: levelName = "50% (Idle)"; break;
            case CMD_HYDRO_STORAGE_LEVEL_4: levelName = "25% (Light Charging)"; break;
            case CMD_HYDRO_STORAGE_LEVEL_5: levelName = "0% Empty (Heavy Charging)"; break;
        }
        Serial.printf("[HYDRO_STORAGE] Power=%.2fW, Normalized=%.2f -> %s (cmd=0x%02X)\n", 
                     powerPerPlant, normalizedPower, levelName, cmd);
        lastHydroStorageDebug = millis();
    }
}

void GameManager::updateCoal(uint8_t slaveType, const PowerPlant& plant) {
    sendAttractionCommand(slaveType, onOffByPercent(plant));
}

void GameManager::updateBattery(uint8_t slaveType, const PowerPlant& plant) {
    float powerPerPlant = computePowerPerPlant(plant);
    uint8_t cmd;
    if (powerPerPlant == 0.0f) {
        cmd = CMD_BATTERY_IDLE;
    } else if (powerPerPlant < 0.0f) {
        cmd = CMD_BATTERY_CHARGE;     // consume
    } else {
        cmd = CMD_BATTERY_DISCHARGE;  // produce
    }
    // Send as pure 2B command [type, cmd4]
    sendCmd2B(slaveType, cmd);
}

// Compute power per plant with a zero-centered deadband for symmetric ranges
float GameManager::computePowerPerPlant(const PowerPlant& plant) const {
    if (plant.maxWatts <= 0.0f) return 0.0f;
    const float pct = plant.powerPercentage.load();
    const float range = plant.maxWatts - plant.minWatts;
    float value = plant.minWatts + pct * range;

    // If range is symmetric around zero (min ~= -max), snap small offsets around 50% to 0
    const float symmetryTol = 0.001f * (fabsf(plant.maxWatts) + fabsf(plant.minWatts) + 1.0f);
    if (fabsf(plant.maxWatts + plant.minWatts) <= symmetryTol) {
        const float deadbandPct = 0.0025f; // 0.25% deadband around center
        if (fabsf(pct - 0.5f) <= deadbandPct) {
            return 0.0f;
        }
    }

    // Additionally, snap very small absolute values to 0 to counter rounding noise
    const float absTol = 0.002f * (fabsf(plant.maxWatts) + fabsf(plant.minWatts)); // 0.2% of sum magnitudes
    if (fabsf(value) <= absTol) {
        return 0.0f;
    }

    return value;
}
