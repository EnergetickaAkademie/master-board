#pragma once
#include <stdint.h>

// Shared constants for Master Board <-> Retranslation Station communication

// UART Protocol Constants
namespace UartProtocol {
    // Frame synchronization bytes
    static constexpr uint8_t SYNC1 = 0xAA;
    static constexpr uint8_t SYNC2 = 0x55;
    
    // Command constants (4-bit commands on the wire)
    static constexpr uint8_t CMD_ON  = 0x01;  // turn ON
    static constexpr uint8_t CMD_OFF = 0x02;  // turn OFF
    
    // Frame structure constants
    static constexpr uint8_t MIN_FRAME_SIZE = 5; // SYNC1 + SYNC2 + LEN + CRC16_H + CRC16_L
    static constexpr uint8_t MAX_PAYLOAD_SIZE_MASTER = 250;
    static constexpr uint8_t MAX_PAYLOAD_SIZE_RETRANS = 100; // Smaller for ESP8266
}

// Protocol documentation:
//
// Packet format:
// [SYNC1][SYNC2][LEN][PAYLOAD...][CRC16_H][CRC16_L]
//
// SYNC1, SYNC2: 0xAA, 0x55 - frame synchronization bytes
// LEN: payload length (1-250 bytes for master, 1-100 for retranslation)
// PAYLOAD: actual data 
// CRC16: CRC16-CCITT of LEN + PAYLOAD
//
// For Master -> Retranslation: PAYLOAD = [slaveType, cmd4] pairs
// For Retranslation -> Master: PAYLOAD = [slaveType, amount] pairs
