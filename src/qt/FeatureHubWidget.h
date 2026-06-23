#pragma once

#include <QByteArray>
#include <QMap>
#include <QPointer>
#include <QString>
#include <QVector>
#include <QSet>
#include <QWidget>

#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>

class QLabel;
class QComboBox;
class QLineEdit;
class QListWidget;
class QPoint;
class QPushButton;
class QTreeWidget;
class QToolButton;
class QFrame;

namespace disk_lens::qt_ui {

/**
 * @brief 新功能工具箱中的独立能力模块。
 */
enum class FeatureModule {
    GrowthTrace,
    SoftwareFootprint,
    AppMover,
    ArchiveAssistant,
    DownloadOrganizer,
    PrivacyRadar,
    DeveloperSpace,
    DockerWsl,
    MediaOrganizer,
    QuotaBudget,
    BackupGap,
    FileUnlocker,
    TransferAssistant,
    CloudSync,
    RestorePoint,
    BrowserCache,
    StartupFootprint,
    MessengerCache,
    MailArchive,
    VirtualMachineImages,
};

/**
 * @brief 新功能工具箱中的一条检测结果。
 */
struct FeatureFinding {
    /**
     * @brief 结果所属模块。
     */
    FeatureModule module = FeatureModule::GrowthTrace;

    /**
     * @brief 结果标题。
     */
    QString title;

    /**
     * @brief 风险、状态或类别文本。
     */
    QString state;

    /**
     * @brief 结果说明。
     */
    QString detail;

    /**
     * @brief 关联路径或系统入口。
     */
    QString path;

    /**
     * @brief 关联空间大小，单位为字节。
     */
    std::uint64_t bytes = 0;
};

/**
 * @brief 承载“空间增长溯源 / 软件体积 / 应用搬家 / 隐私雷达”等新增模块的统一工具箱页。
 */
class FeatureHubWidget : public QWidget {
    Q_OBJECT

public:
    /**
     * @brief 构造新功能工具箱页面。
     * @param parent 父级 Qt 控件。
     */
    explicit FeatureHubWidget(QWidget* parent = nullptr);

    /**
     * @brief 析构:置退出标志并同步回收后台扫描线程,杜绝悬垂回投。
     */
    ~FeatureHubWidget();

    /**
     * @brief 退出前同步收尾:置退出标志、取消在途扫描并 join 工作线程。
     *
     * MainWindow 真实退出路径(closeEvent)调用,镜像 health worker 的 quitting_ 守卫模式:
     * 先置 quitting_ 让回投 lambda 跳过对 this 的访问,再 cancel 让 BuildFindings 在各
     * ScanXxx 的 IsCancelled 检查点快速退出,最后 join 同步回收 std::thread,避免
     * "扫描中关窗→回投悬垂 this"的 UAF。
     */
    void RequestShutdownForQuit();

private:
    /**
     * @brief 选择源路径，用于归档、传输、解锁等模块。
     */
    void BrowseSourcePath();

    /**
     * @brief 选择目标路径，用于传输估算和搬家计划模块。
     */
    void BrowseTargetPath();

    /**
     * @brief 执行全部新增模块的只读体检。
     */
    void RunAllScans();

    /**
     * @brief 执行当前选中模块的只读体检。
     */
    void RunCurrentScan();

    /**
     * @brief 请求取消当前工具箱体检。
     */
    void CancelScan();

    /**
     * @brief 有界等待后台线程结束:轮询 workerFinished_ 最多约 5 秒,完成则即时 join 回收;
     *        超时(极慢盘 / QProcess 阻塞)则 detach,靠 quitting_ + QPointer 守卫跳过回投,杜绝「关窗卡死」。
     */
    void JoinWorkerBounded();

    /**
     * @brief 按左侧模块选择刷新结果树。
     */
    void RefreshModuleFilter();

    /**
     * @brief 打开当前结果关联路径或所在目录。
     */
    void OpenSelectedPath();

    /**
     * @brief 复制当前结果路径到剪贴板。
     */
    void CopySelectedPath();

    /**
     * @brief 复制当前结果的完整行信息到剪贴板。
     */
    void CopySelectedRow();

    /**
     * @brief 显示当前结果的处理方案。
     */
    void ShowActionPlan();

    /**
     * @brief 显示当前视图的批量处理方案包。
     */
    void ShowBulkActionPlan();

    /**
     * @brief 导出当前工具箱结果到 CSV 或 HTML。
     */
    void ExportFindings();

    /**
     * @brief 导出当前视图的专业版 HTML 交付报告。
     */
    void ExportProfessionalReport();

