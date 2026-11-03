#pragma once

#include <windows.h>

#include "MidiInput.h"
#include "MidiLog.h"

#include <string>
#include <vector>

class MainWindow {
public:
    explicit MainWindow(HINSTANCE instance) : instance_(instance) {}
    bool create(int showCommand);

private:
    static LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT handleMessage(UINT message, WPARAM wParam, LPARAM lParam);
    void createControls();
    void layout();
    void refreshPorts();
    void connectSelectedPort();
    void drainMidi();
    void resetLog();
    void saveCsv();
    void updateCounter();
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
    HWND runningStatusLabel_ = nullptr;
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
    std::vector<MidiPortInfo> ports_;
    std::string activePortUtf8_;
    MidiInput midiInput_;
    MidiLog log_{100000};
};
