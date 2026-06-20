#pragma once

#include "core/DirectoryScanner.h"
#include "core/DiskHealth.h"
#include "core/FileHasher.h"
#include "core/NtfsMftScanner.h"
#include "core/ScanModels.h"
#include "qt/ResultTableModel.h"
#include "qt/TreemapWidget.h"
#include "qt/CategoryDonutWidget.h"
#include "qt/FileAgeHistogramWidget.h"

#include <QMainWindow>
#include <QString>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

class QAbstractItemView;
class QComboBox;
class QDateEdit;
class QCheckBox;
class QCloseEvent;
class QEvent;
class QFileSystemWatcher;
class QFrame;
class QIcon;
class QScrollArea;
class QVBoxLayout;
class QLabel;
class QLineEdit;
class QPoint;
class QProgressBar;
class QPushButton;
class QResizeEvent;
class QSplitter;
class QStackedWidget;
class QTabWidget;
class QTableWidget;
class QTableView;
class QTextEdit;
class QTimer;
class QTreeWidget;
class QTreeWidgetItem;
class QWidget;

namespace disk_lens::qt_ui {

/**
 * @brief 面向市场版 UI 的 Qt 主窗口。
 */
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    /**
     * @brief 构造主窗口。
     * @param parent 父级 Qt 控件。
     */
    explicit MainWindow(QWidget* parent = nullptr);

protected:
    /**
     * @brief 处理主窗口尺寸变化并同步加载遮罩尺寸。
     * @param event Qt 尺寸变化事件。
     */
    void resizeEvent(QResizeEvent* event) override;

    /**
     * @brief 关闭窗口时保存用户界面状态。
     * @param event Qt 关闭事件。
     */
    void closeEvent(QCloseEvent* event) override;

    /**
     * @brief 窗口首次显示时挂接屏幕换接 / DPI 变化信号,用于刷新图标缓存。
     * @param event Qt 显示事件。
     */
    void showEvent(QShowEvent* event) override;

    /**
     * @brief 拖拽进入窗口:仅当携带本地文件/目录 URL 时接受,决定拖放光标形态。
     * @param event Qt 拖拽进入事件。
     */
    void dragEnterEvent(QDragEnterEvent* event) override;

    /**
     * @brief 拖拽在窗口内移动:持续接受以保持光标。
     * @param event Qt 拖拽移动事件。
     */
    void dragMoveEvent(QDragMoveEvent* event) override;

    /**
     * @brief 放下:取首个本地路径,目录直接扫描、文件则扫描其所在目录。
     * @param event Qt 放下事件。
     */
    void dropEvent(QDropEvent* event) override;

    /**
     * @brief 跟踪空状态遮罩所在视口的尺寸与显隐，使遮罩始终铺满视口。
     * @param watched 被监视对象（各主功能页视图的视口）。
     * @param event Qt 事件。
     * @return 是否拦截事件。
     */
    bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
    /**
     * @brief 开始后台扫描。
     */
    void StartScan();

    /**
     * @brief 停止后台扫描。
     */
    void StopScan();

    /**
     * @brief 处理扫描完成事件。
     */
    void HandleScanFinished();

    /**
     * @brief 比较刚完成的扫描与该路径上次扫描的持久化基线,显著增长时显示告警横幅,并把当前结果写为新基线。
     */
    void EvaluateGrowthAlert();

    /**
     * @brief 实时监控的单一幂等仲裁器:据开关/模块页/扫描状态/latestResult_ 决定 arm 或 disarm watcher。
     *        所有转换点(扫完/切模块/开关/启动恢复缓存)统一调用;scanning_ 为真时早返回不碰路径。
     */
    void ReevaluateWatcher();
    /**
     * @brief directoryChanged 入口:启动防抖计时器(聚合一波变化)。
     */
    void ScheduleWatcherRescan();
    /**
     * @brief 防抖到期:在各前置条件满足且未冷却时触发 RescanPath 重扫;扫描中或冷却中则重排定时器。
     */
    void OnWatchDebounceTimeout();
    /**
     * @brief 计算应监视的路径列表:根 + 一级子目录(裸盘根返回空以避免全盘重扫循环)。
     */
    QStringList ComputeWatchPaths() const;
    /**
     * @brief 移除 watcher 当前所有路径(不清 watchedRootPath_,由调用方决定)。
     */
    void DisarmWatcher();
    /**
     * @brief 当前是否处于磁盘分析模块页(精确复刻 UpdateModuleChrome 的 isDiskAnalysisPage 判定)。
     */
    bool IsOnDiskAnalysisPage() const;

private:
    /**
     * @brief 快速搜索索引记录的前置声明。
     */
    struct SearchRecord;

    /**
     * @brief 快速搜索索引卷状态的前置声明。
     */
    struct SearchVolumeState;

    /**
     * @brief 使用管理员权限重启当前 Qt 程序。
     */
    void RestartAsAdministrator();

    /**
     * @brief 判断当前进程是否已经拥有管理员权限。
     * @return 已提权时返回 true。
     */
    bool IsRunningAsAdministrator() const;

    /**
     * @brief 显示目录树右键操作菜单。
     * @param position 目录树内的鼠标位置。
     */
    void ShowTreeContextMenu(const QPoint& position);

    /**
     * @brief 显示表格右键操作菜单。
     * @param table 触发菜单的表格。
     * @param position 表格内的鼠标位置。
     */
    void ShowTableContextMenu(QTableWidget* table, const QPoint& position);

    /**
     * @brief 显示类型统计专用右键操作菜单。
     * @param position 表格内的鼠标位置。
     */
    void ShowTypeStatsContextMenu(const QPoint& position);

    /**
     * @brief 显示垃圾清理专用右键操作菜单。
     * @param position 清理树内的鼠标位置。
     */
    void ShowCleanupContextMenu(const QPoint& position);

    /**
     * @brief 更新垃圾清理树的已选中空间汇总。
     */
    void UpdateCleanupSelectionSummary();

    /**
     * @brief 批量设置垃圾清理分组勾选状态。
     * @param mode 选择模式，safe 表示安全项，all 表示全部，none 表示全不选，default 表示恢复默认。
     */
    void SetCleanupCheckedMode(const QString& mode);

    /**
     * @brief 获取清理树条目对应的清理分组下标。
     * @param item 清理树条目。
     * @return 分组下标，无效时返回 cleanupGroups_.size()。
     */
    std::size_t CleanupGroupIndexFromItem(const QTreeWidgetItem* item) const;

