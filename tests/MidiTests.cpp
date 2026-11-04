#include "CsvExporter.h"
#include "MidiByteStreamParser.h"
#include "MidiDecoder.h"
#include "MidiView.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
int checks = 0;

void expect(bool condition, const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}

void decoderTests() {
    auto noteOn = MidiDecoder::decode({0x90, 0x3C, 0x64});
    expect(noteOn.messageType == "Note On", "Note On type");
    expect(noteOn.channel == 1 && noteOn.noteName == "C4" && noteOn.velocity == 100,
           "Note On fields");
    expect(noteOn.rawHex == "90 3C 64", "Upper-case hexadecimal");

    auto noteOff = MidiDecoder::decode({0x82, 0x3C, 0x00});
    expect(noteOff.messageType == "Note Off" && noteOff.channel == 3, "Note Off");

    auto zero = MidiDecoder::decode({0x90, 0x3C, 0x00});
    expect(zero.messageType == "Note Off", "Zero velocity interpretation");
    expect(zero.description == "Note Off - Ch 1 - C4 (60) - Vel 0",
           "Zero velocity description remains concise");
    expect(zero.statusHex == "90", "Original zero velocity status retained");

    auto cc = MidiDecoder::decode({0xB2, 0x40, 0x7F});
    expect(cc.channel == 3 && cc.data1 == 64 && cc.value == 127, "Control Change");
    expect(cc.description.find("Sustain") != std::string::npos, "CC name");

    auto program = MidiDecoder::decode({0xC4, 0x2A});
    expect(program.messageType == "Program Change" && program.value == 42,
           "Two-byte Program Change");

    auto bend = MidiDecoder::decode({0xE0, 0x00, 0x40});
    expect(bend.value == 8192 && bend.description.find("centre") != std::string::npos,
           "Pitch Bend centre");

    auto clock = MidiDecoder::decode({0xF8});
    expect(clock.messageType == "Timing Clock", "MIDI Clock");

    auto sysex = MidiDecoder::decode({0xF0, 0x7D, 0x01, 0xF7});
    expect(sysex.messageType == "System Exclusive", "SysEx decode");
}

void parserTests() {
    MidiByteStreamParser parser;
    auto first = parser.feed({0x90, 0x3C});
    expect(first.empty(), "Incomplete message retained between blocks");
    auto second = parser.feed({0x64, 0x40, 0x64});
    expect(second.size() == 2, "Completed plus running-status messages");
    expect(!second[0].usedRunningStatus, "Explicit status not classified as running");
    expect(second[1].usedRunningStatus, "Real running status detected");
    expect(second[1].bytes == std::vector<std::uint8_t>({0x90, 0x40, 0x64}),
           "Reconstructed running-status message");
    expect(second[1].receivedBytes == std::vector<std::uint8_t>({0x40, 0x64}),
           "Actually received bytes kept distinct");

    parser.reset();
    auto explicitMessages = parser.feed({0x90, 0x3C, 0x64, 0x90, 0x40, 0x64});
    expect(explicitMessages.size() == 2 && !explicitMessages[0].usedRunningStatus
           && !explicitMessages[1].usedRunningStatus,
           "Repeated explicit status never called running status");

    parser.reset();
    auto realTime = parser.feed({0x90, 0x3C, 0xF8, 0x64});
    expect(realTime.size() == 2, "Real-Time interleaving count");
    expect(realTime[0].bytes == std::vector<std::uint8_t>({0xF8}), "Real-Time emitted immediately");
    expect(realTime[1].bytes == std::vector<std::uint8_t>({0x90, 0x3C, 0x64}),
           "Real-Time does not interrupt channel message");

    parser.reset();
    auto sysEx = parser.feed({0xF0, 0x7D, 0xF8, 0x01, 0xF7});
    expect(sysEx.size() == 2 && sysEx[0].bytes[0] == 0xF8,
           "Real-Time interleaved inside SysEx");
    expect(sysEx[1].bytes == std::vector<std::uint8_t>({0xF0, 0x7D, 0x01, 0xF7}),
           "SysEx boundaries");

    parser.reset();
    auto beforeCommon = parser.feed({0x90, 0x3C, 0x64, 0xF6, 0x40, 0x64});
    expect(beforeCommon.size() == 4, "System Common and orphan data emitted");
    expect(beforeCommon[2].malformed && beforeCommon[3].malformed,
           "System Common cancels running status");

    parser.reset();
    parser.feed({0x90, 0x3C});
    auto interrupted = parser.feed({0xF1, 0x01});
    expect(interrupted.size() == 2 && interrupted[0].malformed,
           "Interrupted incomplete message reported");

    parser.reset();
    parser.feed({0xF0, 0x01});
    auto flushed = parser.flush();
    expect(flushed.size() == 1 && flushed[0].malformed, "Incomplete SysEx reported on flush");
}

