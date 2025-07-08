#ifndef SEGMENT_H
#define SEGMENT_H

#include <LedController.h>
#include "Peripheral.h"

class Max7219Segment : public Peripheral {
public:
    // Constructor takes dataPin, clockPin, csPin (chip select/load), and total number of daisy-chained MAX7219 devices
    Max7219Segment(int dataPin, int clkPin, int csPin, int numDevices);

    void init() override; // Renamed from begin()
    void setBrightness(int intensity); // Set brightness for all devices (0-15)
    void clearAll(); // Clears all digits on all devices

    // Set a character on a specific digit across the entire chain of displays.
    // overallDigitIndex: 0 for the first digit of the first display, 8 for the first digit of the second display, etc.
    // character: The character to display (LedController handles common ones like 0-9, A-F, H,E,L,P,space,dash).
    // decimalPoint: true to light up the decimal point for this digit.
    void setChar(int overallDigitIndex, char character, bool decimalPoint);

    // Display a string across all daisy-chained displays.
    // The string will be truncated if it's longer than the total number of digits.
    void printString(const char* text);

    // Display a number across all daisy-chained displays.
    void printNumber(long number);

    // Display an integer (0-9999) on a specific 4-digit logical display.
    // logicalDisplayNum: 0 to (totalNumDevices * 2) - 1
    // Each MAX7219 hosts two 4-digit logical displays.
    void displayPower(int logicalDisplayNum, int value);

    // Display a float on a specific 4-digit logical display.
    // If value is 0.0 to 999.9, displays as XXX.X (e.g., "123.4", " 23.4", "  3.4").
    // If value is 1000.0 to 9999.0, displays as an integer YYYY.
    // Other values (negative, too large) will display "----".
    void displayPower(int logicalDisplayNum, float value);

    // Advanced: Directly access LedController instance if needed for features not wrapped
    LedController& getLedControllerInstance();

private:
    LedController lc;
    int totalNumDevices; // Stores the total number of MAX7219 devices in the chain
    int totalDigits;     // Total number of digits available (totalNumDevices * 8)
};

#endif // SEGMENT_H