    /**
     * @brief 应用垃圾清理分类筛选。
     * @param section 分类名称，空文本表示全部。
     */
    void ApplyCleanupSectionFilter(const QString& section);

    /**
     * @brief 获取当前选中的目录树节点。
     * @return 选中的扫描节点指针，没有选中时返回 nullptr。
     */
    const core::ScanNode* SelectedTreeNode() const;

    /**
     * @brief 获取表格当前选中行的路径。
     * @param table 目标表格。
     * @return 选中行路径，没有路径时返回空字符串。
     */
    QString SelectedTablePath(QTableWidget* table) const;

    /**
     * @brief 复制路径到系统剪贴板。
     * @param path 要复制的路径。
     */
    void CopyPathToClipboard(const QString& path);

    /**
     * @brief 复制文件或目录名称到系统剪贴板。
     * @param path 要复制名称的路径。
     */
    void CopyNameToClipboard(const QString& path);

    /**
     * @brief 复制父目录路径到系统剪贴板。
     * @param path 要读取父目录的路径。
     */
    void CopyParentPathToClipboard(const QString& path);

    /**
     * @brief 直接打开指定文件或目录。
     * @param path 要打开的路径。
     */
    void OpenPathDirectly(const QString& path);

    /**
     * @brief 在资源管理器中定位指定路径。
     * @param path 要定位的文件或目录路径。
     */
    void RevealPathInExplorer(const QString& path);

    /**
     * @brief 打开 Windows 文件属性窗口。
     * @param path 要查看属性的路径。
     */
    void ShowPathProperties(const QString& path);

    /**
     * @brief 显示软件内路径详情窗口。
     * @param path 要查看详情的路径。
     * @param scannedBytes 扫描结果中记录的大小。
     * @param hasScannedBytes 是否存在可用的扫描大小。
     */
    void ShowPathDetails(const QString& path, std::uint64_t scannedBytes = 0, bool hasScannedBytes = false);

    /**
     * @brief 将指定路径移入回收站。
     * @param path 要移入回收站的路径。
     */
    void MovePathToRecycleBin(const QString& path);

    /**
     * @brief 打开首选项对话框:外观主题 + 垃圾清理/去重默认选项,确定即写回并持久化。
     */
    void ShowPreferencesDialog();

    /**
     * @brief 向菜单添加通用路径操作。
     * @param menu 目标菜单。
     * @param path 操作路径。
     * @param includeDirectOpen 是否包含直接打开。
     * @param scannedBytes 扫描结果中记录的大小。
     * @param hasScannedBytes 是否存在可用的扫描大小。
     */
    void AddPathActions(QMenu& menu, const QString& path, bool includeDirectOpen, std::uint64_t scannedBytes = 0, bool hasScannedBytes = false);

    /**
     * @brief 扫描指定路径。
     * @param path 要扫描的文件夹或磁盘路径。
     */
    void RescanPath(const QString& path);

    /**
     * @brief 打开扫描位置选择对话框。
     */
    void BrowseScanLocation();

    /**
     * @brief 返回当前目录的上级目录。
     */
    void GoToParentDirectory();

    /**
     * @brief 根据路径查找扫描节点。
     * @param node 当前查找节点。
     * @param path 要查找的完整路径。
     * @return 找到的节点指针，未找到时返回 nullptr。
     */
    const core::ScanNode* FindNodeByPath(const core::ScanNode& node, const QString& path) const;

    /**
     * @brief 选择指定扫描节点并刷新右侧详情。
     * @param node 要展示的扫描节点。
     */
    void SelectNodeDetails(const core::ScanNode& node);

    /**
     * @brief 激活当前目录内容表格行，目录进入下级，文件打开。
     */
    void ActivateDirectoryTableRow();

    /**
     * @brief 查找指定节点的父目录节点。
     * @param current 当前查找节点。
     * @param target 要查找父级的目标节点。
     * @return 父目录节点，未找到时返回 nullptr。
     */
    const core::ScanNode* FindParentNode(const core::ScanNode& current, const core::ScanNode& target) const;

    /**
     * @brief 在后台加载上次扫描缓存。
     */
    void LoadLastScanCacheAsync();

    /**
     * @brief 加载窗口布局、列宽和常用扫描位置。
     */
    void LoadUiSettings();

    /**
     * @brief 保存窗口布局、列宽和常用扫描位置。
     */
    void SaveUiSettings() const;

    /**
     * @brief 注册专业工具常用快捷键。
     */
    void InstallShortcuts();

    /**
     * @brief 延迟一帧执行操作，让按钮点击反馈先完成绘制。
     * @param stateText 信息栏状态文本。
     * @param detailText 信息栏详情文本。
     * @param action 要延迟执行的操作。
     */
    void RunAfterClickFeedback(const QString& stateText, const QString& detailText, std::function<void()> action);

    /**
     * @brief 创建应用内菜单条。
     * @return 应用内菜单条控件。
     */
    QWidget* CreateApplicationMenu();

    /**
     * @brief 显示快捷键与操作说明。
     */
    void ShowShortcutHelp();

    /**
     * @brief 获取当前焦点区域选中项路径。
     * @return 当前选中项路径，没有选中时返回空字符串。
     */
    QString CurrentSelectedPath() const;

    /**
     * @brief 获取当前焦点区域选中项的扫描大小。
     * @param hasBytes 输出是否存在扫描大小。
     * @return 当前选中项扫描大小，没有可用大小时返回 0。
     */
    std::uint64_t CurrentSelectedScannedBytes(bool& hasBytes) const;

    /**
     * @brief 重建节点父级索引。
     */
    void RebuildParentIndex();

    /**
     * @brief 递归写入节点父级索引。
     * @param node 当前节点。
     * @param parent 父节点。
     */
    void IndexParentNodes(const core::ScanNode& node, const core::ScanNode* parent);

    /**
     * @brief 创建顶部命令栏。
     * @return 顶部命令栏控件。
     */
    QWidget* CreateCommandBar();

    /**
     * @brief 创建应用级主功能导航栏。
     * @return 主功能导航栏控件。
     */
    QWidget* CreateModuleSidebar();

    /**
     * @brief 创建扫描结果工作区。
     * @return 工作区控件。
     */
    QWidget* CreateWorkspace();

    /**
     * @brief 创建实时信息栏。
     * @return 实时信息栏控件。
     */
    QWidget* CreateInfoBar();

    /**
     * @brief 创建全局加载遮罩。
     * @param parent 遮罩父控件。
     * @return 加载遮罩控件。
     */
    QWidget* CreateLoadingOverlay(QWidget* parent);

