#pragma once

#include <windows.h>

#include "MidiInput.h"
#include "MidiLog.h"
#include "MidiView.h"

#include <set>
#include <string>
#include <vector>

class MainWindow {
public:
    explicit MainWindow(HINSTANCE instance) : instance_(instance) {}
    bool create(int showCommand);
#ifdef MIDI_MONITOR_TESTING
    HWND windowHandleForTesting() const { return window_; }
    HWND logViewHandleForTesting() const { return listView_; }
    std::uint64_t totalReceivedForTesting() const { return log_.totalReceived(); }
    std::size_t topRowForTesting() const { return topRow_; }
    void injectShortMessageForTesting(std::vector<std::uint8_t> bytes, double elapsedMs) {
        MidiInputPacket packet;
        packet.bytes = std::move(bytes);
        packet.elapsedMs = elapsedMs;
        midiInput_.enqueueForTesting(std::move(packet), window_);
        scheduleMidiDrain();
    }
#endif

private:
    static LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK logViewProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT handleMessage(UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT handleLogViewMessage(UINT message, WPARAM wParam, LPARAM lParam);
    void createControls();
    void layout();
    void refreshPorts();
    void connectSelectedPort();
    void scheduleMidiDrain();
    void drainMidi();
    void resetLog();
    void saveCsv();
    void updateCounter();
    void rebuildVisibleRows();
    void updateListItemCount(bool redrawAll);
    void paintLogView();
    void updateLogScrollBar();
    int logRowsPerPage() const;
    void setTopRow(std::size_t row);
    void ensureRowVisible(std::size_t row);
    void selectViewRow(std::size_t row, bool ctrl, bool shift);
    void applyFilter();
    void sortByColumn(int column);
    void updateSortIndicator();
    bool visibleRowLess(std::size_t left, std::size_t right) const;
    void setStatus(const std::wstring& text, bool error = false);
    void copySelectedRows();
    bool isListAtBottom() const;
    std::wstring listText(std::size_t row, int column) const;

    HINSTANCE instance_ = nullptr;
    HWND window_ = nullptr;
    HWND inputLabel_ = nullptr;
    HWND inputCombo_ = nullptr;
    HWND refreshButton_ = nullptr;
    HWND statusLabel_ = nullptr;
    HWND filterLabel_ = nullptr;
    HWND filterCombo_ = nullptr;
    HWND runningStatusLabel_ = nullptr;
    HWND header_ = nullptr;
    HWND listView_ = nullptr;
    HWND counterLabel_ = nullptr;
    HWND resetButton_ = nullptr;
    HWND saveButton_ = nullptr;
    HWND footerLabel_ = nullptr;
    HWND footerLink_ = nullptr;
    HWND tooltip_ = nullptr;
    HFONT uiFont_ = nullptr;
    HFONT monoFont_ = nullptr;
    HCURSOR handCursor_ = nullptr;
    int dpi_ = 96;
    bool statusError_ = false;
    bool capacityWarningShown_ = false;
    bool midiBatchTimerArmed_ = false;
    MidiFilter filter_ = MidiFilter::All;
    MidiSortColumn sortColumn_ = MidiSortColumn::None;
    bool sortAscending_ = true;
    std::vector<std::size_t> visibleRows_;
    std::set<std::size_t> selectedViewRows_;
    std::size_t selectionAnchor_ = static_cast<std::size_t>(-1);
    std::size_t selectionCaret_ = static_cast<std::size_t>(-1);
    std::size_t topRow_ = 0;
    int wheelDeltaRemainder_ = 0;
    std::vector<MidiPortInfo> ports_;
    std::string activePortUtf8_;
    MidiInput midiInput_;
    MidiLog log_{100000};
};
