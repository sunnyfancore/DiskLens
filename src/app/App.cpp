#include "app/App.h"

#include "core/Format.h"
#include "app/resource.h"

#include <CommDlg.h>
#include <CommCtrl.h>
#include <Shellapi.h>
#include <Shlwapi.h>
#include <Windowsx.h>

#include <algorithm>
#include <cwchar>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>
#include <vector>

namespace disk_lens::app {

namespace {

/**
 * @brief 主窗口类名。
 */
constexpr const wchar_t* kWindowClassName = L"DiskLensNativeWindow";

/**
 * @brief 应用标题。
 */
constexpr const wchar_t* kWindowTitle = L"磁盘洞察";

/**
 * @brief 扫描按钮控件编号。
 */
constexpr int kScanButtonId = 1001;

/**
 * @brief 停止按钮控件编号。
 */
constexpr int kStopButtonId = 1002;

/**
 * @brief Treemap 控件编号。
 */
constexpr int kTreemapControlId = 1003;

/**
 * @brief 筛选输入框控件编号。
 */
constexpr int kFilterEditId = 1004;

/**
 * @brief 导出按钮控件编号。
 */
constexpr int kExportButtonId = 1005;

/**
 * @brief 打开按钮控件编号。
 */
constexpr int kOpenButtonId = 1006;

/**
 * @brief 管理员重启按钮控件编号。
 */
constexpr int kAdminButtonId = 1007;

/**
 * @brief 加载动画定时器编号。
 */
constexpr UINT_PTR kAnimationTimerId = 2001;

/**
 * @brief 懒加载列表定时器编号。
 */
constexpr UINT_PTR kLazyLoadTimerId = 2002;

/**
 * @brief 扫描完成自定义消息。
 */
constexpr UINT kScanCompleteMessage = WM_APP + 1;

/**
 * @brief 扫描进度自定义消息。
 */
constexpr UINT kScanProgressMessage = WM_APP + 2;

/**
 * @brief 结果列表最多直接显示的节点数量。
 */
constexpr int kMaxVisibleRows = 12000;

/**
 * @brief 扫描进度刷新间隔，单位为毫秒。
 */
constexpr auto kProgressInterval = std::chrono::milliseconds(180);

/**
 * @brief 每个懒加载 tick 最多插入的列表行数。
 */
constexpr int kLazyRowsPerTick = 180;

/**
 * @brief 将节点深度转换为缩进后的显示名。
 * @param name 节点名称。
 * @param depth 节点深度。
 * @return 带缩进的名称。
 */
std::wstring IndentName(const std::wstring& name, int depth) {
    return std::wstring(static_cast<std::size_t>(depth) * 2U, L' ') + name;
}

/**
 * @brief 复制字符串并交给窗口消息接收方释放。
 * @param text 要复制的字符串。
 * @return 堆分配字符串指针。
 */
std::wstring* CopyMessageText(const std::wstring& text) {
    return new std::wstring(text);
}

/**
 * @brief 将文本转义为 CSV 字段。
 * @param value 原始文本。
 * @return CSV 字段文本。
 */
std::wstring EscapeCsv(const std::wstring& value) {
    std::wstring escaped = L"\"";
    for (wchar_t character : value) {
        escaped += character == L'"' ? L"\"\"" : std::wstring(1, character);
    }
    escaped += L"\"";
    return escaped;
}

}  // namespace

App::App(HINSTANCE instance) : instance_(instance) {}

App::~App() {
    StopScan();
    if (worker_.joinable()) {
        worker_.join();
    }
    if (uiFont_ != nullptr) {
        DeleteObject(uiFont_);
    }
}

bool App::Initialize(int showCommand) {
    INITCOMMONCONTROLSEX controls{};
    controls.dwSize = sizeof(controls);
    controls.dwICC = ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES | ICC_STANDARD_CLASSES | ICC_PROGRESS_CLASS | ICC_TAB_CLASSES;
    InitCommonControlsEx(&controls);

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.hInstance = instance_;
    windowClass.lpfnWndProc = &App::WindowProc;
    windowClass.lpszClassName = kWindowClassName;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hIcon = LoadIconW(instance_, MAKEINTRESOURCEW(IDI_APP_ICON));
    windowClass.hIconSm = LoadIconW(instance_, MAKEINTRESOURCEW(IDI_APP_ICON));
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);

    if (RegisterClassExW(&windowClass) == 0) {
        return false;
    }

    window_ = CreateWindowExW(
        0,
        kWindowClassName,
        kWindowTitle,
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        1100,
        720,
        nullptr,
        nullptr,
        instance_,
        this);

    if (window_ == nullptr) {
        return false;
    }

    ShowWindow(window_, showCommand);
    UpdateWindow(window_);
    return true;
}

int App::RunMessageLoop() {
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    return static_cast<int>(message.wParam);
}

LRESULT CALLBACK App::WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        auto* createStruct = reinterpret_cast<CREATESTRUCTW*>(lParam);
        auto* app = reinterpret_cast<App*>(createStruct->lpCreateParams);
        app->window_ = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
        return DefWindowProcW(window, message, wParam, lParam);
    }

    App* app = GetApp(window);
    if (app != nullptr) {
        return app->HandleMessage(window, message, wParam, lParam);
    }

    return DefWindowProcW(window, message, wParam, lParam);
}