void csvTests() {
    const auto path = std::filesystem::temp_directory_path() / "midi_monitor_test.csv";
    auto message = MidiDecoder::decode({0x90, 0x3C, 0x64}, 12.345, 12, "Port; Test");
    message.index = 1;
    message.description += " \"ok\"";
    std::string error;
    expect(CsvExporter::exportFile(path, {message}, error), "CSV export succeeds");
    std::ifstream input(path, std::ios::binary);
    const std::string contents((std::istreambuf_iterator<char>(input)), {});
    expect(contents.size() > 3 && static_cast<unsigned char>(contents[0]) == 0xEF
           && static_cast<unsigned char>(contents[1]) == 0xBB
           && static_cast<unsigned char>(contents[2]) == 0xBF, "UTF-8 BOM");
    expect(contents.find("index;elapsed_ms;driver_timestamp_ms") != std::string::npos,
           "CSV header");
    expect(contents.find("\"Port; Test\"") != std::string::npos, "CSV separator escaping");
    expect(contents.find("\"\"ok\"\"") != std::string::npos, "CSV quote escaping");
    expect(contents.find("not_observable") != std::string::npos,
           "WinMM running status limitation exported");
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
}

void viewTests() {
    auto note = MidiDecoder::decode({0x90, 0x3C, 0x64}, 2.0);
    auto cc = MidiDecoder::decode({0xB0, 0x07, 0x40}, 1.0);
    auto bend = MidiDecoder::decode({0xE0, 0x00, 0x40}, 3.0);
    auto sysex = MidiDecoder::decode({0xF0, 0x7D, 0x01, 0xF7}, 4.0);
    auto clock = MidiDecoder::decode({0xF8}, 5.0);

    expect(midiMessageMatchesFilter(note, MidiFilter::Notes), "Notes filter");
    expect(!midiMessageMatchesFilter(note, MidiFilter::ControlChange), "Notes excluded from CC filter");
    expect(midiMessageMatchesFilter(cc, MidiFilter::ControlChange), "CC filter");
    expect(midiMessageMatchesFilter(bend, MidiFilter::PitchBend), "Pitch filter");
    expect(midiMessageMatchesFilter(sysex, MidiFilter::SysEx), "SysEx filter");
    expect(midiMessageMatchesFilter(clock, MidiFilter::SystemRealtime), "Real-time filter");
    expect(midiParameterText(note) == "C4 (60)", "Note parameter column");
    expect(midiParameterText(cc) == "CC 7 - Volume", "CC parameter column");
    expect(midiValueText(note) == "100", "Velocity value column");
    expect(midiValueText(bend) == "8192 (centre)", "Pitch value column");
    expect(compareMidiMessages(cc, note, MidiSortColumn::Timestamp) < 0,
           "Numeric timestamp sorting");
    expect(compareMidiMessages(note, cc, MidiSortColumn::Channel) == 0,
           "Numeric channel sorting");
    expect(compareMidiMessages(cc, note, MidiSortColumn::Parameter) < 0,
           "Numeric parameter sorting");
}
}

int main() {
    try {
        decoderTests();
        parserTests();
        csvTests();
        viewTests();
        std::cout << "OK - " << checks << " checks\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAILED after " << checks << " checks: " << error.what() << '\n';
        return 1;
    }
}
