#include "MainWindow.h"

#include "CsvExporter.h"
#include "MidiDecoder.h"

#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <sstream>

namespace {
constexpr int IdInputCombo = 1001;
constexpr int IdRefresh = 1002;
constexpr int IdReset = 1003;
constexpr int IdSave = 1004;
constexpr int IdFooterLink = 1005;

std::wstring utf8ToWide(const std::string& value) {
    if (value.empty()) return {};
    const int count = MultiByteToWideChar(CP_UTF8, 0, value.data(),
                                           static_cast<int>(value.size()), nullptr, 0);
    std::wstring result(static_cast<std::size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                        result.data(), count);
    return result;
}

std::string wideToUtf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int count = WideCharToMultiByte(CP_UTF8, 0, value.data(),
                                          static_cast<int>(value.size()), nullptr, 0,
                                          nullptr, nullptr);
    std::string result(static_cast<std::size_t>(count), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                        result.data(), count, nullptr, nullptr);
    return result;
}

int scale(int value, int dpi) { return MulDiv(value, dpi, 96); }

std::wstring formatElapsed(double elapsedMs) {
    const auto totalMs = static_cast<unsigned long long>(std::max(0.0, std::floor(elapsedMs)));
    const auto minutes = totalMs / 60000;
    const auto seconds = (totalMs / 1000) % 60;
    const auto millis = totalMs % 1000;
    wchar_t text[64]{};
    swprintf_s(text, L"%02llu:%02llu.%03llu", minutes, seconds, millis);
    return text;
}
}

bool MainWindow::create(int showCommand) {
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&controls);

    WNDCLASSEXW wc{sizeof(wc)};
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = &MainWindow::windowProc;
    wc.hInstance = instance_;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(instance_, MAKEINTRESOURCEW(1));
    if (!wc.hIcon) wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hIconSm = wc.hIcon;
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = L"OrganologicMidiMonitorWindow";
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;

    RECT rect{0, 0, 760, 540};
    AdjustWindowRectEx(&rect, WS_OVERLAPPEDWINDOW, FALSE, 0);
    window_ = CreateWindowExW(0, wc.lpszClassName, L"MIDI Monitor", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, rect.right - rect.left, rect.bottom - rect.top,
        nullptr, nullptr, instance_, this);
    if (!window_) return false;
    ShowWindow(window_, showCommand);
    UpdateWindow(window_);
    return true;
}

LRESULT CALLBACK MainWindow::windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    MainWindow* self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<MainWindow*>(create->lpCreateParams);
        self->window_ = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    return self ? self->handleMessage(message, wParam, lParam)
                : DefWindowProcW(window, message, wParam, lParam);
}

