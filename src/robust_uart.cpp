#include "robust_uart.h"
#include "GameManager.h"
#include <Arduino.h>
#include <algorithm>

// CRC16-CCITT implementation (same as in com-prot)
uint16_t RobustUart::crc16_ccitt(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 0x8000) {
                crc = (crc << 1) ^ 0x1021;
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

RobustUart::RobustUart() {
    resetRx();
    memset(rxPayload, 0, sizeof(rxPayload)); // Initialize payload buffer
}

void RobustUart::resetRx() {
    rxState = WAIT_SYNC1;
    rxLen = 0;
    rxIndex = 0;
    rxCrc = 0;
}

bool RobustUart::processByte(uint8_t byte) {
    switch (rxState) {
        case WAIT_SYNC1:
            if (byte == SYNC1) {
                rxState = WAIT_SYNC2;
            } else {
                syncErrors++;
            }
            break;
            
        case WAIT_SYNC2:
            if (byte == SYNC2) {
                rxState = READ_LEN;
            } else {
                syncErrors++;
                rxState = (byte == SYNC1) ? WAIT_SYNC2 : WAIT_SYNC1;
            }
            break;
            
        case READ_LEN:
            if (byte > 0 && byte <= MAX_PAYLOAD_SIZE) {
                rxLen = byte;
                rxIndex = 0;
                rxState = READ_PAYLOAD;
            } else {
                // Invalid length, restart sync
                syncErrors++;
                resetRx();
            }
            break;
            
        case READ_PAYLOAD:
            rxPayload[rxIndex++] = byte;
            if (rxIndex >= rxLen) {
                rxState = READ_CRC_H;
            }
            break;
            
        case READ_CRC_H:
            rxCrc = (uint16_t)byte << 8;
            rxState = READ_CRC_L;
            break;
            
        case READ_CRC_L:
            rxCrc |= byte;
            
            // Calculate expected CRC over LEN + PAYLOAD
            {
                uint8_t crcData[MAX_PAYLOAD_SIZE + 1];
                crcData[0] = rxLen;
                memcpy(&crcData[1], rxPayload, rxLen);
                uint16_t expectedCrc = crc16_ccitt(crcData, rxLen + 1);
                
                if (rxCrc == expectedCrc) {
                    framesReceived++;
                    rxState = FRAME_READY;
                    return true; // Frame successfully received
                } else {
                    crcErrors++;
                    resetRx();
                }
            }
            break;
            
        case FRAME_READY:
            // Should not happen - caller should reset after processing
            resetRx();
            return processByte(byte); // Try to process this byte again
    }
    
    return false;
}

bool RobustUart::sendFrame(const uint8_t* payload, uint8_t payloadLen, 
                          void (*writeFunction)(const uint8_t*, size_t)) {
    if (payloadLen == 0 || payloadLen > MAX_PAYLOAD_SIZE || !writeFunction) {
        return false;
    }
    
    // Calculate CRC over LEN + PAYLOAD
    uint8_t crcData[MAX_PAYLOAD_SIZE + 1];
    crcData[0] = payloadLen;
    memcpy(&crcData[1], payload, payloadLen);
    uint16_t crc = crc16_ccitt(crcData, payloadLen + 1);
    
    // Build frame: [SYNC1][SYNC2][LEN][PAYLOAD...][CRC_H][CRC_L]
    uint8_t frame[MAX_PAYLOAD_SIZE + 6];
    frame[0] = SYNC1;
    frame[1] = SYNC2;
    frame[2] = payloadLen;
    memcpy(&frame[3], payload, payloadLen);
    frame[3 + payloadLen] = (crc >> 8) & 0xFF;
    frame[4 + payloadLen] = crc & 0xFF;
    
    writeFunction(frame, 5 + payloadLen);
    return true;
}

void RobustUart::printStats() const {
    Serial.printf("[RobustUART] Stats: Frames=%lu, CRC_errors=%lu, Sync_errors=%lu\n", 
                  framesReceived, crcErrors, syncErrors);
}

// Helper functions implementation
namespace RobustUartHelpers {
    
    void parseSlaveInfo(const uint8_t* payload, uint8_t length, 
                       std::vector<UartSlaveInfo>& connectedSlaves) {
        if (length % 2 != 0) {
            Serial.printf("[RobustUART] Invalid slave info length: %d\n", length);
            return;
        }
        
        for (uint8_t i = 0; i < length; i += 2) {
            uint8_t type = payload[i];
            uint8_t amount = payload[i + 1];
            
            bool found = false;
            for (auto& slave : connectedSlaves) {
                if (slave.slaveType == type) {
                    slave.amount = amount;
                    found = true;
                    break;
                }
            }
            
            if (!found) {
                UartSlaveInfo newSlave;
                newSlave.slaveType = type;
                newSlave.amount = amount;
                connectedSlaves.push_back(newSlave);
            }
            
            // Remove entry if amount is zero
            if (amount == 0) {
                connectedSlaves.erase(
                    std::remove_if(connectedSlaves.begin(), connectedSlaves.end(),
                                 [type](const UartSlaveInfo& s) { return s.slaveType == type; }),
                    connectedSlaves.end());
            }
            
            Serial.printf("[RobustUART] Slave Type %u: %u connected\n", type, amount);
        }
        
        // Update GameManager with new powerplant information
        GameManager::getInstance().updateUartPowerplants(connectedSlaves);
    }
    
    void sendCommand(uint8_t slaveType, uint8_t cmd4, RobustUart& uart,
                    void (*writeFunction)(const uint8_t*, size_t)) {
        uint8_t payload[2] = { slaveType, static_cast<uint8_t>(cmd4 & 0x0F) };
        uart.sendFrame(payload, 2, writeFunction);
    }
}
