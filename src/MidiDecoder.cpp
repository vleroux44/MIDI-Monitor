#include "MidiDecoder.h"

#include <array>
#include <iomanip>
#include <sstream>

namespace {
std::string hexByte(std::uint8_t value) {
    std::ostringstream out;
    out << std::uppercase << std::hex << std::setw(2) << std::setfill('0')
        << static_cast<int>(value);
    return out.str();
}

std::string ccName(int cc) {
    switch (cc) {
    case 1: return "Modulation";
    case 7: return "Volume";
    case 10: return "Pan";
    case 11: return "Expression";
    case 64: return "Sustain";
    case 120: return "All Sound Off";
    case 121: return "Reset All Controllers";
    case 123: return "All Notes Off";
    default: return {};
    }
}
}

std::string MidiDecoder::bytesToHex(const std::vector<std::uint8_t>& bytes) {
    std::ostringstream out;
    out << std::uppercase << std::hex << std::setfill('0');
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        if (i) out << ' ';
        out << std::setw(2) << static_cast<int>(bytes[i]);
    }
    return out.str();
}

std::string MidiDecoder::noteName(int noteNumber) {
    static constexpr std::array<const char*, 12> names{
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    if (noteNumber < 0 || noteNumber > 127) return {};
    return std::string(names[static_cast<std::size_t>(noteNumber % 12)])
        + std::to_string(noteNumber / 12 - 1);
}

int MidiDecoder::expectedMessageLength(std::uint8_t status) {
    if (status < 0x80) return 0;
    if (status < 0xF0) {
        const auto type = status & 0xF0;
        return (type == 0xC0 || type == 0xD0) ? 2 : 3;
    }
    switch (status) {
    case 0xF1: return 2;
    case 0xF2: return 3;
    case 0xF3: return 2;
    case 0xF6: case 0xF7:
    case 0xF8: case 0xF9: case 0xFA: case 0xFB:
    case 0xFC: case 0xFD: case 0xFE: case 0xFF: return 1;
    case 0xF0: return -1;
    default: return 1;
    }
}

MidiMessage MidiDecoder::decode(const std::vector<std::uint8_t>& bytes,
                                double elapsedMs,
                                std::uint32_t driverTimestampMs,
                                const std::string& portName,
                                const std::string& runningStatus,
                                const std::string& source) {
    MidiMessage result;
    result.elapsedMs = elapsedMs;
    result.driverTimestampMs = driverTimestampMs;
    result.bytes = bytes;
    result.rawHex = bytesToHex(bytes);
    result.runningStatus = runningStatus;
    result.source = source;
    result.portName = portName;
    if (bytes.empty()) {
        result.messageType = "Malformed";
        result.description = "Message vide";
        return result;
    }

    const std::uint8_t status = bytes[0];
    result.statusHex = hexByte(status);
    if (status < 0x80) {
        result.messageType = "Malformed";
        result.description = "Octet de données sans statut";
        return result;
    }
    const int expected = expectedMessageLength(status);
    if (expected > 0 && bytes.size() < static_cast<std::size_t>(expected)) {
        result.messageType = "Incomplete";
        result.description = "Message incomplet (" + std::to_string(bytes.size()) + "/"
            + std::to_string(expected) + " octets)";
        return result;
    }

    std::ostringstream text;
    if (status < 0xF0) {
        const int type = status & 0xF0;
        result.channel = (status & 0x0F) + 1;
        if (bytes.size() > 1) result.data1 = bytes[1];
        if (bytes.size() > 2) result.data2 = bytes[2];
        switch (type) {
        case 0x80:
            result.messageType = "Note Off";
            result.noteNumber = result.data1;
            result.noteName = noteName(result.noteNumber);
            result.velocity = result.data2;
            text << "Note Off - Ch " << result.channel << " - " << result.noteName
                 << " (" << result.noteNumber << ") - Vel " << result.velocity;
            break;
        case 0x90:
            result.noteNumber = result.data1;
            result.noteName = noteName(result.noteNumber);
            result.velocity = result.data2;
            if (result.velocity == 0) {
                result.messageType = "Note Off (Note On vel 0)";
                text << "Note Off (via Note On vélocité 0) - Ch " << result.channel;
            } else {
                result.messageType = "Note On";
                text << "Note On - Ch " << result.channel;
            }
            text << " - " << result.noteName << " (" << result.noteNumber
                 << ") - Vel " << result.velocity;
            break;
        case 0xA0:
            result.messageType = "Polyphonic Aftertouch";
            result.noteNumber = result.data1;
            result.noteName = noteName(result.noteNumber);
            result.value = result.data2;
            text << "Poly Aftertouch - Ch " << result.channel << " - " << result.noteName
                 << " (" << result.noteNumber << ") - Value " << result.value;
            break;
        case 0xB0: {
            result.messageType = "Control Change";
            result.value = result.data2;
            const auto name = ccName(result.data1);
            text << "CC " << result.data1;
            if (!name.empty()) text << " - " << name;
            text << " - Ch " << result.channel << " - Value " << result.value;
            break;
        }
        case 0xC0:
            result.messageType = "Program Change";
            result.value = result.data1;
            text << "Program Change - Ch " << result.channel << " - Program " << result.value;
            break;
        case 0xD0:
            result.messageType = "Channel Pressure";
            result.value = result.data1;
            text << "Channel Pressure - Ch " << result.channel << " - Value " << result.value;
            break;
        case 0xE0:
            result.messageType = "Pitch Bend";
            result.value = result.data1 | (result.data2 << 7);
            text << "Pitch Bend - Ch " << result.channel << " - Value " << result.value;
            if (result.value == 8192) text << " (centre)";
            break;
        }
    } else {
        switch (status) {
        case 0xF0:
            result.messageType = "System Exclusive";
            text << "SysEx - " << bytes.size() << " octets";
            if (bytes.back() != 0xF7) text << " (incomplet)";
            break;
        case 0xF1:
            result.messageType = "MIDI Time Code Quarter Frame";
            result.value = bytes[1];
            text << "MIDI Time Code Quarter Frame - Value " << result.value;
            break;
        case 0xF2:
            result.messageType = "Song Position Pointer";
            result.value = bytes[1] | (bytes[2] << 7);
            text << "Song Position Pointer - Value " << result.value;
            break;
        case 0xF3:
            result.messageType = "Song Select";
            result.value = bytes[1];
            text << "Song Select - Value " << result.value;
            break;
        case 0xF6: result.messageType = "Tune Request"; text << "Tune Request"; break;
        case 0xF7: result.messageType = "End of SysEx"; text << "End of SysEx inattendu"; break;
        case 0xF8: result.messageType = "Timing Clock"; text << "MIDI Timing Clock"; break;
        case 0xFA: result.messageType = "Start"; text << "Start"; break;
        case 0xFB: result.messageType = "Continue"; text << "Continue"; break;
        case 0xFC: result.messageType = "Stop"; text << "Stop"; break;
        case 0xFE: result.messageType = "Active Sensing"; text << "Active Sensing"; break;
        case 0xFF: result.messageType = "System Reset"; text << "System Reset"; break;
        default:
            result.messageType = "Reserved System Message";
            text << "Message système réservé 0x" << hexByte(status);
            break;
        }
    }
    result.description = text.str();
    return result;
}