    /**
     * @brief 导出当前视图的 JSON 交付包。
     */
    void ExportDeliveryPackage();

    /**
     * @brief 导出当前视图的 Markdown 处置任务清单。
     */
    void ExportTaskChecklist();

    /**
     * @brief 显示结果树右键菜单。
     * @param position 结果树视口内的右键位置。
     */
    void ShowResultContextMenu(const QPoint& position);

    /**
     * @brief 切换当前结果的忽略状态。
     */
    void ToggleIgnoredFinding();

    /**
     * @brief 切换当前结果的已处理状态。
     */
    void ToggleCompletedFinding();

    /**
     * @brief 编辑当前结果的处置备注。
     */
    void EditFindingNote();

    /**
     * @brief 切换是否显示已忽略结果。
     */
    void ToggleShowIgnored();

    /**
     * @brief 将当前检测结果保存为后续对比基线。
     */
    void SaveCurrentBaseline();

    /**
     * @brief 更新按钮可用状态。
     */
    void UpdateActionState();

    /**
     * @brief 按当前选中模块刷新"是什么/怎么用/提示"说明面板。
     *
     * 选中具体模块时显示该模块的用途与操作流程;选中"全部能力"时显示工具箱总览。
     * 在 RefreshModuleFilter 末尾(单一漏斗,覆盖所有选择/筛选调用点)与构造函数首绘后调用。
     */
    void UpdateModuleGuide();

    /**
     * @brief 关闭首次使用向导条并持久化"已看过"标志。
     */
    void DismissOnboarding();

    /**
     * @brief 打开可重开的"空间工具箱 · 导览"对话框,列出全部分类与模块说明。
     */
    void ShowGuideDialog();

    /**
     * @brief 设置忙碌状态。
     * @param busy 是否正在后台体检。
     * @param text 状态文本。
     */
    void SetBusy(bool busy, const QString& text);

    /**
     * @brief 替换全部检测结果并刷新界面。
     * @param findings 新检测结果。
     * @param requestId 请求序号，用于丢弃过期后台结果。
     * @param cancelled 本次体检是否被取消。
     */
    void ReplaceFindings(QVector<FeatureFinding> findings, std::uint64_t requestId, bool cancelled);

    /**
     * @brief 获取当前选中模块。
     * @param hasModule 输出是否选中了具体模块。
     * @return 当前模块，未选具体模块时返回 GrowthTrace 占位。
     */
    FeatureModule CurrentModule(bool& hasModule) const;

    /**
     * @brief 从结果树当前条目读取检测结果。
     * @param ok 输出是否成功读取。
     * @return 当前条目对应的检测结果；失败时返回默认对象。
     */
    FeatureFinding CurrentFinding(bool& ok) const;

    /**
     * @brief 生成当前检测结果的处理方案文本。
     * @param finding 检测结果。
     * @return 可展示 / 可复制的处理方案。
     */
    QString BuildActionPlanText(const FeatureFinding& finding) const;

    /**
     * @brief 生成当前视图的批量处理方案文本。
     * @param findings 当前视图结果。
     * @return 可展示 / 可复制的批量方案。
     */
    QString BuildBulkActionPlanText(const QVector<FeatureFinding>& findings) const;

    /**
     * @brief 生成当前视图的专业 HTML 报告。
     * @param findings 当前视图结果。
     * @return 完整 HTML 报告文本。
     */
    QString BuildProfessionalReportHtml(const QVector<FeatureFinding>& findings) const;

    /**
     * @brief 生成当前视图的 JSON 交付包。
     * @param findings 当前视图结果。
     * @return UTF-8 JSON 字节流。
     */
    QByteArray BuildDeliveryPackageJson(const QVector<FeatureFinding>& findings) const;

    /**
     * @brief 生成当前视图的 Markdown 处置任务清单。
     * @param findings 当前视图结果。
     * @return UTF-8 Markdown 文本。
     */
    QString BuildTaskChecklistMarkdown(const QVector<FeatureFinding>& findings) const;

    /**
     * @brief 计算当前结果集的治理评分。
     * @param findings 参与评分的结果。
     * @return 0 到 100 的治理评分。
     */
    int GovernanceScore(const QVector<FeatureFinding>& findings) const;

    /**
     * @brief 计算当前基线中已经不再出现的结果数量。
     * @param findings 当前结果。
     * @return 已解决的基线结果数量。
     */
    int ResolvedBaselineCount(const QVector<FeatureFinding>& findings) const;