LRESULT App::HandleMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE:
        CreateControls();
        LoadDrives();
        ApplyTheme();
        return 0;

    case WM_SIZE:
        LayoutControls(LOWORD(lParam), HIWORD(lParam));
        LayoutStatusBar(LOWORD(lParam));
        return 0;

    case WM_TIMER:
        if (wParam == kAnimationTimerId) {
            UpdateLoadingAnimation();
            return 0;
        }
        if (wParam == kLazyLoadTimerId) {
            if (!PumpLazyRows(kLazyRowsPerTick)) {
                KillTimer(window_, kLazyLoadTimerId);
                FinishBusyState();
            }
            return 0;
        }
        break;

    case WM_COMMAND:
        if (LOWORD(wParam) == kScanButtonId) {
            StartScan();
            return 0;
        }
        if (LOWORD(wParam) == kStopButtonId) {
            StopScan();
            return 0;
        }
        if (LOWORD(wParam) == kExportButtonId) {
            ExportCurrentListToCsv();
            return 0;
        }
        if (LOWORD(wParam) == kOpenButtonId) {
            OpenSelectedItemInExplorer();
            return 0;
        }
        if (LOWORD(wParam) == kAdminButtonId) {
            RestartAsAdministrator();
            return 0;
        }
        if (LOWORD(wParam) == kFilterEditId && HIWORD(wParam) == EN_CHANGE) {
            ReloadCurrentView();
            return 0;
        }
        break;

    case WM_NOTIFY: {
        auto* header = reinterpret_cast<NMHDR*>(lParam);
        if (header != nullptr && header->hwndFrom == tabControl_ && header->code == TCN_SELCHANGE) {
            currentTabIndex_ = TabCtrl_GetCurSel(tabControl_);
            ReloadCurrentView();
            return 0;
        }
        if (header != nullptr && header->hwndFrom == treeView_ && header->code == TVN_ITEMEXPANDINGW) {
            auto* treeNotice = reinterpret_cast<NMTREEVIEWW*>(lParam);
            if (treeNotice->action == TVE_EXPAND) {
                EnsureTreeChildren(treeNotice->itemNew.hItem);
            }
            return 0;
        }
        if (header != nullptr && header->hwndFrom == treeView_ && header->code == TVN_SELCHANGEDW) {
            auto* treeNotice = reinterpret_cast<NMTREEVIEWW*>(lParam);
            auto* node = reinterpret_cast<const core::ScanNode*>(treeNotice->itemNew.lParam);
            if (node != nullptr) {
                currentViewNode_ = node;
                ReloadCurrentView();
                InvalidateRect(treemapView_, nullptr, TRUE);
            }
            return 0;
        }
        break;
    }

    case WM_DRAWITEM: {
        auto* drawItem = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
        if (drawItem != nullptr && drawItem->CtlID == kTreemapControlId) {
            DrawTreemap(drawItem->hDC, drawItem->rcItem);
            return TRUE;
        }
        break;
    }

    case kScanProgressMessage: {
        std::unique_ptr<std::wstring> text(reinterpret_cast<std::wstring*>(lParam));
        progressMessagePending_.store(false);
        SetRealtimeStatus(L"扫描中", *text);
        return 0;
    }

    case kScanCompleteMessage:
        if (worker_.joinable()) {
            worker_.join();
        }
        {
            std::lock_guard<std::mutex> lock(resultMutex_);
            if (latestResult_) {
                BeginTreePopulate(*latestResult_);
                UpdateSummary(*latestResult_);
                ReloadCurrentView();
            }
        }
        EnableWindow(scanButton_, TRUE);
        EnableWindow(stopButton_, FALSE);
        progressMessagePending_.store(false);
        return 0;

    case WM_DESTROY:
        StopScan();
        KillTimer(window_, kAnimationTimerId);
        KillTimer(window_, kLazyLoadTimerId);
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(window, message, wParam, lParam);
}

void App::CreateControls() {
    toolbarPanel_ = CreateWindowExW(
        0,
        WC_STATICW,
        nullptr,
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        0,
        0,
        100,
        48,
        window_,
        nullptr,
        instance_,
        nullptr);

    scanPathLabel_ = CreateWindowExW(
        0,
        WC_STATICW,
        L"位置",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        0,
        0,
        80,
        22,
        window_,
        nullptr,
        instance_,
        nullptr);

    driveCombo_ = CreateWindowExW(
        0,
        WC_COMBOBOXW,
        nullptr,
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWN | CBS_AUTOHSCROLL,
        0,
        0,
        260,
        300,
        window_,
        nullptr,
        instance_,
        nullptr);

    modeLabel_ = CreateWindowExW(
        0,
        WC_STATICW,
        L"扫描模式: NTFS 极速优先",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        0,
        0,
        220,
        24,
        window_,
        nullptr,
        instance_,
        nullptr);

    statusHintLabel_ = CreateWindowExW(
        0,
        WC_STATICW,
        L"请选择磁盘并开始扫描",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        0,
        0,
        220,
        24,
        window_,
        nullptr,
        instance_,
        nullptr);

    filterLabel_ = CreateWindowExW(
        0,
        WC_STATICW,
        L"查找",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        0,
        0,
        80,
        22,
        window_,
        nullptr,
        instance_,
        nullptr);

    filterEdit_ = CreateWindowExW(
        WS_EX_CLIENTEDGE,
        WC_EDITW,
        nullptr,
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
        0,
        0,
        220,
        26,
        window_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kFilterEditId)),
        instance_,
        nullptr);
    SendMessageW(filterEdit_, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"输入关键字或 *.log"));

    exportButton_ = CreateWindowExW(
        0,
        WC_BUTTONW,
        L"导出",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0,
        0,
        72,
        28,
        window_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kExportButtonId)),
        instance_,
        nullptr);

    openButton_ = CreateWindowExW(
        0,
        WC_BUTTONW,
        L"打开",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0,
        0,
        72,
        28,
        window_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kOpenButtonId)),
        instance_,
        nullptr);

    adminButton_ = CreateWindowExW(
        0,
        WC_BUTTONW,
        L"极速模式",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0,
        0,
        102,
        28,
        window_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kAdminButtonId)),
        instance_,
        nullptr);

    summaryLabel_ = CreateWindowExW(
        0,
        WC_STATICW,
        L"选择磁盘后开始扫描",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        0,
        0,
        400,
        24,
        window_,
        nullptr,
        instance_,
        nullptr);

    progressBar_ = CreateWindowExW(
        0,
        PROGRESS_CLASSW,
        nullptr,
        WS_CHILD | WS_VISIBLE | PBS_MARQUEE,
        0,
        0,
        160,
        8,
        window_,
        nullptr,
        instance_,
        nullptr);

    treeView_ = CreateWindowExW(
        WS_EX_CLIENTEDGE,
        WC_TREEVIEWW,
        nullptr,
        WS_CHILD | WS_VISIBLE | TVS_HASBUTTONS | TVS_HASLINES | TVS_LINESATROOT | TVS_SHOWSELALWAYS,
        0,
        0,
        100,
        100,
        window_,
        nullptr,
        instance_,
        nullptr);

    tabControl_ = CreateWindowExW(
        0,
        WC_TABCONTROLW,
        nullptr,
        WS_CHILD | WS_VISIBLE | TCS_TABS | TCS_FIXEDWIDTH,
        0,
        0,
        100,
        30,
        window_,
        nullptr,
        instance_,
        nullptr);

    scanButton_ = CreateWindowExW(
        0,
        WC_BUTTONW,
        L"扫描",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0,
        0,
        90,
        28,
        window_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kScanButtonId)),
        instance_,
        nullptr);

    stopButton_ = CreateWindowExW(
        0,
        WC_BUTTONW,
        L"停止",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0,
        0,
        90,
        28,
        window_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kStopButtonId)),
        instance_,
        nullptr);
    EnableWindow(stopButton_, FALSE);

    listView_ = CreateWindowExW(
        WS_EX_CLIENTEDGE,
        WC_LISTVIEWW,
        nullptr,
        WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL,
        0,
        0,
        100,
        100,
        window_,
        nullptr,
        instance_,
        nullptr);

    treemapView_ = CreateWindowExW(
        WS_EX_CLIENTEDGE,
        WC_STATICW,
        nullptr,
        WS_CHILD | WS_VISIBLE | SS_OWNERDRAW,
        0,
        0,
        100,
        100,
        window_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kTreemapControlId)),
        instance_,
        nullptr);

    statusBar_ = CreateWindowExW(
        0,
        STATUSCLASSNAMEW,
        L"就绪",
        WS_CHILD | WS_VISIBLE,
        0,
        0,
        0,
        0,
        window_,
        nullptr,
        instance_,
        nullptr);

    InitializeListColumns();
    InitializeTree();
    InitializeTabs();

    LVITEMW placeholder{};
    placeholder.mask = LVIF_TEXT;
    placeholder.iItem = 0;
    placeholder.iSubItem = 0;
    placeholder.pszText = const_cast<wchar_t*>(L"请选择扫描位置并点击“扫描”");
    ListView_InsertItem(listView_, &placeholder);
    ListView_SetItemText(listView_, 0, 1, const_cast<wchar_t*>(L"-"));
    ListView_SetItemText(listView_, 0, 2, const_cast<wchar_t*>(L"提示"));
    ListView_SetItemText(listView_, 0, 3, const_cast<wchar_t*>(L"扫描完成后可查看目录、大文件、类型统计和重复候选"));
}

