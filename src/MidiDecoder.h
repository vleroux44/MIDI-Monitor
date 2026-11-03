#pragma once

#include "MidiMessage.h"

#include <cstdint>
#include <string>
#include <vector>

class MidiDecoder {
public:
    static MidiMessage decode(const std::vector<std::uint8_t>& bytes,
                              double elapsedMs = 0.0,
                              std::uint32_t driverTimestampMs = 0,
                              const std::string& portName = {},
                              const std::string& runningStatus = "not_observable",
                              const std::string& source = "WinMM");
    static std::string bytesToHex(const std::vector<std::uint8_t>& bytes);
    static std::string noteName(int noteNumber);
    static int expectedMessageLength(std::uint8_t status);
};