LRESULT MainWindow::handleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE:
        dpi_ = GetDpiForWindow(window_);
        createControls();
        refreshPorts();
        return 0;
    case WM_SIZE:
        layout();
        return 0;
    case WM_DPICHANGED: {
        dpi_ = HIWORD(wParam);
        const auto* suggested = reinterpret_cast<RECT*>(lParam);
        SetWindowPos(window_, nullptr, suggested->left, suggested->top,
                     suggested->right - suggested->left, suggested->bottom - suggested->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        layout();
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == IdRefresh && HIWORD(wParam) == BN_CLICKED) refreshPorts();
        else if (LOWORD(wParam) == IdInputCombo && HIWORD(wParam) == CBN_SELCHANGE) connectSelectedPort();
        else if (LOWORD(wParam) == IdReset && HIWORD(wParam) == BN_CLICKED) resetLog();
        else if (LOWORD(wParam) == IdSave && HIWORD(wParam) == BN_CLICKED) saveCsv();
        else if (LOWORD(wParam) == IdFooterLink && HIWORD(wParam) == STN_CLICKED)
            ShellExecuteW(window_, L"open", L"https://www.organologic.fr/", nullptr, nullptr, SW_SHOWNORMAL);
        return 0;
    case WM_NOTIFY: {
        const auto* header = reinterpret_cast<NMHDR*>(lParam);
        if (header->hwndFrom == listView_ && header->code == LVN_GETDISPINFOW) {
            auto* info = reinterpret_cast<NMLVDISPINFOW*>(lParam);
            if ((info->item.mask & LVIF_TEXT) && info->item.iItem >= 0
                && static_cast<std::size_t>(info->item.iItem) < log_.size()) {
                thread_local std::wstring text;
                text = listText(static_cast<std::size_t>(info->item.iItem), info->item.iSubItem);
                info->item.pszText = text.data();
            }
            return 0;
        }
        if (header->hwndFrom == listView_ && header->code == LVN_KEYDOWN) {
            const auto* key = reinterpret_cast<NMLVKEYDOWN*>(lParam);
            if ((key->wVKey == 'C') && (GetKeyState(VK_CONTROL) & 0x8000)) copySelectedRows();
            return 0;
        }
        break;
    }
    case WM_CTLCOLORSTATIC: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        HWND control = reinterpret_cast<HWND>(lParam);
        SetBkMode(dc, TRANSPARENT);
        if (control == footerLink_) SetTextColor(dc, RGB(0, 102, 204));
        else if (control == statusLabel_) SetTextColor(dc, statusError_ ? RGB(190, 35, 35) : RGB(30, 125, 55));
        else if (control == runningStatusLabel_) SetTextColor(dc, RGB(90, 90, 90));
        return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
    }
    case WM_SETCURSOR:
        if (reinterpret_cast<HWND>(wParam) == footerLink_) {
            SetCursor(handCursor_);
            return TRUE;
        }
        break;
    case MidiInput::NotificationMessage:
        drainMidi();
        return 0;
    case WM_CLOSE:
        DestroyWindow(window_);
        return 0;
    case WM_DESTROY:
        midiInput_.close();
        if (uiFont_) DeleteObject(uiFont_);
        if (monoFont_) DeleteObject(monoFont_);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window_, message, wParam, lParam);
}

void MainWindow::createControls() {
    uiFont_ = CreateFontW(-MulDiv(9, dpi_, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH, L"Segoe UI");
    monoFont_ = CreateFontW(-MulDiv(9, dpi_, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        FIXED_PITCH, L"Consolas");
    handCursor_ = LoadCursorW(nullptr, IDC_HAND);

    inputLabel_ = CreateWindowExW(0, L"STATIC", L"Entrée MIDI", WS_CHILD | WS_VISIBLE,
        0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    inputCombo_ = CreateWindowExW(0, L"COMBOBOX", nullptr,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
        0, 0, 0, 200, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IdInputCombo)), instance_, nullptr);
    refreshButton_ = CreateWindowExW(0, L"BUTTON", L"Actualiser",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        0, 0, 0, 0, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IdRefresh)), instance_, nullptr);
    statusLabel_ = CreateWindowExW(0, L"STATIC", L"Déconnecté", WS_CHILD | WS_VISIBLE,
        0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    runningStatusLabel_ = CreateWindowExW(0, L"STATIC",
        L"Running status : non observable via WinMM (messages normalisés par Windows).",
        WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    listView_ = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, nullptr,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_OWNERDATA | LVS_SHOWSELALWAYS,
        0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    ListView_SetExtendedListViewStyle(listView_,
        LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_GRIDLINES);
    LVCOLUMNW column{LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM};
    wchar_t timestamp[] = L"Horodatage";
    column.pszText = timestamp; column.cx = scale(105, dpi_); column.iSubItem = 0;
    ListView_InsertColumn(listView_, 0, &column);
    wchar_t hex[] = L"Données HEX";
    column.pszText = hex; column.cx = scale(210, dpi_); column.iSubItem = 1;
    ListView_InsertColumn(listView_, 1, &column);
    wchar_t interpretation[] = L"Interprétation";
    column.pszText = interpretation; column.cx = scale(400, dpi_); column.iSubItem = 2;
    ListView_InsertColumn(listView_, 2, &column);

    counterLabel_ = CreateWindowExW(0, L"STATIC", L"Messages reçus : 0", WS_CHILD | WS_VISIBLE,
        0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    resetButton_ = CreateWindowExW(0, L"BUTTON", L"Réinitialiser",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, window_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(IdReset)), instance_, nullptr);
    saveButton_ = CreateWindowExW(0, L"BUTTON", L"Sauvegarder CSV",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, window_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(IdSave)), instance_, nullptr);
    footerLabel_ = CreateWindowExW(0, L"STATIC", L"© 2026 Valentin Leroux — ",
        WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    footerLink_ = CreateWindowExW(0, L"STATIC", L"www.organologic.fr",
        WS_CHILD | WS_VISIBLE | SS_NOTIFY, 0, 0, 0, 0, window_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(IdFooterLink)), instance_, nullptr);

    for (HWND control : {inputLabel_, inputCombo_, refreshButton_, statusLabel_, runningStatusLabel_,
                         counterLabel_, resetButton_, saveButton_, footerLabel_, footerLink_})
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), TRUE);
    SendMessageW(listView_, WM_SETFONT, reinterpret_cast<WPARAM>(monoFont_), TRUE);

    tooltip_ = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
        WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX, CW_USEDEFAULT, CW_USEDEFAULT,
        CW_USEDEFAULT, CW_USEDEFAULT, window_, nullptr, instance_, nullptr);
    TOOLINFOW tool{sizeof(tool)};
    tool.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
    tool.hwnd = window_;
    tool.uId = reinterpret_cast<UINT_PTR>(runningStatusLabel_);
    tool.lpszText = const_cast<wchar_t*>(
        L"WinMM livre des messages courts déjà normalisés. La présence du running status dans le flux MIDI original ne peut pas être déduite.");
    SendMessageW(tooltip_, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&tool));
    layout();
}

