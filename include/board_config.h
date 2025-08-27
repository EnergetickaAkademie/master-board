#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

// Master Board Configuration
// This board controls coal and gas power plants using encoders and displays

// Board identification for the ESP-API system
// BOARD_NAME is now defined via PlatformIO build flags for each board environment
// If not defined via build flags, use default fallback
#ifndef BOARD_NAME
#define BOARD_NAME "MasterBoard-001"
#endif

// BOARD_ID is defined via PlatformIO build flags (1-8)
// If not defined, use default ID
#ifndef BOARD_ID
#define BOARD_ID 1
#endif

#define BOARD_VERSION "1.0.0"

// Note: BOARD_GENERIC is defined in ESPGameAPI.h as enum BoardType

// API Server configuration  
//#define SERVER_URL "http://192.168.50.201"
// API_USERNAME and API_PASSWORD are now defined via PlatformIO build flags for each board environment
// If not defined via build flags, use default fallback
#ifndef API_USERNAME
#define API_USERNAME "board1"
#endif

#ifndef API_PASSWORD
#define API_PASSWORD "board123"
#endif

// Timing configurations (in milliseconds)
#define API_UPDATE_INTERVAL_MS 500          // How often to send data to server
#define COEFFICIENT_POLL_INTERVAL_MS 2000   // How often to request coefficients

#endif // BOARD_CONFIG_H