    /**
     * @brief 创建分析表格。
     * @return 分析表格控件。
     */
    QTableWidget* CreateResultTable();

    /**
     * @brief 创建快速搜索标签页。
     * @return 快速搜索标签页控件。
     */
    QWidget* CreateSearchTab();

    /**
     * @brief 创建垃圾清理标签页。
     * @return 垃圾清理标签页控件。
     */
    QWidget* CreateCleanupTab();

    /**
     * @brief 创建底部空间图占位区域。
     * @return 空间图占位控件。
     */
    QWidget* CreateTreemapPanel();

    /**
     * @brief 根据当前功能页更新磁盘专属区域可见性。
     */
    void UpdateModuleChrome();

    /**
     * @brief 切换界面主题。
     * @param themeName 主题名称。
     */
    void SetTheme(const QString& themeName);

    /**
     * @brief 创建单个摘要指标。
     * @param title 指标标题。
     * @param value 指标数值。
     * @param valueTarget 用于保存指标数值标签的输出指针。
     * @return 指标控件。
     */
    QWidget* CreateMetric(const QString& title, const QString& value, QLabel** valueTarget);

    /**
     * @brief 应用整体视觉样式。
     */
    void ApplyStyle();

    /**
     * @brief 按当前主题强调色刷新工具栏动作按钮图标。
     *
     * 动作类图标颜色随主题变化，因此在 ApplyStyle 末尾调用，使浅色 / 暗色 / 蓝色皮肤切换后图标
     * 颜色同步更新。内容类图标（文件夹 / 文件 / 磁盘）使用固定配色，不在此刷新。
     */
    void ApplyActionIcons();

    /**
     * @brief 按当前主题刷新左侧功能导航按钮图标。
     *
     * 导航图标采用多态像素图：未选 Normal/Off 为次要文字色（灰），悬停 Active 与选中 On 态为
     * 强调色，使侧栏具备清晰的状态反馈。在 ApplyStyle 经 ApplyActionIcons 调用，皮肤切换后同步。
     */
    void UpdateModuleNavIcons();

    /**
     * @brief 屏幕换接 / 显示缩放变化后刷新图标。
     *
     * 清空 app_icons 缓存并重建动作 / 导航图标,使其按当前屏幕 devicePixelRatio 重新渲染。
     * 目录树 / 表格项的图标由 MakeIcon 的高 DPR 渲染基线保持清晰,无需逐项刷新。
     */
    void RefreshIconsForScreenChange();

    /**
     * @brief 初始化空状态内容。
     */
    void InitializeEmptyState();

    /**
     * @brief 为各主功能页挂载空状态遮罩，数据为空时居中显示引导卡片。
     */
    void InstallEmptyStateOverlays();

    /**
     * @brief 创建居中的空状态引导卡片。
     * @param view 所属滚动视图（遮罩作为其视口子控件）。
     * @param title 主标题。
     * @param hint 副标题。
     * @param icon 引导图标。
     * @return 空状态遮罩控件。
     */
    QFrame* CreateEmptyOverlay(QAbstractItemView* view, const QString& title, const QString& hint, const QIcon& icon);

    /**
     * @brief 将空状态遮罩可见性绑定到视图行数（无数据时显示）。
     * @param view 所属视图。
     * @param overlay 对应遮罩。
     */
    void AttachEmptyOverlay(QAbstractItemView* view, QFrame* overlay);

    /**
     * @brief 填充扫描结果。
     */
    void PopulateScanResult();

    /**
     * @brief 重置扫描结果相关的延迟表格状态。
     */
    void ResetDeferredTableStates();

    /**
     * @brief 按当前标签页加载延迟表格。
     */
    void PopulateCurrentDeferredTab();

    /**
     * @brief 填充目录树节点。
     * @param parent 父级目录树项。
     * @param node 扫描节点。
     */
    void PopulateTreeItem(QTreeWidgetItem* parent, const core::ScanNode& node);

    /**
     * @brief 填充目录内容表格。
     * @param node 要展示的目录节点。
     */
    void PopulateDirectoryTable(const core::ScanNode& node);

    /**
     * @brief 填充大文件表格。
     */
    void PopulateLargeFilesTable();

    /**
     * @brief 填充类型统计表格。
     */
    void PopulateTypeStatsTable();

    /**
     * @brief 填充文件年龄直方图(按字节加权把扫描结果卷成 7 个年龄分带)。
     */
    void PopulateAgeHistogram();

    /**
     * @brief 填充疑似重复树(快速视图:同名同大小分组,不哈希)。
     */
    void PopulateDuplicateTree();

    /**
     * @brief 启动内容深度校验(后台三段式 SHA-256),完成后用内容确认组替换树。
     */
    void StartDuplicateContentScan();

    /**
     * @brief 取消正在进行的重复内容校验。
     */
    void CancelDuplicateContentScan();

    /**
     * @brief 用内容校验结果填充重复树。
     */
    void PopulateDuplicateTreeFromContent(const std::vector<core::DuplicateGroup>& groups);

    /**
     * @brief 把当前重复分组模型重新渲染到树(用于勾选/删除后刷新)。
     */
    void RebuildDuplicateTreeFromModel();

    /**
     * @brief 删除勾选的重复文件(移入回收站或永久删除)。
     */
    void DeleteSelectedDuplicateItems();

    /**
     * @brief 批量设置重复树的勾选模式(all/none/keepFirst)。
     */
    void SetDuplicateCheckedMode(const QString& mode);

    /**
     * @brief 重复树的右键菜单。
     */
    void ShowDuplicateContextMenu(const QPoint& position);

    /**
     * @brief 更新重复树底部"已选中可回收"汇总。
     */
    void UpdateDuplicateSelectedSummary();

    /**
     * @brief 构造重复文件页(头部操作条 + 树 + 底部操作条)。
     */
    QWidget* CreateDuplicateTab();

    /**
     * @brief 构造磁盘健康页(头部操作条 + 每盘一卡的健康信息卡片)。
     */
    QWidget* CreateHealthTab();

    /**
     * @brief 后台读取所有物理盘 SMART/健康信息并填充健康表格。
     */
    void RefreshDiskHealth();

    /**
     * @brief 用最新健康快照构建每盘一卡的健康信息卡片;清空旧卡片。
     */
    void PopulateHealthCards(const std::vector<disk_lens::core::DiskHealthInfo>& infos);