void MainWindow::layout() {
    if (!inputLabel_) return;
    RECT client{};
    GetClientRect(window_, &client);
    const int width = client.right;
    const int height = client.bottom;
    const int margin = scale(12, dpi_);
    const int gap = scale(8, dpi_);
    const int controlHeight = scale(25, dpi_);
    const int labelWidth = scale(78, dpi_);
    const int refreshWidth = scale(82, dpi_);
    const int statusWidth = scale(125, dpi_);
    int comboWidth = std::max(scale(140, dpi_), width - 2 * margin - labelWidth - refreshWidth - statusWidth - 3 * gap);

    int y = margin;
    MoveWindow(inputLabel_, margin, y + scale(5, dpi_), labelWidth, controlHeight, TRUE);
    int x = margin + labelWidth;
    MoveWindow(inputCombo_, x, y, comboWidth, scale(250, dpi_), TRUE);
    x += comboWidth + gap;
    MoveWindow(refreshButton_, x, y, refreshWidth, controlHeight, TRUE);
    x += refreshWidth + gap;
    MoveWindow(statusLabel_, x, y + scale(5, dpi_), statusWidth, controlHeight, TRUE);

    y += controlHeight + scale(5, dpi_);
    MoveWindow(runningStatusLabel_, margin, y, width - 2 * margin, scale(20, dpi_), TRUE);
    y += scale(23, dpi_);

    const int bottomControlsY = height - margin - scale(52, dpi_);
    const int listHeight = std::max(scale(100, dpi_), bottomControlsY - y - gap);
    MoveWindow(listView_, margin, y, width - 2 * margin, listHeight, TRUE);
    ListView_SetColumnWidth(listView_, 0, scale(105, dpi_));
    ListView_SetColumnWidth(listView_, 1, scale(210, dpi_));
    ListView_SetColumnWidth(listView_, 2, std::max(scale(200, dpi_), width - 2 * margin - scale(315, dpi_) - 4));

    y = bottomControlsY;
    MoveWindow(counterLabel_, margin, y + scale(6, dpi_), scale(260, dpi_), controlHeight, TRUE);
    const int saveWidth = scale(125, dpi_);
    const int resetWidth = scale(100, dpi_);
    MoveWindow(saveButton_, width - margin - saveWidth, y, saveWidth, controlHeight, TRUE);
    MoveWindow(resetButton_, width - margin - saveWidth - gap - resetWidth, y, resetWidth, controlHeight, TRUE);

    y += controlHeight + scale(9, dpi_);
    const int footerLabelWidth = scale(160, dpi_);
    MoveWindow(footerLabel_, margin, y, footerLabelWidth, scale(18, dpi_), TRUE);
    MoveWindow(footerLink_, margin + footerLabelWidth, y, scale(125, dpi_), scale(18, dpi_), TRUE);
}

