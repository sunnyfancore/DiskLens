#pragma once

#include "core/DirectoryScanner.h"
#include "core/NtfsMftScanner.h"
#include "core/ScanModels.h"

#include <Windows.h>
#include <CommCtrl.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace disk_lens::app {

/**
 * @brief Win32 桌面应用主类。
 */
class App {
public:
    /**
     * @brief 构造应用对象。
     * @param instance 当前进程的模块实例句柄。
     */
    explicit App(HINSTANCE instance);

    /**
     * @brief 析构应用对象并等待后台扫描线程退出。
     */
    ~App();

    /**
     * @brief 初始化窗口和控件。
     * @param showCommand 窗口显示命令。
     * @return 初始化成功时返回 true。
     */
    bool Initialize(int showCommand);

    /**
     * @brief 运行 Win32 消息循环。
     * @return 进程退出码。
     */
    int RunMessageLoop();

private:
    /**
     * @brief 窗口过程静态入口。
     * @param window 窗口句柄。
     * @param message 消息编号。
     * @param wParam 消息参数。
     * @param lParam 消息参数。
     * @return 消息处理结果。
     */
    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);

    /**
     * @brief 当前对象的窗口过程。
     * @param window 窗口句柄。
     * @param message 消息编号。
     * @param wParam 消息参数。
     * @param lParam 消息参数。
     * @return 消息处理结果。
     */
    LRESULT HandleMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam);

    /**
     * @brief 创建子控件。
     */
    void CreateControls();

    /**
     * @brief 为窗口和控件应用统一视觉风格。
     */
    void ApplyTheme();

    /**
     * @brief 调整子控件布局。
     * @param width 客户区宽度。
     * @param height 客户区高度。
     */
    void LayoutControls(int width, int height);

    /**
     * @brief 初始化结果列表列头。
     */
    void InitializeListColumns();

    /**
     * @brief 初始化右侧分析标签页。
     */
    void InitializeTabs();

    /**
     * @brief 初始化目录树控件。
     */
    void InitializeTree();

    /**
     * @brief 将可用驱动器填入下拉框。
     */
    void LoadDrives();

    /**
     * @brief 开始扫描当前选择的路径。
     */
    void StartScan();

    /**
     * @brief 请求停止当前扫描。
     */
    void StopScan();

    /**
     * @brief 重新加载当前标签页数据。
     */
    void ReloadCurrentView();

    /**
     * @brief 导出当前列表内容到 CSV 文件。
     */
    void ExportCurrentListToCsv();

    /**
     * @brief 在 Windows 资源管理器中打开当前选择项。
     */
    void OpenSelectedItemInExplorer();

    /**
     * @brief 以管理员身份重启当前程序。
     */
    void RestartAsAdministrator();

    /**
     * @brief 判断当前进程是否以管理员权限运行。
     * @return 以管理员权限运行时返回 true。
     */
    bool IsRunningAsAdministrator() const;

    /**
     * @brief 后台扫描线程入口。
     * @param path 要扫描的路径。
     */
    void ScanWorker(std::wstring path);

    /**
     * @brief 将扫描结果显示到列表控件。
     * @param result 要展示的扫描结果。
     */
    void PopulateResult(const core::ScanResult& result);

    /**
     * @brief 开始异步懒加载扫描结果。
     * @param result 要展示的扫描结果。
     */
    void BeginLazyPopulate(const core::ScanResult& result);

    /**
     * @brief 开始异步加载指定目录的直属子节点。
     * @param node 要展示的目录节点。
     */
    void BeginLazyPopulateChildren(const core::ScanNode& node);

    /**
     * @brief 开始填充左侧目录树。
     * @param result 要展示的扫描结果。
     */
    void BeginTreePopulate(const core::ScanResult& result);

    /**
     * @brief 向目录树追加一个节点。
     * @param parent 父树节点句柄。
     * @param node 要追加的扫描节点。
     * @return 新增树节点句柄。
     */
    HTREEITEM AddTreeNode(HTREEITEM parent, const core::ScanNode& node);

    /**
     * @brief 在用户展开时懒加载目录子节点。
     * @param treeItem 正在展开的树节点句柄。
     */
    void EnsureTreeChildren(HTREEITEM treeItem);

    /**
     * @brief 判断扫描节点是否包含目录子节点。
     * @param node 要检查的扫描节点。
     * @return 存在目录子节点时返回 true。
     */
    bool HasDirectoryChildren(const core::ScanNode& node) const;

    /**
     * @brief 绘制当前目录的空间占比图。
     * @param deviceContext 绘图设备上下文。
     * @param bounds 绘图区域。
     */
    void DrawTreemap(HDC deviceContext, const RECT& bounds);

    /**
     * @brief 根据索引生成稳定的 Treemap 色块颜色。
     * @param index 节点索引。
     * @return 颜色值。
     */
    COLORREF GetTreemapColor(std::size_t index) const;

    /**
     * @brief 执行一批列表节点加载。
     * @param maxRows 本批最多插入的行数。
     * @return 仍有待加载节点时返回 true。
     */
    bool PumpLazyRows(int maxRows);

    /**
     * @brief 填充文件类型统计视图。
     */
    void PopulateExtensionView();

    /**
     * @brief 填充疑似重复文件视图。
     */
    void PopulateDuplicateView();

    /**
     * @brief 收集大文件视图数据。
     * @param node 扫描节点。
     * @param output 输出文件节点列表。
     */
    void CollectFiles(const core::ScanNode& node, std::vector<const core::ScanNode*>& output) const;

    /**
     * @brief 判断节点是否匹配当前筛选文本。
     * @param node 要检查的节点。
     * @return 匹配时返回 true。
     */
    bool MatchesCurrentFilter(const core::ScanNode& node) const;

    /**
     * @brief 判断文本是否匹配简单通配符。
     * @param text 待匹配文本。
     * @param pattern 通配符表达式，支持 * 和 ?。
     * @return 匹配时返回 true。
     */
    bool WildcardMatch(const std::wstring& text, const std::wstring& pattern) const;

    /**
     * @brief 获取当前筛选文本。
     * @return 筛选文本。
     */
    std::wstring GetFilterText() const;

    /**
     * @brief 更新顶部摘要信息。
     * @param result 扫描结果。
     */
    void UpdateSummary(const core::ScanResult& result);

    /**
     * @brief 追加单个节点到列表控件。
     * @param node 要追加的节点。
     * @param depth 当前节点深度。
     */
    void AddSingleNodeToList(const core::ScanNode& node, int depth);

    /**
     * @brief 更新加载动画文本。
     */
    void UpdateLoadingAnimation();

    /**
     * @brief 结束扫描或加载时的界面状态。
     */
    void FinishBusyState();

    /**
     * @brief 设置状态栏文字。
     * @param text 要显示的状态文字。
     */
    void SetStatusText(const std::wstring& text);

    /**
     * @brief 设置多分区实时状态栏。
     * @param stateText 状态文本。
     * @param pathText 当前路径文本。
     */
    void SetRealtimeStatus(const std::wstring& stateText, const std::wstring& pathText);

    /**
     * @brief 调整状态栏分区宽度。
     * @param width 主窗口客户区宽度。
     */
    void LayoutStatusBar(int width);

    /**
     * @brief 尝试发布节流后的扫描进度。
     * @param text 要显示的进度文字。
     */
    void TryPostProgress(const std::wstring& text);

    /**
     * @brief 获取当前选择的扫描路径。
     * @return 当前路径。
     */
    std::wstring GetSelectedPath() const;

    /**
     * @brief 获取当前应用对象。
     * @param window 窗口句柄。
     * @return 应用对象指针。
     */
    static App* GetApp(HWND window);

    /**
     * @brief 待懒加载的节点引用。
     */
    struct PendingDisplayNode {
        /**
         * @brief 要显示的扫描节点。
         */
        const core::ScanNode* node = nullptr;

        /**
         * @brief 节点显示深度。
         */
        int depth = 0;
    };

    /**
     * @brief 主模块实例句柄。
     */
    HINSTANCE instance_;

    /**
     * @brief 主窗口句柄。
     */
    HWND window_ = nullptr;

    /**
     * @brief 路径下拉框句柄。
     */
    HWND driveCombo_ = nullptr;

    /**
     * @brief 扫描位置标签句柄。
     */
    HWND scanPathLabel_ = nullptr;

    /**
     * @brief 扫描按钮句柄。
     */
    HWND scanButton_ = nullptr;

    /**
     * @brief 停止按钮句柄。
     */
    HWND stopButton_ = nullptr;

    /**
     * @brief 筛选输入框句柄。
     */
    HWND filterEdit_ = nullptr;

    /**
     * @brief 筛选标签句柄。
     */
    HWND filterLabel_ = nullptr;

    /**
     * @brief 导出按钮句柄。
     */
    HWND exportButton_ = nullptr;

    /**
     * @brief 打开路径按钮句柄。
     */
    HWND openButton_ = nullptr;

    /**
     * @brief 管理员重启按钮句柄。
     */
    HWND adminButton_ = nullptr;

    /**
     * @brief 顶部工具区域句柄。
     */
    HWND toolbarPanel_ = nullptr;

    /**
     * @brief 扫描模式提示文本句柄。
     */
    HWND modeLabel_ = nullptr;

    /**
     * @brief 当前状态标签句柄。
     */
    HWND statusHintLabel_ = nullptr;

    /**
     * @brief 加载动画进度条句柄。
     */
    HWND progressBar_ = nullptr;

    /**
     * @brief 扫描摘要文本句柄。
     */
    HWND summaryLabel_ = nullptr;

    /**
     * @brief 左侧目录树句柄。
     */
    HWND treeView_ = nullptr;

    /**
     * @brief 右侧视图标签页句柄。
     */
    HWND tabControl_ = nullptr;

    /**
     * @brief 结果列表句柄。
     */
    HWND listView_ = nullptr;

    /**
     * @brief 底部空间占比图句柄。
     */
    HWND treemapView_ = nullptr;

    /**
     * @brief 状态栏句柄。
     */
    HWND statusBar_ = nullptr;

    /**
     * @brief 后台目录扫描器。
     */
    core::DirectoryScanner scanner_;

    /**
     * @brief NTFS MFT 极速扫描器。
     */
    core::NtfsMftScanner ntfsScanner_;

    /**
     * @brief 界面字体句柄。
     */
    HFONT uiFont_ = nullptr;

    /**
     * @brief 后台扫描线程。
     */
    std::thread worker_;

    /**
     * @brief 结果互斥锁。
     */
    std::mutex resultMutex_;

    /**
     * @brief 最近一次扫描结果。
     */
    std::unique_ptr<core::ScanResult> latestResult_;

    /**
     * @brief 正在懒加载到列表中的节点栈。
     */
    std::vector<PendingDisplayNode> pendingDisplayNodes_;

    /**
     * @brief 当前列表和 Treemap 正在展示的节点。
     */
    const core::ScanNode* currentViewNode_ = nullptr;

    /**
     * @brief 当前选中的右侧标签页索引。
     */
    int currentTabIndex_ = 0;

    /**
     * @brief 最近一次扫描耗时，单位为毫秒。
     */
    std::atomic_uint64_t lastScanMilliseconds_ = 0;

    /**
     * @brief 最近一次扫描是否使用了 NTFS MFT 极速通道。
     */
    std::atomic_bool lastScanUsedNtfsMft_ = false;

    /**
     * @brief 当前扫描开始时间。
     */
    std::chrono::steady_clock::time_point scanStartedAt_{};

    /**
     * @brief 最近进度中的文件数量。
     */
    std::atomic_uint64_t liveFileCount_ = 0;

    /**
     * @brief 最近进度中的目录数量。
     */
    std::atomic_uint64_t liveDirectoryCount_ = 0;

    /**
     * @brief 进度消息是否已经进入窗口消息队列。
     */
    std::atomic_bool progressMessagePending_ = false;

    /**
     * @brief 上次发布进度消息的时间点。
     */
    std::chrono::steady_clock::time_point lastProgressPost_{};

    /**
     * @brief 列表已显示行数。
     */
    int displayedRows_ = 0;

    /**
     * @brief 加载动画帧序号。
     */
    int loadingFrame_ = 0;

    /**
     * @brief 扫描结果是否已经完成并进入显示阶段。
     */
    bool scanResultReady_ = false;
};

}  // namespace disk_lens::app