void App::ApplyTheme() {
    uiFont_ = CreateFontW(
        -14,
        0,
        0,
        0,
        FW_NORMAL,
        FALSE,
        FALSE,
        FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE,
        L"Segoe UI");

    const std::vector<HWND> controls = {
        scanPathLabel_,
        driveCombo_,
        scanButton_,
        stopButton_,
        filterLabel_,
        filterEdit_,
        exportButton_,
        openButton_,
        adminButton_,
        modeLabel_,
        statusHintLabel_,
        summaryLabel_,
        progressBar_,
        treeView_,
        tabControl_,
        listView_,
        treemapView_,
        statusBar_};
    for (HWND control : controls) {
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), TRUE);
    }

    ListView_SetExtendedListViewStyle(
        listView_,
        LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_GRIDLINES | LVS_EX_LABELTIP);

    SendMessageW(progressBar_, PBM_SETMARQUEE, FALSE, 0);
    ShowWindow(progressBar_, SW_HIDE);
    SendMessageW(statusBar_, SB_SIMPLE, FALSE, 0);
    LayoutStatusBar(1100);
    SetRealtimeStatus(L"就绪", L"请选择扫描位置");

    if (IsRunningAsAdministrator()) {
        SetWindowTextW(modeLabel_, L"扫描模式: NTFS 极速优先");
        ShowWindow(adminButton_, SW_HIDE);
    } else {
        SetWindowTextW(modeLabel_, L"扫描模式: 兼容扫描");
        SetWindowTextW(statusHintLabel_, L"极速模式需要提权");
        ShowWindow(adminButton_, SW_SHOW);
    }
}

void App::LayoutControls(int width, int height) {
    const int margin = 12;
    const int toolbarHeight = 76;
    const int statusHeight = 25;
    const int treeWidth = 320;
    const int contentTop = toolbarHeight + margin;
    const int contentHeight = height - toolbarHeight - statusHeight - margin * 2;
    const int treemapHeight = std::max(150, contentHeight / 3);
    const int listHeight = contentHeight - treemapHeight - 8;
    const int rightX = margin + treeWidth + 8;
    const int rightWidth = width - treeWidth - margin * 2 - 8;

    MoveWindow(toolbarPanel_, 0, 0, width, toolbarHeight, TRUE);
    const int rowY = 12;
    const int summaryY = 48;
    int x = margin;

    MoveWindow(scanPathLabel_, x, rowY + 5, 38, 22, TRUE);
    x += 44;
    MoveWindow(driveCombo_, x, rowY, 260, 300, TRUE);
    x += 272;
    MoveWindow(scanButton_, x, rowY - 1, 82, 30, TRUE);
    x += 90;
    MoveWindow(stopButton_, x, rowY - 1, 82, 30, TRUE);
    x += 94;
    MoveWindow(adminButton_, x, rowY - 1, 92, 30, TRUE);
    x += 108;

    MoveWindow(filterLabel_, x, rowY + 5, 38, 22, TRUE);
    x += 42;
    const int remainingAfterFilter = width - x - margin - 170;
    const int filterWidth = std::max(180, std::min(360, remainingAfterFilter));
    MoveWindow(filterEdit_, x, rowY, filterWidth, 26, TRUE);
    x += filterWidth + 10;
    MoveWindow(openButton_, x, rowY - 1, 72, 30, TRUE);
    x += 80;
    MoveWindow(exportButton_, x, rowY - 1, 72, 30, TRUE);

    MoveWindow(modeLabel_, margin, summaryY, 260, 22, TRUE);
    MoveWindow(statusHintLabel_, margin + 268, summaryY, 220, 22, TRUE);
    const int summaryX = margin + 500;
    const int summaryWidth = std::max(120, width - margin * 2 - 500);
    MoveWindow(summaryLabel_, summaryX, summaryY, summaryWidth, 22, TRUE);
    MoveWindow(progressBar_, summaryX, summaryY + 17, summaryWidth, 6, TRUE);
    MoveWindow(treeView_, margin, contentTop, treeWidth, contentHeight, TRUE);
    MoveWindow(tabControl_, rightX, contentTop, rightWidth, 30, TRUE);
    MoveWindow(listView_, rightX, contentTop + 30, rightWidth, listHeight - 30, TRUE);
    MoveWindow(treemapView_, rightX, contentTop + listHeight + 8, rightWidth, treemapHeight, TRUE);
    MoveWindow(statusBar_, 0, height - statusHeight, width, statusHeight, TRUE);
    LayoutStatusBar(width);
}

