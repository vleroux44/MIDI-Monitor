#pragma once

#include "MidiMessage.h"

#include <string>

enum class MidiFilter {
    All,
    Notes,
    ControlChange,
    PitchBend,
    SysEx,
    Aftertouch,
    ProgramChange,
    SystemRealtime,
    SystemCommon,
    OtherOrError
};

enum class MidiSortColumn {
    None = -1,
    Timestamp = 0,
    Hex = 1,
    Type = 2,
    Channel = 3,
    Parameter = 4,
    Value = 5
};

bool midiMessageMatchesFilter(const MidiMessage& message, MidiFilter filter);
std::string midiParameterText(const MidiMessage& message);
std::string midiValueText(const MidiMessage& message);
int compareMidiMessages(const MidiMessage& left, const MidiMessage& right,
                        MidiSortColumn column);
