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

// Power plant configuration - hardcoded min/max production values
// These represent the actual power plant capacity ranges
#define COAL_MIN_PRODUCTION_WATTS 0.0f      // Coal plant minimum output
#define COAL_MAX_PRODUCTION_WATTS 500.0f    // Coal plant maximum output  
#define GAS_MIN_PRODUCTION_WATTS 0.0f       // Gas plant minimum output
#define GAS_MAX_PRODUCTION_WATTS 300.0f     // Gas plant maximum output
// Min/max power ranges are determined by the game scenario
// Coefficients are updated in real-time from the game server

// Display and control assignments
#define COAL_ENCODER      1     // Encoder 1 controls coal power
#define GAS_ENCODER       2     // Encoder 2 controls gas power
#define COAL_DISPLAY      1     // Display 1 shows coal power output
#define GAS_DISPLAY       2     // Display 2 shows gas power output
#define COAL_BARGRAPH     1     // Bargraph 1 shows coal power level
#define GAS_BARGRAPH      2     // Bargraph 2 shows gas power level

// Game coefficient displays
#define COAL_COEFF_DISPLAY   5  // Display 5 shows coal coefficient
#define GAS_COEFF_DISPLAY    6  // Display 6 shows gas coefficient
#define COAL_COEFF_BARGRAPH  5  // Bargraph 5 shows coal coefficient
#define GAS_COEFF_BARGRAPH   6  // Bargraph 6 shows gas coefficient

// Update intervals
#define DISPLAY_UPDATE_INTERVAL_MS   100  // How often to update displays
#define POWER_PLANT_DEBUG_INTERVAL   1000 // How often to print debug info

#endif // POWER_PLANT_CONFIG_H
