#include "MidiInput.h"
#include "MidiDecoder.h"

#include <algorithm>

MidiInput::MidiInput() {
    LARGE_INTEGER frequency{};
    QueryPerformanceFrequency(&frequency);
    qpcFrequency_ = frequency.QuadPart > 0 ? frequency.QuadPart : 1;
    resetClock();
}

MidiInput::~MidiInput() {
    close();
}

std::vector<MidiPortInfo> MidiInput::enumerate() {
    std::vector<MidiPortInfo> ports;
    const UINT count = midiInGetNumDevs();
    for (UINT id = 0; id < count; ++id) {
        MIDIINCAPSW caps{};
        if (midiInGetDevCapsW(id, &caps, sizeof(caps)) == MMSYSERR_NOERROR) {
            ports.push_back({id, caps.szPname});
        }
    }
    return ports;
}

std::wstring MidiInput::errorText(MMRESULT result) {
    wchar_t text[MAXERRORLENGTH]{};
    if (midiInGetErrorTextW(result, text, MAXERRORLENGTH) == MMSYSERR_NOERROR) return text;
    return L"Erreur WinMM " + std::to_wstring(result);
}

bool MidiInput::open(UINT deviceId, HWND notificationWindow, std::wstring& error) {
    close();
    notificationWindow_ = notificationWindow;
    closing_.store(false, std::memory_order_release);
    clearQueue();
    dropped_.store(0, std::memory_order_relaxed);
    resetClock();

    MMRESULT result = midiInOpen(&handle_, deviceId,
        reinterpret_cast<DWORD_PTR>(&MidiInput::callback),
        reinterpret_cast<DWORD_PTR>(this), CALLBACK_FUNCTION);
    if (result != MMSYSERR_NOERROR) {
        handle_ = nullptr;
        error = errorText(result);
        return false;
    }

    for (std::size_t i = 0; i < SysExBufferCount; ++i) {
        auto& header = sysExHeaders_[i];
        ZeroMemory(&header, sizeof(header));
        header.lpData = sysExData_[i].data();
        header.dwBufferLength = static_cast<DWORD>(sysExData_[i].size());
        result = midiInPrepareHeader(handle_, &header, sizeof(header));
        if (result != MMSYSERR_NOERROR) {
            error = L"Préparation du buffer SysEx : " + errorText(result);
            close();
            return false;
        }
        result = midiInAddBuffer(handle_, &header, sizeof(header));
        if (result != MMSYSERR_NOERROR) {
            error = L"Ajout du buffer SysEx : " + errorText(result);
            close();
            return false;
        }
    }

    result = midiInStart(handle_);
    if (result != MMSYSERR_NOERROR) {
        error = L"Démarrage de la capture : " + errorText(result);
        close();
        return false;
    }
    return true;
}

void MidiInput::close() {
    closing_.store(true, std::memory_order_release);
    if (handle_) {
        midiInStop(handle_);
        midiInReset(handle_);
        for (auto& header : sysExHeaders_) {
            if (header.dwFlags & MHDR_PREPARED) {
                midiInUnprepareHeader(handle_, &header, sizeof(header));
            }
            ZeroMemory(&header, sizeof(header));
        }
        midiInClose(handle_);
        handle_ = nullptr;
    }
    notificationWindow_ = nullptr;
    notificationPending_.store(false, std::memory_order_release);
}

void MidiInput::resetClock() {
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    originQpc_.store(now.QuadPart, std::memory_order_release);
}

void MidiInput::clearQueue() {
    std::lock_guard lock(queueMutex_);
    queue_.clear();
    notificationPending_.store(false, std::memory_order_release);
}

