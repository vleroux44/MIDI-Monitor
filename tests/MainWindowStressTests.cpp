#include <windows.h>
#include <ole2.h>
#include <UIAutomation.h>
#include <commctrl.h>

#include "MainWindow.h"

#include <atomic>
#include <iostream>
#include <thread>

int main() {
    constexpr int MessageCount = 10000;
    MainWindow window(GetModuleHandleW(nullptr));
    if (!window.create(SW_HIDE)) {
        std::cerr << "FAILED - window creation\n";
        return 1;
    }

    std::atomic<bool> stopAutomation{false};
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
            while (!stopAutomation.load(std::memory_order_acquire)) {
                IUIAutomationElementArray* elements = nullptr;
                root->FindAll(TreeScope_Subtree, condition, &elements);
                if (elements) elements->Release();
                Sleep(5);
            }
        } else {
            automationReady.store(true, std::memory_order_release);
        }
        if (condition) condition->Release();
        if (root) root->Release();
        if (automation) automation->Release();
        CoUninitialize();
        automationFinished.store(true, std::memory_order_release);
    });

    MSG message{};
    while (!automationReady.load(std::memory_order_acquire)) {
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        Sleep(1);
    }

    for (int index = 0; index < MessageCount / 2; ++index) {
        const auto note = static_cast<std::uint8_t>(48 + index % 24);
        const double elapsed = static_cast<double>(index) * 2.0;
        window.injectShortMessageForTesting({0x90, note, 100}, elapsed);
        window.injectShortMessageForTesting({0x80, note, 0}, elapsed + 1.0);
    }

    const ULONGLONG deadline = GetTickCount64() + 5000;
    while (GetTickCount64() < deadline
           && window.totalReceivedForTesting() < static_cast<std::uint64_t>(MessageCount)) {
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        Sleep(1);
    }

    HWND listView = window.logViewHandleForTesting();
    const std::size_t topBeforeScroll = window.topRowForTesting();
    for (int index = 0; index < 12; ++index) {
        SendMessageW(listView, WM_VSCROLL, MAKEWPARAM(SB_PAGEUP, 0), 0);
    }
    stopAutomation.store(true, std::memory_order_release);
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
    const std::size_t topAfterScroll = window.topRowForTesting();
    const bool responsive = !IsHungAppWindow(window.windowHandleForTesting());
    const bool automationCompleted = automationFinished.load(std::memory_order_acquire);
    if (!automationCompleted) {
        std::cerr << "FAILED - UI Automation enumeration did not complete\n";
        TerminateProcess(GetCurrentProcess(), 1);
    }
    automationThread.join();
    DestroyWindow(window.windowHandleForTesting());
    if (received != MessageCount) {
        std::cerr << "FAILED - received " << received << '/' << MessageCount << " messages\n";
        return 1;
    }
    if (topAfterScroll >= topBeforeScroll) {
        std::cerr << "FAILED - upward scrolling did not change the visible range\n";
        return 1;
    }
    if (!responsive) {
        std::cerr << "FAILED - window stopped responding during UI Automation scroll\n";
        return 1;
    }
    std::cout << "OK - " << MessageCount
              << " messages, UI Automation and upward scrolling\n";
    return 0;
}