void App::InitializeListColumns() {
    LVCOLUMNW column{};
    column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;

    column.pszText = const_cast<wchar_t*>(L"名称");
    column.cx = 440;
    column.iSubItem = 0;
    ListView_InsertColumn(listView_, 0, &column);

    column.pszText = const_cast<wchar_t*>(L"大小");
    column.cx = 140;
    column.iSubItem = 1;
    ListView_InsertColumn(listView_, 1, &column);

    column.pszText = const_cast<wchar_t*>(L"类型");
    column.cx = 90;
    column.iSubItem = 2;
    ListView_InsertColumn(listView_, 2, &column);

    column.pszText = const_cast<wchar_t*>(L"路径");
    column.cx = 520;
    column.iSubItem = 3;
    ListView_InsertColumn(listView_, 3, &column);
}

void App::InitializeTree() {
    TreeView_DeleteAllItems(treeView_);
    TVINSERTSTRUCTW insert{};
    insert.hParent = TVI_ROOT;
    insert.hInsertAfter = TVI_LAST;
    insert.item.mask = TVIF_TEXT | TVIF_PARAM;
    insert.item.pszText = const_cast<wchar_t*>(L"扫描后显示目录树");
    insert.item.lParam = 0;
    TreeView_InsertItem(treeView_, &insert);
}

void App::InitializeTabs() {
    TCITEMW item{};
    item.mask = TCIF_TEXT;

    item.pszText = const_cast<wchar_t*>(L"目录内容");
    TabCtrl_InsertItem(tabControl_, 0, &item);

    item.pszText = const_cast<wchar_t*>(L"大文件");
    TabCtrl_InsertItem(tabControl_, 1, &item);

    item.pszText = const_cast<wchar_t*>(L"类型统计");
    TabCtrl_InsertItem(tabControl_, 2, &item);

    item.pszText = const_cast<wchar_t*>(L"疑似重复");
    TabCtrl_InsertItem(tabControl_, 3, &item);

    TabCtrl_SetCurSel(tabControl_, 0);
}

void App::LoadDrives() {
    wchar_t drives[512]{};
    const DWORD length = GetLogicalDriveStringsW(511, drives);
    if (length == 0) {
        ComboBox_AddString(driveCombo_, L"C:\\");
        ComboBox_SetCurSel(driveCombo_, 0);
        return;
    }

    const wchar_t* current = drives;
    while (*current != L'\0') {
        ComboBox_AddString(driveCombo_, current);
        current += wcslen(current) + 1;
    }

    ComboBox_SetCurSel(driveCombo_, 0);
}

void App::StartScan() {
    if (worker_.joinable()) {
        return;
    }

    KillTimer(window_, kLazyLoadTimerId);
    TreeView_DeleteAllItems(treeView_);
    ListView_DeleteAllItems(listView_);
    currentViewNode_ = nullptr;
    InvalidateRect(treemapView_, nullptr, TRUE);
    LVITEMW placeholder{};
    placeholder.mask = LVIF_TEXT;
    placeholder.iItem = 0;
    placeholder.iSubItem = 0;
    placeholder.pszText = const_cast<wchar_t*>(L"正在扫描...");
    ListView_InsertItem(listView_, &placeholder);
    ListView_SetItemText(listView_, 0, 1, const_cast<wchar_t*>(L"计算中"));
    ListView_SetItemText(listView_, 0, 2, const_cast<wchar_t*>(L"任务"));
    ListView_SetItemText(listView_, 0, 3, const_cast<wchar_t*>(L"扫描完成后将异步加载结果"));
    pendingDisplayNodes_.clear();
    displayedRows_ = 0;
    loadingFrame_ = 0;
    scanResultReady_ = false;
    scanStartedAt_ = std::chrono::steady_clock::now();
    liveFileCount_.store(0);
    liveDirectoryCount_.store(0);
    progressMessagePending_.store(false);
    lastProgressPost_ = std::chrono::steady_clock::now() - kProgressInterval;
    EnableWindow(scanButton_, FALSE);
    EnableWindow(stopButton_, TRUE);
    SetWindowTextW(modeLabel_, IsRunningAsAdministrator() ? L"扫描模式: NTFS 极速优先" : L"扫描模式: 兼容扫描");
    SetWindowTextW(statusHintLabel_, L"正在扫描...");
    SetWindowTextW(summaryLabel_, L"扫描中...");
    SetRealtimeStatus(L"扫描中", GetSelectedPath());
    ShowWindow(progressBar_, SW_SHOW);
    SendMessageW(progressBar_, PBM_SETMARQUEE, TRUE, 25);
    SetTimer(window_, kAnimationTimerId, 120, nullptr);

    worker_ = std::thread(&App::ScanWorker, this, GetSelectedPath());
}

void App::StopScan() {
    scanner_.RequestCancel();
    if (stopButton_ != nullptr && IsWindowEnabled(stopButton_)) {
        EnableWindow(stopButton_, FALSE);
        SetWindowTextW(statusHintLabel_, L"正在停止...");
        SetRealtimeStatus(L"正在停止", L"等待扫描线程退出");
    }
}

void App::ReloadCurrentView() {
    if (!latestResult_ || !latestResult_->root) {
        return;
    }

    currentTabIndex_ = TabCtrl_GetCurSel(tabControl_);
    if (currentTabIndex_ == 0) {
        BeginLazyPopulateChildren(currentViewNode_ != nullptr ? *currentViewNode_ : *latestResult_->root);
        return;
    }

    KillTimer(window_, kLazyLoadTimerId);
    ListView_DeleteAllItems(listView_);
    pendingDisplayNodes_.clear();
    displayedRows_ = 0;

    if (currentTabIndex_ == 1) {
        std::vector<const core::ScanNode*> files;
        CollectFiles(*latestResult_->root, files);
        std::sort(files.begin(), files.end(), [](const core::ScanNode* left, const core::ScanNode* right) {
            return left->totalBytes > right->totalBytes;
        });

        const std::size_t limit = std::min<std::size_t>(files.size(), static_cast<std::size_t>(kMaxVisibleRows));
        for (std::size_t index = limit; index > 0; --index) {
            pendingDisplayNodes_.push_back(PendingDisplayNode{files[index - 1], 0});
        }

        ShowWindow(progressBar_, SW_SHOW);
        SendMessageW(progressBar_, PBM_SETMARQUEE, TRUE, 18);
        SetTimer(window_, kAnimationTimerId, 120, nullptr);
        SetTimer(window_, kLazyLoadTimerId, 15, nullptr);
        SetStatusText(L"正在加载大文件列表...");
        return;
    }

    if (currentTabIndex_ == 2) {
        PopulateExtensionView();
        return;
    }

    PopulateDuplicateView();
}

