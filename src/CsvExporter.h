#pragma once

#include "MidiMessage.h"

#include <filesystem>
#include <string>
#include <vector>

class CsvExporter {
public:
    static bool exportFile(const std::filesystem::path& path,
                           const std::vector<MidiMessage>& messages,
                           std::string& error);
    static std::string escape(const std::string& field);
};