    /**
     * @brief 获取当前结果的处置备注。
     * @param finding 检测结果。
     * @return 处置备注；没有备注时返回空字符串。
     */
    QString FindingNote(const FeatureFinding& finding) const;

    /**
     * @brief 判断结果是否匹配当前工作流筛选。
     * @param finding 检测结果。
     * @return 匹配当前工作流筛选时返回 true。
     */
    bool MatchesWorkflowFilter(const FeatureFinding& finding) const;

    /**
     * @brief 获取当前工作流筛选代码。
     * @return 筛选代码。
     */
    QString CurrentWorkflowFilterCode() const;

    /**
     * @brief 获取结果相对当前基线的趋势文本。
     * @param finding 检测结果。
     * @return 趋势文本。
     */
    QString TrendTitleForFinding(const FeatureFinding& finding) const;

    /**
     * @brief 创建顶部操作栏。
     * @return 操作栏控件。
     */
    QWidget* CreateToolbar();

    /**
     * @brief 创建左侧模块列表。
     * @return 模块列表控件。
     */
    QWidget* CreateModuleList();

    /**
     * @brief 创建结果树。
     * @return 结果树控件。
     */
    QWidget* CreateResultTree();

    /**
     * @brief 加载工具箱路径设置。
     */
    void LoadSettings();

    /**
     * @brief 保存工具箱路径设置。
     */
    void SaveSettings() const;

    /**
     * @brief 加载工具箱工作流状态，例如忽略列表和显示忽略开关。
     */
    void LoadWorkflowState();

    /**
     * @brief 保存工具箱工作流状态。
     */
    void SaveWorkflowState() const;

    /**
     * @brief 加载上一次工具箱检测结果缓存。
     */
    void LoadResultCache();

    /**
     * @brief 保存当前工具箱检测结果缓存。
     */
    void SaveResultCache() const;

    /**
     * @brief 获取当前模块与文本过滤后的可见结果。
     * @return 可见检测结果。
     */
    QVector<FeatureFinding> VisibleFindings() const;

    /**
     * @brief 判断结果是否已被用户忽略。
     * @param finding 检测结果。
     * @return 已忽略返回 true。
     */
    bool IsIgnoredFinding(const FeatureFinding& finding) const;

    /**
     * @brief 判断结果是否已被标记为处理完成。
     * @param finding 检测结果。
     * @return 已处理返回 true。
     */
    bool IsCompletedFinding(const FeatureFinding& finding) const;

    /**
     * @brief 左侧模块列表。
     */
    QListWidget* moduleList_ = nullptr;

    /**
     * @brief 工具箱检测结果树。
     */
    QTreeWidget* resultTree_ = nullptr;

    /**
     * @brief 源路径输入框。
     */
    QLineEdit* sourcePathEdit_ = nullptr;

    /**
     * @brief 目标路径输入框。
     */
    QLineEdit* targetPathEdit_ = nullptr;

    /**
     * @brief 结果过滤输入框。
     */
    QLineEdit* resultFilterEdit_ = nullptr;

    /**
     * @brief 工作流筛选下拉框。
     */
    QComboBox* workflowFilterCombo_ = nullptr;

    /**
     * @brief 全部体检按钮。
     */
    QPushButton* scanAllButton_ = nullptr;

    /**
     * @brief 当前模块体检按钮。
     */
    QPushButton* scanCurrentButton_ = nullptr;

    /**
     * @brief 取消体检按钮。
     */
    QPushButton* cancelButton_ = nullptr;

    /**
     * @brief 打开结果路径按钮。
     */
    QPushButton* openPathButton_ = nullptr;

    /**
     * @brief 复制结果路径按钮。
     */
    QPushButton* copyPathButton_ = nullptr;

    /**
     * @brief 显示处理方案按钮。
     */
    QPushButton* actionPlanButton_ = nullptr;

    /**
     * @brief 标记当前结果已处理按钮。
     */
    QPushButton* completeButton_ = nullptr;

    /**
     * @brief 编辑当前结果处置备注按钮。
     */
    QPushButton* noteButton_ = nullptr;

    /**
     * @brief 当前视图批量方案按钮。
     */
    QPushButton* bulkPlanButton_ = nullptr;

    /**
     * @brief 导出结果按钮；保留兼容旧布局，当前由交付菜单触发。
     */
    QPushButton* exportButton_ = nullptr;

    /**
     * @brief 专业报告导出按钮。
     */
    QPushButton* professionalReportButton_ = nullptr;

    /**
     * @brief JSON 交付包导出按钮。
     */
    QPushButton* deliveryPackageButton_ = nullptr;

