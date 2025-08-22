#include "GameManager.h"
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

// Implementation of methods that need ESPGameAPI types

std::vector<ConnectedPowerPlant> GameManager::getConnectedPowerPlants() {
    std::vector<ConnectedPowerPlant> plants;
    // Only report powerplants that are actually connected via UART
    for (const auto& uartPlant : uartPowerplants) {
        if (uartPlant.amount > 0) {
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
                        // Generic ON/OFF: send ON if enabled (>50%), else OFF
                        uint8_t attractionState = (plant.maxWatts > 0.0f && plant.powerPercentage.load() > 0.5f) ? 1 : 0;
                        Serial.printf("number of connected powerplants for type %u: %u\n", uartPlant.slaveType, uartPlant.amount);
                        sendAttractionCommand(uartPlant.slaveType, attractionState);
                    } break;
                }
                hasLocal = true;
                break;
            }
        }

        if (!hasLocal) {
            // No local control for this UART type: turn off attraction
            sendAttractionCommand(uartPlant.slaveType, 0); // OFF (cmd4=0x02)
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
                        return 0.0f;
                    }

                    // Calculate power per plant with center snap
                    float powerPerPlant = computePowerPerPlant(plant);

                    // Total power = power per plant * number of connected plants
                    return powerPerPlant * uartPlant.amount;
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
    sendAttractionCommand(slaveType, onOffByPercent(plant));
}

void GameManager::updateNuclear(uint8_t slaveType, const PowerPlant& plant) {
    sendAttractionCommand(slaveType, onOffByPercent(plant));
}

void GameManager::updateGas(uint8_t slaveType, const PowerPlant& plant) {
    sendAttractionCommand(slaveType, onOffByPercent(plant));
}

void GameManager::updateHydro(uint8_t slaveType, const PowerPlant& plant) {
    sendAttractionCommand(slaveType, onOffByPercent(plant));
}

void GameManager::updateHydroStorage(uint8_t slaveType, const PowerPlant& plant) {
    sendAttractionCommand(slaveType, onOffByPercent(plant));
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
