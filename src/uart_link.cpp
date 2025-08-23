#include <Arduino.h>
#include "uart_link.h"
#include "robust_uart.h"

// These must be provided by your main file
extern HardwareSerial uartComm; // e.g. HardwareSerial uartComm(1);
extern RobustUart robustUart;    // Robust UART protocol handler
extern void uartWriteFunction(const uint8_t* data, size_t len); // Defined in main.cpp

void sendCmd2B(uint8_t slaveType, uint8_t cmd4) {
  RobustUartHelpers::sendCommand(slaveType, cmd4, robustUart, uartWriteFunction);
  // Optional debug:
  //Serial.printf("[RobustUART->RT] TX type=%u cmd=0x%02X\n", slaveType, cmd4 & 0x0F);
}

void sendAttractionCommand(uint8_t slaveType, uint8_t state) {
  static constexpr uint8_t CMD_ON  = 0x01;
  static constexpr uint8_t CMD_OFF = 0x02;
  sendCmd2B(slaveType, state ? CMD_ON : CMD_OFF);
}
