#pragma once
#include <stdint.h>

// 2-byte UART frames to the retranslation station: [slaveType, cmd4]
void sendCmd2B(uint8_t slaveType, uint8_t cmd4);

// Convenience: ON/OFF (maps to cmd4 0x01 / 0x02)
void sendAttractionCommand(uint8_t slaveType, uint8_t state);
