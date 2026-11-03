#include "MidiByteStreamParser.h"
#include "MidiDecoder.h"

std::vector<ParsedStreamMessage> MidiByteStreamParser::feed(const std::uint8_t* data,
                                                             std::size_t size) {
    std::vector<ParsedStreamMessage> output;
    for (std::size_t i = 0; i < size; ++i) processByte(data[i], output);
    return output;
}

void MidiByteStreamParser::reset() {
    runningStatus_ = 0;
    pendingStatus_ = 0;
    expectedData_ = 0;
    pendingUsesRunningStatus_ = false;
    pendingData_.clear();
    inSysEx_ = false;
    sysEx_.clear();
}

void MidiByteStreamParser::emitPending(std::vector<ParsedStreamMessage>& output) {
    ParsedStreamMessage message;
    message.bytes.push_back(pendingStatus_);
    message.bytes.insert(message.bytes.end(), pendingData_.begin(), pendingData_.end());
    if (pendingUsesRunningStatus_) {
        message.receivedBytes = pendingData_;
        message.usedRunningStatus = true;
    } else {
        message.receivedBytes = message.bytes;
    }
    output.push_back(std::move(message));
    pendingStatus_ = 0;
    expectedData_ = 0;
    pendingUsesRunningStatus_ = false;
    pendingData_.clear();
}

void MidiByteStreamParser::emitMalformedPending(const char* reason,
                                                 std::vector<ParsedStreamMessage>& output) {
    if (!pendingStatus_) return;
    ParsedStreamMessage message;
    message.bytes.push_back(pendingStatus_);
    message.bytes.insert(message.bytes.end(), pendingData_.begin(), pendingData_.end());
    message.receivedBytes = pendingUsesRunningStatus_ ? pendingData_ : message.bytes;
    message.usedRunningStatus = pendingUsesRunningStatus_;
    message.malformed = true;
    message.error = reason;
    output.push_back(std::move(message));
    pendingStatus_ = 0;
    expectedData_ = 0;
    pendingUsesRunningStatus_ = false;
    pendingData_.clear();
}

void MidiByteStreamParser::startStatus(std::uint8_t status,
                                       std::vector<ParsedStreamMessage>& output) {
    if (pendingStatus_) emitMalformedPending("Message interrompu par un nouveau statut", output);
    if (status < 0xF0) {
        runningStatus_ = status;
        pendingStatus_ = status;
        expectedData_ = MidiDecoder::expectedMessageLength(status) - 1;
        return;
    }

    runningStatus_ = 0; // Les statuts System Common et SysEx annulent le running status.
    if (status == 0xF0) {
        inSysEx_ = true;
        sysEx_.assign(1, status);
        return;
    }
    const int length = MidiDecoder::expectedMessageLength(status);
    if (length <= 1) {
        ParsedStreamMessage message;
        message.bytes = {status};
        message.receivedBytes = message.bytes;
        if (status == 0xF7) {
            message.malformed = true;
            message.error = "Fin SysEx sans début SysEx";
        }
        output.push_back(std::move(message));
    } else {
        pendingStatus_ = status;
        expectedData_ = length - 1;
    }
}

void MidiByteStreamParser::processByte(std::uint8_t byte,
                                       std::vector<ParsedStreamMessage>& output) {
    if (byte >= 0xF8) { // Real-Time: n'interrompt ni message courant, ni SysEx, ni running status.
        ParsedStreamMessage message;
        message.bytes = {byte};
        message.receivedBytes = message.bytes;
        output.push_back(std::move(message));
        return;
    }

    if (inSysEx_) {
        if (byte == 0xF7) {
            sysEx_.push_back(byte);
            ParsedStreamMessage message;
            message.bytes = sysEx_;
            message.receivedBytes = message.bytes;
            output.push_back(std::move(message));
            sysEx_.clear();
            inSysEx_ = false;
            return;
        }
        if (byte & 0x80) {
            ParsedStreamMessage malformed;
            malformed.bytes = sysEx_;
            malformed.receivedBytes = malformed.bytes;
            malformed.malformed = true;
            malformed.error = "SysEx interrompu par un statut";
            output.push_back(std::move(malformed));
            sysEx_.clear();
            inSysEx_ = false;
            startStatus(byte, output);
            return;
        }
        sysEx_.push_back(byte);
        return;
    }

    if (byte & 0x80) {
        startStatus(byte, output);
        return;
    }

    if (!pendingStatus_) {
        if (!runningStatus_) {
            ParsedStreamMessage malformed;
            malformed.bytes = {byte};
            malformed.receivedBytes = malformed.bytes;
            malformed.malformed = true;
            malformed.error = "Octet de données sans statut ni running status";
            output.push_back(std::move(malformed));
            return;
        }
        pendingStatus_ = runningStatus_;
        expectedData_ = MidiDecoder::expectedMessageLength(runningStatus_) - 1;
        pendingUsesRunningStatus_ = true;
    }
    pendingData_.push_back(byte);
    if (static_cast<int>(pendingData_.size()) == expectedData_) emitPending(output);
}

std::vector<ParsedStreamMessage> MidiByteStreamParser::flush() {
    std::vector<ParsedStreamMessage> output;
    if (inSysEx_) {
        ParsedStreamMessage message;
        message.bytes = sysEx_;
        message.receivedBytes = message.bytes;
        message.malformed = true;
        message.error = "SysEx incomplet en fin de flux";
        output.push_back(std::move(message));
        inSysEx_ = false;
        sysEx_.clear();
    }
    emitMalformedPending("Message incomplet en fin de flux", output);
    return output;
}
