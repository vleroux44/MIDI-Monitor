#include "MidiInput.h"

#include <algorithm>
#include <chrono>
#include <cwctype>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {
std::wstring lower(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
    return value;
}
}

int main() {
    const auto inputs = MidiInput::enumerate();
    auto input = std::find_if(inputs.begin(), inputs.end(), [](const MidiPortInfo& port) {
        return lower(port.name).find(L"loopmidi") != std::wstring::npos;
    });
    if (input == inputs.end()) {
        std::cout << "SKIP - aucun port d'entrée loopMIDI\n";
        return 77;
    }

    UINT outputId = UINT_MAX;
    const UINT outputCount = midiOutGetNumDevs();
    for (UINT id = 0; id < outputCount; ++id) {
        MIDIOUTCAPSW caps{};
        if (midiOutGetDevCapsW(id, &caps, sizeof(caps)) == MMSYSERR_NOERROR
            && lower(caps.szPname) == lower(input->name)) {
            outputId = id;
            break;
        }
    }
    if (outputId == UINT_MAX) {
        std::cout << "SKIP - aucune sortie loopMIDI correspondant à l'entrée\n";
        return 77;
    }

    MidiInput capture;
    std::wstring error;
    if (!capture.open(input->id, nullptr, error)) {
        std::wcerr << L"ÉCHEC - ouverture entrée loopMIDI : " << error << L'\n';
        return 1;
    }
    HMIDIOUT output = nullptr;
    const MMRESULT openResult = midiOutOpen(&output, outputId, 0, 0, CALLBACK_NULL);
    if (openResult != MMSYSERR_NOERROR) {
        std::cerr << "ÉCHEC - ouverture sortie loopMIDI : " << openResult << '\n';
        return 1;
    }

    // 90 3C 64 puis 80 3C 00. Le statut se trouve dans l'octet de poids faible WinMM.
    const MMRESULT noteOnResult = midiOutShortMsg(output, 0x00643C90u);
    const MMRESULT noteOffResult = midiOutShortMsg(output, 0x00003C80u);
    midiOutClose(output);
    if (noteOnResult != MMSYSERR_NOERROR || noteOffResult != MMSYSERR_NOERROR) {
        std::cerr << "ÉCHEC - envoi des messages loopMIDI\n";
        return 1;
    }

    std::vector<MidiInputPacket> packets;
    for (int attempt = 0; attempt < 50 && packets.size() < 2; ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        auto batch = capture.drain();
        packets.insert(packets.end(), std::make_move_iterator(batch.begin()),
                       std::make_move_iterator(batch.end()));
    }
    capture.close();

    std::vector<std::vector<std::uint8_t>> received;
    for (const auto& packet : packets) {
        if (packet.kind == MidiInputPacket::Kind::ShortMessage) received.push_back(packet.bytes);
    }
    const auto noteOn = std::find(received.begin(), received.end(),
                                  std::vector<std::uint8_t>{0x90, 0x3C, 0x64});
    const auto noteOff = std::find(received.begin(), received.end(),
                                   std::vector<std::uint8_t>{0x80, 0x3C, 0x00});
    if (noteOn == received.end() || noteOff == received.end() || noteOn > noteOff) {
        std::cerr << "ÉCHEC - Note On/Off non reçus dans l'ordre attendu\n";
        return 1;
    }
    std::wcout << L"OK - capture WinMM réelle sur " << input->name << L"\n";
    return 0;
}
