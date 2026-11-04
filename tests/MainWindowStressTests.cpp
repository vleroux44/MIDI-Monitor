#include <windows.h>
#include <ole2.h>
#include <UIAutomation.h>
#include <commctrl.h>

#include "MainWindow.h"

#include <atomic>
#include <iostream>
#include <thread>

int main() {
    MainWindow window(GetModuleHandleW(nullptr));
    if (!window.create(SW_HIDE)) {
        std::cerr << "FAILED - window creation\n";
        return 1;
    }

    for (int index = 0; index < 225; ++index) {
        const auto note = static_cast<std::uint8_t>(48 + index % 24);
        const double elapsed = static_cast<double>(index) * 2.0;
        window.injectShortMessageForTesting({0x90, note, 100}, elapsed);
        window.injectShortMessageForTesting({0x80, note, 0}, elapsed + 1.0);
    }

    const ULONGLONG deadline = GetTickCount64() + 3000;
    MSG message{};
    while (GetTickCount64() < deadline && window.totalReceivedForTesting() < 450) {
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        Sleep(1);
    }

    std::atomic<bool> automationReady{false};
    std::atomic<bool> automationFinished{false};
    const HWND windowHandle = window.windowHandleForTesting();
    std::thread automationThread([&] {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        IUIAutomation* automation = nullptr;
        IUIAutomationElement* root = nullptr;
        IUIAutomationCondition* condition = nullptr;
        if (SUCCEEDED(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER,
                                       IID_PPV_ARGS(&automation)))
            && SUCCEEDED(automation->ElementFromHandle(windowHandle, &root))
            && SUCCEEDED(automation->CreateTrueCondition(&condition))) {
            automationReady.store(true, std::memory_order_release);
            IUIAutomationElementArray* elements = nullptr;
            root->FindAll(TreeScope_Subtree, condition, &elements);
            if (elements) elements->Release();
        } else {
            automationReady.store(true, std::memory_order_release);
        }
        if (condition) condition->Release();
        if (root) root->Release();
        if (automation) automation->Release();
        CoUninitialize();
        automationFinished.store(true, std::memory_order_release);
    });
    while (!automationReady.load(std::memory_order_acquire)) {
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        Sleep(1);
    }

    HWND listView = FindWindowExW(window.windowHandleForTesting(), nullptr, WC_LISTVIEWW, nullptr);
    for (int index = 0; index < 12; ++index) {
        SendMessageW(listView, WM_VSCROLL, MAKEWPARAM(SB_PAGEUP, 0), 0);
    }
    const ULONGLONG automationDeadline = GetTickCount64() + 5000;
    while (!automationFinished.load(std::memory_order_acquire)
           && GetTickCount64() < automationDeadline) {
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        Sleep(1);
    }

    const auto received = window.totalReceivedForTesting();
    const bool responsive = !IsHungAppWindow(window.windowHandleForTesting());
    const bool automationCompleted = automationFinished.load(std::memory_order_acquire);
    if (!automationCompleted) {
        std::cerr << "FAILED - UI Automation enumeration did not complete\n";
        TerminateProcess(GetCurrentProcess(), 1);
    }
    automationThread.join();
    DestroyWindow(window.windowHandleForTesting());
    if (received != 450) {
        std::cerr << "FAILED - received " << received << "/450 messages\n";
        return 1;
    }
    if (!responsive) {
        std::cerr << "FAILED - window stopped responding during UI Automation scroll\n";
        return 1;
    }
    std::cout << "OK - 450 messages, UI Automation and upward scrolling\n";
    return 0;
}