void App::ExportCurrentListToCsv() {
    wchar_t filePath[MAX_PATH] = L"磁盘洞察分析结果.csv";
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = window_;
    dialog.lpstrFilter = L"CSV 文件 (*.csv)\0*.csv\0所有文件 (*.*)\0*.*\0";
    dialog.lpstrFile = filePath;
    dialog.nMaxFile = MAX_PATH;
    dialog.lpstrDefExt = L"csv";
    dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;

    if (!GetSaveFileNameW(&dialog)) {
        return;
    }

    std::wofstream file(filePath, std::ios::binary);
    file.imbue(std::locale(".UTF-8"));
    file << L"\xfeff";
    file << L"名称,大小,类型,路径\n";

    wchar_t buffer[4096]{};
    const int rows = ListView_GetItemCount(listView_);
    for (int row = 0; row < rows; ++row) {
        std::wstring fields[4];
        for (int column = 0; column < 4; ++column) {
            ListView_GetItemText(listView_, row, column, buffer, static_cast<int>(std::size(buffer)));
            fields[column] = buffer;
        }

        file << EscapeCsv(fields[0]) << L"," << EscapeCsv(fields[1]) << L"," << EscapeCsv(fields[2]) << L"," << EscapeCsv(fields[3]) << L"\n";
    }

    SetStatusText(L"已导出 CSV。");
}

void App::OpenSelectedItemInExplorer() {
    const int selected = ListView_GetNextItem(listView_, -1, LVNI_SELECTED);
    if (selected < 0) {
        SetStatusText(L"请先选择要打开的项目。");
        return;
    }

    wchar_t path[MAX_PATH * 4]{};
    ListView_GetItemText(listView_, selected, 3, path, static_cast<int>(std::size(path)));
    if (path[0] == L'\0') {
        return;
    }

    ShellExecuteW(window_, L"open", L"explorer.exe", path, nullptr, SW_SHOWNORMAL);
}

void App::RestartAsAdministrator() {
    wchar_t executablePath[MAX_PATH]{};
    if (GetModuleFileNameW(nullptr, executablePath, MAX_PATH) == 0) {
        SetStatusText(L"无法获取当前程序路径。");
        return;
    }

    HINSTANCE result = ShellExecuteW(window_, L"runas", executablePath, nullptr, nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(result) <= 32) {
        SetStatusText(L"管理员重启已取消或失败。");
        return;
    }

    PostMessageW(window_, WM_CLOSE, 0, 0);
}

bool App::IsRunningAsAdministrator() const {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        return false;
    }

    TOKEN_ELEVATION elevation{};
    DWORD returnedBytes = 0;
    const BOOL ok = GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &returnedBytes);
    CloseHandle(token);
    return ok && elevation.TokenIsElevated != 0;
}

void App::ScanWorker(std::wstring path) {
    const auto startedAt = std::chrono::steady_clock::now();
    auto result = std::make_unique<core::ScanResult>();

    bool usedNtfsMft = false;
    try {
        if (ntfsScanner_.CanScan(path)) {
            TryPostProgress(L"正在使用 NTFS MFT 极速扫描，需要管理员权限...");
            *result = ntfsScanner_.Scan(path);
            usedNtfsMft = true;
        }
    } catch (...) {
        usedNtfsMft = false;
    }

    if (!usedNtfsMft) {
        TryPostProgress(L"NTFS 极速扫描不可用，正在回退到多线程兼容扫描...");
        *result = scanner_.Scan(path, [this](const core::ScanProgress& progress) {
            liveFileCount_.store(progress.filesVisited);
            liveDirectoryCount_.store(progress.directoriesVisited);
            std::wstringstream stream;
            stream << L"正在扫描: " << progress.currentPath << L"    文件: " << progress.filesVisited
                   << L"    目录: " << progress.directoriesVisited;
            TryPostProgress(stream.str());
        });
    }
    lastScanUsedNtfsMft_.store(usedNtfsMft);

    const auto finishedAt = std::chrono::steady_clock::now();
    lastScanMilliseconds_.store(static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(finishedAt - startedAt).count()));
    liveFileCount_.store(result->fileCount);
    liveDirectoryCount_.store(result->directoryCount);

    {
        std::lock_guard<std::mutex> lock(resultMutex_);
        latestResult_ = std::move(result);
    }

    PostMessageW(window_, kScanCompleteMessage, 0, 0);
}

void App::PopulateResult(const core::ScanResult& result) {
    ListView_DeleteAllItems(listView_);
    displayedRows_ = 0;
    BeginLazyPopulate(result);
}

void App::BeginLazyPopulate(const core::ScanResult& result) {
    ListView_DeleteAllItems(listView_);
    pendingDisplayNodes_.clear();
    displayedRows_ = 0;
    scanResultReady_ = true;

    if (result.root) {
        pendingDisplayNodes_.push_back(PendingDisplayNode{result.root.get(), 0});
    }

    SetStatusText(L"扫描完成，正在异步加载结果...");
    ShowWindow(progressBar_, SW_SHOW);
    SendMessageW(progressBar_, PBM_SETMARQUEE, TRUE, 18);
    SetTimer(window_, kAnimationTimerId, 120, nullptr);
    SetTimer(window_, kLazyLoadTimerId, 15, nullptr);
}

void App::BeginLazyPopulateChildren(const core::ScanNode& node) {
    KillTimer(window_, kLazyLoadTimerId);
    ListView_DeleteAllItems(listView_);
    pendingDisplayNodes_.clear();
    displayedRows_ = 0;
    currentViewNode_ = &node;
    scanResultReady_ = true;

    for (auto iterator = node.children.rbegin(); iterator != node.children.rend(); ++iterator) {
        if (MatchesCurrentFilter(*iterator->get())) {
            pendingDisplayNodes_.push_back(PendingDisplayNode{iterator->get(), 0});
        }
    }

    ShowWindow(progressBar_, SW_SHOW);
    SendMessageW(progressBar_, PBM_SETMARQUEE, TRUE, 18);
    SetTimer(window_, kAnimationTimerId, 120, nullptr);
    SetTimer(window_, kLazyLoadTimerId, 15, nullptr);
    InvalidateRect(treemapView_, nullptr, TRUE);

    std::wstringstream stream;
    stream << L"正在加载: " << node.path << L"    子项: " << node.children.size();
    SetStatusText(stream.str());
}