    /**
     * @brief 构建单块物理盘的健康信息卡片(型号 + 状态徽章 + 健康度条 + 指标网格)。
     */
    QFrame* BuildHealthCard(const disk_lens::core::DiskHealthInfo& info, int row);

    /**
     * @brief 取消正在进行的磁盘健康后台读取。
     */
    void CancelDiskHealth();

    /**
     * @brief 弹出磁盘健康详情对话框,完整展示单块物理盘的全部健康指标与诊断备注。
     *
     * 卡片字段精简,长文本(序列号、诊断备注)展开不便;
     * 此对话框以全宽展开所有字段,并把失败原因备注单独成可换行、可选中复制的段落。
     * @param row 健康卡片序号(对应 healthInfos_ 的下标)。
     */
    void ShowHealthDetailDialog(int row);

    /**
     * @brief 填充长期未动文件表格（按修改时间最旧排序）。
     */
    void PopulateStaleFilesTable();

    /**
     * @brief 根据当前搜索框填充快速搜索结果。
     */
    void PopulateSearchTable();

    /**
     * @brief 渲染当前快速搜索结果的可见部分。
     * @param keyword 当前搜索关键字。
     */
    void RenderVisibleSearchResults(const QString& keyword);

    /**
     * @brief 加载更多快速搜索结果。
     */
    void LoadMoreSearchResults();

    /**
     * @brief 开始构建全系统快速搜索索引。
     */
    void StartSystemSearchIndex();

    /**
     * @brief 加载本地保存的全系统快速搜索索引。
     */
    void LoadSystemSearchIndexCache();

    /**
     * @brief 按需触发快速搜索索引缓存加载。
     */
    void EnsureSearchIndexCacheLoading();

    /**
     * @brief 保存当前全系统快速搜索索引。
     */
    void SaveSystemSearchIndexCache() const;

    /**
     * @brief 将指定快速搜索索引快照保存到缓存文件。
     * @param records 要保存的索引记录。
     * @param volumeStates 要保存的卷增量状态。
     */
    static void SaveSystemSearchIndexCacheSnapshot(const std::vector<SearchRecord>& records, const std::vector<SearchVolumeState>& volumeStates);

    /**
     * @brief 扫描安全垃圾清理候选项。
     */
    void ScanCleanupCandidates();

    /**
     * @brief 清理垃圾清理表格中选中的候选项。
     */
    void DeleteSelectedCleanupItems();

    /**
     * @brief 延迟触发快速搜索，避免输入过程中频繁刷新表格。
     */
    void ScheduleSearch();

    /**
     * @brief 重建快速搜索索引。
     */
    void RebuildSearchIndex();

    /**
     * @brief 收集文件节点。
     * @param node 扫描节点。
     * @param output 输出文件列表。
     */
    void CollectFiles(const core::ScanNode& node, std::vector<const core::ScanNode*>& output) const;

    /**
     * @brief 收集匹配快速搜索关键字的节点。
     * @param node 扫描节点。
     * @param keyword 搜索关键字。
     * @param output 输出节点列表。
     */
    void CollectSearchMatches(const core::ScanNode& node, const QString& keyword, std::vector<const core::ScanNode*>& output) const;

    /**
     * @brief 收集快速搜索索引记录。
     * @param node 扫描节点。
     */
    void CollectSearchIndex(const core::ScanNode& node);

    /**
     * @brief 向表格添加一行。
     * @param table 目标表格。
     * @param name 名称。
     * @param size 大小。
     * @param type 类型。
     * @param path 路径。
     */
    void AddTableRow(QTableWidget* table, const QString& name, const QString& size, const QString& type, const QString& path);

    /**
     * @brief 向目录内容表格添加一行并绑定扫描节点。
     * @param node 扫描节点。
     */
    void AddDirectoryTableNodeRow(const core::ScanNode& node);

    /**
     * @brief 添加垃圾清理候选行。
     * @param name 名称。
     * @param size 大小。
     * @param type 类型。
     * @param path 路径。
     */
    void AddCleanupRow(const QString& name, const QString& size, const QString& type, const QString& path);

    /**
     * @brief 开始批量更新表格，减少刷新卡顿。
     * @param table 目标表格。
     */
    void BeginTableUpdate(QTableWidget* table) const;

    /**
     * @brief 结束批量更新表格。
     * @param table 目标表格。
     */
    void EndTableUpdate(QTableWidget* table) const;

    /**
     * @brief 判断节点是否匹配筛选框。
     * @param node 扫描节点。
     * @return 匹配时返回 true。
     */
    bool MatchesFilter(const core::ScanNode& node) const;

    /**
     * @brief 打开当前标签页选中的路径。
     */
    void OpenSelectedPath();

    /**
     * @brief 导出当前标签页表格为 CSV。
     */
    void ExportCurrentTable();

    /**
     * @brief 获取当前标签页表格。
     * @return 当前表格指针。
     */
    QTableWidget* CurrentTable() const;

    /**
     * @brief 设置实时信息栏。
     * @param state 状态文本。
     * @param files 文件数量。
     * @param directories 目录数量。
     * @param path 当前路径。
     */
    void SetInfoBar(const QString& state, std::uint64_t files, std::uint64_t directories, const QString& path);

    /**
     * @brief 刷新「文件搜索」模块底部信息栏（命中数 / 关键字 / 耗时）。
     */
    void UpdateSearchInfoBar();

    /**
     * @brief 切换全局忙碌动画状态。
     * @param busy 是否正在执行后台任务。
     * @param text 忙碌时显示的状态文本。
     */
    void SetBusyState(bool busy, const QString& text);

    /**
     * @brief 预显示加载遮罩，不改变后台任务计数。
     * @param title 遮罩标题文本。
     * @param detail 遮罩说明文本。
     */
    void PrimeLoadingFeedback(const QString& title, const QString& detail);

    /**
     * @brief 推进状态栏忙碌动画帧。
     */
    void AdvanceBusyAnimation();

    /**
     * @brief 立即刷新关键视觉反馈。
     */
    void FlushImmediateFeedback();

    /**
     * @brief 根据主窗口尺寸更新加载遮罩覆盖范围。
     */
    void UpdateLoadingOverlayGeometry();

    /**
     * @brief 向表格显示加载中的占位行。
     * @param table 目标表格。
     * @param title 加载标题。
     * @param detail 加载说明。
     */
    void ShowLoadingRow(QTableWidget* table, const QString& title, const QString& detail);

    /**
     * @brief 扫描位置选择框。
     */
    QComboBox* driveCombo_ = nullptr;

    /**
     * @brief 浏览扫描目录按钮。
     */
    QPushButton* browseButton_ = nullptr;

