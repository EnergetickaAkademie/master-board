#ifndef POWER_PLANT_CONFIG_H
#define POWER_PLANT_CONFIG_H

// Power Plant Configuration
// Based on Enak.py Source enumeration

// Source IDs from Enak.py
#define SOURCE_PHOTOVOLTAIC  1
#define SOURCE_WIND          2
#define SOURCE_NUCLEAR       3
#define SOURCE_GAS           4
#define SOURCE_HYDRO         5
#define SOURCE_HYDRO_STORAGE 6
#define SOURCE_COAL          7
#define SOURCE_BATTERY       8

// Power plant physical IDs (for reporting to server)
#define COAL_PLANT_ID     1001  // Coal power plant physical ID
#define GAS_PLANT_ID      1002  // Gas power plant physical ID
#define WIND_PLANT_ID     1003  // Wind power plant physical ID (example)
#define NUCLEAR_PLANT_ID  1004  // Nuclear power plant physical ID (example)
#define HYDRO_PLANT_ID    1005  // Hydro power plant physical ID (example)
#define SOLAR_PLANT_ID    1006  // Solar power plant physical ID (example)

// NOTE: Power plant min/max production values are now automatically 
// retrieved from the game server via the production ranges API endpoint.
// The GameManager will automatically update these values when the game
// scenario changes or when new coefficients are received.

// Update intervals (in milliseconds)
#define DISPLAY_UPDATE_INTERVAL_MS   100  // How often to update displays
#define POWER_PLANT_DEBUG_INTERVAL   1000 // How often to print debug info

#endif // POWER_PLANT_CONFIG_H
