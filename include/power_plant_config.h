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

// Power plant configuration - hardcoded min/max production values
// These represent the actual power plant capacity ranges
#define COAL_MIN_PRODUCTION_WATTS 0.0f      // Coal plant minimum output
#define COAL_MAX_PRODUCTION_WATTS 500.0f    // Coal plant maximum output  
#define GAS_MIN_PRODUCTION_WATTS 0.0f       // Gas plant minimum output
#define GAS_MAX_PRODUCTION_WATTS 300.0f     // Gas plant maximum output

// Example additional power plant capacities
#define WIND_MIN_PRODUCTION_WATTS 0.0f      // Wind plant minimum output
#define WIND_MAX_PRODUCTION_WATTS 400.0f    // Wind plant maximum output
#define NUCLEAR_MIN_PRODUCTION_WATTS 100.0f // Nuclear plant minimum output
#define NUCLEAR_MAX_PRODUCTION_WATTS 800.0f // Nuclear plant maximum output
#define HYDRO_MIN_PRODUCTION_WATTS 0.0f     // Hydro plant minimum output
#define HYDRO_MAX_PRODUCTION_WATTS 600.0f   // Hydro plant maximum output
#define SOLAR_MIN_PRODUCTION_WATTS 0.0f     // Solar plant minimum output
#define SOLAR_MAX_PRODUCTION_WATTS 350.0f   // Solar plant maximum output

// Min/max power ranges are determined by the game scenario
// Coefficients are updated in real-time from the game server

// Update intervals (in milliseconds)
#define DISPLAY_UPDATE_INTERVAL_MS   100  // How often to update displays
#define POWER_PLANT_DEBUG_INTERVAL   1000 // How often to print debug info

#endif // POWER_PLANT_CONFIG_H