    /**
     * @brief 主窗口中央根控件。
     */
    QWidget* rootWidget_ = nullptr;

    /**
     * @brief 磁盘分析专属命令栏。
     */
    QWidget* commandBar_ = nullptr;

    /**
     * @brief 查找输入框。
     */
    QLineEdit* filterEdit_ = nullptr;

    /**
     * @brief 扫描按钮。
     */
    QPushButton* scanButton_ = nullptr;

    /**
     * @brief 停止按钮。
     */
    QPushButton* stopButton_ = nullptr;

    /**
     * @brief 极速模式按钮。
     */
    QPushButton* boostButton_ = nullptr;

    /**
     * @brief 导出按钮。
     */
    QPushButton* exportButton_ = nullptr;

    /**
     * @brief 打开位置按钮。
     */
    QPushButton* openButton_ = nullptr;

    /**
     * @brief 左侧目录树。
     */
    QTreeWidget* directoryTree_ = nullptr;

    /**
     * @brief 右侧分析标签页。
     */
    QTabWidget* tabs_ = nullptr;

    /**
     * @brief 主功能导航中的磁盘分析按钮。
     */
    QPushButton* diskModuleButton_ = nullptr;

    /**
     * @brief 主功能导航中的文件搜索按钮。
     */
    QPushButton* searchModuleButton_ = nullptr;

    /**
     * @brief 主功能导航中的垃圾清理按钮。
     */
    QPushButton* cleanupModuleButton_ = nullptr;

    /**
     * @brief 主功能导航中的磁盘健康按钮。
     */
    QPushButton* healthModuleButton_ = nullptr;

    /**
     * @brief 主工作区分割栏。
     */
    QSplitter* workspaceSplitter_ = nullptr;

    /**
     * @brief 磁盘分析顶部指标区。
     */
    QWidget* metricsPanel_ = nullptr;

    /**
     * @brief 磁盘分析右侧空间概览区。
     */
    QWidget* treemapPanel_ = nullptr;

    /**
     * @brief 当前界面主题名称。
     */
    QString currentTheme_ = QStringLiteral("light");

    /**
     * @brief 屏幕换接 / DPI 信号是否已挂接(在首次 showEvent 时挂一次)。
     */
    bool screenHooksWired_ = false;

    /**
     * @brief 目录内容表格。
     */
    QTableWidget* directoryTable_ = nullptr;

    /**
     * @brief 目录内容虚拟表格。
     */
    QTableView* directoryView_ = nullptr;

    /**
     * @brief 目录内容虚拟模型。
     */
    ResultTableModel* directoryModel_ = nullptr;

    /**
     * @brief 大文件表格。
     */
    QTableWidget* largeFilesTable_ = nullptr;

    /**
     * @brief 大文件虚拟表格。
     */
    QTableView* largeFilesView_ = nullptr;

    /**
     * @brief 大文件虚拟模型。
     */
    ResultTableModel* largeFilesModel_ = nullptr;

    /**
     * @brief 类型统计表格。
     */
    QTableWidget* typeStatsTable_ = nullptr;

    /**
     * @brief 类型统计页(QSplitter:左侧表格 + 右侧分类环形图)。作为 QTabWidget 的页控件。
     */
    QWidget* typeStatsPage_ = nullptr;

    /**
     * @brief 类型统计页右侧的分类占比环形图。
     */
    CategoryDonutWidget* typeStatsDonut_ = nullptr;

    /**
     * @brief 文件年龄分布直方图页(作为标签页直接挂载的自绘控件,setCurrentWidget/页签判定都用它)。
     */
    FileAgeHistogramWidget* ageHistogramWidget_ = nullptr;

    /**
     * @brief 疑似重复树(顶层=去重组,子项=各重复文件,带勾选框)。
     */
    QTreeWidget* duplicateTree_ = nullptr;

    /**
     * @brief 疑似重复页(作为标签页直接挂载的容器,setCurrentWidget/页签判定用它)。
     */
    QWidget* duplicatePage_ = nullptr;
    /**
     * @brief 重复页状态文案(模式 / 进度 / 可回收量)。
     */
    QLabel* duplicateStatusLabel_ = nullptr;

    /**
     * @brief 重复页底部已选中可回收汇总。
     */
    QLabel* duplicateSelectedLabel_ = nullptr;

    /**
     * @brief 内容深度校验按钮(启动后台 SHA-256 去重)。
     */
    QPushButton* duplicateDeepScanButton_ = nullptr;

    /**
     * @brief 快速视图按钮(同名同大小,不哈希)。
     */
    QPushButton* duplicateQuickButton_ = nullptr;

    /**
     * @brief 取消正在进行的重复内容校验。
     */
    QPushButton* duplicateCancelButton_ = nullptr;

    /**
     * @brief 永久删除开关(不进回收站)。
     */
    QCheckBox* duplicatePermanentCheckBox_ = nullptr;

    /**
     * @brief 勾选:每组保留首项,其余待删。
     */
    QPushButton* duplicateKeepFirstButton_ = nullptr;

    /**
     * @brief 一键去重按钮(移入回收站 / 永久删除)。
     */
    QPushButton* duplicateDeleteButton_ = nullptr;

    /**
     * @brief 磁盘健康页(标签页直接挂载的容器,页签判定用它)。
     */
    QWidget* healthPage_ = nullptr;

    /**
     * @brief 健康页卡片滚动容器(每盘一卡,竖向排列)。
     */
    QScrollArea* healthScroll_ = nullptr;

    /**
     * @brief 卡片宿主容器(承载所有健康卡片 + 末尾弹簧)。
     */
    QWidget* healthCardsHost_ = nullptr;

    /**
     * @brief 卡片宿主的竖向布局。
     */
    QVBoxLayout* healthCardsLayout_ = nullptr;

    /**
     * @brief 健康页空状态占位(无数据 / 非管理员时居中提示)。
     */
    QLabel* healthEmptyHint_ = nullptr;

    /**
     * @brief 磁盘健康页状态文案(进度 / 提示)。
     */
    QLabel* healthStatusLabel_ = nullptr;

    /**
     * @brief 磁盘健康模块信息栏文案(盘数 / 状态汇总)。
     */
    QLabel* healthInfoLabel_ = nullptr;

    /**
     * @brief 长期未动文件虚拟结果表。
     */
    QTableView* staleFilesView_ = nullptr;

    /**
     * @brief 长期未动文件虚拟结果模型。
     */
    ResultTableModel* staleFilesModel_ = nullptr;

