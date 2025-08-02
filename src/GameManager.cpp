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
    // Return empty vector for now - can be expanded for consumption tracking
    return {};
}
