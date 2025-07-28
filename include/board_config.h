#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

// Master Board Configuration
// This board controls coal and gas power plants using encoders and displays

// Board identification for the ESP-API system
#define BOARD_NAME "MasterBoard-001"
#define BOARD_VERSION "1.0.0"

// API Server configuration
#define API_SERVER_URL "http://192.168.50.201/coreapi"

// Timing configurations (in milliseconds)
#define API_UPDATE_INTERVAL_MS 2000         // How often to send data to server
#define COEFFICIENT_POLL_INTERVAL_MS 5000   // How often to request coefficients
#define DISPLAY_UPDATE_INTERVAL_MS 100      // How often to update displays/bargraphs
#define POWER_PLANT_DEBUG_INTERVAL 1000     // How often to print debug info

// Power plant configuration
#define MAX_POWER_OUTPUT_WATTS 1000.0f      // Maximum power output per plant
#define MIN_POWER_OUTPUT_WATTS 0.0f         // Minimum power output per plant

// Hardware assignments for power plants
#define COAL_ENCODER_INDEX 1     // Encoder 1 controls coal power plant
#define GAS_ENCODER_INDEX 2      // Encoder 2 controls gas power plant
#define COAL_DISPLAY_INDEX 1     // Display 1 shows coal output
#define GAS_DISPLAY_INDEX 2      // Display 2 shows gas output
#define COAL_BARGRAPH_INDEX 1    // Bargraph 1 shows coal level
#define GAS_BARGRAPH_INDEX 2     // Bargraph 2 shows gas level

// Status displays
#define COAL_COEFF_DISPLAY_INDEX 5   // Display 5 shows coal coefficient
#define GAS_COEFF_DISPLAY_INDEX 6    // Display 6 shows gas coefficient
#define COAL_COEFF_BARGRAPH_INDEX 5  // Bargraph 5 shows coal coefficient
#define GAS_COEFF_BARGRAPH_INDEX 6   // Bargraph 6 shows gas coefficient

#endif // BOARD_CONFIG_H