    /**
     * @brief 快速搜索输入框。
     */
    QLineEdit* searchEdit_ = nullptr;

    /**
     * @brief 快速搜索全系统索引按钮。
     */
    QPushButton* searchIndexButton_ = nullptr;

    /**
     * @brief 快速搜索索引范围提示。
     */
    QLabel* searchScopeLabel_ = nullptr;

    /**
     * @brief 快速搜索加载更多结果按钮。
     */
    QPushButton* searchLoadMoreButton_ = nullptr;

    /**
     * @brief 搜索结果筛选条：修改时间范围(全部/今天/近7天/近30天/近一年/自定义)。
     */
    QComboBox* searchTimeFilterCombo_ = nullptr;

    /**
     * @brief 搜索结果筛选条：文件大小范围(全部/<10MB/10-100MB/100MB-1GB/>1GB)。
     */
    QComboBox* searchSizeFilterCombo_ = nullptr;

    /**
     * @brief 搜索结果筛选条：仅文件/仅目录。
     */
    QComboBox* searchTypeFilterCombo_ = nullptr;

    /**
     * @brief 自定义修改时间起止(仅"自定义"时间时可见)。
     */
    QDateEdit* searchStartDateEdit_ = nullptr;
    QDateEdit* searchEndDateEdit_ = nullptr;

    /**
     * @brief 清除全部筛选条件按钮。
     */
    QPushButton* searchClearFilterButton_ = nullptr;

    /**
     * @brief 快速搜索防抖计时器。
     */
    QTimer* searchDebounceTimer_ = nullptr;

    /**
     * @brief 实时文件夹监控(E2):监视已扫描根及其一级子目录,变化经防抖触发自动重扫。默认关闭(liveWatchEnabled_)。
     */
    QFileSystemWatcher* folderWatcher_ = nullptr;
    /**
     * @brief 实时监控的防抖计时器(聚合一波 directoryChanged 后再重扫),单次触发 500ms。
     */
    QTimer* watchDebounceTimer_ = nullptr;
    /**
     * @brief 是否启用实时文件夹监控(QSettings 键 watch/liveEnabled,默认 false)。
     */
    bool liveWatchEnabled_ = false;
    /**
     * @brief 当前 watcher 已 arm 的根路径(归一为原生分隔符、去尾分隔符,与 driveCombo 对齐)。
     */
    QString watchedRootPath_;
    /**
     * @brief 当前 watcher 实际监视的路径列表(幂等 swap 检测)。
     */
    QStringList currentWatchPaths_;
    /**
     * @brief 上一次 watcher 触发重扫的时刻(epoch ms),节流用。
     */
    qint64 lastWatcherRescanMsec_ = 0;
    /**
     * @brief watcher 重扫冷却上限(epoch ms);防外部搅动导致的稳态重扫循环。
     */
    qint64 watcherCooldownUntilMsec_ = 0;

    /**
     * @brief 快速搜索结果表格。
     */
    QTableWidget* searchTable_ = nullptr;

    /**
     * @brief 快速搜索虚拟结果表。
     */
    QTableView* searchView_ = nullptr;

    /**
     * @brief 快速搜索虚拟结果模型。
     */
    ResultTableModel* searchModel_ = nullptr;

    /**
     * @brief 当前关键字对应的完整搜索结果。
     */
    QVector<ResultRow> searchResultRows_;

    /**
     * @brief 当前已经展示到表格里的搜索结果数量。
     */
    int searchVisibleResultCount_ = 0;

    /**
     * @brief 当前关键字匹配到的去重结果总数。
     */
    std::uint64_t searchTotalMatchCount_ = 0;

    /**
     * @brief 快速搜索滚动自动加载是否正在节流。
     */
    bool searchAutoLoadPending_ = false;

    /**
     * @brief 垃圾清理树形清单。
     */
    QTreeWidget* cleanupTree_ = nullptr;

    /**
     * @brief 扫描垃圾按钮。
     */
    QPushButton* cleanupScanButton_ = nullptr;

    /**
     * @brief 清理选中项按钮。
     */
    QPushButton* cleanupDeleteButton_ = nullptr;

    /**
     * @brief 垃圾清理摘要文本。
     */
    QLabel* cleanupSummaryLabel_ = nullptr;

    /**
     * @brief 垃圾清理已选中项目汇总文本。
     */
    QLabel* cleanupSelectedLabel_ = nullptr;

    /**
     * @brief 垃圾清理分类概览卡片数值。
     */
    std::vector<QLabel*> cleanupSectionValueLabels_;

    /**
     * @brief 垃圾清理分类筛选按钮。
     */
    std::vector<QPushButton*> cleanupSectionButtons_;

    /**
     * @brief 当前垃圾清理分类筛选。
     */
    QString cleanupSectionFilter_;

    /**
     * @brief 垃圾清理可释放空间总览。
     */
    QLabel* cleanupTotalLabel_ = nullptr;

    /**
     * @brief 垃圾清理安全项目数量。
     */
    QLabel* cleanupSafeCountLabel_ = nullptr;

    /**
     * @brief 垃圾清理需确认项目数量。
     */
    QLabel* cleanupAttentionCountLabel_ = nullptr;

    /**
     * @brief 垃圾清理当前建议状态。
     */
    QLabel* cleanupStatusLabel_ = nullptr;

    /**
     * @brief 是否直接删除清理项，不移入回收站。
     */
    QCheckBox* cleanupDeepCleanCheckBox_ = nullptr;

    /**
     * @brief 是否扫描隐私痕迹类清理项。
     */
    QCheckBox* cleanupPrivacyCheckBox_ = nullptr;

    /**
     * @brief 是否扫描开发工具缓存类清理项。
     */
    QCheckBox* cleanupDeveloperCheckBox_ = nullptr;

    /**
     * @brief 垃圾清理树是否正在程序化更新。
     */
    bool cleanupTreeUpdating_ = false;

    /**
     * @brief 快速搜索索引记录。
     */
    struct SearchRecord {
        /**
         * @brief 名称。
         */
        QString name;

        /**
         * @brief 格式化大小。
         */
        QString size;

        /**
         * @brief 类型。
         */
        QString type;

        /**
         * @brief 完整路径。
         */
        QString path;

        /**
         * @brief 预计算的小写搜索键。
         */
        QString searchKey;

        /**
         * @brief 原始大小，单位为字节。
         */
        std::uint64_t bytes = 0;

        /**
         * @brief 所属卷根路径。
         */
        QString volumeRoot;

