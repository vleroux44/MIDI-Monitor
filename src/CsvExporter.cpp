#include "CsvExporter.h"

#include <fstream>
#include <iomanip>
#include <sstream>

namespace {
std::string numberOrEmpty(int value) {
    return value < 0 ? std::string{} : std::to_string(value);
}
}

std::string CsvExporter::escape(const std::string& field) {
    if (field.find_first_of(";\"\r\n") == std::string::npos) return field;
    std::string escaped = "\"";
    for (const char ch : field) {
        if (ch == '"') escaped += "\"\"";
        else escaped += ch;
    }
    escaped += '"';
    return escaped;
}

bool CsvExporter::exportFile(const std::filesystem::path& path,
                             const std::vector<MidiMessage>& messages,
                             std::string& error) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        error = "Impossible d'ouvrir le fichier pour écriture.";
        return false;
    }
    out.write("\xEF\xBB\xBF", 3);
    out << "index;elapsed_ms;driver_timestamp_ms;raw_hex;status_hex;data1;data2;"
           "message_type;channel;note_number;note_name;velocity;value;description;"
           "running_status;source;port_name\r\n";
    out << std::fixed << std::setprecision(3);
    for (const auto& message : messages) {
        out << message.index << ';'
            << message.elapsedMs << ';'
            << message.driverTimestampMs << ';'
            << escape(message.rawHex) << ';'
            << escape(message.statusHex) << ';'
            << numberOrEmpty(message.data1) << ';'
            << numberOrEmpty(message.data2) << ';'
            << escape(message.messageType) << ';'
            << numberOrEmpty(message.channel) << ';'
            << numberOrEmpty(message.noteNumber) << ';'
            << escape(message.noteName) << ';'
            << numberOrEmpty(message.velocity) << ';'
            << numberOrEmpty(message.value) << ';'
            << escape(message.description) << ';'
            << escape(message.runningStatus) << ';'
            << escape(message.source) << ';'
            << escape(message.portName) << "\r\n";
        if (!out) {
            error = "Erreur pendant l'écriture du fichier CSV.";
            return false;
        }
    }
    out.flush();
    if (!out) {
        error = "Impossible de finaliser le fichier CSV.";
        return false;
    }
    return true;
}