void App::BeginTreePopulate(const core::ScanResult& result) {
    TreeView_DeleteAllItems(treeView_);

    if (!result.root) {
        return;
    }

    HTREEITEM rootItem = AddTreeNode(TVI_ROOT, *result.root);
    TreeView_SelectItem(treeView_, rootItem);
    UpdateSummary(result);
}

HTREEITEM App::AddTreeNode(HTREEITEM parent, const core::ScanNode& node) {
    if (node.kind != core::NodeKind::Directory) {
        return nullptr;
    }

    TVINSERTSTRUCTW insert{};
    insert.hParent = parent;
    insert.hInsertAfter = TVI_LAST;
    insert.item.mask = TVIF_TEXT | TVIF_PARAM;
    insert.item.pszText = const_cast<wchar_t*>(node.name.c_str());
    insert.item.lParam = reinterpret_cast<LPARAM>(&node);

    HTREEITEM treeItem = TreeView_InsertItem(treeView_, &insert);
    if (treeItem != nullptr && HasDirectoryChildren(node)) {
        TVINSERTSTRUCTW placeholder{};
        placeholder.hParent = treeItem;
        placeholder.hInsertAfter = TVI_LAST;
        placeholder.item.mask = TVIF_TEXT | TVIF_PARAM;
        placeholder.item.pszText = const_cast<wchar_t*>(L"正在加载...");
        placeholder.item.lParam = 0;
        TreeView_InsertItem(treeView_, &placeholder);
    }

    return treeItem;
}

void App::EnsureTreeChildren(HTREEITEM treeItem) {
    TVITEMW item{};
    item.mask = TVIF_PARAM;
    item.hItem = treeItem;
    if (!TreeView_GetItem(treeView_, &item)) {
        return;
    }

    auto* node = reinterpret_cast<const core::ScanNode*>(item.lParam);
    if (node == nullptr) {
        return;
    }

    HTREEITEM firstChild = TreeView_GetChild(treeView_, treeItem);
    if (firstChild != nullptr) {
        TVITEMW childItem{};
        childItem.mask = TVIF_PARAM;
        childItem.hItem = firstChild;
        if (TreeView_GetItem(treeView_, &childItem) && childItem.lParam != 0) {
            return;
        }
        TreeView_DeleteItem(treeView_, firstChild);
    }

    for (const auto& child : node->children) {
        if (child->kind == core::NodeKind::Directory) {
            AddTreeNode(treeItem, *child);
        }
    }
}

bool App::HasDirectoryChildren(const core::ScanNode& node) const {
    for (const auto& child : node.children) {
        if (child->kind == core::NodeKind::Directory) {
            return true;
        }
    }

    return false;
}

bool App::PumpLazyRows(int maxRows) {
    int insertedRows = 0;
    while (!pendingDisplayNodes_.empty() && insertedRows < maxRows && displayedRows_ < kMaxVisibleRows) {
        const PendingDisplayNode current = pendingDisplayNodes_.back();
        pendingDisplayNodes_.pop_back();

        if (current.node == nullptr) {
            continue;
        }

        AddSingleNodeToList(*current.node, current.depth);
        ++insertedRows;

        if (currentTabIndex_ == 0 && current.depth > 0) {
            const auto& children = current.node->children;
            for (auto iterator = children.rbegin(); iterator != children.rend(); ++iterator) {
                if (MatchesCurrentFilter(*iterator->get())) {
                    pendingDisplayNodes_.push_back(PendingDisplayNode{iterator->get(), current.depth + 1});
                }
            }
        }
    }

    if (displayedRows_ >= kMaxVisibleRows) {
        pendingDisplayNodes_.clear();
    }

    std::wstringstream stream;
    stream << L"正在加载结果    已显示: " << displayedRows_;
    if (!pendingDisplayNodes_.empty()) {
        stream << L"    待加载: " << pendingDisplayNodes_.size();
    }
    SetStatusText(stream.str());

    return !pendingDisplayNodes_.empty();
}

void App::PopulateExtensionView() {
    if (!latestResult_) {
        return;
    }

    ListView_DeleteAllItems(listView_);
    displayedRows_ = 0;
    currentTabIndex_ = 2;
    KillTimer(window_, kAnimationTimerId);
    SendMessageW(progressBar_, PBM_SETMARQUEE, FALSE, 0);
    ShowWindow(progressBar_, SW_HIDE);

    std::vector<core::ExtensionSummary> summaries;
    for (const auto& entry : latestResult_->extensions) {
        summaries.push_back(entry.second);
    }

    std::sort(summaries.begin(), summaries.end(), [](const auto& left, const auto& right) {
        return left.totalBytes > right.totalBytes;
    });

    const std::wstring filter = GetFilterText();
    for (const auto& summary : summaries) {
        if (!filter.empty() && !WildcardMatch(summary.extension, filter)) {
            continue;
        }

        const int index = ListView_GetItemCount(listView_);
        std::wstring sizeText = core::FormatBytes(summary.totalBytes);
        std::wstring countText = std::to_wstring(summary.fileCount) + L" 个文件";

        LVITEMW item{};
        item.mask = LVIF_TEXT;
        item.iItem = index;
        item.iSubItem = 0;
        item.pszText = const_cast<wchar_t*>(summary.extension.c_str());
        ListView_InsertItem(listView_, &item);
        ListView_SetItemText(listView_, index, 1, sizeText.data());
        ListView_SetItemText(listView_, index, 2, countText.data());
        ListView_SetItemText(listView_, index, 3, const_cast<wchar_t*>(L"文件类型统计"));
        ++displayedRows_;
    }

    SetStatusText(L"类型统计已加载。");
}

