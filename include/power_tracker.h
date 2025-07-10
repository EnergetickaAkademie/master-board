#ifndef POWER_TRACKER_H
#define POWER_TRACKER_H
#include <Arduino.h>
#include <map>
#include <vector>
//needs to be singleton


uint32_t hashString(const String& s)
{
  const uint32_t FNV_PRIME   = 0x01000193UL; // 16777619
  uint32_t       hash        = 0x811C9DC5UL; // offset basis

  for (size_t i = 0; i < s.length(); ++i) {
    hash ^= (uint8_t)s[i];   // XOR byte into the low 8 bits
    hash *= FNV_PRIME;       // multiply by prime (wrap-around is intentional)
  }
  return hash;               // 0…4 294 967 295
}

class PowerTracker {
private:
    std::map<uint8_t, int32_t> buildingConsumption; // Maps building id to power consumption
    std::map<uint8_t, int32_t> powerPlantConsumption; // Maps power plant id to power consumption

    std::map<uint32_t, uint8_t> buildings; // uid hash to building type
    std::map<uint32_t, uint8_t> powerPlants; // uid hash to power plant type

    void addBuilding(const String& uid, uint8_t buildingType) {
        // Add building to the list and initialize its consumption
        uint32_t id = hashString(uid);
        buildings[id] = buildingType;
    }

    void removeBuilding(const String& uid) {
        // Remove building from the list
        uint32_t id = hashString(uid);
        buildings.erase(id);
    }

public:
    PowerTracker() {}
    ~PowerTracker() {}
    static PowerTracker& getInstance() {
        static PowerTracker instance; 
        return instance;
    }
    PowerTracker(const PowerTracker&) = delete; // Prevent copy construction
    PowerTracker& operator=(const PowerTracker&) = delete; // Prevent assignment
    PowerTracker(PowerTracker&&) = delete; // Prevent move construction
    PowerTracker& operator=(PowerTracker&&) = delete; // Prevent move assignment


    // Callback methods for NFC building events
    static void onNewBuilding(uint8_t buildingType, const String& uid) {
        PowerTracker& instance = getInstance();
        instance.addBuilding(uid, buildingType);
        Serial.println("PowerTracker: New building added - Type: " + String(buildingType) + ", UID: " + uid);
    }

    static void onDeleteBuilding(uint8_t buildingType, const String& uid) {
        PowerTracker& instance = getInstance();
        instance.removeBuilding(uid);
        Serial.println("PowerTracker: Building removed - Type: " + String(buildingType) + ", UID: " + uid);
    }

    int getOverallConsumption() const {
        int totalConsumption = 0;
        for (const auto& pair : buildingConsumption) {
            totalConsumption += pair.second;
        }
        return totalConsumption;
    }

    int getOverallProduction() const {
        int totalProduction = 0;
        for (const auto& pair : powerPlantConsumption) {
            totalProduction += pair.second;
        }
        return totalProduction;
    }

    int getNetConsumption() const {
        return getOverallProduction() - getOverallConsumption();
    }

    void updateBuildingsConsumption(const std::map<uint8_t, int32_t>& consumptions) {
        for (const auto& it : consumptions) {
            buildingConsumption[it.first] = it.second;
        }
    }

    void updatePowerPlantsProduction(const std::map<uint8_t, int32_t>& productions) {
        for (const auto& it : productions) {
            powerPlantConsumption[it.first] = it.second;
        }
    }


};
#endif // POWER_TRACKER_H

