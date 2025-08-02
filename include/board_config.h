#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

// Master Board Configuration
// This board controls coal and gas power plants using encoders and displays

// Board identification for the ESP-API system
#define BOARD_NAME "MasterBoard-001"
#define BOARD_VERSION "1.0.0"

// Note: BOARD_GENERIC is defined in ESPGameAPI.h as enum BoardType

// API Server configuration  
#define SERVER_URL "http://192.168.50.201"
#define API_USERNAME "board1"
#define API_PASSWORD "board123"

// Timing configurations (in milliseconds)
#define API_UPDATE_INTERVAL_MS 500          // How often to send data to server
#define COEFFICIENT_POLL_INTERVAL_MS 2000   // How often to request coefficients

#endif // BOARD_CONFIG_H