void App::PopulateDuplicateView() {
    if (!latestResult_ || !latestResult_->root) {
        return;
    }

    ListView_DeleteAllItems(listView_);
    displayedRows_ = 0;
    currentTabIndex_ = 3;
    KillTimer(window_, kAnimationTimerId);
    SendMessageW(progressBar_, PBM_SETMARQUEE, FALSE, 0);
    ShowWindow(progressBar_, SW_HIDE);

    std::vector<const core::ScanNode*> files;
    CollectFiles(*latestResult_->root, files);

    std::map<std::wstring, std::vector<const core::ScanNode*>> groups;
    for (const core::ScanNode* file : files) {
        if (file == nullptr || file->totalBytes == 0) {
            continue;
        }

        const std::wstring key = file->name + L"|" + std::to_wstring(file->totalBytes);
        groups[key].push_back(file);
    }

    for (const auto& group : groups) {
        if (group.second.size() < 2) {
            continue;
        }

        for (const core::ScanNode* file : group.second) {
            if (displayedRows_ >= kMaxVisibleRows || file == nullptr) {
                break;
            }

            const int index = ListView_GetItemCount(listView_);
            std::wstring sizeText = core::FormatBytes(file->totalBytes);
            std::wstring typeText = L"重复候选 " + std::to_wstring(group.second.size()) + L" 项";

            LVITEMW item{};
            item.mask = LVIF_TEXT;
            item.iItem = index;
            item.iSubItem = 0;
            item.pszText = const_cast<wchar_t*>(file->name.c_str());
            ListView_InsertItem(listView_, &item);
            ListView_SetItemText(listView_, index, 1, sizeText.data());
            ListView_SetItemText(listView_, index, 2, typeText.data());
            ListView_SetItemText(listView_, index, 3, const_cast<wchar_t*>(file->path.c_str()));
            ++displayedRows_;
        }
    }

    SetStatusText(L"疑似重复文件已加载。");
}

void App::CollectFiles(const core::ScanNode& node, std::vector<const core::ScanNode*>& output) const {
    if (node.kind == core::NodeKind::File) {
        if (MatchesCurrentFilter(node)) {
            output.push_back(&node);
        }
        return;
    }

    for (const auto& child : node.children) {
        CollectFiles(*child, output);
    }
}

bool App::MatchesCurrentFilter(const core::ScanNode& node) const {
    const std::wstring filter = GetFilterText();
    if (filter.empty()) {
        return true;
    }

    return WildcardMatch(node.name, filter) || WildcardMatch(node.path, filter);
}

bool App::WildcardMatch(const std::wstring& text, const std::wstring& pattern) const {
    if (pattern.empty()) {
        return true;
    }

    std::wstring normalizedPattern = pattern;
    if (normalizedPattern.find(L'*') == std::wstring::npos && normalizedPattern.find(L'?') == std::wstring::npos) {
        normalizedPattern = L"*" + normalizedPattern + L"*";
    }

    return PathMatchSpecW(text.c_str(), normalizedPattern.c_str()) == TRUE;
}

std::wstring App::GetFilterText() const {
    wchar_t buffer[512]{};
    if (filterEdit_ != nullptr) {
        GetWindowTextW(filterEdit_, buffer, static_cast<int>(std::size(buffer)));
    }
    return buffer;
}

void App::UpdateSummary(const core::ScanResult& result) {
    std::wstringstream stream;
    stream << L"总大小 " << core::FormatBytes(result.root ? result.root->totalBytes : 0)
           << L"    文件 " << result.fileCount
           << L"    目录 " << result.directoryCount
           << L"    错误 " << result.errorCount
           << L"    模式 " << (lastScanUsedNtfsMft_.load() ? L"NTFS MFT" : L"兼容扫描")
           << L"    耗时 " << (lastScanMilliseconds_.load() / 1000.0) << L" 秒";
    SetWindowTextW(summaryLabel_, stream.str().c_str());
    SetRealtimeStatus(lastScanUsedNtfsMft_.load() ? L"完成: NTFS MFT" : L"完成: 兼容扫描", result.root ? result.root->path : L"");
}

void App::AddSingleNodeToList(const core::ScanNode& node, int depth) {
    const int index = ListView_GetItemCount(listView_);
    std::wstring displayName = IndentName(node.name, depth);
    std::wstring sizeText = core::FormatBytes(node.totalBytes);
    std::wstring typeText = node.kind == core::NodeKind::Directory ? L"目录" : L"文件";

    LVITEMW item{};
    item.mask = LVIF_TEXT;
    item.iItem = index;
    item.iSubItem = 0;
    item.pszText = displayName.data();
    ListView_InsertItem(listView_, &item);

    ListView_SetItemText(listView_, index, 1, sizeText.data());
    ListView_SetItemText(listView_, index, 2, typeText.data());
    ListView_SetItemText(listView_, index, 3, const_cast<wchar_t*>(node.path.c_str()));
    ++displayedRows_;
}

void App::UpdateLoadingAnimation() {
    static constexpr const wchar_t* frames[] = {L"正在处理", L"正在处理.", L"正在处理..", L"正在处理..."};
    SetWindowTextW(statusHintLabel_, frames[loadingFrame_ % 4]);
    ++loadingFrame_;
}

void App::FinishBusyState() {
    KillTimer(window_, kAnimationTimerId);
    SendMessageW(progressBar_, PBM_SETMARQUEE, FALSE, 0);
    ShowWindow(progressBar_, SW_HIDE);

    SetWindowTextW(modeLabel_, IsRunningAsAdministrator() ? L"扫描模式: NTFS 极速优先" : L"扫描模式: 兼容扫描");
    SetWindowTextW(statusHintLabel_, scanResultReady_ ? L"结果已加载" : L"请选择磁盘并开始扫描");

    std::wstringstream stream;
    const core::ScanResult* result = latestResult_.get();
    if (currentViewNode_ != nullptr) {
        stream << L"当前目录: " << currentViewNode_->path
               << L"    大小: " << core::FormatBytes(currentViewNode_->totalBytes)
               << L"    已显示: " << displayedRows_;
    } else {
        stream << L"完成";
        if (result != nullptr) {
            stream << L"    总大小: " << core::FormatBytes(result->root ? result->root->totalBytes : 0)
                   << L"    文件: " << result->fileCount
                   << L"    目录: " << result->directoryCount
                   << L"    错误: " << result->errorCount
                   << L"    已显示: " << displayedRows_;
        }
    }
    if (displayedRows_ >= kMaxVisibleRows) {
        stream << L" / 前 " << kMaxVisibleRows << L" 项";
    }
    SetRealtimeStatus(scanResultReady_ ? L"结果已加载" : L"就绪", currentViewNode_ != nullptr ? currentViewNode_->path : stream.str());
}

