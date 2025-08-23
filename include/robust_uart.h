#pragma once
#include <stdint.h>
#include <vector>

// Forward declaration
struct UartSlaveInfo;

// Robust UART protocol between Master Board and Retranslation Station
// 
// Packet format:
// [SYNC1][SYNC2][LEN][PAYLOAD...][CRC16_H][CRC16_L]
//
// SYNC1, SYNC2: 0xAA, 0x55 - frame synchronization bytes
// LEN: payload length (1-250 bytes)
// PAYLOAD: actual data 
// CRC16: CRC16-CCITT of LEN + PAYLOAD
//
// For Master -> Retranslation: PAYLOAD = [slaveType, cmd4] pairs
// For Retranslation -> Master: PAYLOAD = [slaveType, amount] pairs

class RobustUart {
private:
    static const uint8_t SYNC1 = 0xAA;
    static const uint8_t SYNC2 = 0x55;
    static const uint8_t MAX_PAYLOAD_SIZE = 250;
    static const uint8_t MIN_FRAME_SIZE = 5; // SYNC1 + SYNC2 + LEN + CRC16
    
    // RX state machine
    enum RxState : uint8_t {
        WAIT_SYNC1,
        WAIT_SYNC2, 
        READ_LEN, 
        READ_PAYLOAD,
        READ_CRC_H,
        READ_CRC_L,
        FRAME_READY
    };
    
    RxState rxState = WAIT_SYNC1;
    uint8_t rxLen = 0;
    uint8_t rxIndex = 0;
    uint8_t rxPayload[MAX_PAYLOAD_SIZE];
    uint16_t rxCrc = 0;
    
    // Statistics
    unsigned long framesReceived = 0;
    unsigned long crcErrors = 0;
    unsigned long syncErrors = 0;
    
    // CRC16-CCITT calculation
    static uint16_t crc16_ccitt(const uint8_t* data, size_t len);
    
public:
    RobustUart();
    
    // Process incoming byte, returns true if complete frame received
    bool processByte(uint8_t byte);
    
    // Get received payload after successful frame reception
    const uint8_t* getPayload() const { return rxPayload; }
    uint8_t getPayloadLength() const { return rxLen; }
    
    // Reset RX state machine (useful after errors)
    void resetRx();
    
    // Send a frame with given payload
    bool sendFrame(const uint8_t* payload, uint8_t payloadLen, 
                   void (*writeFunction)(const uint8_t*, size_t));
    
    // Statistics
    void printStats() const;
    unsigned long getFramesReceived() const { return framesReceived; }
    unsigned long getCrcErrors() const { return crcErrors; }
    unsigned long getSyncErrors() const { return syncErrors; }
};

// Helper functions for Master Board usage
namespace RobustUartHelpers {
    // Parse slave info from robust UART payload
    void parseSlaveInfo(const uint8_t* payload, uint8_t length, 
                       std::vector<UartSlaveInfo>& connectedSlaves);
    
    // Send command to retranslation station  
    void sendCommand(uint8_t slaveType, uint8_t cmd4, RobustUart& uart,
                    void (*writeFunction)(const uint8_t*, size_t));
}
