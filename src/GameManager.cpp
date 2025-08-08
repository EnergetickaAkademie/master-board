#include "GameManager.h"
#include <ESPGameAPI.h>
#include <Arduino.h>

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
            // Use building type as consumer ID for now
            // You might want to use the UID hash or another unique identifier
            uint32_t consumerId = static_cast<uint32_t>(building.second.buildingType);
            consumers.push_back({consumerId});
        }
    }
    
    return consumers;
}

void GameManager::updateUartPowerplants(const std::vector<UartSlaveInfo>& powerplants) {
    uartPowerplants = powerplants;
    
    // Update attraction states when powerplant info changes
    updateAttractionStates();
}

void GameManager::updateAttractionStates() {
    // Only update attractions every 1 second to avoid spam
    if (millis() - lastUartAttractionUpdate < 1000) {
        return;
    }
    
    // Check each powerplant type and control attractions
    for (const auto& uartPlant : uartPowerplants) {
        if (uartPlant.amount == 0) continue;
        
        // Find matching local powerplant to get power percentage
        bool found = false;
        for (size_t i = 0; i < powerPlantCount; i++) {
            if (static_cast<uint8_t>(powerPlants[i].plantType) == uartPlant.slaveType) {
                const auto& plant = powerPlants[i];
                uint8_t attractionState = 0; // Default off
                
                // Only turn on attraction if powerplant is enabled (max power > 0) and percentage > 50%
                if (plant.maxWatts > 0.0f && plant.powerPercentage.load() > 0.5f) {
                    attractionState = 1;
                }
                
                // Send attraction command (this will be called from main.cpp)
                extern void sendAttractionCommand(uint8_t slaveType, uint8_t state);
                sendAttractionCommand(uartPlant.slaveType, attractionState);
                
                found = true;
                break;
            }
        }
        
        if (!found) {
            // If no local powerplant controls this type, turn off attraction
            extern void sendAttractionCommand(uint8_t slaveType, uint8_t state);
            sendAttractionCommand(uartPlant.slaveType, 0);
        }
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
                    
                    // Calculate power per plant: min + (max - min) * percentage
                    float powerPerPlant = plant.minWatts + 
                                        (plant.powerPercentage.load()) * (plant.maxWatts - plant.minWatts);
                    
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