void App::DrawTreemap(HDC deviceContext, const RECT& bounds) {
    HBRUSH background = CreateSolidBrush(RGB(250, 250, 250));
    FillRect(deviceContext, &bounds, background);
    DeleteObject(background);

    if (currentViewNode_ == nullptr || currentViewNode_->children.empty() || currentViewNode_->totalBytes == 0) {
        SetBkMode(deviceContext, TRANSPARENT);
        SetTextColor(deviceContext, RGB(110, 110, 110));
        DrawTextW(deviceContext, L"空间占比图会在扫描完成后显示当前目录的主要占用项", -1, const_cast<RECT*>(&bounds), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        return;
    }

    RECT drawBounds = bounds;
    InflateRect(&drawBounds, -8, -8);

    std::uint64_t remainingBytes = currentViewNode_->totalBytes;
    RECT remainingRect = drawBounds;
    std::size_t colorIndex = 0;

    for (const auto& child : currentViewNode_->children) {
        if (child->totalBytes == 0 || remainingBytes == 0 || remainingRect.right <= remainingRect.left || remainingRect.bottom <= remainingRect.top) {
            continue;
        }

        RECT itemRect = remainingRect;
        const int width = remainingRect.right - remainingRect.left;
        const int height = remainingRect.bottom - remainingRect.top;
        const bool splitVertically = width >= height;

        if (splitVertically) {
            const int slice = std::max(2, static_cast<int>((static_cast<unsigned long long>(width) * child->totalBytes) / remainingBytes));
            itemRect.right = std::min(remainingRect.right, remainingRect.left + slice);
            remainingRect.left = itemRect.right;
        } else {
            const int slice = std::max(2, static_cast<int>((static_cast<unsigned long long>(height) * child->totalBytes) / remainingBytes));
            itemRect.bottom = std::min(remainingRect.bottom, remainingRect.top + slice);
            remainingRect.top = itemRect.bottom;
        }

        remainingBytes -= std::min(remainingBytes, child->totalBytes);

        HBRUSH brush = CreateSolidBrush(GetTreemapColor(colorIndex));
        FillRect(deviceContext, &itemRect, brush);
        DeleteObject(brush);

        FrameRect(deviceContext, &itemRect, reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));

        if ((itemRect.right - itemRect.left) > 90 && (itemRect.bottom - itemRect.top) > 24) {
            RECT textRect = itemRect;
            InflateRect(&textRect, -4, -3);
            std::wstring label = child->name + L"  " + core::FormatBytes(child->totalBytes);
            SetBkMode(deviceContext, TRANSPARENT);
            SetTextColor(deviceContext, RGB(20, 20, 20));
            DrawTextW(deviceContext, label.c_str(), -1, &textRect, DT_LEFT | DT_TOP | DT_END_ELLIPSIS | DT_SINGLELINE);
        }

        ++colorIndex;
        if (colorIndex >= 48) {
            break;
        }
    }
}

COLORREF App::GetTreemapColor(std::size_t index) const {
    static constexpr COLORREF colors[] = {
        RGB(77, 144, 254),
        RGB(52, 168, 83),
        RGB(251, 188, 5),
        RGB(234, 67, 53),
        RGB(0, 188, 212),
        RGB(156, 39, 176),
        RGB(255, 112, 67),
        RGB(124, 179, 66)
    };

    return colors[index % (sizeof(colors) / sizeof(colors[0]))];
}

void App::SetStatusText(const std::wstring& text) {
    SendMessageW(statusBar_, SB_SETTEXTW, 0, reinterpret_cast<LPARAM>(text.c_str()));
}

void App::SetRealtimeStatus(const std::wstring& stateText, const std::wstring& pathText) {
    const auto now = std::chrono::steady_clock::now();
    double elapsedSeconds = 0.0;
    if (scanStartedAt_.time_since_epoch().count() != 0) {
        elapsedSeconds = std::chrono::duration_cast<std::chrono::milliseconds>(now - scanStartedAt_).count() / 1000.0;
    }

    const std::uint64_t files = liveFileCount_.load();
    const std::uint64_t directories = liveDirectoryCount_.load();
    const double speed = elapsedSeconds > 0.0 ? (files + directories) / elapsedSeconds : 0.0;

    std::wstringstream filesText;
    filesText << L"文件 " << files;

    std::wstringstream directoriesText;
    directoriesText << L"目录 " << directories;

    std::wstringstream speedText;
    speedText << L"速度 " << static_cast<std::uint64_t>(speed) << L"/秒";

    std::wstringstream elapsedText;
    elapsedText << L"耗时 " << std::fixed << std::setprecision(1) << elapsedSeconds << L" 秒";

    SendMessageW(statusBar_, SB_SETTEXTW, 0, reinterpret_cast<LPARAM>(stateText.c_str()));
    SendMessageW(statusBar_, SB_SETTEXTW, 1, reinterpret_cast<LPARAM>(filesText.str().c_str()));
    SendMessageW(statusBar_, SB_SETTEXTW, 2, reinterpret_cast<LPARAM>(directoriesText.str().c_str()));
    SendMessageW(statusBar_, SB_SETTEXTW, 3, reinterpret_cast<LPARAM>(speedText.str().c_str()));
    SendMessageW(statusBar_, SB_SETTEXTW, 4, reinterpret_cast<LPARAM>(elapsedText.str().c_str()));
    SendMessageW(statusBar_, SB_SETTEXTW, 5, reinterpret_cast<LPARAM>(pathText.c_str()));
}

void App::LayoutStatusBar(int width) {
    int parts[6]{};
    parts[0] = 160;
    parts[1] = 280;
    parts[2] = 400;
    parts[3] = 540;
    parts[4] = 660;
    parts[5] = width - 8;
    SendMessageW(statusBar_, SB_SETPARTS, 6, reinterpret_cast<LPARAM>(parts));
}

void App::TryPostProgress(const std::wstring& text) {
    const auto now = std::chrono::steady_clock::now();
    if (now - lastProgressPost_ < kProgressInterval) {
        return;
    }

    bool expected = false;
    if (!progressMessagePending_.compare_exchange_strong(expected, true)) {
        return;
    }

    lastProgressPost_ = now;
    PostMessageW(window_, kScanProgressMessage, 0, reinterpret_cast<LPARAM>(CopyMessageText(text)));
}

std::wstring App::GetSelectedPath() const {
    wchar_t buffer[MAX_PATH]{};
    GetWindowTextW(driveCombo_, buffer, MAX_PATH);
    return buffer;
}

App* App::GetApp(HWND window) {
    return reinterpret_cast<App*>(GetWindowLongPtrW(window, GWLP_USERDATA));
}

}  // namespace disk_lens::app
