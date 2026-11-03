#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct MidiMessage {
    std::uint64_t index = 0;
    double elapsedMs = 0.0;
    std::uint32_t driverTimestampMs = 0;
    std::vector<std::uint8_t> bytes;
    std::string rawHex;
    std::string statusHex;
    std::string messageType;
    int channel = -1;
    int data1 = -1;
    int data2 = -1;
    int noteNumber = -1;
    std::string noteName;
    int velocity = -1;
    int value = -1;
    std::string description;
    std::string runningStatus = "not_observable";
    std::string source = "WinMM";
    std::string portName;
};
