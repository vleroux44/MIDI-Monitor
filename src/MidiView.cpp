#include "MidiView.h"

#include <algorithm>

namespace {
template <typename T>
int compareValues(const T& left, const T& right) {
    if (left < right) return -1;
    if (right < left) return 1;
    return 0;
}

int displayedValue(const MidiMessage& message) {
    return message.velocity >= 0 ? message.velocity : message.value;
}

bool isError(const MidiMessage& message) {
    return message.messageType == "Malformed" || message.messageType == "Incomplete"
        || message.messageType == "Reserved System Message";
}
}

bool midiMessageMatchesFilter(const MidiMessage& message, MidiFilter filter) {
    if (filter == MidiFilter::All) return true;
    if (message.bytes.empty() || isError(message)) return filter == MidiFilter::OtherOrError;

    const auto status = message.bytes.front();
    const auto family = status & 0xF0;
    switch (filter) {
    case MidiFilter::Notes: return family == 0x80 || family == 0x90;
    case MidiFilter::ControlChange: return family == 0xB0;
    case MidiFilter::PitchBend: return family == 0xE0;
    case MidiFilter::SysEx: return status == 0xF0;
    case MidiFilter::Aftertouch: return family == 0xA0 || family == 0xD0;
    case MidiFilter::ProgramChange: return family == 0xC0;
    case MidiFilter::SystemRealtime: return status >= 0xF8;
    case MidiFilter::SystemCommon: return status >= 0xF1 && status <= 0xF7;
    case MidiFilter::OtherOrError:
        return status < 0x80 || (status >= 0xF0 && status != 0xF0
            && !(status >= 0xF1 && status <= 0xF7) && status < 0xF8);
    case MidiFilter::All: return true;
    }
    return false;
}

std::string midiParameterText(const MidiMessage& message) {
    if (message.noteNumber >= 0) {
        return message.noteName + " (" + std::to_string(message.noteNumber) + ")";
    }
    if (message.messageType == "Control Change") {
        std::string result = "CC " + std::to_string(message.data1);
        if (!message.controllerName.empty()) result += " - " + message.controllerName;
        return result;
    }
    if (message.messageType == "System Exclusive") {
        return std::to_string(message.bytes.size()) + " octets";
    }
    if (isError(message)) return message.description;
    return {};
}

std::string midiValueText(const MidiMessage& message) {
    const int value = displayedValue(message);
    if (value < 0) return {};
    std::string result = std::to_string(value);
    if (message.messageType == "Pitch Bend" && value == 8192) result += " (centre)";
    return result;
}

int compareMidiMessages(const MidiMessage& left, const MidiMessage& right,
                        MidiSortColumn column) {
    switch (column) {
    case MidiSortColumn::Timestamp: return compareValues(left.elapsedMs, right.elapsedMs);
    case MidiSortColumn::Hex: return compareValues(left.bytes, right.bytes);
    case MidiSortColumn::Type: return compareValues(left.messageType, right.messageType);
    case MidiSortColumn::Channel: return compareValues(left.channel, right.channel);
    case MidiSortColumn::Parameter:
        if (left.noteNumber >= 0 || right.noteNumber >= 0)
            return compareValues(left.noteNumber, right.noteNumber);
        if (left.messageType == "Control Change" || right.messageType == "Control Change")
            return compareValues(left.data1, right.data1);
        if (left.messageType == "System Exclusive" || right.messageType == "System Exclusive")
            return compareValues(left.bytes.size(), right.bytes.size());
        return compareValues(midiParameterText(left), midiParameterText(right));
    case MidiSortColumn::Value: return compareValues(displayedValue(left), displayedValue(right));
    case MidiSortColumn::None: return compareValues(left.index, right.index);
    }
    return 0;
}