void MainWindow::refreshPorts() {
    drainMidi();
    midiInput_.close();
    activePortUtf8_.clear();
    setStatus(L"Déconnecté");
    SendMessageW(inputCombo_, CB_RESETCONTENT, 0, 0);
    SendMessageW(inputCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Sélectionner un port…"));
    SendMessageW(inputCombo_, CB_SETITEMDATA, 0, static_cast<LPARAM>(-1));
    ports_ = MidiInput::enumerate();
    for (const auto& port : ports_) {
        const LRESULT index = SendMessageW(inputCombo_, CB_ADDSTRING, 0,
                                            reinterpret_cast<LPARAM>(port.name.c_str()));
        SendMessageW(inputCombo_, CB_SETITEMDATA, index, port.id);
    }
    if (ports_.empty()) {
        SendMessageW(inputCombo_, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(L"Aucune entrée MIDI disponible"));
        SendMessageW(inputCombo_, CB_SETITEMDATA, 1, static_cast<LPARAM>(-1));
    }
    SendMessageW(inputCombo_, CB_SETCURSEL, 0, 0);
}

void MainWindow::connectSelectedPort() {
    drainMidi();
    const LRESULT selection = SendMessageW(inputCombo_, CB_GETCURSEL, 0, 0);
    if (selection == CB_ERR) return;
    const LRESULT device = SendMessageW(inputCombo_, CB_GETITEMDATA, selection, 0);
    midiInput_.close();
    activePortUtf8_.clear();
    if (device < 0) {
        setStatus(L"Déconnecté");
        return;
    }
    wchar_t name[256]{};
    SendMessageW(inputCombo_, CB_GETLBTEXT, selection, reinterpret_cast<LPARAM>(name));
    std::wstring error;
    if (!midiInput_.open(static_cast<UINT>(device), window_, error)) {
        setStatus(L"Erreur : " + error, true);
        MessageBoxW(window_, (L"Impossible d'ouvrir l'entrée MIDI.\n\n" + error).c_str(),
                    L"MIDI Monitor", MB_OK | MB_ICONERROR);
        return;
    }
    activePortUtf8_ = wideToUtf8(name);
    setStatus(L"Connecté");
}

bool MainWindow::isListAtBottom() const {
    if (log_.size() == 0) return true;
    const int top = ListView_GetTopIndex(listView_);
    const int perPage = ListView_GetCountPerPage(listView_);
    return top + perPage >= static_cast<int>(log_.size()) - 1;
}

void MainWindow::drainMidi() {
    const bool follow = isListAtBottom();
    const auto packets = midiInput_.drain();
    bool discarded = false;
    for (const auto& packet : packets) {
        if (!packet.bytes.empty()) {
            auto decoded = MidiDecoder::decode(packet.bytes, packet.elapsedMs,
                packet.driverTimestampMs, activePortUtf8_, "not_observable", "WinMM");
            if (packet.kind == MidiInputPacket::Kind::Error) {
                decoded.description += " [WinMM: " + wideToUtf8(packet.error) + "]";
            }
            discarded = log_.append(std::move(decoded)) || discarded;
        }
        if (packet.kind == MidiInputPacket::Kind::Error) setStatus(L"Erreur MIDI : " + packet.error, true);
    }
    const auto queueDrops = midiInput_.takeDroppedCount();
    if (queueDrops) {
        setStatus(L"Saturation : " + std::to_wstring(queueDrops) + L" message(s) perdu(s)", true);
        MessageBoxW(window_, L"La file de réception MIDI a saturé. Des messages ont été perdus.",
                    L"MIDI Monitor", MB_OK | MB_ICONWARNING);
    }
    if (discarded && !capacityWarningShown_) {
        capacityWarningShown_ = true;
        MessageBoxW(window_,
            L"La limite de 100 000 messages est atteinte. Les messages les plus anciens seront supprimés et le compteur de suppressions restera visible.",
            L"MIDI Monitor", MB_OK | MB_ICONWARNING);
    }
    if (!packets.empty()) {
        ListView_SetItemCountEx(listView_, static_cast<int>(log_.size()), LVSICF_NOINVALIDATEALL);
        InvalidateRect(listView_, nullptr, FALSE);
        if (follow && log_.size()) ListView_EnsureVisible(listView_, static_cast<int>(log_.size() - 1), FALSE);
        updateCounter();
    }
}

void MainWindow::resetLog() {
    midiInput_.resetClock();
    midiInput_.clearQueue();
    log_.clear();
    capacityWarningShown_ = false;
    ListView_SetItemCountEx(listView_, 0, 0);
    updateCounter();
}

void MainWindow::updateCounter() {
    std::wstring text = L"Messages reçus : " + std::to_wstring(log_.totalReceived());
    if (log_.discarded()) text += L" — anciens supprimés : " + std::to_wstring(log_.discarded());
    SetWindowTextW(counterLabel_, text.c_str());
}

void MainWindow::setStatus(const std::wstring& text, bool error) {
    statusError_ = error;
    SetWindowTextW(statusLabel_, text.c_str());
    InvalidateRect(statusLabel_, nullptr, TRUE);
}

std::wstring MainWindow::listText(std::size_t row, int column) const {
    const auto& message = log_.at(row);
    if (column == 0) return formatElapsed(message.elapsedMs);
    if (column == 1) return utf8ToWide(message.rawHex);
    return utf8ToWide(message.description);
}

void MainWindow::copySelectedRows() {
    std::wstring text;
    int item = -1;
    while ((item = ListView_GetNextItem(listView_, item, LVNI_SELECTED)) != -1) {
        text += listText(static_cast<std::size_t>(item), 0) + L"\t"
              + listText(static_cast<std::size_t>(item), 1) + L"\t"
              + listText(static_cast<std::size_t>(item), 2) + L"\r\n";
    }
    if (text.empty() || !OpenClipboard(window_)) return;
    EmptyClipboard();
    const SIZE_T bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (memory) {
        void* target = GlobalLock(memory);
        memcpy(target, text.c_str(), bytes);
        GlobalUnlock(memory);
        if (!SetClipboardData(CF_UNICODETEXT, memory)) GlobalFree(memory);
    }
    CloseClipboard();
}

void MainWindow::saveCsv() {
    SYSTEMTIME now{};
    GetLocalTime(&now);
    wchar_t filename[MAX_PATH]{};
    swprintf_s(filename, L"MIDI_Monitor_%04u-%02u-%02u_%02u%02u%02u.csv",
               now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond);
    const wchar_t filter[] = L"Fichier CSV (*.csv)\0*.csv\0Tous les fichiers (*.*)\0*.*\0\0";
    OPENFILENAMEW dialog{sizeof(dialog)};
    dialog.hwndOwner = window_;
    dialog.lpstrFilter = filter;
    dialog.lpstrFile = filename;
    dialog.nMaxFile = MAX_PATH;
    dialog.lpstrDefExt = L"csv";
    dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    if (!GetSaveFileNameW(&dialog)) return;
    std::string error;
    if (!CsvExporter::exportFile(std::filesystem::path(filename), log_.snapshot(), error)) {
        MessageBoxW(window_, utf8ToWide(error).c_str(), L"Erreur d'export CSV", MB_OK | MB_ICONERROR);
        return;
    }
    MessageBoxW(window_, L"Le journal MIDI a été sauvegardé.", L"MIDI Monitor", MB_OK | MB_ICONINFORMATION);
}