        /**
         * @brief NTFS 文件引用号低 48 位。
         */
        std::uint64_t fileReference = 0;

        /**
         * @brief NTFS 父目录文件引用号低 48 位。
         */
        std::uint64_t parentReference = 0;

        /**
         * @brief 最后修改时间，Unix epoch 毫秒；0 表示未采集。
         */
        qint64 lastModifiedMsec = 0;
    };

    /**
     * @brief 快速搜索索引对应的卷增量状态。
     */
    struct SearchVolumeState {
        /**
         * @brief 卷根路径。
         */
        QString rootPath;

        /**
         * @brief Windows 卷序列号。
         */
        std::uint64_t volumeSerialNumber = 0;

        /**
         * @brief USN Journal 唯一标识。
         */
        std::uint64_t journalId = 0;

        /**
         * @brief 当前 Journal 最早可读 USN。
         */
        std::int64_t firstUsn = 0;

        /**
         * @brief 下次增量读取起始 USN。
         */
        std::int64_t nextUsn = 0;

        /**
         * @brief 状态是否可以用于增量更新。
         */
        bool valid = false;
    };

    /**
     * @brief 收集指定路径下的全系统快速搜索索引记录。
     * @param rootPath 要索引的根路径。
     * @param output 输出索引记录。
     * @param cancelFlag 取消标志，外部置 true 时提前停止。
     */
    void CollectSystemSearchIndex(const QString& rootPath, std::vector<SearchRecord>& output, const std::atomic_bool& cancelFlag) const;

    /**
     * @brief 使用 NTFS MFT 扁平索引收集指定卷的快速搜索记录。
     * @param rootPath 要索引的卷根路径。
     * @param output 输出索引记录。
     * @param volumeState 输出卷增量状态。
     * @param cancelFlag 取消标志，外部置 true 时提前停止。
     * @return 成功使用 NTFS 扁平索引时返回 true。
     */
    bool CollectNtfsSearchIndex(const QString& rootPath, std::vector<SearchRecord>& output, SearchVolumeState& volumeState, const std::atomic_bool& cancelFlag) const;

    /**
     * @brief 尝试用 USN Journal 增量刷新搜索索引缓存。
     * @param records 搜索索引记录，会被就地更新。
     * @param volumeStates 卷增量状态，会被更新到最新状态。
     * @return 成功应用增量时返回 true；需要全量重建时返回 false。
     */
    bool RefreshSearchIndexFromJournal(std::vector<SearchRecord>& records, std::vector<SearchVolumeState>& volumeStates) const;

    /**
     * @brief 从扫描树收集快速搜索索引记录到指定容器。
     * @param node 扫描节点。
     * @param output 输出索引记录。
     * @param cancelFlag 取消标志，外部置 true 时提前停止。
     */
    void CollectSearchIndexFromNode(const core::ScanNode& node, std::vector<SearchRecord>& output, const std::atomic_bool& cancelFlag) const;

    /**
     * @brief 垃圾清理分组。
     */
    struct CleanupGroup {
        /**
         * @brief 分组名称。
         */
        QString name;

        /**
         * @brief 分组说明。
         */
        QString description;

        /**
         * @brief 分组所属大类。
         */
        QString section;

        /**
         * @brief 清理建议。
         */
        QString recommendation;

        /**
         * @brief 风险级别。
         */
        QString risk;

        /**
         * @brief 分组总大小。
         */
        std::uint64_t bytes = 0;

        /**
         * @brief 分组包含的路径列表。
         */
        std::vector<QString> paths;

        /**
         * @brief 分组内每个路径对应的可释放字节数。
         */
        std::vector<std::uint64_t> pathBytes;

        /**
         * @brief 是否默认勾选。
         */
        bool checkedByDefault = false;
    };

    /**
     * @brief 当前扫描结果对应的快速搜索索引。
     */
    std::shared_ptr<std::vector<SearchRecord>> searchIndex_ = std::make_shared<std::vector<SearchRecord>>();

    /**
     * @brief 当前快速搜索索引对应的卷增量状态。
     */
    std::shared_ptr<std::vector<SearchVolumeState>> searchVolumeStates_ = std::make_shared<std::vector<SearchVolumeState>>();

    /**
     * @brief 快速搜索请求序号，用于丢弃过期后台查询结果。
     */
    std::atomic_uint64_t searchRequestId_ = 0;

    /**
     * @brief 全系统快速搜索索引是否正在构建。
     */
    std::atomic_bool searchIndexing_ = false;

    /**
     * @brief 全系统快速搜索索引缓存是否正在加载。
     */
    std::atomic_bool searchCacheLoading_ = false;

    /**
     * @brief 搜索索引缓存是否已经尝试加载。
     */
    bool searchCacheLoadRequested_ = false;

    /**
     * @brief 大文件表格是否已经为当前扫描结果加载。
     */
    bool largeFilesTableLoaded_ = false;

    /**
     * @brief 类型统计表格是否已经为当前扫描结果加载。
     */
    bool typeStatsTableLoaded_ = false;

    /**
     * @brief 文件年龄直方图是否已经为当前扫描结果加载。
     */
    bool ageHistogramLoaded_ = false;

    /**
     * @brief 疑似重复树是否已经为当前扫描结果加载。
     */
    bool duplicateTreeLoaded_ = false;

    /**
     * @brief 是否正在后台执行重复内容校验。
     */
    std::atomic_bool duplicateHashing_{false};

    /**
     * @brief 重复内容校验取消标记。
     */
    std::atomic_bool duplicateHashCancel_{false};

    /**
     * @brief 是否正在后台执行磁盘健康读取。
     */
    std::atomic_bool healthQuerying_{false};

    /**
     * @brief 磁盘健康读取取消标记。
     */
    std::atomic_bool healthQueryCancel_{false};

    /**
     * @brief 最近一次读取到的物理盘健康快照(供表格刷新/导出复用)。
     */
    std::vector<disk_lens::core::DiskHealthInfo> healthInfos_;

    /**
     * @brief 长期未动文件表格是否已经为当前扫描结果加载。
     */
    bool staleFilesTableLoaded_ = false;

    /**
     * @brief 当前垃圾清理扫描分组。
     */
    std::vector<CleanupGroup> cleanupGroups_;

    /**
     * @brief 重复文件单个成员(显示信息 + 字节)。
     */
    struct DuplicateMemberUi {
        /**
         * @brief 显示路径。
         */
        QString path;

        /**
         * @brief 字节数。
         */
        std::uint64_t bytes = 0;

