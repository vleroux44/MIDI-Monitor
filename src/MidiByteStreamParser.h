#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct ParsedStreamMessage {
    std::vector<std::uint8_t> bytes;
    std::vector<std::uint8_t> receivedBytes;
    bool usedRunningStatus = false;
    bool malformed = false;
    std::string error;
};

class MidiByteStreamParser {
public:
    std::vector<ParsedStreamMessage> feed(const std::uint8_t* data, std::size_t size);
    std::vector<ParsedStreamMessage> feed(const std::vector<std::uint8_t>& data) {
        return feed(data.data(), data.size());
    }
    std::vector<ParsedStreamMessage> flush();
    void reset();

private:
    void processByte(std::uint8_t byte, std::vector<ParsedStreamMessage>& output);
    void startStatus(std::uint8_t status, std::vector<ParsedStreamMessage>& output);
    void emitPending(std::vector<ParsedStreamMessage>& output);
    void emitMalformedPending(const char* reason, std::vector<ParsedStreamMessage>& output);

    std::uint8_t runningStatus_ = 0;
    std::uint8_t pendingStatus_ = 0;
    int expectedData_ = 0;
    bool pendingUsesRunningStatus_ = false;
    std::vector<std::uint8_t> pendingData_;
    bool inSysEx_ = false;
    std::vector<std::uint8_t> sysEx_;
};