void MidiInput::enqueue(MidiInputPacket packet) {
    {
        std::lock_guard lock(queueMutex_);
        if (queue_.size() >= QueueCapacity) {
            dropped_.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        queue_.push_back(std::move(packet));
    }
    if (!notificationPending_.exchange(true, std::memory_order_acq_rel)) {
        const HWND window = notificationWindow_;
        if (window) PostMessageW(window, NotificationMessage, 0, 0);
    }
}

std::vector<MidiInputPacket> MidiInput::drain(std::size_t maximum) {
    std::vector<MidiInputPacket> result;
    bool hasMore = false;
    {
        std::lock_guard lock(queueMutex_);
        const std::size_t count = std::min(maximum, queue_.size());
        result.reserve(count);
        for (std::size_t i = 0; i < count; ++i) {
            result.push_back(std::move(queue_.front()));
            queue_.pop_front();
        }
        notificationPending_.store(false, std::memory_order_release);
        hasMore = !queue_.empty();
    }
    if (hasMore && !notificationPending_.exchange(true, std::memory_order_acq_rel)) {
        const HWND window = notificationWindow_;
        if (window) PostMessageW(window, NotificationMessage, 0, 0);
    }
    return result;
}

std::uint64_t MidiInput::takeDroppedCount() {
    return dropped_.exchange(0, std::memory_order_acq_rel);
}

void CALLBACK MidiInput::callback(HMIDIIN input, UINT message, DWORD_PTR instance,
                                  DWORD_PTR parameter1, DWORD_PTR parameter2) {
    auto* self = reinterpret_cast<MidiInput*>(instance);
    if (!self) return;
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now); // Timestamp pris dès l'entrée du callback.
    self->onCallback(input, message, parameter1, parameter2, now.QuadPart);
}

void MidiInput::onCallback(HMIDIIN input, UINT message, DWORD_PTR parameter1,
                           DWORD_PTR parameter2, std::int64_t qpcNow) {
    if (message != MIM_DATA && message != MIM_MOREDATA
        && message != MIM_LONGDATA && message != MIM_LONGERROR && message != MIM_ERROR) return;

    MidiInputPacket packet;
    packet.elapsedMs = static_cast<double>(qpcNow - originQpc_.load(std::memory_order_acquire))
        * 1000.0 / static_cast<double>(qpcFrequency_);
    packet.driverTimestampMs = static_cast<std::uint32_t>(parameter2);

    if (message == MIM_DATA || message == MIM_MOREDATA || message == MIM_ERROR) {
        const DWORD packed = static_cast<DWORD>(parameter1);
        const std::uint8_t status = static_cast<std::uint8_t>(packed & 0xFF);
        int length = MidiDecoder::expectedMessageLength(status);
        if (length < 1) length = 1;
        length = std::min(length, 3);
        packet.bytes.reserve(static_cast<std::size_t>(length));
        for (int i = 0; i < length; ++i) {
            packet.bytes.push_back(static_cast<std::uint8_t>((packed >> (8 * i)) & 0xFF));
        }
        if (message == MIM_ERROR) {
            packet.kind = MidiInputPacket::Kind::Error;
            packet.error = L"Message MIDI court invalide signalé par WinMM";
        }
        enqueue(std::move(packet));
        return;
    }

    auto* header = reinterpret_cast<MIDIHDR*>(parameter1);
    if (!header) return;
    packet.kind = message == MIM_LONGERROR ? MidiInputPacket::Kind::Error
                                           : MidiInputPacket::Kind::SysEx;
    const auto* begin = reinterpret_cast<const std::uint8_t*>(header->lpData);
    packet.bytes.assign(begin, begin + header->dwBytesRecorded);
    if (message == MIM_LONGERROR) packet.error = L"Message SysEx invalide ou incomplet signalé par WinMM";
    enqueue(std::move(packet));

    if (!closing_.load(std::memory_order_acquire) && input == handle_) {
        header->dwBytesRecorded = 0;
        const MMRESULT result = midiInAddBuffer(input, header, sizeof(*header));
        if (result != MMSYSERR_NOERROR) {
            MidiInputPacket errorPacket;
            errorPacket.kind = MidiInputPacket::Kind::Error;
            errorPacket.error = L"Impossible de réutiliser un buffer SysEx : " + errorText(result);
            errorPacket.elapsedMs = packet.elapsedMs;
            errorPacket.driverTimestampMs = packet.driverTimestampMs;
            enqueue(std::move(errorPacket));
        }
    }
}
