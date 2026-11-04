#pragma once

#include <windows.h>
#include <mmsystem.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

struct MidiInputPacket {
    enum class Kind { ShortMessage, SysEx, Error };
    Kind kind = Kind::ShortMessage;
    std::vector<std::uint8_t> bytes;
    double elapsedMs = 0.0;
    std::uint32_t driverTimestampMs = 0;
    std::wstring error;
};

struct MidiPortInfo {
    UINT id = 0;
    std::wstring name;
};

class MidiInput {
public:
    MidiInput();
    ~MidiInput();
    MidiInput(const MidiInput&) = delete;
    MidiInput& operator=(const MidiInput&) = delete;

    static std::vector<MidiPortInfo> enumerate();
    bool open(UINT deviceId, HWND notificationWindow, std::wstring& error);
    void close();
    void resetClock();
    void clearQueue();
    std::vector<MidiInputPacket> drain(std::size_t maximum = 4096);
    std::uint64_t takeDroppedCount();
    bool isOpen() const { return handle_ != nullptr; }
#ifdef MIDI_MONITOR_TESTING
    void enqueueForTesting(MidiInputPacket packet, HWND notificationWindow) {
        notificationWindow_ = notificationWindow;
        enqueue(std::move(packet));
    }
#endif

    static constexpr UINT NotificationMessage = WM_APP + 42;

private:
    static void CALLBACK callback(HMIDIIN input, UINT message, DWORD_PTR instance,
                                  DWORD_PTR parameter1, DWORD_PTR parameter2);
    void onCallback(HMIDIIN input, UINT message, DWORD_PTR parameter1,
                    DWORD_PTR parameter2, std::int64_t qpcNow);
    void enqueue(MidiInputPacket packet);
    static std::wstring errorText(MMRESULT result);

    static constexpr std::size_t SysExBufferCount = 4;
    static constexpr std::size_t SysExBufferSize = 4096;
    static constexpr std::size_t QueueCapacity = 65536;

    HMIDIIN handle_ = nullptr;
    HWND notificationWindow_ = nullptr;
    std::array<std::array<char, SysExBufferSize>, SysExBufferCount> sysExData_{};
    std::array<MIDIHDR, SysExBufferCount> sysExHeaders_{};
    std::mutex queueMutex_;
    std::deque<MidiInputPacket> queue_;
    std::atomic<bool> notificationPending_{false};
    std::atomic<bool> closing_{false};
    std::atomic<std::uint64_t> dropped_{0};
    std::atomic<std::int64_t> originQpc_{0};
    std::int64_t qpcFrequency_ = 1;
};
