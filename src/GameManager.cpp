#include "GameManager.h"
#include <ESPGameAPI.h>
#include <Arduino.h>

// Implementation of methods that need ESPGameAPI types

std::vector<ConnectedPowerPlant> GameManager::getConnectedPowerPlants() {
    std::vector<ConnectedPowerPlant> plants;
    for (size_t i = 0; i < powerPlantCount; i++) {
        const auto& plant = powerPlants[i];
        plants.push_back({plant.plantId, plant.powerSetting.load()});
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