    /**
     * @brief Markdown 处置任务清单导出按钮。
     */
    QPushButton* taskChecklistButton_ = nullptr;

    /**
     * @brief 交付动作下拉按钮。
     */
    QToolButton* deliveryMenuButton_ = nullptr;

    /**
     * @brief 保存当前基线按钮。
     */
    QPushButton* baselineButton_ = nullptr;

    /**
     * @brief 显示已忽略结果按钮。
     */
    QPushButton* showIgnoredButton_ = nullptr;

    /**
     * @brief 顶部状态标签。
     */
    QLabel* statusLabel_ = nullptr;

    /**
     * @brief 首次使用向导条(可关闭,持久化 onboardingSeen)。
     */
    QFrame* onboardingBanner_ = nullptr;

    /**
     * @brief 工具栏"导览"按钮(重开 ShowGuideDialog)。
     */
    QPushButton* helpButton_ = nullptr;

    /**
     * @brief 当前选中模块的"是什么/怎么用/提示"说明面板。
     */
    QFrame* moduleGuideFrame_ = nullptr;

    /**
     * @brief 模块用途(是什么)说明。
     */
    QLabel* guidePurposeLabel_ = nullptr;

    /**
     * @brief 模块操作流程(怎么用)说明。
     */
    QLabel* guideHowToUseLabel_ = nullptr;

    /**
     * @brief 模块操作提示。
     */
    QLabel* guideTipsLabel_ = nullptr;

    /**
     * @brief 结果数量指标。
     */
    QLabel* resultCountLabel_ = nullptr;

    /**
     * @brief 结果空间指标。
     */
    QLabel* resultBytesLabel_ = nullptr;

    /**
     * @brief 需关注数量指标。
     */
    QLabel* attentionCountLabel_ = nullptr;

    /**
     * @brief 高风险数量指标。
     */
    QLabel* highRiskCountLabel_ = nullptr;

    /**
     * @brief 已忽略数量指标。
     */
    QLabel* ignoredCountLabel_ = nullptr;

    /**
     * @brief 已处理数量指标。
     */
    QLabel* completedCountLabel_ = nullptr;

    /**
     * @brief 基线已解决数量指标。
     */
    QLabel* resolvedCountLabel_ = nullptr;

    /**
     * @brief 治理评分指标。
     */
    QLabel* governanceScoreLabel_ = nullptr;

    /**
     * @brief 基线结果数量指标。
     */
    QLabel* baselineCountLabel_ = nullptr;

    /**
     * @brief 当前检测结果。
     */
    QVector<FeatureFinding> findings_;

    /**
     * @brief 用户忽略的结果稳定键集合。
     */
    QSet<QString> ignoredFindingKeys_;

    /**
     * @brief 用户标记为已处理的结果稳定键集合。
     */
    QSet<QString> completedFindingKeys_;

    /**
     * @brief 用户记录的处置备注，键为结果稳定键。
     */
    QMap<QString, QString> findingNotes_;

    /**
     * @brief 当前基线中的结果稳定键集合。
     */
    QSet<QString> baselineFindingKeys_;

    /**
     * @brief 当前基线保存时间。
     */
    QString baselineCapturedAt_;

    /**
     * @brief 当前扫描批次号。
     */
    QString currentBatchId_;

    /**
     * @brief 当前结果生成时间。
     */
    QString resultCapturedAt_;

    /**
     * @brief 后台扫描是否正在运行。
     */
    std::atomic_bool scanning_{false};

    /**
     * @brief 扫描请求序号。
     */
    std::atomic_uint64_t requestId_{0};

    /**
     * @brief 当前扫描取消标志。
     */
    std::shared_ptr<std::atomic_bool> cancelFlag_;

    /**
     * @brief 后台扫描线程句柄(由 detach 改为成员持有,退出时可 join 回收,杜绝悬垂回投)。
     */
    std::thread scanWorker_;

    /**
     * @brief 退出标志:置真后回投 lambda 不再访问 this;析构/关闭时先置真再 join 工作线程。
     */
    std::atomic_bool quitting_{false};

    /**
     * @brief 当前在途 worker 的完成标志(堆上 atomic,shared_ptr 管理)。
     *        worker 按值拷贝持有同一实例、退出时置真;主线程的 JoinWorkerBounded 读它做有界等待。
     *        刻意用 shared_ptr 而非成员:detach 降级后 worker 可能晚于 widget 析构才退出,
     *        写堆 atomic 不触碰 this,杜绝悬垂成员访问。
     */
    std::shared_ptr<std::atomic_bool> workerFinishedFlag_;
};

}  // namespace disk_lens::qt_ui