        /**
         * @brief 文件名。
         */
        QString name;

        /**
         * @brief 修改时间文案。
         */
        QString modifiedText;
    };

    /**
     * @brief 重复文件分组(contentConfirmed=true 经 SHA-256 确认,false 为快速同名同大小)。
     */
    struct DuplicateGroupUi {
        /**
         * @brief 每个成员字节数(组内一致)。
         */
        std::uint64_t bytes = 0;

        /**
         * @brief 成员列表(至少 2)。
         */
        std::vector<DuplicateMemberUi> members;

        /**
         * @brief 是否经过内容哈希确认。
         */
        bool contentConfirmed = false;
    };

    /**
     * @brief 当前重复页分组模型(快速视图或内容校验结果)。
     */
    std::vector<DuplicateGroupUi> duplicateGroups_;

    /**
     * @brief 空间占比图占位说明。
     */
    QLabel* treemapHint_ = nullptr;

    /**
     * @brief 空间占比图控件。
     */
    TreemapWidget* treemapWidget_ = nullptr;

    /**
     * @brief 总占用指标数值。
     */
    QLabel* totalMetricLabel_ = nullptr;

    /**
     * @brief 文件数量指标数值。
     */
    QLabel* fileMetricLabel_ = nullptr;

    /**
     * @brief 目录数量指标数值。
     */
    QLabel* directoryMetricLabel_ = nullptr;

    /**
     * @brief 扫描模式指标数值。
     */
    QLabel* modeMetricLabel_ = nullptr;

    /**
     * @brief 磁盘增长告警横幅(置于 metricsPanel_ 内,默认隐藏;扫描完成且相较上次基线显著增长时显示)。
     */
    QFrame* growthAlertFrame_ = nullptr;

    /**
     * @brief 增长告警横幅的正文标签("自 <上次时间> 以来,增长 X · Y%")。
     */
    QLabel* growthAlertBodyLabel_ = nullptr;

    /**
     * @brief 实时状态文本。
     */
    QLabel* stateLabel_ = nullptr;

    /**
     * @brief 底部信息栏堆栈：磁盘 / 搜索 / 清理各一页，随模块切换。
     */
    QStackedWidget* infoBarStack_ = nullptr;

    /**
     * @brief 「文件搜索」模块底部信息栏文本。
     */
    QLabel* searchInfoLabel_ = nullptr;

    /**
     * @brief 「垃圾清理」模块底部信息栏文本。
     */
    QLabel* cleanupInfoLabel_ = nullptr;

    /**
     * @brief 最近一次搜索关键字（切换到搜索模块时刷新信息栏用）。
     */
    QString lastSearchKeyword_;

    /**
     * @brief 最近一次搜索耗时（毫秒）。
     */
    double lastSearchElapsedMs_ = 0.0;

    /**
     * @brief 实时状态忙碌动画点。
     */
    QLabel* busyIndicatorLabel_ = nullptr;

    /**
     * @brief 忙碌动画计时器。
     */
    QTimer* busyAnimationTimer_ = nullptr;

    /**
     * @brief 全局加载遮罩。
     */
    QFrame* loadingOverlay_ = nullptr;

    /**
     * @brief 加载遮罩旋转符号。
     */
    QLabel* loadingSpinnerLabel_ = nullptr;

    /**
     * @brief 加载遮罩标题。
     */
    QLabel* loadingTitleLabel_ = nullptr;

    /**
     * @brief 加载遮罩说明。
     */
    QLabel* loadingDetailLabel_ = nullptr;

    /**
     * @brief 加载遮罩内部循环进度条。
     */
    QProgressBar* loadingProgressBar_ = nullptr;

    /**
     * @brief 忙碌动画当前帧序号。
     */
    int busyAnimationFrame_ = 0;

    /**
     * @brief 当前正在运行的忙碌任务数量。
     */
    int busyTaskCount_ = 0;

    /**
     * @brief 文件数量文本。
     */
    QLabel* filesLabel_ = nullptr;

    /**
     * @brief 目录数量文本。
     */
    QLabel* directoriesLabel_ = nullptr;

    /**
     * @brief 扫描速度文本。
     */
    QLabel* speedLabel_ = nullptr;

    /**
     * @brief 扫描耗时文本。
     */
    QLabel* elapsedLabel_ = nullptr;

    /**
     * @brief 当前路径文本。
     */
    QLabel* pathLabel_ = nullptr;

    /**
     * @brief 后台目录扫描器。
     */
    core::DirectoryScanner scanner_;

    /**
     * @brief NTFS MFT 极速扫描器。
     */
    core::NtfsMftScanner ntfsScanner_;

    /**
     * @brief 最近一次扫描结果。
     */
    std::shared_ptr<core::ScanResult> latestResult_;

    /**
     * @brief 最近一次扫描是否使用 NTFS MFT 极速通道。
     */
    bool lastScanUsedNtfsMft_ = false;

    /**
     * @brief 最近一次扫描展示用模式文本。
     */
    QString lastScanModeText_ = QStringLiteral("兼容");

    /**
     * @brief 最近一次扫描模式说明。
     */
    QString lastScanModeDetail_;

    /**
     * @brief 当前右侧目录内容表格展示的目录节点。
     */
    const core::ScanNode* currentDirectoryNode_ = nullptr;

    /**
     * @brief 扫描节点到父节点的索引，用于快速返回上级。
     */
    std::unordered_map<const core::ScanNode*, const core::ScanNode*> parentByNode_;

    /**
     * @brief 扫描是否正在运行。
     */
    std::atomic_bool scanning_ = false;

    /**
     * @brief 垃圾清理扫描是否正在运行。
     */
    std::atomic_bool cleanupScanning_ = false;

    /**
     * @brief 扫描开始时间。
     */
    std::chrono::steady_clock::time_point scanStartedAt_{};

    /**
     * @brief 上次 UI 进度刷新时间，单位为毫秒。
     */
    std::atomic_int64_t lastUiProgressMilliseconds_ = 0;

    /**
     * @brief 空状态遮罩与其所属视图的配对，用于尺寸跟随与可见性同步。
     */
    struct EmptyOverlayEntry {
        /**
         * @brief 所属滚动视图。
         */
        QAbstractItemView* view = nullptr;

        /**
         * @brief 空状态遮罩。
         */
        QFrame* overlay = nullptr;
    };

    /**
     * @brief 各主功能页空状态遮罩集合。
     */
    std::vector<EmptyOverlayEntry> emptyOverlays_;
};

}  // namespace disk_lens::qt_ui
