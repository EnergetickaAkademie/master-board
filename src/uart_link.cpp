#include <Arduino.h>
#include "uart_link.h"

// This must be provided by your main file (kept as-is)
extern HardwareSerial uartComm; // e.g. HardwareSerial uartComm(1);

void sendCmd2B(uint8_t slaveType, uint8_t cmd4) {
  uint8_t frame[2] = { slaveType, static_cast<uint8_t>(cmd4 & 0x0F) };
  uartComm.write(frame, 2);
  // Optional:
  // Serial.printf("[UART->RT] TX type=%u cmd=0x%02X\n", frame[0], frame[1]);
}

void sendAttractionCommand(uint8_t slaveType, uint8_t state) {
  static constexpr uint8_t CMD_ON  = 0x01;
  static constexpr uint8_t CMD_OFF = 0x02;
  sendCmd2B(slaveType, state ? CMD_ON : CMD_OFF);
}
