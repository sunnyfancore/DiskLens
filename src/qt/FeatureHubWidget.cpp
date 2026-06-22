#include "qt/FeatureHubWidget.h"

#include "core/Format.h"
#include "qt/AppIcons.h"
#include "qt/TableExport.h"

#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFrame>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMetaObject>
#include <QPoint>
#include <QPushButton>
#include <QSet>
#include <QSettings>
#include <QSplitter>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QThread>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QTextEdit>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QHBoxLayout>

#include <Windows.h>
#include <RestartManager.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <thread>

namespace disk_lens::qt_ui {
namespace {

/**
 * @brief 单个工具箱模块的展示元数据。
 */
struct ModuleInfo {
    /**
     * @brief 模块枚举值。
     */
    FeatureModule module = FeatureModule::GrowthTrace;

    /**
     * @brief 模块标题。
     */
    QString title;

    /**
     * @brief 模块说明。
     */
    QString description;
};

/**
 * @brief 目录扫描的轻量统计结果。
 */
struct PathSizeSummary {
    /**
     * @brief 累计字节数。
     */
    std::uint64_t bytes = 0;

    /**
     * @brief 文件数量。
     */
    int files = 0;

    /**
     * @brief 目录数量。
     */
    int directories = 0;

    /**
     * @brief 是否因数量上限截断。
     */
    bool truncated = false;
};

/**
 * @brief 最近写入文件的中间记录。
 */
struct RecentFileRecord {
    /**
     * @brief 文件名。
     */
    QString name;

    /**
     * @brief 文件完整路径。
     */
    QString path;

    /**
     * @brief 文件大小。
     */
    std::uint64_t bytes = 0;

    /**
     * @brief 最后修改时间。
     */
    qint64 modifiedMsec = 0;
};

/**
 * @brief 专业报告中的结果风险等级。
 */
enum class FindingSeverity {
    Normal,
    Notice,
    Warning,
    Critical,
};

/**
 * @brief 结果树条目上保存完整检测结果的自定义角色。
 */
constexpr int kRolePath = Qt::UserRole;
constexpr int kRoleModule = Qt::UserRole + 1;
constexpr int kRoleTitle = Qt::UserRole + 2;
constexpr int kRoleState = Qt::UserRole + 3;
constexpr int kRoleDetail = Qt::UserRole + 4;
constexpr int kRoleBytes = Qt::UserRole + 5;

/**
 * @brief 缓存结果最大条数，避免设置存储无限膨胀。
 */
constexpr int kMaxCachedFindings = 2000;

/**
 * @brief 判断后台体检是否已请求取消。
 * @param cancelFlag 共享取消标志。
 * @return 请求取消时返回 true。
 */
bool IsCancelled(const std::shared_ptr<std::atomic_bool>& cancelFlag) {
    return cancelFlag != nullptr && cancelFlag->load();
}

/**
 * @brief 返回全部工具箱模块元数据。
 * @return 模块元数据列表。
 */
QVector<ModuleInfo> AllModules() {
    return {
        {FeatureModule::GrowthTrace, QStringLiteral("空间增长溯源"), QStringLiteral("定位近期持续增长的目录和大文件")},
        {FeatureModule::SoftwareFootprint, QStringLiteral("软件体积管理器"), QStringLiteral("估算已安装软件及安装目录真实占用")},
        {FeatureModule::AppMover, QStringLiteral("应用 / 游戏搬家"), QStringLiteral("识别适合迁移到目标盘的大型应用和游戏")},
        {FeatureModule::ArchiveAssistant, QStringLiteral("归档助手"), QStringLiteral("为长期未动资料生成迁移归档计划")},
        {FeatureModule::DownloadOrganizer, QStringLiteral("下载整理中心"), QStringLiteral("按类型整理下载、桌面和聊天接收文件")},
        {FeatureModule::PrivacyRadar, QStringLiteral("隐私文件雷达"), QStringLiteral("发现密钥、证书、合同和身份信息等敏感文件")},
        {FeatureModule::DeveloperSpace, QStringLiteral("开发环境空间中心"), QStringLiteral("定位 node_modules、构建产物、包缓存和虚拟环境")},
        {FeatureModule::DockerWsl, QStringLiteral("Docker / WSL 空间管理"), QStringLiteral("识别镜像缓存、卷和 WSL 虚拟磁盘")},
        {FeatureModule::MediaOrganizer, QStringLiteral("照片 / 视频整理器"), QStringLiteral("按媒体类型、年代和体积生成整理建议")},
        {FeatureModule::QuotaBudget, QStringLiteral("磁盘配额与预算"), QStringLiteral("给常用目录套默认预算并标记超额位置")},
        {FeatureModule::BackupGap, QStringLiteral("备份缺口检查"), QStringLiteral("检查重要目录是否缺少目标备份位置")},
        {FeatureModule::FileUnlocker, QStringLiteral("文件占用解锁器"), QStringLiteral("用 Windows Restart Manager 识别占用进程")},
        {FeatureModule::TransferAssistant, QStringLiteral("大文件传输助手"), QStringLiteral("估算迁移体积、目标盘空间和执行计划")},
        {FeatureModule::CloudSync, QStringLiteral("同步盘空间分析"), QStringLiteral("识别同步盘本地占用、冲突文件和大缓存")},
        {FeatureModule::RestorePoint, QStringLiteral("系统镜像 / 恢复点管理"), QStringLiteral("发现 Windows.old、更新备份和卷影副本入口")},
        {FeatureModule::BrowserCache, QStringLiteral("浏览器缓存中心"), QStringLiteral("识别 Chrome、Edge、Firefox 等浏览器缓存和离线数据")},
        {FeatureModule::StartupFootprint, QStringLiteral("启动项体积检查"), QStringLiteral("盘点开机启动入口及其关联程序占用")},
        {FeatureModule::MessengerCache, QStringLiteral("聊天缓存治理"), QStringLiteral("识别微信、Teams、Slack、Discord 等聊天客户端本地缓存")},
        {FeatureModule::MailArchive, QStringLiteral("邮件归档库检查"), QStringLiteral("发现 PST、OST、MBOX 等邮件归档和离线邮箱文件")},
        {FeatureModule::VirtualMachineImages, QStringLiteral("虚拟机镜像管理"), QStringLiteral("发现 VHD、VMDK、VDI、QCOW2 等大型虚拟磁盘")},
    };
}

/**
 * @brief 查找模块枚举对应的标题。
 * @param module 模块枚举值。
 * @return 模块标题。
 */
QString ModuleTitle(FeatureModule module) {
    for (const ModuleInfo& info : AllModules()) {
        if (info.module == module) {
            return info.title;
        }
    }
    return QStringLiteral("未知模块");
}

/**
 * @brief 把模块枚举转换为稳定整数。
 * @param module 模块枚举值。
 * @return 稳定整数。
 */
int ModuleToInt(FeatureModule module) {
    return static_cast<int>(module);
}

/**
 * @brief 把稳定整数转换为模块枚举。
 * @param value 稳定整数。
 * @return 模块枚举。
 */
FeatureModule IntToModule(int value) {
    return static_cast<FeatureModule>(value);
}

/**
 * @brief 判断稳定整数是否对应当前版本已知模块。
 * @param value 稳定整数。
 * @return 已知模块返回 true。
 */
bool IsKnownModuleValue(int value) {
    for (const ModuleInfo& info : AllModules()) {
        if (ModuleToInt(info.module) == value) {
            return true;
        }
    }
    return false;
}

/**
 * @brief 将字节数格式化为界面文本。
 * @param bytes 字节数。
 * @return 格式化文本。
 */
QString FormatBytesText(std::uint64_t bytes) {
    return QString::fromStdWString(core::FormatBytes(bytes));
}

/**
 * @brief 为 Windows 命令行参数添加双引号。
 * @param value 参数文本。
 * @return 已加引号并转义内部引号的参数。
 */
QString QuoteCmd(const QString& value) {
    QString escaped = QDir::toNativeSeparators(value);
    escaped.replace(QStringLiteral("\""), QStringLiteral("\\\""));
    return QStringLiteral("\"%1\"").arg(escaped);
}

/**
 * @brief 为 PowerShell 单引号字符串转义。
 * @param value 字符串文本。
 * @return 可直接放入 PowerShell 单引号的文本。
 */
QString QuotePowerShell(const QString& value) {
    QString escaped = QDir::toNativeSeparators(value);
    escaped.replace(QStringLiteral("'"), QStringLiteral("''"));
    return QStringLiteral("'%1'").arg(escaped);
}

/**
 * @brief 判断路径是否存在。
 * @param path 路径。
 * @return 存在时返回 true。
 */
bool PathExists(const QString& path) {
    return !path.trimmed().isEmpty() && QFileInfo::exists(path);
}

/**
 * @brief 返回标准路径，失败时回退到用户目录。
 * @param location Qt 标准路径枚举。
 * @return 本机可用路径。
 */
QString StandardPathOrHome(QStandardPaths::StandardLocation location) {
    QString path = QStandardPaths::writableLocation(location);
    if (path.isEmpty()) {
        path = QDir::homePath();
    }
    return QDir::toNativeSeparators(path);
}

/**
 * @brief 展开 Windows 风格环境变量。
 * @param value 可能包含 %LOCALAPPDATA% 这类占位符的文本。
 * @return 展开后的文本；失败时返回原文本。
 */
QString ExpandEnvironmentStringsText(const QString& value) {
    const std::wstring input = value.toStdWString();
    const DWORD required = ExpandEnvironmentStringsW(input.c_str(), nullptr, 0);
    if (required == 0) {
        return value;
    }

    std::wstring output(required, L'\0');
    const DWORD written = ExpandEnvironmentStringsW(input.c_str(), output.data(), required);
    if (written == 0 || written > required) {
        return value;
    }
    if (!output.empty() && output.back() == L'\0') {
        output.pop_back();
    }
    return QString::fromStdWString(output);
}

/**
 * @brief 从启动项命令中提取可执行文件路径。
 * @param command 启动项命令行。
 * @return 可执行文件路径；无法提取时返回空字符串。
 */
QString ExecutablePathFromCommand(const QString& command) {
    QString text = ExpandEnvironmentStringsText(command).trimmed();
    if (text.isEmpty()) {
        return {};
    }
    if (text.startsWith(QLatin1Char('"'))) {
        const int closeQuote = text.indexOf(QLatin1Char('"'), 1);
        if (closeQuote > 1) {
            return QDir::toNativeSeparators(text.mid(1, closeQuote - 1));
        }
    }

    const QString lower = text.toCaseFolded();
    const int exeIndex = lower.indexOf(QStringLiteral(".exe"));
    if (exeIndex >= 0) {
        return QDir::toNativeSeparators(text.left(exeIndex + 4).trimmed());
    }

    const int firstSpace = text.indexOf(QLatin1Char(' '));
    if (firstSpace > 0) {
        text = text.left(firstSpace);
    }
    return QDir::toNativeSeparators(text);
}

/**
 * @brief 取当前用户常用资料目录。
 * @return 常用目录列表。
 */
QStringList ImportantUserFolders() {
    QStringList paths;
    paths << StandardPathOrHome(QStandardPaths::DesktopLocation);
    paths << StandardPathOrHome(QStandardPaths::DocumentsLocation);
    paths << StandardPathOrHome(QStandardPaths::PicturesLocation);
    paths << StandardPathOrHome(QStandardPaths::MoviesLocation);
    paths.removeDuplicates();
    return paths;
}

/**
 * @brief 取目录直属子项，且只保留真实存在的路径。
 * @param paths 候选路径列表。
 * @return 已存在路径列表。
 */
QStringList ExistingPaths(const QStringList& paths) {
    QStringList out;
    for (const QString& path : paths) {
        if (PathExists(path)) {
            out << QDir::toNativeSeparators(path);
        }
    }
    out.removeDuplicates();
    return out;
}

/**
 * @brief 计算路径占用，并设置最大遍历数量避免单个候选拖慢全部模块。
 * @param path 文件或目录路径。
 * @param maxEntries 最大遍历条目数量。
 * @return 路径统计。
 */
PathSizeSummary ComputePathSizeLimited(const QString& path, int maxEntries = 12000,
                                       std::shared_ptr<std::atomic_bool> cancelFlag = nullptr) {
    PathSizeSummary summary;
    if (IsCancelled(cancelFlag)) {
        summary.truncated = true;
        return summary;
    }
    const QFileInfo info(path);
    if (!info.exists()) {
        return summary;
    }
    if (info.isFile()) {
        summary.bytes = static_cast<std::uint64_t>(std::max<qint64>(0, info.size()));
        summary.files = 1;
        return summary;
    }

    QDirIterator iterator(path, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                          QDirIterator::Subdirectories);
    int visited = 0;
    while (iterator.hasNext()) {
        if (IsCancelled(cancelFlag)) {
            summary.truncated = true;
            break;
        }
        iterator.next();
        ++visited;
        const QFileInfo item = iterator.fileInfo();
        if (item.isDir()) {
            ++summary.directories;
        } else if (item.isFile()) {
            summary.bytes += static_cast<std::uint64_t>(std::max<qint64>(0, item.size()));
            ++summary.files;
        }
        if (visited >= maxEntries) {
            summary.truncated = iterator.hasNext();
            break;
        }
    }
    return summary;
}

/**
 * @brief 单个文件在磁盘上的真实占用(逻辑大小 vs 实占簇大小)。
 *
 * 用于 NTFS 压缩 / 稀疏文件(vhdx/vmdk/OST/Windows.old 等)的体积准确性:逻辑大小(QFileInfo::size)
 * 会高估可回收空间,实占大小(GetCompressedFileSizeW 返回的压缩后簇占用)才反映真实磁盘占用。
 */
struct AllocatedBytes {
    /** 文件逻辑大小(QFileInfo::size,字节)。 */
    std::uint64_t logical = 0;
    /** 文件实际占用磁盘的字节数(压缩/稀疏后的真实簇占用)。 */
    std::uint64_t allocated = 0;
    /** 是否为压缩或稀疏文件(allocated < logical,即逻辑大小被高估)。 */
    bool sparse = false;
};

/**
 * @brief 取文件在磁盘上的真实占用大小。
 * @param path 文件路径。
 * @return 逻辑/实占/是否稀疏;路径无效或取值失败时 allocated 回退为 logical。
 *
 * GetCompressedFileSizeW(kernel32,无需额外链接)对 NTFS 压缩/稀疏文件返回压缩后的实际簇占用,对普通
 * 文件返回按簇对齐大小(≥ 逻辑大小)。按文档须用"返回值==INVALID_FILE_SIZE 且 GetLastError()!=NO_ERROR"
 * 判定失败(0xFFFFFFFF 可能是超大文件的合法低位)。失败时回退 allocated=logical,保证调用方始终拿到
 * 合理估值。sparse 判定用 allocated<logical(内容驱动,比属性位更贴合"大小是否被高估")。
 */
AllocatedBytes AllocatedBytesOnDisk(const QString& path) {
    AllocatedBytes result;
    const QFileInfo info(path);
    result.logical = static_cast<std::uint64_t>(std::max<qint64>(0, info.size()));
    result.allocated = result.logical;  // 默认回退:即便取值失败也保证 allocated 合理。
    if (path.trimmed().isEmpty() || !info.exists() || !info.isFile()) {
        return result;
    }
    const std::wstring native = QDir::toNativeSeparators(info.absoluteFilePath()).toStdWString();
    if (native.empty()) {
        return result;
    }
    DWORD high = 0;
    const DWORD low = GetCompressedFileSizeW(native.c_str(), &high);
    if (low == INVALID_FILE_SIZE && GetLastError() != NO_ERROR) {
        return result;  // 取值失败:保持 allocated = logical 回退。
    }
    const std::uint64_t allocated = (static_cast<std::uint64_t>(high) << 32) | static_cast<std::uint64_t>(low);
    result.allocated = allocated;  // 调用已成功(失败在上方 INVALID_FILE_SIZE+GetLastError 处早退),0 是合法的"零占用"。
    result.sparse = (result.allocated < result.logical);
    return result;
}

/**
 * @brief 将逻辑/实占大小格式化为对比文本。
 * @param bytes AllocatedBytesOnDisk 的结果。
 * @return 稀疏时如"逻辑 100.0 GB / 实占 1.2 GB(稀疏)";非稀疏时仅返回逻辑大小文本。
 */
QString FormatAllocatedText(const AllocatedBytes& bytes) {
    if (!bytes.sparse || bytes.allocated >= bytes.logical) {
        return FormatBytesText(bytes.logical);
    }
    return QStringLiteral("逻辑 %1 / 实占 %2(稀疏)").arg(FormatBytesText(bytes.logical), FormatBytesText(bytes.allocated));
}

/**
 * @brief 添加一条检测结果。
 * @param out 结果容器。
 * @param module 所属模块。
 * @param title 标题。
 * @param state 状态。
 * @param detail 说明。
 * @param path 路径。
 * @param bytes 字节数。
 */
void AddFinding(QVector<FeatureFinding>& out, FeatureModule module, const QString& title, const QString& state,
                const QString& detail, const QString& path = QString(), std::uint64_t bytes = 0) {
    FeatureFinding finding;
    finding.module = module;
    finding.title = title;
    finding.state = state;
    finding.detail = detail;
    finding.path = QDir::toNativeSeparators(path);
    finding.bytes = bytes;
    out.push_back(std::move(finding));
}

/**
 * @brief 生成结果稳定键，用于忽略列表与跨会话复核。
 * @param finding 检测结果。
 * @return 稳定键文本。
 */
QString FindingStableKey(const FeatureFinding& finding) {
    return QStringLiteral("%1|%2|%3|%4")
        .arg(ModuleToInt(finding.module))
        .arg(finding.title.trimmed().toCaseFolded())
        .arg(finding.path.trimmed().toCaseFolded())
        .arg(finding.state.trimmed().toCaseFolded());
}

/**
 * @brief 推断结果的风险等级。
 * @param finding 检测结果。
 * @return 风险等级。
 */
FindingSeverity SeverityForFinding(const FeatureFinding& finding) {
    const QString state = finding.state.toCaseFolded();
    if (state.contains(QStringLiteral("敏感")) ||
        state.contains(QStringLiteral("被占用")) ||
        state.contains(QStringLiteral("备份缺口")) ||
        state.contains(QStringLiteral("目标空间不足"))) {
        return FindingSeverity::Critical;
    }
    if (state.contains(QStringLiteral("风险")) ||
        state.contains(QStringLiteral("超预算")) ||
        state.contains(QStringLiteral("缺口")) ||
        state.contains(QStringLiteral("不足"))) {
        return FindingSeverity::Warning;
    }
    if (finding.bytes >= 10ULL * 1024ULL * 1024ULL * 1024ULL ||
        state.contains(QStringLiteral("虚拟磁盘")) ||
        state.contains(QStringLiteral("聊天缓存")) ||
        state.contains(QStringLiteral("邮件归档")) ||
        state.contains(QStringLiteral("浏览器缓存"))) {
        return FindingSeverity::Notice;
    }
    return FindingSeverity::Normal;
}

/**
 * @brief 将风险等级转换为展示文本。
 * @param severity 风险等级。
 * @return 展示文本。
 */
QString SeverityTitle(FindingSeverity severity) {
    switch (severity) {
    case FindingSeverity::Critical:
        return QStringLiteral("高");
    case FindingSeverity::Warning:
        return QStringLiteral("中");
    case FindingSeverity::Notice:
        return QStringLiteral("提示");
    case FindingSeverity::Normal:
        return QStringLiteral("低");
    }
    return QStringLiteral("低");
}

/**
 * @brief 将风险等级转换为稳定代码。
 * @param severity 风险等级。
 * @return 稳定代码。
 */
QString SeverityCode(FindingSeverity severity) {
    switch (severity) {
    case FindingSeverity::Critical:
        return QStringLiteral("critical");
    case FindingSeverity::Warning:
        return QStringLiteral("warning");
    case FindingSeverity::Notice:
        return QStringLiteral("notice");
    case FindingSeverity::Normal:
        return QStringLiteral("normal");
    }
    return QStringLiteral("normal");
}

/**
 * @brief 将风险等级转换为排序权重。
 * @param severity 风险等级。
 * @return 权重，数值越大越需要优先处理。
 */
int SeverityRank(FindingSeverity severity) {
    switch (severity) {
    case FindingSeverity::Critical:
        return 3;
    case FindingSeverity::Warning:
        return 2;
    case FindingSeverity::Notice:
        return 1;
    case FindingSeverity::Normal:
        return 0;
    }
    return 0;
}

/**
 * @brief 转义 HTML 文本。
 * @param value 原始文本。
 * @return 可安全放入 HTML 的文本。
 */
QString EscapeHtmlText(const QString& value) {
    QString escaped = value;
    escaped.replace(QStringLiteral("&"), QStringLiteral("&amp;"));
    escaped.replace(QStringLiteral("<"), QStringLiteral("&lt;"));
    escaped.replace(QStringLiteral(">"), QStringLiteral("&gt;"));
    escaped.replace(QStringLiteral("\""), QStringLiteral("&quot;"));
    escaped.replace(QStringLiteral("'"), QStringLiteral("&#39;"));
    return escaped;
}

/**
 * @brief 判断结果是否属于需要优先关注的状态。
 * @param finding 检测结果。
 * @return 需要关注时返回 true。
 */
bool IsAttentionFinding(const FeatureFinding& finding) {
    return SeverityRank(SeverityForFinding(finding)) >= SeverityRank(FindingSeverity::Warning);
}

/**
 * @brief 判断结果是否匹配模块与文本过滤条件。
 * @param finding 检测结果。
 * @param hasModule 是否限定到某个模块。
 * @param currentModule 当前限定模块。
 * @param filterText 已折叠大小写的文本过滤词。
 * @return 匹配当前视图时返回 true。
 */
bool MatchesVisibleFilter(const FeatureFinding& finding, bool hasModule, FeatureModule currentModule,
                          const QString& filterText) {
    if (hasModule && finding.module != currentModule) {
        return false;
    }
    if (filterText.isEmpty()) {
        return true;
    }
    const QString haystack = (ModuleTitle(finding.module) + QLatin1Char('\n') +
                             finding.title + QLatin1Char('\n') +
                             finding.state + QLatin1Char('\n') +
                             finding.detail + QLatin1Char('\n') +
                             finding.path).toCaseFolded();
    return haystack.contains(filterText);
}

/**
 * @brief 生成一条结果的剪贴板摘要。
 * @param finding 检测结果。
 * @return 可直接复制给用户或工单系统的文本。
 */
QString FindingClipboardText(const FeatureFinding& finding) {
    QStringList lines;
    lines << QStringLiteral("模块：%1").arg(ModuleTitle(finding.module));
    lines << QStringLiteral("项目：%1").arg(finding.title);
    lines << QStringLiteral("等级：%1").arg(SeverityTitle(SeverityForFinding(finding)));
    lines << QStringLiteral("状态：%1").arg(finding.state);
    lines << QStringLiteral("大小：%1").arg(finding.bytes > 0 ? FormatBytesText(finding.bytes) : QStringLiteral("-"));
    if (!finding.path.isEmpty()) {
        lines << QStringLiteral("路径：%1").arg(QDir::toNativeSeparators(finding.path));
    }
    lines << QStringLiteral("说明：%1").arg(finding.detail);
    return lines.join(QStringLiteral("\n"));
}

/**
 * @brief 扫描目录中的近期大文件。
 * @param root 扫描根路径。
 * @param minBytes 最小文件大小。
 * @param sinceMsec 最早修改时间。
 * @param maxRows 最大返回数量。
 * @return 最近大文件列表。
 */
QVector<RecentFileRecord> CollectRecentLargeFiles(const QString& root, std::uint64_t minBytes, qint64 sinceMsec,
                                                  int maxRows, std::shared_ptr<std::atomic_bool> cancelFlag) {
    QVector<RecentFileRecord> records;
    if (!PathExists(root)) {
        return records;
    }

    QDirIterator iterator(root, QDir::Files | QDir::Hidden | QDir::System, QDirIterator::Subdirectories);
    int visited = 0;
    while (iterator.hasNext() && visited < 150000 && !IsCancelled(cancelFlag)) {
        iterator.next();
        ++visited;
        const QFileInfo info = iterator.fileInfo();
        const std::uint64_t bytes = static_cast<std::uint64_t>(std::max<qint64>(0, info.size()));
        const qint64 modified = info.lastModified().toMSecsSinceEpoch();
        if (bytes < minBytes || modified < sinceMsec) {
            continue;
        }
        records.push_back(RecentFileRecord{info.fileName(), info.absoluteFilePath(), bytes, modified});
    }

    std::sort(records.begin(), records.end(), [](const RecentFileRecord& left, const RecentFileRecord& right) {
        if (left.bytes != right.bytes) {
            return left.bytes > right.bytes;
        }
        return left.modifiedMsec > right.modifiedMsec;
    });
    if (records.size() > maxRows) {
        records.resize(maxRows);
    }
    return records;
}

/**
 * @brief 扫描空间增长溯源模块。
 * @param out 输出结果。
 * @param sourcePath 用户选择的源路径。
 */
void ScanGrowthTrace(QVector<FeatureFinding>& out, const QString& sourcePath, std::shared_ptr<std::atomic_bool> cancelFlag) {
    const QString root = PathExists(sourcePath) ? sourcePath : StandardPathOrHome(QStandardPaths::DownloadLocation);
    const qint64 since = QDateTime::currentMSecsSinceEpoch() - 7LL * 86400000LL;
    const QVector<RecentFileRecord> files = CollectRecentLargeFiles(root, 50ULL * 1024ULL * 1024ULL, since, 40, cancelFlag);

    std::map<QString, std::uint64_t> folderBytes;
    for (const RecentFileRecord& file : files) {
        if (IsCancelled(cancelFlag)) {
            break;
        }
        folderBytes[QFileInfo(file.path).absolutePath()] += file.bytes;
        AddFinding(out, FeatureModule::GrowthTrace, file.name, QStringLiteral("近期写入"),
                   QStringLiteral("近 7 天修改的大文件，可能是空间增长来源。"), file.path, file.bytes);
    }
    for (const auto& pair : folderBytes) {
        if (pair.second >= 300ULL * 1024ULL * 1024ULL) {
            AddFinding(out, FeatureModule::GrowthTrace, QFileInfo(pair.first).fileName().isEmpty() ? pair.first : QFileInfo(pair.first).fileName(),
                       QStringLiteral("增长目录"), QStringLiteral("该目录近 7 天内累计产生较多大文件。"),
                       pair.first, pair.second);
        }
    }
    if (files.isEmpty()) {
        AddFinding(out, FeatureModule::GrowthTrace, root, QStringLiteral("未发现明显增长"),
                   QStringLiteral("近 7 天内未发现超过 50 MB 的新增或修改文件。"), root, 0);
    }
}

/**
 * @brief 从注册表读取已安装软件记录。
 * @param registryRoot 卸载项注册表根。
 * @param out 输出结果。
 */
void CollectInstalledSoftwareFromRegistry(const QString& registryRoot, QVector<FeatureFinding>& out,
                                          std::shared_ptr<std::atomic_bool> cancelFlag) {
    QSettings registry(registryRoot, QSettings::NativeFormat);
    const QStringList groups = registry.childGroups();
    for (const QString& group : groups) {
        if (IsCancelled(cancelFlag)) {
            break;
        }
        registry.beginGroup(group);
        const QString name = registry.value(QStringLiteral("DisplayName")).toString().trimmed();
        const QString installLocation = registry.value(QStringLiteral("InstallLocation")).toString().trimmed();
        const QString publisher = registry.value(QStringLiteral("Publisher")).toString().trimmed();
        const qulonglong estimatedKb = registry.value(QStringLiteral("EstimatedSize")).toULongLong();
        registry.endGroup();

        if (name.isEmpty()) {
            continue;
        }
        std::uint64_t bytes = static_cast<std::uint64_t>(estimatedKb) * 1024ULL;
        QString detail = publisher.isEmpty()
            ? QStringLiteral("来自系统卸载注册表。")
            : QStringLiteral("发布者：%1。").arg(publisher);
        QString path = installLocation;
        if (bytes == 0 && PathExists(path)) {
            const PathSizeSummary summary = ComputePathSizeLimited(path, 8000, cancelFlag);
            bytes = summary.bytes;
            if (summary.truncated) {
                detail += QStringLiteral(" 安装目录较大，已按上限估算。");
            }
        }
        if (bytes < 50ULL * 1024ULL * 1024ULL && path.isEmpty()) {
            continue;
        }
        AddFinding(out, FeatureModule::SoftwareFootprint, name, QStringLiteral("已安装软件"), detail, path, bytes);
    }
}

/**
 * @brief 扫描软件体积管理器模块。
 * @param out 输出结果。
 */
void ScanSoftwareFootprint(QVector<FeatureFinding>& out, std::shared_ptr<std::atomic_bool> cancelFlag) {
    CollectInstalledSoftwareFromRegistry(QStringLiteral("HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall"), out, cancelFlag);
    CollectInstalledSoftwareFromRegistry(QStringLiteral("HKEY_LOCAL_MACHINE\\SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall"), out, cancelFlag);
    CollectInstalledSoftwareFromRegistry(QStringLiteral("HKEY_CURRENT_USER\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall"), out, cancelFlag);
}

/**
 * @brief 扫描目录直属大型子目录，生成搬家候选。
 * @param out 输出结果。
 * @param root 父目录。
 * @param state 状态文案。
 * @param minBytes 最小体积。
 */
void CollectLargeChildDirectories(QVector<FeatureFinding>& out, const QString& root, const QString& state,
                                  std::uint64_t minBytes, std::shared_ptr<std::atomic_bool> cancelFlag) {
    if (!PathExists(root)) {
        return;
    }
    const QFileInfoList entries = QDir(root).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
    for (const QFileInfo& entry : entries) {
        if (IsCancelled(cancelFlag)) {
            break;
        }
        const PathSizeSummary summary = ComputePathSizeLimited(entry.absoluteFilePath(), 10000, cancelFlag);
        if (summary.bytes < minBytes) {
            continue;
        }
        const QString detail = QStringLiteral("可先复制到目标盘，再用 junction 保持原路径；当前版本生成计划，不直接执行迁移。");
        AddFinding(out, FeatureModule::AppMover, entry.fileName(), state, detail, entry.absoluteFilePath(), summary.bytes);
    }
}

/**
 * @brief 扫描应用和游戏搬家模块。
 * @param out 输出结果。
 * @param targetPath 用户选择的目标路径。
 */
void ScanAppMover(QVector<FeatureFinding>& out, const QString& targetPath, std::shared_ptr<std::atomic_bool> cancelFlag) {
    Q_UNUSED(targetPath);
    QStringList roots;
    roots << qEnvironmentVariable("ProgramFiles");
    roots << qEnvironmentVariable("ProgramFiles(x86)");
    roots << QStringLiteral("C:/Program Files (x86)/Steam/steamapps/common");
    roots << QStringLiteral("C:/Program Files/Steam/steamapps/common");
    roots << QStringLiteral("D:/SteamLibrary/steamapps/common");
    for (const QString& root : ExistingPaths(roots)) {
        if (IsCancelled(cancelFlag)) {
            break;
        }
        CollectLargeChildDirectories(out, root, QStringLiteral("搬家候选"), 2ULL * 1024ULL * 1024ULL * 1024ULL, cancelFlag);
    }
}

/**
 * @brief 扫描归档助手模块。
 * @param out 输出结果。
 * @param sourcePath 源路径。
 * @param targetPath 目标路径。
 */
void ScanArchiveAssistant(QVector<FeatureFinding>& out, const QString& sourcePath, const QString& targetPath,
                          std::shared_ptr<std::atomic_bool> cancelFlag) {
    const QString root = PathExists(sourcePath) ? sourcePath : QDir::homePath();
    const qint64 cutoff = QDateTime::currentMSecsSinceEpoch() - 365LL * 86400000LL;
    QDirIterator iterator(root, QDir::Files | QDir::Hidden | QDir::System, QDirIterator::Subdirectories);
    int emitted = 0;
    while (iterator.hasNext() && emitted < 80 && !IsCancelled(cancelFlag)) {
        iterator.next();
        const QFileInfo info = iterator.fileInfo();
        const std::uint64_t bytes = static_cast<std::uint64_t>(std::max<qint64>(0, info.size()));
        if (bytes < 100ULL * 1024ULL * 1024ULL || info.lastModified().toMSecsSinceEpoch() > cutoff) {
            continue;
        }
        const QString detail = targetPath.isEmpty()
            ? QStringLiteral("超过一年未修改的大文件，建议移动到归档盘或离线介质。")
            : QStringLiteral("超过一年未修改的大文件，可归档到：%1。").arg(QDir::toNativeSeparators(targetPath));
        AddFinding(out, FeatureModule::ArchiveAssistant, info.fileName(), QStringLiteral("归档候选"), detail, info.absoluteFilePath(), bytes);
        ++emitted;
    }
}

/**
 * @brief 根据扩展名返回下载整理类型。
 * @param extension 小写扩展名。
 * @return 整理类型。
 */
QString DownloadBucketForExtension(const QString& extension) {
    static const QSet<QString> installers{QStringLiteral("exe"), QStringLiteral("msi"), QStringLiteral("msix"), QStringLiteral("apk")};
    static const QSet<QString> archives{QStringLiteral("zip"), QStringLiteral("rar"), QStringLiteral("7z"), QStringLiteral("tar"), QStringLiteral("gz")};
    static const QSet<QString> docs{QStringLiteral("pdf"), QStringLiteral("doc"), QStringLiteral("docx"), QStringLiteral("xls"), QStringLiteral("xlsx"), QStringLiteral("ppt"), QStringLiteral("pptx")};
    static const QSet<QString> media{QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("png"), QStringLiteral("gif"), QStringLiteral("mp4"), QStringLiteral("mov"), QStringLiteral("mkv")};
    if (installers.contains(extension)) {
        return QStringLiteral("安装包");
    }
    if (archives.contains(extension)) {
        return QStringLiteral("压缩包");
    }
    if (docs.contains(extension)) {
        return QStringLiteral("文档");
    }
    if (media.contains(extension)) {
        return QStringLiteral("媒体");
    }
    return QStringLiteral("其他");
}

/**
 * @brief 扫描下载整理中心模块。
 * @param out 输出结果。
 * @param sourcePath 额外扫描路径。
 */
void ScanDownloadOrganizer(QVector<FeatureFinding>& out, const QString& sourcePath, std::shared_ptr<std::atomic_bool> cancelFlag) {
    QStringList roots;
    roots << StandardPathOrHome(QStandardPaths::DownloadLocation);
    roots << StandardPathOrHome(QStandardPaths::DesktopLocation);
    roots << QDir::homePath() + QStringLiteral("/Documents/WeChat Files");
    roots << QDir::homePath() + QStringLiteral("/Documents/Tencent Files");
    if (PathExists(sourcePath)) {
        roots << sourcePath;
    }
    for (const QString& root : ExistingPaths(roots)) {
        if (IsCancelled(cancelFlag)) {
            break;
        }
        std::map<QString, std::pair<std::uint64_t, int>> buckets;
        QDirIterator iterator(root, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
        int visited = 0;
        while (iterator.hasNext() && visited < 60000 && !IsCancelled(cancelFlag)) {
            iterator.next();
            ++visited;
            const QFileInfo info = iterator.fileInfo();
            const QString ext = info.suffix().toLower();
            auto& bucket = buckets[DownloadBucketForExtension(ext)];
            bucket.first += static_cast<std::uint64_t>(std::max<qint64>(0, info.size()));
            bucket.second += 1;
        }
        for (const auto& item : buckets) {
            AddFinding(out, FeatureModule::DownloadOrganizer,
                       QStringLiteral("%1 · %2").arg(QFileInfo(root).fileName().isEmpty() ? root : QFileInfo(root).fileName(), item.first),
                       QStringLiteral("整理计划"),
                       QStringLiteral("共 %1 个文件，可按类型移动到子目录。").arg(item.second.second),
                       root, item.second.first);
        }
    }
}

/**
 * @brief 判断文件名是否像敏感文件。
 * @param info 文件信息。
 * @return 命中敏感规则时返回 true。
 */
bool LooksSensitiveFile(const QFileInfo& info) {
    const QString name = info.fileName().toCaseFolded();
    const QString suffix = info.suffix().toCaseFolded();
    static const QSet<QString> sensitiveSuffixes{
        QStringLiteral("pem"), QStringLiteral("key"), QStringLiteral("pfx"), QStringLiteral("p12"),
        QStringLiteral("crt"), QStringLiteral("cer"), QStringLiteral("kdbx"), QStringLiteral("env")
    };
    if (sensitiveSuffixes.contains(suffix)) {
        return true;
    }
    const QStringList needles{
        QStringLiteral("id_rsa"), QStringLiteral("password"), QStringLiteral("passwd"), QStringLiteral("secret"),
        QStringLiteral("token"), QStringLiteral("credential"), QStringLiteral("密码"), QStringLiteral("身份证"),
        QStringLiteral("合同"), QStringLiteral("私钥"), QStringLiteral("证书")
    };
    for (const QString& needle : needles) {
        if (name.contains(needle)) {
            return true;
        }
    }
    return false;
}

/**
 * @brief 扫描隐私文件雷达模块。
 * @param out 输出结果。
 * @param sourcePath 源路径。
 */
void ScanPrivacyRadar(QVector<FeatureFinding>& out, const QString& sourcePath, std::shared_ptr<std::atomic_bool> cancelFlag) {
    QStringList roots = ImportantUserFolders();
    if (PathExists(sourcePath)) {
        roots << sourcePath;
    }
    for (const QString& root : ExistingPaths(roots)) {
        if (IsCancelled(cancelFlag)) {
            break;
        }
        QDirIterator iterator(root, QDir::Files | QDir::Hidden | QDir::System, QDirIterator::Subdirectories);
        int emitted = 0;
        int visited = 0;
        while (iterator.hasNext() && emitted < 120 && visited < 120000 && !IsCancelled(cancelFlag)) {
            iterator.next();
            ++visited;
            const QFileInfo info = iterator.fileInfo();
            if (!LooksSensitiveFile(info)) {
                continue;
            }
            AddFinding(out, FeatureModule::PrivacyRadar, info.fileName(), QStringLiteral("敏感文件"),
                       QStringLiteral("建议确认是否应放在同步盘、下载目录或公开项目中。"),
                       info.absoluteFilePath(), static_cast<std::uint64_t>(std::max<qint64>(0, info.size())));
            ++emitted;
        }
    }
}

/**
 * @brief 判断目录名是否为开发空间热点。
 * @param name 目录名。
 * @return 命中开发热点时返回 true。
 */
bool IsDeveloperHotDirectory(const QString& name) {
    static const QSet<QString> names{
        QStringLiteral("node_modules"), QStringLiteral(".venv"), QStringLiteral("venv"), QStringLiteral("target"),
        QStringLiteral("build"), QStringLiteral("dist"), QStringLiteral(".next"), QStringLiteral(".gradle"),
        QStringLiteral(".m2"), QStringLiteral(".cargo"), QStringLiteral(".pnpm-store"), QStringLiteral("packages")
    };
    return names.contains(name.toCaseFolded());
}

/**
 * @brief 扫描开发环境空间中心模块。
 * @param out 输出结果。
 * @param sourcePath 源路径。
 */
void ScanDeveloperSpace(QVector<FeatureFinding>& out, const QString& sourcePath, std::shared_ptr<std::atomic_bool> cancelFlag) {
    QStringList roots;
    roots << QDir::homePath();
    if (PathExists(sourcePath)) {
        roots << sourcePath;
    }
    for (const QString& root : ExistingPaths(roots)) {
        if (IsCancelled(cancelFlag)) {
            break;
        }
        QDirIterator iterator(root, QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden, QDirIterator::Subdirectories);
        int emitted = 0;
        int visited = 0;
        while (iterator.hasNext() && emitted < 120 && visited < 120000 && !IsCancelled(cancelFlag)) {
            iterator.next();
            ++visited;
            const QFileInfo info = iterator.fileInfo();
            if (!IsDeveloperHotDirectory(info.fileName())) {
                continue;
            }
            const PathSizeSummary summary = ComputePathSizeLimited(info.absoluteFilePath(), 12000, cancelFlag);
            if (summary.bytes < 100ULL * 1024ULL * 1024ULL) {
                continue;
            }
            AddFinding(out, FeatureModule::DeveloperSpace, info.fileName(), QStringLiteral("开发空间热点"),
                       QStringLiteral("依赖、构建产物或包缓存，可结合项目状态决定清理或重建。"),
                       info.absoluteFilePath(), summary.bytes);
            ++emitted;
        }
    }
}

/**
 * @brief 扫描 Docker 和 WSL 空间管理模块。
 * @param out 输出结果。
 */
void ScanDockerWsl(QVector<FeatureFinding>& out, std::shared_ptr<std::atomic_bool> cancelFlag) {
    QStringList candidates;
    const QString localAppData = qEnvironmentVariable("LOCALAPPDATA");
    candidates << localAppData + QStringLiteral("/Docker");
    candidates << localAppData + QStringLiteral("/Packages");
    candidates << QDir::homePath() + QStringLiteral("/AppData/Local/Docker");
    for (const QString& root : ExistingPaths(candidates)) {
        if (IsCancelled(cancelFlag)) {
            break;
        }
        QDirIterator iterator(root, QStringList{QStringLiteral("*.vhdx")}, QDir::Files | QDir::Hidden | QDir::System, QDirIterator::Subdirectories);
        while (iterator.hasNext() && !IsCancelled(cancelFlag)) {
            iterator.next();
            const QFileInfo info = iterator.fileInfo();
            AddFinding(out, FeatureModule::DockerWsl, info.fileName(), QStringLiteral("虚拟磁盘"),
                       QStringLiteral("Docker / WSL 虚拟磁盘。可在确认停止相关发行版后执行压缩或 prune。"),
                       info.absoluteFilePath(), static_cast<std::uint64_t>(std::max<qint64>(0, info.size())));
        }
    }
    if (QStandardPaths::findExecutable(QStringLiteral("docker.exe")).isEmpty()) {
        AddFinding(out, FeatureModule::DockerWsl, QStringLiteral("Docker CLI"), QStringLiteral("未检测到"),
                   QStringLiteral("未在 PATH 中找到 docker.exe，Docker 统计仅基于磁盘文件发现。"));
    } else {
        AddFinding(out, FeatureModule::DockerWsl, QStringLiteral("Docker CLI"), QStringLiteral("可用"),
                   QStringLiteral("检测到 docker.exe，可在后续版本接入 image / volume / build cache 细分统计。"));
    }
}

/**
 * @brief 判断目录名是否为常见浏览器缓存目录。
 * @param name 目录名。
 * @param absolutePath 目录完整路径。
 * @return 命中缓存目录时返回 true。
 */
bool IsBrowserCacheDirectory(const QString& name, const QString& absolutePath) {
    const QString foldedName = name.toCaseFolded();
    const QString foldedPath = QDir::fromNativeSeparators(absolutePath).toCaseFolded();
    static const QSet<QString> names{
        QStringLiteral("cache"), QStringLiteral("code cache"), QStringLiteral("gpucache"),
        QStringLiteral("cachestorage"), QStringLiteral("shadercache"), QStringLiteral("blob_storage"),
        QStringLiteral("indexeddb")
    };
    return names.contains(foldedName) || foldedPath.contains(QStringLiteral("/service worker/cachestorage"));
}

/**
 * @brief 扫描浏览器缓存中心模块。
 * @param out 输出结果。
 * @param cancelFlag 取消标志。
 */
void ScanBrowserCache(QVector<FeatureFinding>& out, std::shared_ptr<std::atomic_bool> cancelFlag) {
    const QString localAppData = qEnvironmentVariable("LOCALAPPDATA");
    const QString roamingAppData = qEnvironmentVariable("APPDATA");
    QStringList roots;
    roots << localAppData + QStringLiteral("/Google/Chrome/User Data");
    roots << localAppData + QStringLiteral("/Microsoft/Edge/User Data");
    roots << localAppData + QStringLiteral("/BraveSoftware/Brave-Browser/User Data");
    roots << localAppData + QStringLiteral("/Mozilla/Firefox/Profiles");
    roots << roamingAppData + QStringLiteral("/Mozilla/Firefox/Profiles");

    int emitted = 0;
    for (const QString& root : ExistingPaths(roots)) {
        if (IsCancelled(cancelFlag)) {
            break;
        }
        QDirIterator iterator(root, QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                              QDirIterator::Subdirectories);
        int visited = 0;
        while (iterator.hasNext() && emitted < 120 && visited < 90000 && !IsCancelled(cancelFlag)) {
            iterator.next();
            ++visited;
            const QFileInfo info = iterator.fileInfo();
            if (!IsBrowserCacheDirectory(info.fileName(), info.absoluteFilePath())) {
                continue;
            }
            const PathSizeSummary summary = ComputePathSizeLimited(info.absoluteFilePath(), 12000, cancelFlag);
            if (summary.bytes < 80ULL * 1024ULL * 1024ULL) {
                continue;
            }
            AddFinding(out, FeatureModule::BrowserCache, info.fileName(), QStringLiteral("浏览器缓存"),
                       QStringLiteral("建议在浏览器设置中清理缓存或站点离线数据，避免直接删除整个用户配置。"),
                       info.absoluteFilePath(), summary.bytes);
            ++emitted;
        }
    }

    if (emitted == 0) {
        AddFinding(out, FeatureModule::BrowserCache, QStringLiteral("浏览器缓存"), QStringLiteral("未发现大缓存"),
                   QStringLiteral("未在常见 Chrome、Edge、Firefox 配置目录发现超过阈值的缓存目录。"));
    }
}

/**
 * @brief 扫描启动文件夹中的启动入口。
 * @param out 输出结果。
 * @param folder 启动文件夹路径。
 * @param cancelFlag 取消标志。
 */
void ScanStartupFolder(QVector<FeatureFinding>& out, const QString& folder, std::shared_ptr<std::atomic_bool> cancelFlag) {
    if (!PathExists(folder)) {
        return;
    }
    QDirIterator iterator(folder, QDir::Files | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
    while (iterator.hasNext() && !IsCancelled(cancelFlag)) {
        iterator.next();
        const QFileInfo info = iterator.fileInfo();
        AddFinding(out, FeatureModule::StartupFootprint, info.fileName(), QStringLiteral("启动文件夹"),
                   QStringLiteral("开机启动文件夹入口。可在确认不需要后从系统启动管理中禁用。"),
                   info.absoluteFilePath(), static_cast<std::uint64_t>(std::max<qint64>(0, info.size())));
    }
}

/**
 * @brief 扫描注册表 Run 启动项。
 * @param out 输出结果。
 * @param registryRoot 注册表 Run 项路径。
 * @param label 结果来源标签。
 * @param cancelFlag 取消标志。
 */
void ScanStartupRegistry(QVector<FeatureFinding>& out, const QString& registryRoot, const QString& label,
                         std::shared_ptr<std::atomic_bool> cancelFlag) {
    QSettings registry(registryRoot, QSettings::NativeFormat);
    const QStringList keys = registry.allKeys();
    for (const QString& key : keys) {
        if (IsCancelled(cancelFlag)) {
            break;
        }
        const QString command = registry.value(key).toString();
        const QString executable = ExecutablePathFromCommand(command);
        const QFileInfo info(executable);
        const std::uint64_t bytes = info.exists() && info.isFile()
            ? static_cast<std::uint64_t>(std::max<qint64>(0, info.size()))
            : 0;
        AddFinding(out, FeatureModule::StartupFootprint, QStringLiteral("%1 · %2").arg(label, key),
                   bytes > 0 ? QStringLiteral("启动项") : QStringLiteral("路径待确认"),
                   QStringLiteral("启动命令：%1").arg(command),
                   info.exists() ? info.absoluteFilePath() : executable, bytes);
    }
}

/**
 * @brief 扫描启动项体积检查模块。
 * @param out 输出结果。
 * @param cancelFlag 取消标志。
 */
void ScanStartupFootprint(QVector<FeatureFinding>& out, std::shared_ptr<std::atomic_bool> cancelFlag) {
    const QString appData = qEnvironmentVariable("APPDATA");
    const QString programData = qEnvironmentVariable("ProgramData");
    ScanStartupFolder(out, appData + QStringLiteral("/Microsoft/Windows/Start Menu/Programs/Startup"), cancelFlag);
    ScanStartupFolder(out, programData + QStringLiteral("/Microsoft/Windows/Start Menu/Programs/Startup"), cancelFlag);

    ScanStartupRegistry(out, QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"),
                        QStringLiteral("当前用户"), cancelFlag);
    ScanStartupRegistry(out, QStringLiteral("HKEY_LOCAL_MACHINE\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"),
                        QStringLiteral("全局"), cancelFlag);
    ScanStartupRegistry(out, QStringLiteral("HKEY_LOCAL_MACHINE\\Software\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Run"),
                        QStringLiteral("全局 32 位"), cancelFlag);

    bool hasStartupFinding = false;
    for (const FeatureFinding& finding : out) {
        if (finding.module == FeatureModule::StartupFootprint) {
            hasStartupFinding = true;
            break;
        }
    }
    if (!hasStartupFinding) {
        AddFinding(out, FeatureModule::StartupFootprint, QStringLiteral("启动项"), QStringLiteral("未发现"),
                   QStringLiteral("常见启动文件夹和 Run 注册表项未发现可展示入口。"));
    }
}

/**
 * @brief 扫描聊天缓存治理模块。
 * @param out 输出结果。
 * @param cancelFlag 取消标志。
 */
void ScanMessengerCache(QVector<FeatureFinding>& out, std::shared_ptr<std::atomic_bool> cancelFlag) {
    struct CacheCandidate {
        /**
         * @brief 展示名称。
         */
        QString name;

        /**
         * @brief 缓存候选路径。
         */
        QString path;
    };

    const QString localAppData = qEnvironmentVariable("LOCALAPPDATA");
    const QString roamingAppData = qEnvironmentVariable("APPDATA");
    const QString documents = StandardPathOrHome(QStandardPaths::DocumentsLocation);
    QVector<CacheCandidate> candidates{
        {QStringLiteral("微信文件"), documents + QStringLiteral("/WeChat Files")},
        {QStringLiteral("企业微信文件"), documents + QStringLiteral("/WXWork")},
        {QStringLiteral("微信配置缓存"), roamingAppData + QStringLiteral("/Tencent/WeChat")},
        {QStringLiteral("QQ 缓存"), roamingAppData + QStringLiteral("/Tencent/QQ")},
        {QStringLiteral("Microsoft Teams"), roamingAppData + QStringLiteral("/Microsoft/Teams")},
        {QStringLiteral("新版 Teams"), localAppData + QStringLiteral("/Packages/MSTeams_8wekyb3d8bbwe")},
        {QStringLiteral("Slack"), roamingAppData + QStringLiteral("/Slack")},
        {QStringLiteral("Discord"), roamingAppData + QStringLiteral("/discord")},
        {QStringLiteral("Telegram Desktop"), roamingAppData + QStringLiteral("/Telegram Desktop")},
    };

    int emitted = 0;
    for (const CacheCandidate& candidate : candidates) {
        if (IsCancelled(cancelFlag)) {
            break;
        }
        if (!PathExists(candidate.path)) {
            continue;
        }
        const PathSizeSummary summary = ComputePathSizeLimited(candidate.path, 30000, cancelFlag);
        if (summary.bytes < 150ULL * 1024ULL * 1024ULL) {
            continue;
        }
        AddFinding(out, FeatureModule::MessengerCache, candidate.name, QStringLiteral("聊天缓存"),
                   QStringLiteral("聊天客户端本地文件和缓存。建议优先在客户端内清理或迁移存储位置。"),
                   candidate.path, summary.bytes);
        ++emitted;
    }

    if (emitted == 0) {
        AddFinding(out, FeatureModule::MessengerCache, QStringLiteral("聊天缓存"), QStringLiteral("未发现大缓存"),
                   QStringLiteral("常见聊天客户端目录未发现超过阈值的本地缓存。"));
    }
}

/**
 * @brief 扫描邮件归档库检查模块。
 * @param out 输出结果。
 * @param sourcePath 源路径。
 * @param cancelFlag 取消标志。
 */
void ScanMailArchive(QVector<FeatureFinding>& out, const QString& sourcePath, std::shared_ptr<std::atomic_bool> cancelFlag) {
    QStringList roots;
    const QString localAppData = qEnvironmentVariable("LOCALAPPDATA");
    const QString roamingAppData = qEnvironmentVariable("APPDATA");
    roots << StandardPathOrHome(QStandardPaths::DocumentsLocation);
    roots << localAppData + QStringLiteral("/Microsoft/Outlook");
    roots << roamingAppData + QStringLiteral("/Microsoft/Outlook");
    if (PathExists(sourcePath)) {
        roots << sourcePath;
    }

    int emitted = 0;
    for (const QString& root : ExistingPaths(roots)) {
        if (IsCancelled(cancelFlag)) {
            break;
        }
        QDirIterator iterator(root,
                              QStringList{QStringLiteral("*.pst"), QStringLiteral("*.ost"),
                                          QStringLiteral("*.mbox"), QStringLiteral("*.olm")},
                              QDir::Files | QDir::Hidden | QDir::System,
                              QDirIterator::Subdirectories);
        int visited = 0;
        while (iterator.hasNext() && emitted < 120 && visited < 120000 && !IsCancelled(cancelFlag)) {
            iterator.next();
            ++visited;
            const QFileInfo info = iterator.fileInfo();
            const std::uint64_t bytes = static_cast<std::uint64_t>(std::max<qint64>(0, info.size()));
            if (bytes < 100ULL * 1024ULL * 1024ULL) {
                continue;
            }
            AddFinding(out, FeatureModule::MailArchive, info.fileName(), QStringLiteral("邮件归档"),
                       QStringLiteral("大型邮件归档或离线邮箱文件。处理前应先确认账户同步和备份状态。"),
                       info.absoluteFilePath(), bytes);
            ++emitted;
        }
    }

    if (emitted == 0) {
        AddFinding(out, FeatureModule::MailArchive, QStringLiteral("邮件归档"), QStringLiteral("未发现大归档"),
                   QStringLiteral("常见 Outlook 与文档目录未发现超过阈值的 PST / OST / MBOX 文件。"));
    }
}

/**
 * @brief 扫描虚拟机镜像管理模块。
 * @param out 输出结果。
 * @param sourcePath 源路径。
 * @param cancelFlag 取消标志。
 */
void ScanVirtualMachineImages(QVector<FeatureFinding>& out, const QString& sourcePath,
                              std::shared_ptr<std::atomic_bool> cancelFlag) {
    QStringList roots;
    roots << StandardPathOrHome(QStandardPaths::DocumentsLocation);
    roots << QDir::homePath() + QStringLiteral("/VirtualBox VMs");
    roots << QDir::homePath() + QStringLiteral("/VMware");
    if (PathExists(sourcePath)) {
        roots << sourcePath;
    }

    int emitted = 0;
    for (const QString& root : ExistingPaths(roots)) {
        if (IsCancelled(cancelFlag)) {
            break;
        }
        QDirIterator iterator(root,
                              QStringList{QStringLiteral("*.vhd"), QStringLiteral("*.vhdx"),
                                          QStringLiteral("*.avhdx"), QStringLiteral("*.vmdk"),
                                          QStringLiteral("*.vdi"), QStringLiteral("*.qcow2")},
                              QDir::Files | QDir::Hidden | QDir::System,
                              QDirIterator::Subdirectories);
        int visited = 0;
        while (iterator.hasNext() && emitted < 120 && visited < 160000 && !IsCancelled(cancelFlag)) {
            iterator.next();
            ++visited;
            const QFileInfo info = iterator.fileInfo();
            const std::uint64_t bytes = static_cast<std::uint64_t>(std::max<qint64>(0, info.size()));
            if (bytes < 1024ULL * 1024ULL * 1024ULL) {
                continue;
            }
            AddFinding(out, FeatureModule::VirtualMachineImages, info.fileName(), QStringLiteral("虚拟磁盘"),
                       QStringLiteral("大型虚拟机磁盘。建议通过对应虚拟化软件迁移、压缩或清理快照。"),
                       info.absoluteFilePath(), bytes);
            ++emitted;
        }
    }

    if (emitted == 0) {
        AddFinding(out, FeatureModule::VirtualMachineImages, QStringLiteral("虚拟机镜像"), QStringLiteral("未发现大镜像"),
                   QStringLiteral("常见虚拟机目录未发现超过 1 GB 的虚拟磁盘文件。"));
    }
}

/**
 * @brief 判断扩展名是否为媒体文件。
 * @param suffix 小写扩展名。
 * @return 是媒体文件时返回 true。
 */
bool IsMediaSuffix(const QString& suffix) {
    static const QSet<QString> suffixes{
        QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("png"), QStringLiteral("gif"), QStringLiteral("bmp"),
        QStringLiteral("heic"), QStringLiteral("raw"), QStringLiteral("cr2"), QStringLiteral("nef"), QStringLiteral("arw"),
        QStringLiteral("mp4"), QStringLiteral("mov"), QStringLiteral("mkv"), QStringLiteral("avi"), QStringLiteral("wmv")
    };
    return suffixes.contains(suffix);
}

/**
 * @brief 扫描照片视频整理器模块。
 * @param out 输出结果。
 * @param sourcePath 源路径。
 */
void ScanMediaOrganizer(QVector<FeatureFinding>& out, const QString& sourcePath, std::shared_ptr<std::atomic_bool> cancelFlag) {
    const QString root = PathExists(sourcePath) ? sourcePath : StandardPathOrHome(QStandardPaths::PicturesLocation);
    std::map<QString, std::pair<std::uint64_t, int>> byExt;
    QDirIterator iterator(root, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    int visited = 0;
    while (iterator.hasNext() && visited < 150000 && !IsCancelled(cancelFlag)) {
        iterator.next();
        ++visited;
        const QFileInfo info = iterator.fileInfo();
        const QString suffix = info.suffix().toLower();
        if (!IsMediaSuffix(suffix)) {
            continue;
        }
        auto& bucket = byExt[suffix.isEmpty() ? QStringLiteral("无扩展名") : suffix];
        bucket.first += static_cast<std::uint64_t>(std::max<qint64>(0, info.size()));
        bucket.second += 1;
    }
    for (const auto& item : byExt) {
        AddFinding(out, FeatureModule::MediaOrganizer, QStringLiteral(".%1 媒体").arg(item.first),
                   QStringLiteral("媒体分组"),
                   QStringLiteral("共 %1 个文件，可按年份 / 设备 / 类型继续整理。").arg(item.second.second),
                   root, item.second.first);
    }
}

/**
 * @brief 扫描目录预算模块。
 * @param out 输出结果。
 * @param sourcePath 源路径。
 */
void ScanQuotaBudget(QVector<FeatureFinding>& out, const QString& sourcePath, std::shared_ptr<std::atomic_bool> cancelFlag) {
    struct Budget {
        QString path;
        std::uint64_t budgetBytes = 0;
    };
    QVector<Budget> budgets{
        {StandardPathOrHome(QStandardPaths::DownloadLocation), 20ULL * 1024ULL * 1024ULL * 1024ULL},
        {StandardPathOrHome(QStandardPaths::DesktopLocation), 10ULL * 1024ULL * 1024ULL * 1024ULL},
        {StandardPathOrHome(QStandardPaths::DocumentsLocation), 50ULL * 1024ULL * 1024ULL * 1024ULL},
    };
    if (PathExists(sourcePath)) {
        budgets.push_back({sourcePath, 50ULL * 1024ULL * 1024ULL * 1024ULL});
    }
    for (const Budget& budget : budgets) {
        if (IsCancelled(cancelFlag)) {
            break;
        }
        const PathSizeSummary summary = ComputePathSizeLimited(budget.path, 20000, cancelFlag);
        const bool over = summary.bytes > budget.budgetBytes;
        // 说明里明确这是"建议预算(参考值)"而非系统实测配额——NTFS 真实配额查询留给后续批次;当前硬
        // 编码预算不可被读成"系统认定的配额"。截断时 bytes 是下限估算,需告知用户实际可能更高。
        QString detail = QStringLiteral("建议预算 %1（参考值，非系统实测配额），当前约 %2。")
                             .arg(FormatBytesText(budget.budgetBytes), FormatBytesText(summary.bytes));
        if (summary.truncated) {
            detail += QStringLiteral("目录条目较多，已按扫描上限估算，实际占用可能更高。");
        }
        AddFinding(out, FeatureModule::QuotaBudget, QFileInfo(budget.path).fileName().isEmpty() ? budget.path : QFileInfo(budget.path).fileName(),
                   over ? QStringLiteral("超预算") : QStringLiteral("预算内"),
                   detail,
                   budget.path, summary.bytes);
    }
}

/**
 * @brief 扫描备份缺口模块。
 * @param out 输出结果。
 * @param sourcePath 源路径。
 * @param targetPath 目标路径。
 */
void ScanBackupGap(QVector<FeatureFinding>& out, const QString& sourcePath, const QString& targetPath,
                   std::shared_ptr<std::atomic_bool> cancelFlag) {
    QStringList important = ImportantUserFolders();
    if (PathExists(sourcePath)) {
        important << sourcePath;
    }
    important.removeDuplicates();
    const bool hasTarget = PathExists(targetPath);
    if (!hasTarget) {
        // 无目标盘时不可判定备份缺口:原行为会把每个重要目录都打成"备份缺口"(SeverityForFinding 命中
        // Critical"备份缺口"),整列假性高严重度。改为单条中性"等待输入"提示,引导用户先选备份目标盘,
        // 既有"有目标时逐目录核对"逻辑不变。
        AddFinding(out, FeatureModule::BackupGap, QStringLiteral("请选择备份目标"), QStringLiteral("等待输入"),
                   QStringLiteral("未设置备份目标盘，无法核对备份缺口。请在目标路径选择一个备份盘或目录后重新体检。"));
        return;
    }
    for (const QString& path : ExistingPaths(important)) {
        if (IsCancelled(cancelFlag)) {
            break;
        }
        const PathSizeSummary summary = ComputePathSizeLimited(path, 15000, cancelFlag);
        const QString expectedBackup = QDir(targetPath).filePath(QFileInfo(path).fileName());
        const bool backed = QFileInfo::exists(expectedBackup);
        QString detail = backed
            ? QStringLiteral("目标路径下存在同名备份目录，仍建议比对修改时间。")
            : QStringLiteral("未在目标路径发现同名备份目录；磁盘健康异常时应优先处理。");
        if (summary.truncated) {
            detail += QStringLiteral("目录条目较多，已按扫描上限估算，实际占用可能更高。");
        }
        AddFinding(out, FeatureModule::BackupGap, QFileInfo(path).fileName().isEmpty() ? path : QFileInfo(path).fileName(),
                   backed ? QStringLiteral("发现备份目录") : QStringLiteral("备份缺口"),
                   detail,
                   path, summary.bytes);
    }
}

/**
 * @brief 通过 Restart Manager 查询文件占用进程。
 * @param path 文件路径。
 * @return 占用进程说明列表。
 */
QStringList QueryLockingProcesses(const QString& path) {
    QStringList processes;
    if (!PathExists(path)) {
        return processes;
    }

    DWORD session = 0;
    WCHAR sessionKey[CCH_RM_SESSION_KEY + 1]{};
    if (RmStartSession(&session, 0, sessionKey) != ERROR_SUCCESS) {
        return processes;
    }

    const std::wstring resource = QDir::toNativeSeparators(path).toStdWString();
    LPCWSTR resources[] = {resource.c_str()};
    if (RmRegisterResources(session, 1, resources, 0, nullptr, 0, nullptr) == ERROR_SUCCESS) {
        UINT needed = 0;
        UINT count = 0;
        DWORD reason = 0;
        DWORD status = RmGetList(session, &needed, &count, nullptr, &reason);
        if (status == ERROR_MORE_DATA && needed > 0) {
            QVector<RM_PROCESS_INFO> infos(static_cast<int>(needed));
            count = needed;
            if (RmGetList(session, &needed, &count, infos.data(), &reason) == ERROR_SUCCESS) {
                for (UINT index = 0; index < count; ++index) {
                    const RM_PROCESS_INFO& info = infos[static_cast<int>(index)];
                    processes << QStringLiteral("%1 (PID %2)")
                                     .arg(QString::fromWCharArray(info.strAppName))
                                     .arg(info.Process.dwProcessId);
                }
            }
        }
    }
    RmEndSession(session);
    processes.removeDuplicates();
    return processes;
}

/**
 * @brief 扫描文件占用解锁器模块。
 * @param out 输出结果。
 * @param sourcePath 源路径。
 */
void ScanFileUnlocker(QVector<FeatureFinding>& out, const QString& sourcePath, std::shared_ptr<std::atomic_bool> cancelFlag) {
    if (!PathExists(sourcePath)) {
        AddFinding(out, FeatureModule::FileUnlocker, QStringLiteral("请选择文件或目录"), QStringLiteral("等待输入"),
                   QStringLiteral("设置源路径后可查询占用进程。"));
        return;
    }

    QStringList targets;
    const QFileInfo info(sourcePath);
    if (info.isFile()) {
        targets << info.absoluteFilePath();
    } else {
        QDirIterator iterator(sourcePath, QDir::Files | QDir::Hidden | QDir::System, QDirIterator::Subdirectories);
        while (iterator.hasNext() && targets.size() < 30 && !IsCancelled(cancelFlag)) {
            iterator.next();
            targets << iterator.filePath();
        }
    }

    int locked = 0;
    for (const QString& target : targets) {
        if (IsCancelled(cancelFlag)) {
            break;
        }
        const QStringList processes = QueryLockingProcesses(target);
        if (processes.isEmpty()) {
            continue;
        }
        ++locked;
        AddFinding(out, FeatureModule::FileUnlocker, QFileInfo(target).fileName(), QStringLiteral("被占用"),
                   QStringLiteral("占用进程：%1。").arg(processes.join(QStringLiteral("、"))),
                   target, static_cast<std::uint64_t>(std::max<qint64>(0, QFileInfo(target).size())));
    }
    if (locked == 0) {
        AddFinding(out, FeatureModule::FileUnlocker, QFileInfo(sourcePath).fileName().isEmpty() ? sourcePath : QFileInfo(sourcePath).fileName(),
                   QStringLiteral("未发现占用"), QStringLiteral("Restart Manager 未发现当前路径样本被进程占用。"), sourcePath, 0);
    }
}

/**
 * @brief 扫描大文件传输助手模块。
 * @param out 输出结果。
 * @param sourcePath 源路径。
 * @param targetPath 目标路径。
 */
void ScanTransferAssistant(QVector<FeatureFinding>& out, const QString& sourcePath, const QString& targetPath,
                           std::shared_ptr<std::atomic_bool> cancelFlag) {
    if (!PathExists(sourcePath)) {
        AddFinding(out, FeatureModule::TransferAssistant, QStringLiteral("源路径不可用"), QStringLiteral("等待输入"),
                   QStringLiteral("请选择要迁移的大文件或目录。"));
        return;
    }
    if (!PathExists(targetPath)) {
        // 无目标盘时不再静默回退到 QDir::rootPath()(C:):原行为会把系统盘当成迁移目标,误报其可用空间
        // 充足。改为中性"等待输入"提示,引导用户先选目标盘。
        AddFinding(out, FeatureModule::TransferAssistant, QStringLiteral("目标路径不可用"), QStringLiteral("等待输入"),
                   QStringLiteral("未设置迁移目标盘，无法核对目标空间。请在目标路径选择一个空间充足的盘或目录后重新体检。"));
        return;
    }
    const PathSizeSummary summary = ComputePathSizeLimited(sourcePath, 50000, cancelFlag);
    QStorageInfo targetStorage(targetPath);
    const std::uint64_t freeBytes = targetStorage.isValid() ? static_cast<std::uint64_t>(std::max<qint64>(0, targetStorage.bytesAvailable())) : 0;
    // 截断时 summary.bytes 是下限估算:既不能据此时断言"目标空间充足"(假阴性),也不宜直接判"不足"
    // (假阳性,Critical)。改用中性"体积待复核"状态并提示人工核对,避免任一方向的错误结论。
    QString state;
    QString detail = QStringLiteral("待迁移约 %1，目标可用约 %2。建议迁移前启用校验并保留源文件到确认完成。")
                         .arg(FormatBytesText(summary.bytes), FormatBytesText(freeBytes));
    if (summary.truncated) {
        state = QStringLiteral("体积待复核");
        detail += QStringLiteral("源体积超过扫描上限，以下为部分估算，实际可能更大，请勿仅据此判断目标空间是否充足。");
    } else {
        state = freeBytes > summary.bytes ? QStringLiteral("目标空间充足") : QStringLiteral("目标空间不足");
    }
    AddFinding(out, FeatureModule::TransferAssistant, QFileInfo(sourcePath).fileName().isEmpty() ? sourcePath : QFileInfo(sourcePath).fileName(),
               state, detail, sourcePath, summary.bytes);
}

/**
 * @brief 扫描同步盘空间分析模块。
 * @param out 输出结果。
 */
void ScanCloudSync(QVector<FeatureFinding>& out, std::shared_ptr<std::atomic_bool> cancelFlag) {
    QStringList roots;
    roots << qEnvironmentVariable("OneDrive");
    roots << QDir::homePath() + QStringLiteral("/Dropbox");
    roots << QDir::homePath() + QStringLiteral("/Google Drive");
    roots << QDir::homePath() + QStringLiteral("/iCloudDrive");
    roots << QDir::homePath() + QStringLiteral("/百度网盘");
    roots << QDir::homePath() + QStringLiteral("/坚果云");
    for (const QString& root : ExistingPaths(roots)) {
        if (IsCancelled(cancelFlag)) {
            break;
        }
        const PathSizeSummary summary = ComputePathSizeLimited(root, 30000, cancelFlag);
        AddFinding(out, FeatureModule::CloudSync, QFileInfo(root).fileName().isEmpty() ? root : QFileInfo(root).fileName(),
                   QStringLiteral("同步目录"), QStringLiteral("本地同步目录占用估算；建议检查仅云端、冲突副本和重复下载。"),
                   root, summary.bytes);
        QDirIterator iterator(root, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
        int conflicts = 0;
        while (iterator.hasNext() && conflicts < 40 && !IsCancelled(cancelFlag)) {
            iterator.next();
            const QFileInfo info = iterator.fileInfo();
            const QString name = info.fileName().toCaseFolded();
            if (name.contains(QStringLiteral("conflict")) || name.contains(QStringLiteral("冲突"))) {
                AddFinding(out, FeatureModule::CloudSync, info.fileName(), QStringLiteral("同步冲突"),
                           QStringLiteral("疑似同步冲突副本，建议确认后合并或删除。"),
                           info.absoluteFilePath(), static_cast<std::uint64_t>(std::max<qint64>(0, info.size())));
                ++conflicts;
            }
        }
    }
}

/**
 * @brief 扫描系统镜像与恢复点模块。
 * @param out 输出结果。
 */
void ScanRestorePoint(QVector<FeatureFinding>& out, std::shared_ptr<std::atomic_bool> cancelFlag) {
    const QStringList roots = ExistingPaths({
        QStringLiteral("C:/Windows.old"),
        QStringLiteral("C:/$WINDOWS.~BT"),
        QStringLiteral("C:/Windows/SoftwareDistribution/Download"),
        QStringLiteral("C:/Recovery"),
    });
    for (const QString& root : roots) {
        if (IsCancelled(cancelFlag)) {
            break;
        }
        const PathSizeSummary summary = ComputePathSizeLimited(root, 30000, cancelFlag);
        AddFinding(out, FeatureModule::RestorePoint, QFileInfo(root).fileName().isEmpty() ? root : QFileInfo(root).fileName(),
                   QStringLiteral("系统备份"), QStringLiteral("系统升级、恢复或更新缓存。建议通过 Windows 系统入口处理。"),
                   root, summary.bytes);
    }
    AddFinding(out, FeatureModule::RestorePoint, QStringLiteral("系统保护入口"), QStringLiteral("系统入口"),
               QStringLiteral("卷影副本 / 还原点需要通过 Windows 系统保护界面查看和调整。"));
}

/**
 * @brief 执行单个模块的体检(异常隔离层)。
 *
 * 本函数封装"按模块分发到对应 ScanXxx"的 switch。它在 BuildFindings 中被 per-module try/catch 包裹
 * 调用:任一模块抛异常时,由该 catch 捕获并以一条"扫描失败"占位行替代该模块结果,不让异常冒泡到
 * 后台工作线程(否则 std::terminate 整个进程),其余模块继续扫描。
 */
void ScanOneModule(QVector<FeatureFinding>& out, FeatureModule module, const QString& sourcePath, const QString& targetPath,
                   std::shared_ptr<std::atomic_bool> cancelFlag) {
    switch (module) {
    case FeatureModule::GrowthTrace:
        ScanGrowthTrace(out, sourcePath, cancelFlag);
        break;
    case FeatureModule::SoftwareFootprint:
        ScanSoftwareFootprint(out, cancelFlag);
        break;
    case FeatureModule::AppMover:
        ScanAppMover(out, targetPath, cancelFlag);
        break;
    case FeatureModule::ArchiveAssistant:
        ScanArchiveAssistant(out, sourcePath, targetPath, cancelFlag);
        break;
    case FeatureModule::DownloadOrganizer:
        ScanDownloadOrganizer(out, sourcePath, cancelFlag);
        break;
    case FeatureModule::PrivacyRadar:
        ScanPrivacyRadar(out, sourcePath, cancelFlag);
        break;
    case FeatureModule::DeveloperSpace:
        ScanDeveloperSpace(out, sourcePath, cancelFlag);
        break;
    case FeatureModule::DockerWsl:
        ScanDockerWsl(out, cancelFlag);
        break;
    case FeatureModule::MediaOrganizer:
        ScanMediaOrganizer(out, sourcePath, cancelFlag);
        break;
    case FeatureModule::QuotaBudget:
        ScanQuotaBudget(out, sourcePath, cancelFlag);
        break;
    case FeatureModule::BackupGap:
        ScanBackupGap(out, sourcePath, targetPath, cancelFlag);
        break;
    case FeatureModule::FileUnlocker:
        ScanFileUnlocker(out, sourcePath, cancelFlag);
        break;
    case FeatureModule::TransferAssistant:
        ScanTransferAssistant(out, sourcePath, targetPath, cancelFlag);
        break;
    case FeatureModule::CloudSync:
        ScanCloudSync(out, cancelFlag);
        break;
    case FeatureModule::RestorePoint:
        ScanRestorePoint(out, cancelFlag);
        break;
    case FeatureModule::BrowserCache:
        ScanBrowserCache(out, cancelFlag);
        break;
    case FeatureModule::StartupFootprint:
        ScanStartupFootprint(out, cancelFlag);
        break;
    case FeatureModule::MessengerCache:
        ScanMessengerCache(out, cancelFlag);
        break;
    case FeatureModule::MailArchive:
        ScanMailArchive(out, sourcePath, cancelFlag);
        break;
    case FeatureModule::VirtualMachineImages:
        ScanVirtualMachineImages(out, sourcePath, cancelFlag);
        break;
    }
}

/**
 * @brief 按模块执行扫描。
 * @param modules 需要执行的模块列表。
 * @param sourcePath 源路径。
 * @param targetPath 目标路径。
 * @return 检测结果。
 */
QVector<FeatureFinding> BuildFindings(const QVector<FeatureModule>& modules, const QString& sourcePath, const QString& targetPath,
                                      std::shared_ptr<std::atomic_bool> cancelFlag) {
    QVector<FeatureFinding> out;
    for (FeatureModule module : modules) {
        if (IsCancelled(cancelFlag)) {
            break;
        }
        // 单模块异常隔离:任一 ScanXxx 抛异常只以"扫描失败"占位替代该模块结果,不冒泡到调用者(后台
        // 工作线程),杜绝 std::terminate;其余模块继续扫描。
        try {
            ScanOneModule(out, module, sourcePath, targetPath, cancelFlag);
        } catch (const std::exception& e) {
            AddFinding(out, module, ModuleTitle(module), QStringLiteral("扫描失败"),
                       QStringLiteral("模块扫描异常,已跳过该模块:") + QString::fromLocal8Bit(e.what()));
        } catch (...) {
            AddFinding(out, module, ModuleTitle(module), QStringLiteral("扫描失败"),
                       QStringLiteral("模块扫描遇到未知异常,已跳过该模块"));
        }
    }
    std::sort(out.begin(), out.end(), [](const FeatureFinding& left, const FeatureFinding& right) {
        if (left.module != right.module) {
            return ModuleToInt(left.module) < ModuleToInt(right.module);
        }
        if (left.bytes != right.bytes) {
            return left.bytes > right.bytes;
        }
        return left.title < right.title;
    });
    return out;
}

}  // namespace

FeatureHubWidget::FeatureHubWidget(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    auto* hero = new QFrame(this);
    hero->setObjectName(QStringLiteral("CleanupHero"));
    auto* heroLayout = new QVBoxLayout(hero);
    heroLayout->setContentsMargins(16, 12, 16, 12);
    heroLayout->setSpacing(8);

    auto* titleLabel = new QLabel(QStringLiteral("空间工具箱"), hero);
    titleLabel->setObjectName(QStringLiteral("CleanupTitle"));
    statusLabel_ = new QLabel(QStringLiteral("新增能力集中在这里：增长溯源、软件体积、应用搬家、隐私雷达、Docker / WSL、备份缺口等。"), hero);
    statusLabel_->setObjectName(QStringLiteral("CleanupStatus"));
    statusLabel_->setWordWrap(true);
    heroLayout->addWidget(titleLabel);
    heroLayout->addWidget(statusLabel_);
    heroLayout->addWidget(CreateToolbar());
    LoadSettings();

    auto* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setObjectName(QStringLiteral("WorkspaceSplitter"));
    splitter->setChildrenCollapsible(false);
    splitter->addWidget(CreateModuleList());
    splitter->addWidget(CreateResultTree());
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes(QList<int>{260, 860});

    layout->addWidget(hero);
    layout->addWidget(splitter, 1);
    LoadWorkflowState();
    LoadResultCache();
    UpdateActionState();
}

QWidget* FeatureHubWidget::CreateToolbar() {
    auto* toolbar = new QWidget(this);
    auto* rootLayout = new QVBoxLayout(toolbar);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(6);

    auto* pathRow = new QWidget(toolbar);
    auto* pathLayout = new QHBoxLayout(pathRow);
    pathLayout->setContentsMargins(0, 0, 0, 0);
    pathLayout->setSpacing(8);

    auto* actionRow = new QWidget(toolbar);
    auto* actionLayout = new QHBoxLayout(actionRow);
    actionLayout->setContentsMargins(0, 0, 0, 0);
    actionLayout->setSpacing(8);

    sourcePathEdit_ = new QLineEdit(toolbar);
    sourcePathEdit_->setPlaceholderText(QStringLiteral("源路径：用于增长、归档、隐私、开发空间、传输等模块"));
    // 源路径默认留空(仅以占位符引导):原默认 Downloads 会误导传输/备份/配额/媒体等模块把"下载"当
    // 成操作对象。留空时各模块回退到自身恰当的标准路径(照片→图片、下载整理→下载等)或显示"等待
    // 输入",不再产生与默认值绑定的错误输出。LoadSettings 仍会恢复用户上次显式选择的路径。

    auto* sourceButton = new QPushButton(QStringLiteral("源路径"), toolbar);
    sourceButton->setToolTip(QStringLiteral("选择源文件或目录"));

    targetPathEdit_ = new QLineEdit(toolbar);
    targetPathEdit_->setPlaceholderText(QStringLiteral("目标路径：用于搬家、归档、传输、备份检查"));

    auto* targetButton = new QPushButton(QStringLiteral("目标"), toolbar);
    targetButton->setToolTip(QStringLiteral("选择目标目录"));

    scanAllButton_ = new QPushButton(QStringLiteral("全部体检"), toolbar);
    scanAllButton_->setObjectName(QStringLiteral("PrimaryButton"));
    scanCurrentButton_ = new QPushButton(QStringLiteral("当前模块"), toolbar);
    cancelButton_ = new QPushButton(QStringLiteral("取消"), toolbar);
    cancelButton_->setEnabled(false);
    openPathButton_ = new QPushButton(QStringLiteral("打开位置"), toolbar);
    copyPathButton_ = new QPushButton(QStringLiteral("复制路径"), toolbar);
    actionPlanButton_ = new QPushButton(QStringLiteral("处理方案"), toolbar);
    completeButton_ = new QPushButton(QStringLiteral("标记处理"), toolbar);
    noteButton_ = new QPushButton(QStringLiteral("备注"), toolbar);
    bulkPlanButton_ = new QPushButton(QStringLiteral("方案包"), toolbar);
    deliveryMenuButton_ = new QToolButton(toolbar);
    deliveryMenuButton_->setText(QStringLiteral("交付"));
    deliveryMenuButton_->setPopupMode(QToolButton::InstantPopup);
    auto* deliveryMenu = new QMenu(deliveryMenuButton_);
    QAction* exportAction = deliveryMenu->addAction(QStringLiteral("导出当前视图"));
    QAction* professionalReportAction = deliveryMenu->addAction(QStringLiteral("专业 HTML 报告"));
    QAction* deliveryPackageAction = deliveryMenu->addAction(QStringLiteral("JSON 交付包"));
    QAction* taskChecklistAction = deliveryMenu->addAction(QStringLiteral("Markdown 任务清单"));
    deliveryMenuButton_->setMenu(deliveryMenu);
    baselineButton_ = new QPushButton(QStringLiteral("保存基线"), toolbar);
    showIgnoredButton_ = new QPushButton(QStringLiteral("显示忽略"), toolbar);
    showIgnoredButton_->setCheckable(true);

    pathLayout->addWidget(sourcePathEdit_, 3);
    pathLayout->addWidget(sourceButton);
    pathLayout->addWidget(targetPathEdit_, 3);
    pathLayout->addWidget(targetButton);
    pathLayout->addWidget(scanAllButton_);
    pathLayout->addWidget(scanCurrentButton_);
    pathLayout->addWidget(cancelButton_);

    actionLayout->addWidget(openPathButton_);
    actionLayout->addWidget(copyPathButton_);
    actionLayout->addWidget(actionPlanButton_);
    actionLayout->addWidget(completeButton_);
    actionLayout->addWidget(noteButton_);
    actionLayout->addWidget(bulkPlanButton_);
    actionLayout->addWidget(deliveryMenuButton_);
    actionLayout->addWidget(baselineButton_);
    actionLayout->addWidget(showIgnoredButton_);
    actionLayout->addStretch(1);

    rootLayout->addWidget(pathRow);
    rootLayout->addWidget(actionRow);

    connect(sourceButton, &QPushButton::clicked, this, &FeatureHubWidget::BrowseSourcePath);
    connect(targetButton, &QPushButton::clicked, this, &FeatureHubWidget::BrowseTargetPath);
    connect(scanAllButton_, &QPushButton::clicked, this, &FeatureHubWidget::RunAllScans);
    connect(scanCurrentButton_, &QPushButton::clicked, this, &FeatureHubWidget::RunCurrentScan);
    connect(cancelButton_, &QPushButton::clicked, this, &FeatureHubWidget::CancelScan);
    connect(openPathButton_, &QPushButton::clicked, this, &FeatureHubWidget::OpenSelectedPath);
    connect(copyPathButton_, &QPushButton::clicked, this, &FeatureHubWidget::CopySelectedPath);
    connect(actionPlanButton_, &QPushButton::clicked, this, &FeatureHubWidget::ShowActionPlan);
    connect(completeButton_, &QPushButton::clicked, this, &FeatureHubWidget::ToggleCompletedFinding);
    connect(noteButton_, &QPushButton::clicked, this, &FeatureHubWidget::EditFindingNote);
    connect(bulkPlanButton_, &QPushButton::clicked, this, &FeatureHubWidget::ShowBulkActionPlan);
    connect(exportAction, &QAction::triggered, this, &FeatureHubWidget::ExportFindings);
    connect(professionalReportAction, &QAction::triggered, this, &FeatureHubWidget::ExportProfessionalReport);
    connect(deliveryPackageAction, &QAction::triggered, this, &FeatureHubWidget::ExportDeliveryPackage);
    connect(taskChecklistAction, &QAction::triggered, this, &FeatureHubWidget::ExportTaskChecklist);
    connect(baselineButton_, &QPushButton::clicked, this, &FeatureHubWidget::SaveCurrentBaseline);
    connect(showIgnoredButton_, &QPushButton::clicked, this, &FeatureHubWidget::ToggleShowIgnored);
    connect(sourcePathEdit_, &QLineEdit::editingFinished, this, &FeatureHubWidget::SaveSettings);
    connect(targetPathEdit_, &QLineEdit::editingFinished, this, &FeatureHubWidget::SaveSettings);

    return toolbar;
}

QWidget* FeatureHubWidget::CreateModuleList() {
    moduleList_ = new QListWidget(this);
    moduleList_->setObjectName(QStringLiteral("DirectoryTree"));
    moduleList_->setUniformItemSizes(true);
    moduleList_->setIconSize(QSize(16, 16));

    auto* allItem = new QListWidgetItem(app_icons::info(16), QStringLiteral("全部能力"), moduleList_);
    allItem->setData(Qt::UserRole, -1);

    for (const ModuleInfo& info : AllModules()) {
        auto* item = new QListWidgetItem(app_icons::folder(16), info.title, moduleList_);
        item->setToolTip(info.description);
        item->setData(Qt::UserRole, ModuleToInt(info.module));
    }
    moduleList_->setCurrentRow(0);
    connect(moduleList_, &QListWidget::currentRowChanged, this, &FeatureHubWidget::RefreshModuleFilter);
    return moduleList_;
}

QWidget* FeatureHubWidget::CreateResultTree() {
    auto* host = new QWidget(this);
    auto* layout = new QVBoxLayout(host);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    auto* metricRow = new QWidget(host);
    auto* metricLayout = new QHBoxLayout(metricRow);
    metricLayout->setContentsMargins(0, 0, 0, 0);
    metricLayout->setSpacing(8);
    resultCountLabel_ = new QLabel(QStringLiteral("结果 0"), metricRow);
    resultBytesLabel_ = new QLabel(QStringLiteral("空间 0 B"), metricRow);
    attentionCountLabel_ = new QLabel(QStringLiteral("需关注 0"), metricRow);
    highRiskCountLabel_ = new QLabel(QStringLiteral("高风险 0"), metricRow);
    ignoredCountLabel_ = new QLabel(QStringLiteral("已忽略 0"), metricRow);
    completedCountLabel_ = new QLabel(QStringLiteral("已处理 0"), metricRow);
    resolvedCountLabel_ = new QLabel(QStringLiteral("已解决 0"), metricRow);
    governanceScoreLabel_ = new QLabel(QStringLiteral("评分 100"), metricRow);
    baselineCountLabel_ = new QLabel(QStringLiteral("基线 0"), metricRow);
    for (QLabel* label : {resultCountLabel_, resultBytesLabel_, attentionCountLabel_, highRiskCountLabel_, ignoredCountLabel_,
                          completedCountLabel_, resolvedCountLabel_, governanceScoreLabel_, baselineCountLabel_}) {
        label->setObjectName(QStringLiteral("ModeBadge"));
        metricLayout->addWidget(label);
    }
    metricLayout->addStretch(1);
    workflowFilterCombo_ = new QComboBox(metricRow);
    workflowFilterCombo_->addItem(QStringLiteral("全部结果"), QStringLiteral("all"));
    workflowFilterCombo_->addItem(QStringLiteral("待处理"), QStringLiteral("pending"));
    workflowFilterCombo_->addItem(QStringLiteral("已处理"), QStringLiteral("completed"));
    workflowFilterCombo_->addItem(QStringLiteral("新增"), QStringLiteral("new"));
    workflowFilterCombo_->addItem(QStringLiteral("高风险"), QStringLiteral("critical"));
    workflowFilterCombo_->addItem(QStringLiteral("已忽略"), QStringLiteral("ignored"));
    workflowFilterCombo_->addItem(QStringLiteral("有备注"), QStringLiteral("noted"));
    workflowFilterCombo_->setMinimumWidth(120);
    metricLayout->addWidget(workflowFilterCombo_);

    resultFilterEdit_ = new QLineEdit(metricRow);
    resultFilterEdit_->setPlaceholderText(QStringLiteral("过滤结果：模块、标题、状态、路径、说明"));
    resultFilterEdit_->setClearButtonEnabled(true);
    resultFilterEdit_->setMinimumWidth(260);
    metricLayout->addWidget(resultFilterEdit_);

    resultTree_ = new QTreeWidget(host);
    resultTree_->setObjectName(QStringLiteral("CleanupTree"));
    resultTree_->setHeaderLabels({
        QStringLiteral("模块 / 项目"),
        QStringLiteral("等级"),
        QStringLiteral("趋势"),
        QStringLiteral("状态"),
        QStringLiteral("大小"),
        QStringLiteral("路径 / 入口"),
        QStringLiteral("说明")
    });
    resultTree_->setAlternatingRowColors(true);
    resultTree_->setRootIsDecorated(true);
    resultTree_->setSelectionMode(QAbstractItemView::SingleSelection);
    resultTree_->setTextElideMode(Qt::ElideMiddle);
    resultTree_->header()->setStretchLastSection(false);
    resultTree_->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    resultTree_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    resultTree_->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    resultTree_->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    resultTree_->header()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    resultTree_->header()->setSectionResizeMode(5, QHeaderView::Stretch);
    resultTree_->header()->setSectionResizeMode(6, QHeaderView::Stretch);
    resultTree_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(resultTree_, &QTreeWidget::itemSelectionChanged, this, &FeatureHubWidget::UpdateActionState);
    connect(resultTree_, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem*, int) {
        OpenSelectedPath();
    });
    connect(resultTree_, &QTreeWidget::customContextMenuRequested, this, &FeatureHubWidget::ShowResultContextMenu);
    connect(resultFilterEdit_, &QLineEdit::textChanged, this, &FeatureHubWidget::RefreshModuleFilter);
    connect(workflowFilterCombo_, &QComboBox::currentIndexChanged, this, [this](int) {
        SaveWorkflowState();
        RefreshModuleFilter();
    });

    layout->addWidget(metricRow);
    layout->addWidget(resultTree_, 1);
    return host;
}

void FeatureHubWidget::BrowseSourcePath() {
    const QString directory = QFileDialog::getExistingDirectory(this, QStringLiteral("选择源目录"), sourcePathEdit_->text());
    if (!directory.isEmpty()) {
        sourcePathEdit_->setText(QDir::toNativeSeparators(directory));
        SaveSettings();
    }
}

void FeatureHubWidget::BrowseTargetPath() {
    const QString directory = QFileDialog::getExistingDirectory(this, QStringLiteral("选择目标目录"), targetPathEdit_->text());
    if (!directory.isEmpty()) {
        targetPathEdit_->setText(QDir::toNativeSeparators(directory));
        SaveSettings();
    }
}

void FeatureHubWidget::LoadSettings() {
    QSettings settings(QStringLiteral("SunnyFan"), QStringLiteral("DiskLens"));
    if (sourcePathEdit_ != nullptr) {
        const QString source = settings.value(QStringLiteral("featureHub/sourcePath")).toString();
        if (!source.isEmpty()) {
            sourcePathEdit_->setText(QDir::toNativeSeparators(source));
        }
    }
    if (targetPathEdit_ != nullptr) {
        const QString target = settings.value(QStringLiteral("featureHub/targetPath")).toString();
        if (!target.isEmpty()) {
            targetPathEdit_->setText(QDir::toNativeSeparators(target));
        }
    }
}

void FeatureHubWidget::SaveSettings() const {
    QSettings settings(QStringLiteral("SunnyFan"), QStringLiteral("DiskLens"));
    if (sourcePathEdit_ != nullptr) {
        settings.setValue(QStringLiteral("featureHub/sourcePath"), sourcePathEdit_->text().trimmed());
    }
    if (targetPathEdit_ != nullptr) {
        settings.setValue(QStringLiteral("featureHub/targetPath"), targetPathEdit_->text().trimmed());
    }
}

void FeatureHubWidget::LoadWorkflowState() {
    QSettings settings(QStringLiteral("SunnyFan"), QStringLiteral("DiskLens"));
    ignoredFindingKeys_.clear();
    completedFindingKeys_.clear();
    baselineFindingKeys_.clear();
    findingNotes_.clear();
    const QStringList ignoredKeys = settings.value(QStringLiteral("featureHub/ignoredKeys")).toStringList();
    for (const QString& key : ignoredKeys) {
        if (!key.trimmed().isEmpty()) {
            ignoredFindingKeys_.insert(key);
        }
    }
    const QStringList completedKeys = settings.value(QStringLiteral("featureHub/completedKeys")).toStringList();
    for (const QString& key : completedKeys) {
        if (!key.trimmed().isEmpty()) {
            completedFindingKeys_.insert(key);
        }
    }
    const QStringList baselineKeys = settings.value(QStringLiteral("featureHub/baselineKeys")).toStringList();
    for (const QString& key : baselineKeys) {
        if (!key.trimmed().isEmpty()) {
            baselineFindingKeys_.insert(key);
        }
    }
    baselineCapturedAt_ = settings.value(QStringLiteral("featureHub/baselineCapturedAt")).toString();
    const int noteCount = settings.beginReadArray(QStringLiteral("featureHub/notes"));
    for (int index = 0; index < noteCount; ++index) {
        settings.setArrayIndex(index);
        const QString key = settings.value(QStringLiteral("key")).toString();
        const QString note = settings.value(QStringLiteral("note")).toString();
        if (!key.trimmed().isEmpty() && !note.trimmed().isEmpty()) {
            findingNotes_.insert(key, note);
        }
    }
    settings.endArray();
    if (showIgnoredButton_ != nullptr) {
        showIgnoredButton_->setChecked(settings.value(QStringLiteral("featureHub/showIgnored"), false).toBool());
    }
    if (workflowFilterCombo_ != nullptr) {
        const QString workflowCode = settings.value(QStringLiteral("featureHub/workflowFilter"), QStringLiteral("all")).toString();
        const int index = workflowFilterCombo_->findData(workflowCode);
        workflowFilterCombo_->setCurrentIndex(index >= 0 ? index : 0);
    }
}

void FeatureHubWidget::SaveWorkflowState() const {
    QSettings settings(QStringLiteral("SunnyFan"), QStringLiteral("DiskLens"));
    QStringList ignoredKeys;
    ignoredKeys.reserve(ignoredFindingKeys_.size());
    for (const QString& key : ignoredFindingKeys_) {
        ignoredKeys << key;
    }
    ignoredKeys.sort(Qt::CaseInsensitive);
    QStringList completedKeys;
    completedKeys.reserve(completedFindingKeys_.size());
    for (const QString& key : completedFindingKeys_) {
        completedKeys << key;
    }
    completedKeys.sort(Qt::CaseInsensitive);
    QStringList baselineKeys;
    baselineKeys.reserve(baselineFindingKeys_.size());
    for (const QString& key : baselineFindingKeys_) {
        baselineKeys << key;
    }
    baselineKeys.sort(Qt::CaseInsensitive);
    settings.setValue(QStringLiteral("featureHub/ignoredKeys"), ignoredKeys);
    settings.setValue(QStringLiteral("featureHub/completedKeys"), completedKeys);
    settings.setValue(QStringLiteral("featureHub/showIgnored"), showIgnoredButton_ != nullptr && showIgnoredButton_->isChecked());
    settings.setValue(QStringLiteral("featureHub/workflowFilter"), CurrentWorkflowFilterCode());
    settings.setValue(QStringLiteral("featureHub/baselineKeys"), baselineKeys);
    settings.setValue(QStringLiteral("featureHub/baselineCapturedAt"), baselineCapturedAt_);
    settings.remove(QStringLiteral("featureHub/notes"));
    settings.beginWriteArray(QStringLiteral("featureHub/notes"), findingNotes_.size());
    int noteIndex = 0;
    for (auto it = findingNotes_.cbegin(); it != findingNotes_.cend(); ++it) {
        settings.setArrayIndex(noteIndex);
        settings.setValue(QStringLiteral("key"), it.key());
        settings.setValue(QStringLiteral("note"), it.value());
        ++noteIndex;
    }
    settings.endArray();
    settings.sync();
}

void FeatureHubWidget::LoadResultCache() {
    QSettings settings(QStringLiteral("SunnyFan"), QStringLiteral("DiskLens"));
    currentBatchId_ = settings.value(QStringLiteral("featureHub/resultCache/batchId")).toString();
    resultCapturedAt_ = settings.value(QStringLiteral("featureHub/resultCache/capturedAt")).toString();
    const int count = settings.beginReadArray(QStringLiteral("featureHub/resultCache/items"));
    QVector<FeatureFinding> cached;
    cached.reserve(std::min(count, kMaxCachedFindings));
    for (int index = 0; index < count && cached.size() < kMaxCachedFindings; ++index) {
        settings.setArrayIndex(index);
        const int moduleValue = settings.value(QStringLiteral("module"), ModuleToInt(FeatureModule::GrowthTrace)).toInt();
        if (!IsKnownModuleValue(moduleValue)) {
            continue;
        }

        FeatureFinding finding;
        finding.module = IntToModule(moduleValue);
        finding.title = settings.value(QStringLiteral("title")).toString();
        finding.state = settings.value(QStringLiteral("state")).toString();
        finding.detail = settings.value(QStringLiteral("detail")).toString();
        finding.path = QDir::toNativeSeparators(settings.value(QStringLiteral("path")).toString());
        finding.bytes = settings.value(QStringLiteral("bytes")).toULongLong();
        if (!finding.title.isEmpty()) {
            cached.push_back(std::move(finding));
        }
    }
    settings.endArray();

    if (cached.isEmpty()) {
        RefreshModuleFilter();
        return;
    }

    findings_ = std::move(cached);
    if (statusLabel_ != nullptr) {
        statusLabel_->setText(QStringLiteral("已恢复上次空间工具箱结果：%1 条。批次：%2。可以继续过滤、导出或重新体检。")
                                  .arg(findings_.size())
                                  .arg(currentBatchId_.isEmpty() ? QStringLiteral("未记录") : currentBatchId_));
    }
    RefreshModuleFilter();
}

void FeatureHubWidget::SaveResultCache() const {
    QSettings settings(QStringLiteral("SunnyFan"), QStringLiteral("DiskLens"));
    settings.remove(QStringLiteral("featureHub/resultCache"));
    settings.setValue(QStringLiteral("featureHub/resultCache/batchId"), currentBatchId_);
    settings.setValue(QStringLiteral("featureHub/resultCache/capturedAt"), resultCapturedAt_);
    const int count = static_cast<int>(std::min(findings_.size(), static_cast<qsizetype>(kMaxCachedFindings)));
    settings.beginWriteArray(QStringLiteral("featureHub/resultCache/items"), count);
    for (int index = 0; index < count; ++index) {
        const FeatureFinding& finding = findings_.at(index);
        settings.setArrayIndex(index);
        settings.setValue(QStringLiteral("module"), ModuleToInt(finding.module));
        settings.setValue(QStringLiteral("title"), finding.title);
        settings.setValue(QStringLiteral("state"), finding.state);
        settings.setValue(QStringLiteral("detail"), finding.detail);
        settings.setValue(QStringLiteral("path"), finding.path);
        settings.setValue(QStringLiteral("bytes"), static_cast<qulonglong>(finding.bytes));
    }
    settings.endArray();
    settings.sync();
}

void FeatureHubWidget::RunAllScans() {
    if (scanning_.load() || quitting_.load()) {  // 退出已发起则拒绝新扫描(深度防御)。
        return;
    }
    QVector<FeatureModule> modules;
    for (const ModuleInfo& info : AllModules()) {
        modules.push_back(info.module);
    }
    const std::uint64_t requestId = ++requestId_;
    auto cancelFlag = std::make_shared<std::atomic_bool>(false);
    cancelFlag_ = cancelFlag;
    const QString sourcePath = sourcePathEdit_ != nullptr ? sourcePathEdit_->text().trimmed() : QString();
    const QString targetPath = targetPathEdit_ != nullptr ? targetPathEdit_->text().trimmed() : QString();
    SaveSettings();
    SetBusy(true, QStringLiteral("正在执行全部新增能力体检"));
    if (scanWorker_.joinable()) {
        scanWorker_.join();  // scanning_ 守卫已保证进入时上一轮已完成,此处双保险。
    }
    // 每轮新建堆上完成标志(shared_ptr 管理):worker 拷贝持有、退出时置真。与 widget 生命周期解耦,
    // 即便 detach 降级后 worker 晚于析构退出,写堆 atomic 也不触碰 this,杜绝悬垂成员访问与复位竞态。
    auto finished = std::make_shared<std::atomic_bool>(false);
    workerFinishedFlag_ = finished;
    // 工作线程不再 detach 也不捕获裸 this:由 scanWorker_ 成员持有,退出时 RequestShutdownForQuit/
    // 析构经 JoinWorkerBounded 回收;回投经 QPointer 守卫 + quitting_ 标志,杜绝"扫描中关窗→回投悬垂 this"的 UAF。
    try {
        scanWorker_ = std::thread([self = QPointer<FeatureHubWidget>(this), modules, sourcePath, targetPath, requestId, cancelFlag, finished]() {
            // 工作线程终极兜底:BuildFindings 内部已对每个 ScanXxx 单独 try/catch 并产出"扫描失败"行,
            // 此处再包一层捕获 BuildFindings 其余极罕见异常(如排序比较器抛出),杜绝异常冒泡出线程函数→
            // std::terminate 整个进程。捕获后尽力回投复位 UI(本线程执行期间 scanning_ 恒为真,无更新的
            // 体检并发,复位安全),避免扫描卡在"进行中"。finished 置真位于内层 try/catch 之外,保证关窗
            // join 及时返回。
            try {
                QVector<FeatureFinding> findings = BuildFindings(modules, sourcePath, targetPath, cancelFlag);
                const bool cancelled = IsCancelled(cancelFlag);
                FeatureHubWidget* const target = self.data();
                if (target != nullptr) {
                    QMetaObject::invokeMethod(target, [self, findings = std::move(findings), requestId, cancelled]() mutable {
                        if (self.isNull() || self->quitting_.load()) {
                            return;
                        }
                        self->ReplaceFindings(std::move(findings), requestId, cancelled);
                    }, Qt::QueuedConnection);
                }
            } catch (...) {
                // 仅置忙碌假 + 诚实状态文案(不伪造"完成"),self/quitting 守卫防悬垂回投。
                // 回投 invokeMethod 本身再包一层 try/catch:即便回投构造(OOM 等)抛异常也吞掉,杜绝异常
                // 二次逃逸出线程函数(std::terminate)。终极目标是线程函数绝不抛。
                FeatureHubWidget* const target = self.data();
                if (target != nullptr) {
                    try {
                        QMetaObject::invokeMethod(target, [self]() {
                            if (self.isNull() || self->quitting_.load()) {
                                return;
                            }
                            self->SetBusy(false, QStringLiteral("体检遇到意外错误,请重试;若持续出现请检查源路径权限。"));
                        }, Qt::QueuedConnection);
                    } catch (...) {
                        // 回投失败已尽力,吞掉保线程不抛。
                    }
                }
            }
            // 标记完成:写堆上 shared atomic(非 this 成员),与 widget 析构彻底解耦,杜绝悬垂访问。
            finished->store(true);
        });
    } catch (const std::exception&) {
        // 线程创建/lambda 捕获失败(system_error 资源耗尽、bad_alloc 拷贝捕获等极罕见):复位忙碌状态,
        // 避免异常逃逸出 Qt 槽或 UI 卡在"扫描中";下一轮体检可重试。捕 std::exception 兼顾两类。
        workerFinishedFlag_.reset();
        SetBusy(false, QStringLiteral("无法启动后台体检:线程创建失败,请关闭多余程序后重试。"));
    }
}

void FeatureHubWidget::RunCurrentScan() {
    if (scanning_.load() || quitting_.load()) {  // 退出已发起则拒绝新扫描(深度防御)。
        return;
    }
    bool hasModule = false;
    const FeatureModule module = CurrentModule(hasModule);
    if (!hasModule) {
        RunAllScans();
        return;
    }
    const std::uint64_t requestId = ++requestId_;
    auto cancelFlag = std::make_shared<std::atomic_bool>(false);
    cancelFlag_ = cancelFlag;
    const QString sourcePath = sourcePathEdit_ != nullptr ? sourcePathEdit_->text().trimmed() : QString();
    const QString targetPath = targetPathEdit_ != nullptr ? targetPathEdit_->text().trimmed() : QString();
    SaveSettings();
    SetBusy(true, QStringLiteral("正在体检：%1").arg(ModuleTitle(module)));
    if (scanWorker_.joinable()) {
        scanWorker_.join();  // scanning_ 守卫已保证进入时上一轮已完成,此处双保险。
    }
    // 每轮新建堆上完成标志(shared_ptr 管理):worker 拷贝持有、退出时置真,与 widget 析构彻底解耦。
    auto finished = std::make_shared<std::atomic_bool>(false);
    workerFinishedFlag_ = finished;
    try {
        scanWorker_ = std::thread([self = QPointer<FeatureHubWidget>(this), module, sourcePath, targetPath, requestId, cancelFlag, finished]() {
            // 工作线程终极兜底:BuildFindings 内部已对每个 ScanXxx 单独 try/catch 并产出"扫描失败"行,
            // 此处再包一层捕获 BuildFindings 其余极罕见异常,杜绝异常冒泡出线程函数→std::terminate 整个
            // 进程;捕获后尽力回投复位 UI,避免扫描卡在"进行中"。finished 置真位于内层 try/catch 之外。
            try {
                QVector<FeatureFinding> findings = BuildFindings(QVector<FeatureModule>{module}, sourcePath, targetPath, cancelFlag);
                const bool cancelled = IsCancelled(cancelFlag);
                FeatureHubWidget* const target = self.data();
                if (target != nullptr) {
                    QMetaObject::invokeMethod(target, [self, findings = std::move(findings), requestId, cancelled]() mutable {
                        if (self.isNull() || self->quitting_.load()) {
                            return;
                        }
                        self->ReplaceFindings(std::move(findings), requestId, cancelled);
                    }, Qt::QueuedConnection);
                }
            } catch (...) {
                // 仅置忙碌假 + 诚实状态文案(不伪造"完成"),self/quitting 守卫防悬垂回投。
                // 回投 invokeMethod 本身再包一层 try/catch:即便回投构造(OOM 等)抛异常也吞掉,杜绝异常
                // 二次逃逸出线程函数(std::terminate)。终极目标是线程函数绝不抛。
                FeatureHubWidget* const target = self.data();
                if (target != nullptr) {
                    try {
                        QMetaObject::invokeMethod(target, [self]() {
                            if (self.isNull() || self->quitting_.load()) {
                                return;
                            }
                            self->SetBusy(false, QStringLiteral("体检遇到意外错误,请重试;若持续出现请检查源路径权限。"));
                        }, Qt::QueuedConnection);
                    } catch (...) {
                        // 回投失败已尽力,吞掉保线程不抛。
                    }
                }
            }
            // 标记完成:写堆上 shared atomic(非 this 成员),与 widget 析构彻底解耦,杜绝悬垂访问。
            finished->store(true);
        });
    } catch (const std::exception&) {
        // 线程创建/lambda 捕获失败(system_error 资源耗尽、bad_alloc 拷贝捕获等极罕见):复位忙碌状态,
        // 避免异常逃逸出 Qt 槽或 UI 卡在"扫描中";下一轮体检可重试。捕 std::exception 兼顾两类。
        workerFinishedFlag_.reset();
        SetBusy(false, QStringLiteral("无法启动后台体检:线程创建失败,请关闭多余程序后重试。"));
    }
}

void FeatureHubWidget::CancelScan() {
    if (!scanning_.load()) {
        return;
    }
    if (cancelFlag_ != nullptr) {
        cancelFlag_->store(true);
    }
    if (statusLabel_ != nullptr) {
        statusLabel_->setText(QStringLiteral("正在取消工具箱体检，已发现结果会保留为部分结果..."));
    }
    if (cancelButton_ != nullptr) {
        cancelButton_->setEnabled(false);
    }
}

void FeatureHubWidget::JoinWorkerBounded() {
    // 镜像 MainWindow health worker 的 wait+detach 降级:轮询完成标志最多约 2 秒,worker 已结束则即时
    // join 回收;超时(极慢盘 / Restart Manager / 未来的 QProcess 阻塞调用)则 detach 解除阻塞,退出靠
    // quitting_ 守卫 + 回投前 QPointer 判空兜底,杜绝「关窗卡死」。预算取 2s(而非 health 的 5s):工具箱
    // cancel 粒度更细(每模块间 / 每目录条目),与下方 health worker wait(5000) 串行后总关窗最坏 ~7s,不致假死。
    constexpr int kPollRounds = 20;   // 20 × 100ms = 2s。
    // 快照当前在途标志:关闭/析构路径不会启动新 worker,这里持引用防极少数竞态下被新一轮覆盖。
    const std::shared_ptr<std::atomic_bool> finished = workerFinishedFlag_;
    for (int i = 0; i < kPollRounds; ++i) {
        if (finished == nullptr || finished->load()) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    if (finished != nullptr && finished->load() && scanWorker_.joinable()) {
        scanWorker_.join();  // worker 已结束,join 即时返回。
    } else if (scanWorker_.joinable()) {
        scanWorker_.detach();  // 仍未结束:解除阻塞,worker 自行退出;quitting_/QPointer 守卫保证安全。
    }
}

void FeatureHubWidget::RequestShutdownForQuit() {
    // 镜像 MainWindow health worker 的 quitting_ 守卫模式:先置退出标志让回投 lambda 跳过对 this
    // 的访问,再取消让 BuildFindings 在各 ScanXxx 的 IsCancelled 检查点快速退出,最后有界 join 回收
    // std::thread。cancelFlag 的细粒度轮询保证 worker 秒级退出,正常情况 join 即时返回。
    quitting_.store(true);
    if (cancelFlag_ != nullptr) {
        cancelFlag_->store(true);
    }
    JoinWorkerBounded();
}

FeatureHubWidget::~FeatureHubWidget() {
    // 析构路径同样收尾:置退出 + 取消 + 有界 join,确保 widget 销毁时无后台线程回投悬垂 this。
    quitting_.store(true);
    if (cancelFlag_ != nullptr) {
        cancelFlag_->store(true);
    }
    JoinWorkerBounded();
}

void FeatureHubWidget::RefreshModuleFilter() {
    if (resultTree_ == nullptr) {
        return;
    }
    resultTree_->clear();

    const QVector<FeatureFinding> visibleFindings = VisibleFindings();
    bool hasModule = false;
    const FeatureModule currentModule = CurrentModule(hasModule);
    const QString filterText = resultFilterEdit_ != nullptr ? resultFilterEdit_->text().trimmed().toCaseFolded() : QString();
    std::map<FeatureModule, QTreeWidgetItem*> groupItems;
    std::uint64_t visibleBytes = 0;
    int visibleCount = 0;
    int attentionCount = 0;
    int highRiskCount = 0;
    int ignoredMatchingCount = 0;
    int completedMatchingCount = 0;

    for (const FeatureFinding& finding : findings_) {
        if (IsIgnoredFinding(finding) && MatchesVisibleFilter(finding, hasModule, currentModule, filterText)) {
            ++ignoredMatchingCount;
        }
        if (IsCompletedFinding(finding) && MatchesVisibleFilter(finding, hasModule, currentModule, filterText)) {
            ++completedMatchingCount;
        }
    }

    for (const FeatureFinding& finding : visibleFindings) {
        const FindingSeverity severity = SeverityForFinding(finding);
        const bool ignored = IsIgnoredFinding(finding);
        const bool completed = IsCompletedFinding(finding);
        QTreeWidgetItem* groupItem = nullptr;
        const auto groupIt = groupItems.find(finding.module);
        if (groupIt == groupItems.end()) {
            groupItem = new QTreeWidgetItem(resultTree_);
            groupItem->setIcon(0, app_icons::folder(16));
            groupItem->setText(0, ModuleTitle(finding.module));
            groupItem->setText(1, QStringLiteral("-"));
            groupItem->setText(2, baselineFindingKeys_.isEmpty() ? QStringLiteral("未建基线") : QStringLiteral("模块"));
            groupItem->setText(3, QStringLiteral("模块"));
            groupItem->setText(4, QStringLiteral("-"));
            groupItem->setText(5, QString());
            groupItem->setText(6, QString());
            QFont font = groupItem->font(0);
            font.setBold(true);
            groupItem->setFont(0, font);
            groupItems[finding.module] = groupItem;
        } else {
            groupItem = groupIt->second;
        }

        auto* item = new QTreeWidgetItem(groupItem);
        item->setIcon(0, app_icons::fileGlyph(16));
        item->setText(0, finding.title);
        item->setText(1, SeverityTitle(severity));
        item->setText(2, TrendTitleForFinding(finding));
        QString workflowState = finding.state;
        if (completed) {
            workflowState = QStringLiteral("已处理 / %1").arg(workflowState);
        }
        if (ignored) {
            workflowState = QStringLiteral("已忽略 / %1").arg(workflowState);
        }
        item->setText(3, workflowState);
        item->setText(4, finding.bytes > 0 ? FormatBytesText(finding.bytes) : QStringLiteral("-"));
        item->setText(5, finding.path);
        const QString note = FindingNote(finding);
        item->setText(6, note.isEmpty() ? finding.detail : QStringLiteral("%1｜备注：%2").arg(finding.detail, note));
        item->setData(0, kRolePath, finding.path);
        item->setData(0, kRoleModule, ModuleToInt(finding.module));
        item->setData(0, kRoleTitle, finding.title);
        item->setData(0, kRoleState, finding.state);
        item->setData(0, kRoleDetail, finding.detail);
        item->setData(0, kRoleBytes, static_cast<qulonglong>(finding.bytes));
        item->setToolTip(0, finding.title);
        item->setToolTip(1, SeverityTitle(severity));
        item->setToolTip(2, TrendTitleForFinding(finding));
        item->setToolTip(5, finding.path);
        item->setToolTip(6, note.isEmpty() ? finding.detail : QStringLiteral("%1\n备注：%2").arg(finding.detail, note));

        visibleBytes += finding.bytes;
        ++visibleCount;
        if (!completed && IsAttentionFinding(finding)) {
            ++attentionCount;
        }
        if (!completed && SeverityRank(severity) >= SeverityRank(FindingSeverity::Critical)) {
            ++highRiskCount;
        }
    }

    if (visibleCount == 0) {
        auto* emptyItem = new QTreeWidgetItem(resultTree_);
        emptyItem->setIcon(0, app_icons::info(16));
        emptyItem->setText(0, scanning_.load() ? QStringLiteral("正在体检") : QStringLiteral("暂无结果"));
        emptyItem->setText(1, QStringLiteral("-"));
        emptyItem->setText(2, QStringLiteral("-"));
        emptyItem->setText(3, QStringLiteral("-"));
        emptyItem->setText(4, QStringLiteral("-"));
        emptyItem->setText(5, QString());
        emptyItem->setText(6, scanning_.load() ? QStringLiteral("后台扫描完成后会自动刷新。") : QStringLiteral("点击“全部体检”或“当前模块”开始。"));
    }
    resultTree_->expandToDepth(1);

    if (resultCountLabel_ != nullptr) {
        resultCountLabel_->setText(QStringLiteral("结果 %1").arg(visibleCount));
    }
    if (resultBytesLabel_ != nullptr) {
        resultBytesLabel_->setText(QStringLiteral("空间 %1").arg(FormatBytesText(visibleBytes)));
    }
    if (attentionCountLabel_ != nullptr) {
        attentionCountLabel_->setText(QStringLiteral("需关注 %1").arg(attentionCount));
    }
    if (highRiskCountLabel_ != nullptr) {
        highRiskCountLabel_->setText(QStringLiteral("高风险 %1").arg(highRiskCount));
    }
    if (ignoredCountLabel_ != nullptr) {
        ignoredCountLabel_->setText(QStringLiteral("已忽略 %1").arg(ignoredMatchingCount));
    }
    if (completedCountLabel_ != nullptr) {
        completedCountLabel_->setText(QStringLiteral("已处理 %1").arg(completedMatchingCount));
    }
    if (resolvedCountLabel_ != nullptr) {
        resolvedCountLabel_->setText(QStringLiteral("已解决 %1").arg(ResolvedBaselineCount(findings_)));
    }
    if (governanceScoreLabel_ != nullptr) {
        governanceScoreLabel_->setText(QStringLiteral("评分 %1").arg(GovernanceScore(visibleFindings)));
    }
    if (baselineCountLabel_ != nullptr) {
        baselineCountLabel_->setText(QStringLiteral("基线 %1").arg(baselineFindingKeys_.size()));
    }
    UpdateActionState();
}

void FeatureHubWidget::OpenSelectedPath() {
    if (resultTree_ == nullptr) {
        return;
    }
    QTreeWidgetItem* item = resultTree_->currentItem();
    if (item == nullptr) {
        return;
    }
    QString path = item->data(0, kRolePath).toString();
    if (path.isEmpty()) {
        path = item->text(5);
    }
    if (!PathExists(path)) {
        QMessageBox::information(this, QStringLiteral("打开位置"), QStringLiteral("当前结果没有可打开的本地路径。"));
        return;
    }
    const QFileInfo info(path);
    const QUrl url = QUrl::fromLocalFile(info.isDir() ? info.absoluteFilePath() : info.absolutePath());
    QDesktopServices::openUrl(url);
}

void FeatureHubWidget::CopySelectedPath() {
    bool ok = false;
    const FeatureFinding finding = CurrentFinding(ok);
    if (!ok || finding.path.isEmpty()) {
        return;
    }
    if (QApplication::clipboard() != nullptr) {
        QApplication::clipboard()->setText(QDir::toNativeSeparators(finding.path));
    }
    if (statusLabel_ != nullptr) {
        statusLabel_->setText(QStringLiteral("已复制路径：%1").arg(QDir::toNativeSeparators(finding.path)));
    }
}

void FeatureHubWidget::CopySelectedRow() {
    bool ok = false;
    const FeatureFinding finding = CurrentFinding(ok);
    if (!ok) {
        return;
    }
    if (QApplication::clipboard() != nullptr) {
        QApplication::clipboard()->setText(FindingClipboardText(finding));
    }
    if (statusLabel_ != nullptr) {
        statusLabel_->setText(QStringLiteral("已复制结果：%1").arg(finding.title));
    }
}

void FeatureHubWidget::ShowResultContextMenu(const QPoint& position) {
    if (resultTree_ == nullptr) {
        return;
    }

    QTreeWidgetItem* item = resultTree_->itemAt(position);
    if (item != nullptr) {
        resultTree_->setCurrentItem(item);
    }

    bool ok = false;
    const FeatureFinding finding = CurrentFinding(ok);
    const bool hasVisibleRows = !VisibleFindings().isEmpty();

    QMenu menu(this);
    QAction* openAction = menu.addAction(QStringLiteral("打开位置"));
    QAction* copyPathAction = menu.addAction(QStringLiteral("复制路径"));
    QAction* copyRowAction = menu.addAction(QStringLiteral("复制结果信息"));
    QAction* planAction = menu.addAction(QStringLiteral("处理方案"));
    QAction* completeAction = menu.addAction(ok && IsCompletedFinding(finding)
        ? QStringLiteral("标记为未处理")
        : QStringLiteral("标记为已处理"));
    QAction* noteAction = menu.addAction(QStringLiteral("编辑处置备注"));
    QAction* ignoreAction = menu.addAction(ok && IsIgnoredFinding(finding)
        ? QStringLiteral("取消忽略")
        : QStringLiteral("忽略此项"));
    menu.addSeparator();
    QAction* bulkPlanAction = menu.addAction(QStringLiteral("当前视图方案包"));
    QAction* exportAction = menu.addAction(QStringLiteral("导出当前视图"));
    QAction* reportAction = menu.addAction(QStringLiteral("导出专业报告"));
    QAction* packageAction = menu.addAction(QStringLiteral("导出 JSON 交付包"));
    QAction* checklistAction = menu.addAction(QStringLiteral("导出处置任务清单"));
    QAction* baselineAction = menu.addAction(QStringLiteral("保存当前结果为基线"));

    openAction->setEnabled(ok && PathExists(finding.path));
    copyPathAction->setEnabled(ok && !finding.path.isEmpty());
    copyRowAction->setEnabled(ok);
    planAction->setEnabled(ok);
    completeAction->setEnabled(ok);
    noteAction->setEnabled(ok);
    ignoreAction->setEnabled(ok);
    bulkPlanAction->setEnabled(hasVisibleRows);
    exportAction->setEnabled(hasVisibleRows);
    reportAction->setEnabled(hasVisibleRows);
    packageAction->setEnabled(hasVisibleRows);
    checklistAction->setEnabled(hasVisibleRows);
    baselineAction->setEnabled(!findings_.isEmpty());

    QAction* selectedAction = menu.exec(resultTree_->viewport()->mapToGlobal(position));
    if (selectedAction == openAction) {
        OpenSelectedPath();
    } else if (selectedAction == copyPathAction) {
        CopySelectedPath();
    } else if (selectedAction == copyRowAction) {
        CopySelectedRow();
    } else if (selectedAction == planAction) {
        ShowActionPlan();
    } else if (selectedAction == completeAction) {
        ToggleCompletedFinding();
    } else if (selectedAction == noteAction) {
        EditFindingNote();
    } else if (selectedAction == ignoreAction) {
        ToggleIgnoredFinding();
    } else if (selectedAction == bulkPlanAction) {
        ShowBulkActionPlan();
    } else if (selectedAction == exportAction) {
        ExportFindings();
    } else if (selectedAction == reportAction) {
        ExportProfessionalReport();
    } else if (selectedAction == packageAction) {
        ExportDeliveryPackage();
    } else if (selectedAction == checklistAction) {
        ExportTaskChecklist();
    } else if (selectedAction == baselineAction) {
        SaveCurrentBaseline();
    }
}

void FeatureHubWidget::ToggleIgnoredFinding() {
    bool ok = false;
    const FeatureFinding finding = CurrentFinding(ok);
    if (!ok) {
        return;
    }

    const QString key = FindingStableKey(finding);
    const bool ignored = ignoredFindingKeys_.contains(key);
    if (ignored) {
        ignoredFindingKeys_.remove(key);
    } else {
        ignoredFindingKeys_.insert(key);
    }
    SaveWorkflowState();
    if (statusLabel_ != nullptr) {
        statusLabel_->setText(ignored
            ? QStringLiteral("已恢复结果：%1").arg(finding.title)
            : QStringLiteral("已忽略结果：%1。可通过“显示忽略”复核。").arg(finding.title));
    }
    RefreshModuleFilter();
}

void FeatureHubWidget::ToggleCompletedFinding() {
    bool ok = false;
    const FeatureFinding finding = CurrentFinding(ok);
    if (!ok) {
        return;
    }

    const QString key = FindingStableKey(finding);
    const bool completed = completedFindingKeys_.contains(key);
    if (completed) {
        completedFindingKeys_.remove(key);
    } else {
        completedFindingKeys_.insert(key);
    }
    SaveWorkflowState();
    if (statusLabel_ != nullptr) {
        statusLabel_->setText(completed
            ? QStringLiteral("已恢复为待处理：%1").arg(finding.title)
            : QStringLiteral("已标记为已处理：%1").arg(finding.title));
    }
    RefreshModuleFilter();
}

void FeatureHubWidget::EditFindingNote() {
    bool ok = false;
    const FeatureFinding finding = CurrentFinding(ok);
    if (!ok) {
        QMessageBox::information(this, QStringLiteral("处置备注"), QStringLiteral("请先选择一条工具箱结果。"));
        return;
    }

    const QString key = FindingStableKey(finding);
    bool accepted = false;
    const QString text = QInputDialog::getMultiLineText(this, QStringLiteral("处置备注 · %1").arg(finding.title),
                                                        QStringLiteral("备注内容："), findingNotes_.value(key), &accepted);
    if (!accepted) {
        return;
    }

    const QString note = text.trimmed();
    if (note.isEmpty()) {
        findingNotes_.remove(key);
    } else {
        findingNotes_.insert(key, note);
    }
    SaveWorkflowState();
    if (statusLabel_ != nullptr) {
        statusLabel_->setText(note.isEmpty()
            ? QStringLiteral("已清除备注：%1").arg(finding.title)
            : QStringLiteral("已保存备注：%1").arg(finding.title));
    }
    RefreshModuleFilter();
}

void FeatureHubWidget::ToggleShowIgnored() {
    SaveWorkflowState();
    RefreshModuleFilter();
}

void FeatureHubWidget::SaveCurrentBaseline() {
    if (findings_.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("保存基线"), QStringLiteral("当前没有可保存为基线的工具箱结果。"));
        return;
    }

    baselineFindingKeys_.clear();
    for (const FeatureFinding& finding : findings_) {
        baselineFindingKeys_.insert(FindingStableKey(finding));
    }
    baselineCapturedAt_ = QDateTime::currentDateTime().toString(Qt::ISODate);
    SaveWorkflowState();
    if (statusLabel_ != nullptr) {
        statusLabel_->setText(QStringLiteral("已保存基线：%1 条结果。后续扫描将标记新增 / 持续。").arg(baselineFindingKeys_.size()));
    }
    RefreshModuleFilter();
}

void FeatureHubWidget::ShowActionPlan() {
    bool ok = false;
    const FeatureFinding finding = CurrentFinding(ok);
    if (!ok) {
        QMessageBox::information(this, QStringLiteral("处理方案"), QStringLiteral("请先选择一条工具箱结果。"));
        return;
    }

    const QString planText = BuildActionPlanText(finding);
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("处理方案 · %1").arg(finding.title));
    dialog.resize(760, 560);

    auto* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(16, 14, 16, 14);
    layout->setSpacing(10);

    auto* textEdit = new QTextEdit(&dialog);
    textEdit->setReadOnly(true);
    textEdit->setPlainText(planText);
    textEdit->setLineWrapMode(QTextEdit::WidgetWidth);
    layout->addWidget(textEdit, 1);

    auto* buttons = new QDialogButtonBox(&dialog);
    QPushButton* copyButton = buttons->addButton(QStringLiteral("复制方案"), QDialogButtonBox::ActionRole);
    QPushButton* closeButton = buttons->addButton(QStringLiteral("关闭"), QDialogButtonBox::RejectRole);
    closeButton->setDefault(true);
    connect(copyButton, &QPushButton::clicked, &dialog, [planText]() {
        if (QApplication::clipboard() != nullptr) {
            QApplication::clipboard()->setText(planText);
        }
    });
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);

    dialog.exec();
}

void FeatureHubWidget::ShowBulkActionPlan() {
    const QVector<FeatureFinding> visibleFindings = VisibleFindings();
    if (visibleFindings.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("方案包"), QStringLiteral("当前视图没有可生成方案的结果。"));
        return;
    }

    const QString planText = BuildBulkActionPlanText(visibleFindings);
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("当前视图方案包"));
    dialog.resize(820, 620);

    auto* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(16, 14, 16, 14);
    layout->setSpacing(10);

    auto* textEdit = new QTextEdit(&dialog);
    textEdit->setReadOnly(true);
    textEdit->setPlainText(planText);
    textEdit->setLineWrapMode(QTextEdit::WidgetWidth);
    layout->addWidget(textEdit, 1);

    auto* buttons = new QDialogButtonBox(&dialog);
    QPushButton* copyButton = buttons->addButton(QStringLiteral("复制方案包"), QDialogButtonBox::ActionRole);
    QPushButton* closeButton = buttons->addButton(QStringLiteral("关闭"), QDialogButtonBox::RejectRole);
    closeButton->setDefault(true);
    connect(copyButton, &QPushButton::clicked, &dialog, [planText]() {
        if (QApplication::clipboard() != nullptr) {
            QApplication::clipboard()->setText(planText);
        }
    });
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);

    dialog.exec();
}

FeatureFinding FeatureHubWidget::CurrentFinding(bool& ok) const {
    ok = false;
    if (resultTree_ == nullptr || resultTree_->currentItem() == nullptr) {
        return {};
    }

    QTreeWidgetItem* item = resultTree_->currentItem();
    if (!item->data(0, kRoleTitle).isValid()) {
        return {};
    }

    FeatureFinding finding;
    finding.module = IntToModule(item->data(0, kRoleModule).toInt());
    finding.title = item->data(0, kRoleTitle).toString();
    finding.state = item->data(0, kRoleState).toString();
    finding.detail = item->data(0, kRoleDetail).toString();
    finding.path = item->data(0, kRolePath).toString();
    finding.bytes = item->data(0, kRoleBytes).toULongLong();
    ok = true;
    return finding;
}

QVector<FeatureFinding> FeatureHubWidget::VisibleFindings() const {
    bool hasModule = false;
    const FeatureModule currentModule = CurrentModule(hasModule);
    const QString filterText = resultFilterEdit_ != nullptr ? resultFilterEdit_->text().trimmed().toCaseFolded() : QString();
    const bool showIgnored = showIgnoredButton_ != nullptr && showIgnoredButton_->isChecked();
    const bool wantsIgnored = CurrentWorkflowFilterCode() == QStringLiteral("ignored");

    QVector<FeatureFinding> visible;
    visible.reserve(findings_.size());
    for (const FeatureFinding& finding : findings_) {
        if (!showIgnored && !wantsIgnored && IsIgnoredFinding(finding)) {
            continue;
        }
        if (!MatchesWorkflowFilter(finding)) {
            continue;
        }
        const bool matchesText = MatchesVisibleFilter(finding, hasModule, currentModule, filterText) ||
                                 (!filterText.isEmpty() && FindingNote(finding).toCaseFolded().contains(filterText));
        if (matchesText) {
            visible.push_back(finding);
        }
    }
    return visible;
}

bool FeatureHubWidget::IsIgnoredFinding(const FeatureFinding& finding) const {
    return ignoredFindingKeys_.contains(FindingStableKey(finding));
}

bool FeatureHubWidget::IsCompletedFinding(const FeatureFinding& finding) const {
    return completedFindingKeys_.contains(FindingStableKey(finding));
}

QString FeatureHubWidget::FindingNote(const FeatureFinding& finding) const {
    return findingNotes_.value(FindingStableKey(finding)).trimmed();
}

QString FeatureHubWidget::CurrentWorkflowFilterCode() const {
    if (workflowFilterCombo_ == nullptr) {
        return QStringLiteral("all");
    }
    const QString code = workflowFilterCombo_->currentData().toString();
    return code.isEmpty() ? QStringLiteral("all") : code;
}

bool FeatureHubWidget::MatchesWorkflowFilter(const FeatureFinding& finding) const {
    const QString code = CurrentWorkflowFilterCode();
    if (code == QStringLiteral("pending")) {
        return !IsCompletedFinding(finding) && !IsIgnoredFinding(finding);
    }
    if (code == QStringLiteral("completed")) {
        return IsCompletedFinding(finding);
    }
    if (code == QStringLiteral("new")) {
        return TrendTitleForFinding(finding) == QStringLiteral("新增");
    }
    if (code == QStringLiteral("critical")) {
        return SeverityForFinding(finding) == FindingSeverity::Critical;
    }
    if (code == QStringLiteral("ignored")) {
        return IsIgnoredFinding(finding);
    }
    if (code == QStringLiteral("noted")) {
        return !FindingNote(finding).isEmpty();
    }
    return true;
}

QString FeatureHubWidget::TrendTitleForFinding(const FeatureFinding& finding) const {
    if (baselineFindingKeys_.isEmpty()) {
        return QStringLiteral("未建基线");
    }
    return baselineFindingKeys_.contains(FindingStableKey(finding))
        ? QStringLiteral("持续")
        : QStringLiteral("新增");
}

int FeatureHubWidget::GovernanceScore(const QVector<FeatureFinding>& findings) const {
    int score = 100;
    for (const FeatureFinding& finding : findings) {
        if (IsCompletedFinding(finding)) {
            continue;
        }
        if (IsIgnoredFinding(finding)) {
            score -= 1;
            continue;
        }
        const FindingSeverity severity = SeverityForFinding(finding);
        if (severity == FindingSeverity::Critical) {
            score -= 8;
        } else if (severity == FindingSeverity::Warning) {
            score -= 4;
        } else if (severity == FindingSeverity::Notice) {
            score -= 1;
        }
        if (TrendTitleForFinding(finding) == QStringLiteral("新增") &&
            SeverityRank(severity) >= SeverityRank(FindingSeverity::Warning)) {
            score -= 2;
        }
    }
    return std::clamp(score, 0, 100);
}

int FeatureHubWidget::ResolvedBaselineCount(const QVector<FeatureFinding>& findings) const {
    if (baselineFindingKeys_.isEmpty()) {
        return 0;
    }
    QSet<QString> currentKeys;
    currentKeys.reserve(findings.size());
    for (const FeatureFinding& finding : findings) {
        currentKeys.insert(FindingStableKey(finding));
    }

    int resolved = 0;
    for (const QString& key : baselineFindingKeys_) {
        if (!currentKeys.contains(key)) {
            ++resolved;
        }
    }
    return resolved;
}

QString FeatureHubWidget::BuildActionPlanText(const FeatureFinding& finding) const {
    const QString path = finding.path;
    const QString nativePath = QDir::toNativeSeparators(path);
    const QString targetRoot = targetPathEdit_ != nullptr ? targetPathEdit_->text().trimmed() : QString();
    const QString targetPath = !targetRoot.isEmpty() && !path.isEmpty()
        ? QDir(targetRoot).filePath(QFileInfo(path).fileName())
        : QString();

    QStringList lines;
    lines << QStringLiteral("批次：%1").arg(currentBatchId_.isEmpty() ? QStringLiteral("未记录") : currentBatchId_);
    lines << QStringLiteral("模块：%1").arg(ModuleTitle(finding.module));
    lines << QStringLiteral("项目：%1").arg(finding.title);
    lines << QStringLiteral("等级：%1").arg(SeverityTitle(SeverityForFinding(finding)));
    lines << QStringLiteral("趋势：%1").arg(TrendTitleForFinding(finding));
    lines << QStringLiteral("复核状态：%1").arg(IsIgnoredFinding(finding) ? QStringLiteral("已忽略") : QStringLiteral("待复核"));
    lines << QStringLiteral("处置状态：%1").arg(IsCompletedFinding(finding) ? QStringLiteral("已处理") : QStringLiteral("待处理"));
    if (!FindingNote(finding).isEmpty()) {
        lines << QStringLiteral("处置备注：%1").arg(FindingNote(finding));
    }
    lines << QStringLiteral("状态：%1").arg(finding.state);
    lines << QStringLiteral("大小：%1").arg(finding.bytes > 0 ? FormatBytesText(finding.bytes) : QStringLiteral("-"));
    if (!path.isEmpty()) {
        lines << QStringLiteral("路径：%1").arg(nativePath);
    }
    lines << QString();
    lines << QStringLiteral("说明：%1").arg(finding.detail);
    lines << QString();
    lines << QStringLiteral("建议方案：");

    switch (finding.module) {
    case FeatureModule::GrowthTrace:
        lines << QStringLiteral("1. 打开所在目录，确认近期写入来源。");
        lines << QStringLiteral("2. 如果来自录屏、下载器、编译缓存或日志，优先在对应软件里调整输出目录。");
        lines << QStringLiteral("3. 对该目录设置预算阈值，后续由“磁盘配额与预算”定期检查。");
        break;
    case FeatureModule::SoftwareFootprint:
        lines << QStringLiteral("1. 先通过 Windows“已安装的应用”确认是否仍需要该软件。");
        lines << QStringLiteral("2. 若要卸载，优先使用软件自带卸载器或系统卸载入口。");
        lines << QStringLiteral("3. 卸载后回到 DiskLens 扫描安装目录残留，再决定是否手动处理。");
        lines << QStringLiteral("入口命令：start ms-settings:appsfeatures");
        break;
    case FeatureModule::AppMover:
        if (targetPath.isEmpty()) {
            lines << QStringLiteral("1. 先在顶部填写目标路径，例如 D:\\Apps 或 D:\\Games。");
        } else {
            lines << QStringLiteral("1. 退出相关软件 / 游戏及启动器。");
            lines << QStringLiteral("2. 复制目录到目标盘并校验：");
            lines << QStringLiteral("   robocopy %1 %2 /MIR /COPY:DAT /DCOPY:DAT /R:2 /W:2 /XJ").arg(QuoteCmd(path), QuoteCmd(targetPath));
            lines << QStringLiteral("3. 确认目标可运行后，把原目录改名备份：");
            lines << QStringLiteral("   ren %1 \"%2.backup\"").arg(QuoteCmd(path), QFileInfo(path).fileName());
            lines << QStringLiteral("4. 建立 junction 保持原路径：");
            lines << QStringLiteral("   mklink /J %1 %2").arg(QuoteCmd(path), QuoteCmd(targetPath));
            lines << QStringLiteral("5. 运行软件确认无误后，再删除 .backup 目录。");
        }
        break;
    case FeatureModule::ArchiveAssistant:
        if (targetPath.isEmpty()) {
            lines << QStringLiteral("1. 先选择目标归档目录或移动硬盘路径。");
        } else {
            lines << QStringLiteral("1. 使用复制而不是直接移动，保留回滚窗口。");
            lines << QStringLiteral("2. 归档命令示例：");
            lines << QStringLiteral("   robocopy %1 %2 /E /COPY:DAT /DCOPY:DAT /R:2 /W:2").arg(QuoteCmd(path), QuoteCmd(targetPath));
            lines << QStringLiteral("3. 归档后抽查打开文件，再决定是否删除源文件。");
        }
        break;
    case FeatureModule::DownloadOrganizer:
        lines << QStringLiteral("1. 在该目录下创建“安装包 / 压缩包 / 文档 / 媒体 / 其他”子目录。");
        lines << QStringLiteral("2. 先移动 30 天以前的文件，保留最近下载文件在原位。");
        lines << QStringLiteral("3. 对安装包和压缩包可设置 90 天自动归档规则。");
        break;
    case FeatureModule::PrivacyRadar:
        lines << QStringLiteral("1. 确认该文件是否包含密钥、证书、合同、密码或身份信息。");
        lines << QStringLiteral("2. 若位于下载目录、桌面、同步盘或代码仓库，建议移到受控私有目录。");
        lines << QStringLiteral("3. 对 .env / .pem / .key 等文件检查是否被 Git 跟踪。");
        lines << QStringLiteral("检查命令：git ls-files -- %1").arg(QuotePowerShell(path));
        break;
    case FeatureModule::DeveloperSpace:
        lines << QStringLiteral("1. 确认项目是否仍活跃；活跃项目不要直接清理依赖目录。");
        lines << QStringLiteral("2. 对可重建目录可执行清理后按需重装依赖。");
        lines << QStringLiteral("3. 推荐先提交/保存代码，再处理 build、dist、target、node_modules 等目录。");
        lines << QStringLiteral("PowerShell 预览：Get-ChildItem %1 -Force | Sort-Object Length -Descending").arg(QuotePowerShell(path));
        break;
    case FeatureModule::DockerWsl:
        lines << QStringLiteral("1. 先查看 Docker 空间分布：docker system df");
        lines << QStringLiteral("2. 确认无重要容器 / 镜像后再考虑：docker system prune");
        lines << QStringLiteral("3. WSL 虚拟盘压缩前先执行：wsl --shutdown");
        lines << QStringLiteral("4. 对 ext4.vhdx 可使用 Optimize-VHD，但需要 Hyper-V PowerShell 模块。");
        break;
    case FeatureModule::MediaOrganizer:
        lines << QStringLiteral("1. 按年份和媒体类型建立目录，例如 Photos\\2026、Videos\\2026。");
        lines << QStringLiteral("2. RAW+JPG 成对文件先不要拆散。");
        lines << QStringLiteral("3. 大视频建议先转移到归档盘，再用备份缺口检查确认有副本。");
        break;
    case FeatureModule::QuotaBudget:
        lines << QStringLiteral("1. 这里的“预算”是参考建议值，并非系统实测配额（NTFS 真实配额需另行查询）。");
        lines << QStringLiteral("2. 若目录明显超出建议预算，可按“保留 / 归档 / 可删除缓存”三类取舍，不必视为硬性限额。");
        lines << QStringLiteral("3. 后续可把该目录加入周期重扫，超阈值时提示。");
        break;
    case FeatureModule::BackupGap:
        if (targetPath.isEmpty()) {
            lines << QStringLiteral("1. 先选择目标备份根目录。");
        } else {
            lines << QStringLiteral("1. 先用镜像复制建立备份：");
            lines << QStringLiteral("   robocopy %1 %2 /MIR /COPY:DAT /DCOPY:DAT /R:2 /W:2 /XJ").arg(QuoteCmd(path), QuoteCmd(targetPath));
            lines << QStringLiteral("2. 备份完成后抽查文件，再把目标目录纳入定期备份流程。");
        }
        break;
    case FeatureModule::FileUnlocker:
        lines << QStringLiteral("1. 优先在相关应用中关闭文件，而不是强制结束进程。");
        lines << QStringLiteral("2. 如果是编辑器、压缩软件、播放器或同步盘占用，关闭后点击原操作重试。");
        lines << QStringLiteral("3. 只有确认进程不重要时，才在任务管理器结束。");
        break;
    case FeatureModule::TransferAssistant:
        if (targetPath.isEmpty()) {
            lines << QStringLiteral("1. 先选择目标目录以评估空间是否足够。");
        } else {
            lines << QStringLiteral("1. 使用 robocopy 执行可重试复制：");
            lines << QStringLiteral("   robocopy %1 %2 /E /COPY:DAT /DCOPY:DAT /R:2 /W:2 /XJ").arg(QuoteCmd(path), QuoteCmd(targetPath));
            lines << QStringLiteral("2. 抽样校验大文件哈希：");
            lines << QStringLiteral("   Get-FileHash %1").arg(QuotePowerShell(path));
            lines << QStringLiteral("3. 验证无误后再删除源路径。");
        }
        break;
    case FeatureModule::CloudSync:
        lines << QStringLiteral("1. 先在同步盘客户端确认文件同步状态。");
        lines << QStringLiteral("2. 对“冲突”副本逐个比对修改时间与内容。");
        lines << QStringLiteral("3. 大文件可改为仅云端保留或迁移到非同步目录。");
        break;
    case FeatureModule::RestorePoint:
        lines << QStringLiteral("1. 系统恢复点和卷影副本请通过 Windows 系统保护入口处理。");
        lines << QStringLiteral("2. Windows.old / 更新缓存建议使用系统“存储感知”或“磁盘清理”。");
        lines << QStringLiteral("入口命令：SystemPropertiesProtection.exe");
        lines << QStringLiteral("入口命令：cleanmgr");
        break;
    case FeatureModule::BrowserCache:
        lines << QStringLiteral("1. 关闭对应浏览器中正在播放、下载或离线编辑的页面。");
        lines << QStringLiteral("2. 优先在浏览器设置中清理缓存和站点数据，不要直接删除整个 Profile。");
        lines << QStringLiteral("3. 如果是 IndexedDB / CacheStorage 暴涨，先确认对应站点是否保存离线文档。");
        break;
    case FeatureModule::StartupFootprint:
        lines << QStringLiteral("1. 先确认启动项名称、发布者和路径是否可信。");
        lines << QStringLiteral("2. 需要禁用时优先使用任务管理器“启动应用”页，而不是直接删除程序文件。");
        lines << QStringLiteral("入口命令：taskmgr");
        lines << QStringLiteral("入口命令：start ms-settings:startupapps");
        break;
    case FeatureModule::MessengerCache:
        lines << QStringLiteral("1. 先在聊天客户端内确认文件、图片、视频是否已同步或备份。");
        lines << QStringLiteral("2. 优先使用客户端的缓存清理、文件管理或迁移存储位置功能。");
        lines << QStringLiteral("3. 企业聊天记录可能有合规要求，处理前先确认保留策略。");
        break;
    case FeatureModule::MailArchive:
        lines << QStringLiteral("1. PST / OST / MBOX 处理前先确认邮箱账户、同步状态和备份。");
        lines << QStringLiteral("2. Outlook 数据文件建议通过 Outlook 的账户设置或归档工具迁移。");
        lines << QStringLiteral("3. OST 通常是离线缓存，不建议在 Outlook 运行时移动或删除。");
        break;
    case FeatureModule::VirtualMachineImages:
        lines << QStringLiteral("1. 先完整关闭虚拟机，确认没有挂起、快照合并或后台同步任务。");
        lines << QStringLiteral("2. 优先通过 VirtualBox、VMware、Hyper-V 等管理器迁移磁盘。");
        lines << QStringLiteral("3. 压缩或删除快照前先备份虚拟机配置和关键数据。");
        break;
    }

    lines << QString();
    lines << QStringLiteral("安全边界：DiskLens 当前只生成检测结果和方案，不会自动删除、迁移、结束进程或执行 prune。");
    return lines.join(QStringLiteral("\n"));
}

QString FeatureHubWidget::BuildBulkActionPlanText(const QVector<FeatureFinding>& findings) const {
    QVector<FeatureFinding> ordered = findings;
    std::sort(ordered.begin(), ordered.end(), [](const FeatureFinding& left, const FeatureFinding& right) {
        const int leftSeverity = SeverityRank(SeverityForFinding(left));
        const int rightSeverity = SeverityRank(SeverityForFinding(right));
        if (leftSeverity != rightSeverity) {
            return leftSeverity > rightSeverity;
        }
        if (left.bytes != right.bytes) {
            return left.bytes > right.bytes;
        }
        return left.title < right.title;
    });

    std::uint64_t totalBytes = 0;
    int criticalCount = 0;
    int warningCount = 0;
    int ignoredCount = 0;
    int newCount = 0;
    int completedCount = 0;
    std::map<FeatureModule, int> moduleCounts;
    for (const FeatureFinding& finding : ordered) {
        totalBytes += finding.bytes;
        moduleCounts[finding.module] += 1;
        const FindingSeverity severity = SeverityForFinding(finding);
        if (severity == FindingSeverity::Critical) {
            ++criticalCount;
        } else if (severity == FindingSeverity::Warning) {
            ++warningCount;
        }
        if (IsIgnoredFinding(finding)) {
            ++ignoredCount;
        }
        if (IsCompletedFinding(finding)) {
            ++completedCount;
        }
        if (TrendTitleForFinding(finding) == QStringLiteral("新增")) {
            ++newCount;
        }
    }

    QStringList lines;
    lines << QStringLiteral("DiskLens 空间工具箱 · 当前视图方案包");
    lines << QStringLiteral("批次：%1").arg(currentBatchId_.isEmpty() ? QStringLiteral("未记录") : currentBatchId_);
    lines << QStringLiteral("生成时间：%1").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));
    lines << QStringLiteral("结果数量：%1").arg(ordered.size());
    lines << QStringLiteral("关联空间：%1").arg(FormatBytesText(totalBytes));
    lines << QStringLiteral("治理评分：%1").arg(GovernanceScore(ordered));
    lines << QStringLiteral("高风险：%1，中风险：%2，新增：%3，已处理：%4，已忽略：%5")
                 .arg(criticalCount).arg(warningCount).arg(newCount).arg(completedCount).arg(ignoredCount);
    lines << QStringLiteral("基线：%1").arg(baselineCapturedAt_.isEmpty()
        ? QStringLiteral("未保存")
        : QStringLiteral("%1，%2 项").arg(baselineCapturedAt_).arg(baselineFindingKeys_.size()));
    lines << QString();
    lines << QStringLiteral("交付顺序：");
    lines << QStringLiteral("1. 先处理高风险：隐私文件、被占用文件、备份缺口、目标空间不足。");
    lines << QStringLiteral("2. 再处理可回收大项：浏览器缓存、聊天缓存、开发缓存、虚拟机镜像、邮件归档。");
    lines << QStringLiteral("3. 最后处理迁移与归档：应用搬家、长期未动资料、同步盘大文件。");
    lines << QStringLiteral("4. 所有删除、迁移、压缩动作都应先做备份或校验，DiskLens 不自动执行危险动作。");
    lines << QString();
    lines << QStringLiteral("模块分布：");
    for (const auto& item : moduleCounts) {
        lines << QStringLiteral("- %1：%2 项").arg(ModuleTitle(item.first)).arg(item.second);
    }
    lines << QString();
    lines << QStringLiteral("优先处理清单：");
    const int limit = static_cast<int>(std::min(ordered.size(), static_cast<qsizetype>(30)));
    for (int index = 0; index < limit; ++index) {
        const FeatureFinding& finding = ordered.at(index);
        lines << QStringLiteral("%1. [%2] %3 / %4 / %5")
                     .arg(index + 1)
                     .arg(SeverityTitle(SeverityForFinding(finding)))
                     .arg(ModuleTitle(finding.module))
                     .arg(finding.title)
                     .arg(finding.bytes > 0 ? FormatBytesText(finding.bytes) : QStringLiteral("-"));
        lines << QStringLiteral("   趋势：%1").arg(TrendTitleForFinding(finding));
        if (!finding.path.isEmpty()) {
            lines << QStringLiteral("   路径：%1").arg(QDir::toNativeSeparators(finding.path));
        }
        lines << QStringLiteral("   处置：%1").arg(IsCompletedFinding(finding) ? QStringLiteral("已处理") : QStringLiteral("待处理"));
        if (!FindingNote(finding).isEmpty()) {
            lines << QStringLiteral("   备注：%1").arg(FindingNote(finding));
        }
        lines << QStringLiteral("   建议：%1").arg(finding.detail);
    }
    if (ordered.size() > limit) {
        lines << QStringLiteral("... 其余 %1 项请在工具箱中继续按模块复核。").arg(ordered.size() - limit);
    }
    return lines.join(QStringLiteral("\n"));
}

QString FeatureHubWidget::BuildProfessionalReportHtml(const QVector<FeatureFinding>& findings) const {
    QVector<FeatureFinding> ordered = findings;
    std::sort(ordered.begin(), ordered.end(), [](const FeatureFinding& left, const FeatureFinding& right) {
        const int leftSeverity = SeverityRank(SeverityForFinding(left));
        const int rightSeverity = SeverityRank(SeverityForFinding(right));
        if (leftSeverity != rightSeverity) {
            return leftSeverity > rightSeverity;
        }
        if (left.bytes != right.bytes) {
            return left.bytes > right.bytes;
        }
        return ModuleTitle(left.module) < ModuleTitle(right.module);
    });

    std::uint64_t totalBytes = 0;
    int criticalCount = 0;
    int warningCount = 0;
    int noticeCount = 0;
    int ignoredCount = 0;
    int newCount = 0;
    int completedCount = 0;
    std::map<FeatureModule, int> moduleCounts;
    for (const FeatureFinding& finding : ordered) {
        totalBytes += finding.bytes;
        moduleCounts[finding.module] += 1;
        const FindingSeverity severity = SeverityForFinding(finding);
        if (severity == FindingSeverity::Critical) {
            ++criticalCount;
        } else if (severity == FindingSeverity::Warning) {
            ++warningCount;
        } else if (severity == FindingSeverity::Notice) {
            ++noticeCount;
        }
        if (IsIgnoredFinding(finding)) {
            ++ignoredCount;
        }
        if (IsCompletedFinding(finding)) {
            ++completedCount;
        }
        if (TrendTitleForFinding(finding) == QStringLiteral("新增")) {
            ++newCount;
        }
    }

    QString out;
    out += QStringLiteral("<!DOCTYPE html><html><head><meta charset=\"utf-8\">");
    out += QStringLiteral("<title>DiskLens 空间工具箱专业报告</title>");
    out += QStringLiteral("<style>");
    out += QStringLiteral("body{font-family:'Microsoft YaHei','Segoe UI',Arial,sans-serif;margin:0;background:#f5f7fb;color:#1f2937;}");
    out += QStringLiteral("header{background:#263746;color:#fff;padding:24px 32px;}");
    out += QStringLiteral("h1{font-size:24px;margin:0 0 8px;}h2{font-size:18px;margin:28px 0 12px;}p{margin:4px 0 0;color:#dbe7f3;}");
    out += QStringLiteral("main{padding:24px 32px;}section{background:#fff;border:1px solid #dde5ee;border-radius:8px;padding:18px;margin-bottom:18px;}");
    out += QStringLiteral(".metrics{display:grid;grid-template-columns:repeat(auto-fit,minmax(120px,1fr));gap:12px;}.metric{border:1px solid #dbe3ec;border-radius:6px;padding:12px;background:#fbfdff;}");
    out += QStringLiteral(".metric b{display:block;font-size:20px;margin-top:4px;color:#172033;}.tag{display:inline-block;border-radius:999px;padding:3px 9px;font-size:12px;font-weight:600;}");
    out += QStringLiteral(".critical{background:#fde7e7;color:#9b1c1c;}.warning{background:#fff3cd;color:#7a4d00;}.notice{background:#e8f1ff;color:#174ea6;}.normal{background:#ecfdf3;color:#166534;}");
    out += QStringLiteral("table{border-collapse:collapse;width:100%;font-size:13px;}th,td{border:1px solid #dfe7f0;padding:7px 9px;text-align:left;vertical-align:top;}th{background:#edf3f9;color:#223044;}tr:nth-child(even){background:#fafcff;}");
    out += QStringLiteral(".path{font-family:Consolas,'Microsoft YaHei',monospace;font-size:12px;word-break:break-all;color:#475569;}.muted{color:#64748b;}");
    out += QStringLiteral("</style></head><body>");
    out += QStringLiteral("<header><h1>DiskLens 空间工具箱专业报告</h1>");
    out += QStringLiteral("<p>批次：%1 · 生成时间：%2 · 当前视图结果：%3 项 · 关联空间：%4</p></header>")
               .arg(EscapeHtmlText(currentBatchId_.isEmpty() ? QStringLiteral("未记录") : currentBatchId_))
               .arg(EscapeHtmlText(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))))
               .arg(ordered.size())
               .arg(EscapeHtmlText(FormatBytesText(totalBytes)));
    out += QStringLiteral("<main>");

    out += QStringLiteral("<section><h2>执行摘要</h2><div class=\"metrics\">");
    out += QStringLiteral("<div class=\"metric\">高风险<b>%1</b></div>").arg(criticalCount);
    out += QStringLiteral("<div class=\"metric\">中风险<b>%1</b></div>").arg(warningCount);
    out += QStringLiteral("<div class=\"metric\">提示项<b>%1</b></div>").arg(noticeCount);
    out += QStringLiteral("<div class=\"metric\">新增项<b>%1</b></div>").arg(newCount);
    out += QStringLiteral("<div class=\"metric\">已处理<b>%1</b></div>").arg(completedCount);
    out += QStringLiteral("<div class=\"metric\">已解决<b>%1</b></div>").arg(ResolvedBaselineCount(findings_));
    out += QStringLiteral("<div class=\"metric\">已忽略<b>%1</b></div>").arg(ignoredCount);
    out += QStringLiteral("<div class=\"metric\">治理评分<b>%1</b></div>").arg(GovernanceScore(ordered));
    out += QStringLiteral("</div></section>");

    out += QStringLiteral("<section><h2>基线信息</h2><p class=\"muted\">%1</p></section>")
               .arg(EscapeHtmlText(baselineCapturedAt_.isEmpty()
                   ? QStringLiteral("尚未保存基线，当前报告中的趋势均显示为未建基线。")
                   : QStringLiteral("当前基线保存于 %1，包含 %2 条稳定结果键。").arg(baselineCapturedAt_).arg(baselineFindingKeys_.size())));

    out += QStringLiteral("<section><h2>模块分布</h2><table><thead><tr><th>模块</th><th>结果数</th></tr></thead><tbody>");
    for (const auto& item : moduleCounts) {
        out += QStringLiteral("<tr><td>%1</td><td>%2</td></tr>")
                   .arg(EscapeHtmlText(ModuleTitle(item.first)))
                   .arg(item.second);
    }
    out += QStringLiteral("</tbody></table></section>");

    out += QStringLiteral("<section><h2>优先处理清单</h2><table><thead><tr><th>等级</th><th>趋势</th><th>处置</th><th>模块</th><th>项目</th><th>状态</th><th>大小</th><th>路径 / 入口</th><th>备注</th><th>说明</th></tr></thead><tbody>");
    for (const FeatureFinding& finding : ordered) {
        const FindingSeverity severity = SeverityForFinding(finding);
        const QString severityClass = severity == FindingSeverity::Critical ? QStringLiteral("critical")
            : severity == FindingSeverity::Warning ? QStringLiteral("warning")
            : severity == FindingSeverity::Notice ? QStringLiteral("notice")
            : QStringLiteral("normal");
        out += QStringLiteral("<tr><td><span class=\"tag %1\">%2</span></td><td>%3</td><td>%4</td><td>%5</td><td>%6</td><td>%7</td><td>%8</td><td class=\"path\">%9</td><td>%10</td><td>%11</td></tr>")
                   .arg(severityClass)
                   .arg(EscapeHtmlText(SeverityTitle(severity)))
                   .arg(EscapeHtmlText(TrendTitleForFinding(finding)))
                   .arg(EscapeHtmlText(IsCompletedFinding(finding) ? QStringLiteral("已处理") : QStringLiteral("待处理")))
                   .arg(EscapeHtmlText(ModuleTitle(finding.module)))
                   .arg(EscapeHtmlText(finding.title))
                   .arg(EscapeHtmlText(IsIgnoredFinding(finding) ? QStringLiteral("已忽略 / %1").arg(finding.state) : finding.state))
                   .arg(EscapeHtmlText(finding.bytes > 0 ? FormatBytesText(finding.bytes) : QStringLiteral("-")))
                   .arg(EscapeHtmlText(QDir::toNativeSeparators(finding.path)))
                   .arg(EscapeHtmlText(FindingNote(finding)))
                   .arg(EscapeHtmlText(finding.detail));
    }
    out += QStringLiteral("</tbody></table></section>");

    out += QStringLiteral("<section><h2>交付建议</h2><p class=\"muted\">先处理高风险和备份缺口，再处理可回收大项，最后执行迁移和归档。DiskLens 当前只生成检测结果和处理方案，不会自动删除、迁移、结束进程或执行 prune。</p></section>");
    out += QStringLiteral("</main></body></html>");
    return out;
}

QString FeatureHubWidget::BuildTaskChecklistMarkdown(const QVector<FeatureFinding>& findings) const {
    QVector<FeatureFinding> ordered = findings;
    std::sort(ordered.begin(), ordered.end(), [](const FeatureFinding& left, const FeatureFinding& right) {
        const int leftSeverity = SeverityRank(SeverityForFinding(left));
        const int rightSeverity = SeverityRank(SeverityForFinding(right));
        if (leftSeverity != rightSeverity) {
            return leftSeverity > rightSeverity;
        }
        if (left.bytes != right.bytes) {
            return left.bytes > right.bytes;
        }
        return left.title < right.title;
    });

    QStringList lines;
    lines << QStringLiteral("# DiskLens 空间治理任务清单");
    lines << QString();
    lines << QStringLiteral("- 批次：%1").arg(currentBatchId_.isEmpty() ? QStringLiteral("未记录") : currentBatchId_);
    lines << QStringLiteral("- 生成时间：%1").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));
    lines << QStringLiteral("- 治理评分：%1").arg(GovernanceScore(ordered));
    lines << QStringLiteral("- 结果数量：%1").arg(ordered.size());
    lines << QStringLiteral("- 已解决基线项：%1").arg(ResolvedBaselineCount(findings_));
    lines << QString();
    lines << QStringLiteral("## 优先任务");
    lines << QString();

    for (const FeatureFinding& finding : ordered) {
        const QString checked = IsCompletedFinding(finding) ? QStringLiteral("x") : QStringLiteral(" ");
        lines << QStringLiteral("- [%1] [%2][%3] %4 · %5 · %6")
                     .arg(checked)
                     .arg(SeverityTitle(SeverityForFinding(finding)))
                     .arg(TrendTitleForFinding(finding))
                     .arg(ModuleTitle(finding.module))
                     .arg(finding.title)
                     .arg(finding.bytes > 0 ? FormatBytesText(finding.bytes) : QStringLiteral("-"));
        lines << QStringLiteral("  - 状态：%1").arg(finding.state);
        if (!finding.path.isEmpty()) {
            lines << QStringLiteral("  - 路径：%1").arg(QDir::toNativeSeparators(finding.path));
        }
        if (!FindingNote(finding).isEmpty()) {
            lines << QStringLiteral("  - 备注：%1").arg(FindingNote(finding));
        }
        lines << QStringLiteral("  - 建议：%1").arg(finding.detail);
    }

    lines << QString();
    lines << QStringLiteral("> 安全边界：DiskLens 只生成检测结果和任务清单，不自动删除、迁移、结束进程或执行 prune。");
    return lines.join(QStringLiteral("\n"));
}

QByteArray FeatureHubWidget::BuildDeliveryPackageJson(const QVector<FeatureFinding>& findings) const {
    std::uint64_t totalBytes = 0;
    int criticalCount = 0;
    int warningCount = 0;
    int noticeCount = 0;
    int ignoredCount = 0;
    int newCount = 0;
    int completedCount = 0;
    std::map<FeatureModule, int> moduleCounts;
    for (const FeatureFinding& finding : findings) {
        totalBytes += finding.bytes;
        moduleCounts[finding.module] += 1;
        const FindingSeverity severity = SeverityForFinding(finding);
        if (severity == FindingSeverity::Critical) {
            ++criticalCount;
        } else if (severity == FindingSeverity::Warning) {
            ++warningCount;
        } else if (severity == FindingSeverity::Notice) {
            ++noticeCount;
        }
        if (IsIgnoredFinding(finding)) {
            ++ignoredCount;
        }
        if (IsCompletedFinding(finding)) {
            ++completedCount;
        }
        if (TrendTitleForFinding(finding) == QStringLiteral("新增")) {
            ++newCount;
        }
    }

    QJsonArray moduleArray;
    for (const auto& item : moduleCounts) {
        QJsonObject module;
        module.insert(QStringLiteral("id"), ModuleToInt(item.first));
        module.insert(QStringLiteral("title"), ModuleTitle(item.first));
        module.insert(QStringLiteral("count"), item.second);
        moduleArray.append(module);
    }

    QJsonArray findingArray;
    for (const FeatureFinding& finding : findings) {
        const FindingSeverity severity = SeverityForFinding(finding);
        QJsonObject row;
        row.insert(QStringLiteral("moduleId"), ModuleToInt(finding.module));
        row.insert(QStringLiteral("module"), ModuleTitle(finding.module));
        row.insert(QStringLiteral("title"), finding.title);
        row.insert(QStringLiteral("severity"), SeverityCode(severity));
        row.insert(QStringLiteral("severityTitle"), SeverityTitle(severity));
        row.insert(QStringLiteral("trend"), TrendTitleForFinding(finding));
        row.insert(QStringLiteral("ignored"), IsIgnoredFinding(finding));
        row.insert(QStringLiteral("completed"), IsCompletedFinding(finding));
        row.insert(QStringLiteral("disposition"), IsCompletedFinding(finding) ? QStringLiteral("completed") : QStringLiteral("pending"));
        row.insert(QStringLiteral("state"), finding.state);
        row.insert(QStringLiteral("bytes"), QString::number(finding.bytes));
        row.insert(QStringLiteral("bytesHuman"), finding.bytes > 0 ? FormatBytesText(finding.bytes) : QStringLiteral("-"));
        row.insert(QStringLiteral("path"), QDir::toNativeSeparators(finding.path));
        row.insert(QStringLiteral("detail"), finding.detail);
        row.insert(QStringLiteral("note"), FindingNote(finding));
        row.insert(QStringLiteral("stableKey"), FindingStableKey(finding));
        findingArray.append(row);
    }

    QJsonObject summary;
    summary.insert(QStringLiteral("findingCount"), static_cast<int>(findings.size()));
    summary.insert(QStringLiteral("totalBytes"), QString::number(totalBytes));
    summary.insert(QStringLiteral("totalBytesHuman"), FormatBytesText(totalBytes));
    summary.insert(QStringLiteral("governanceScore"), GovernanceScore(findings));
    summary.insert(QStringLiteral("criticalCount"), criticalCount);
    summary.insert(QStringLiteral("warningCount"), warningCount);
    summary.insert(QStringLiteral("noticeCount"), noticeCount);
    summary.insert(QStringLiteral("ignoredCount"), ignoredCount);
    summary.insert(QStringLiteral("newCount"), newCount);
    summary.insert(QStringLiteral("completedCount"), completedCount);
    summary.insert(QStringLiteral("resolvedBaselineCount"), ResolvedBaselineCount(findings_));

    QJsonObject baseline;
    baseline.insert(QStringLiteral("capturedAt"), baselineCapturedAt_);
    baseline.insert(QStringLiteral("keyCount"), static_cast<int>(baselineFindingKeys_.size()));
    baseline.insert(QStringLiteral("available"), !baselineFindingKeys_.isEmpty());

    QJsonObject context;
    context.insert(QStringLiteral("sourcePath"), sourcePathEdit_ != nullptr ? sourcePathEdit_->text().trimmed() : QString());
    context.insert(QStringLiteral("targetPath"), targetPathEdit_ != nullptr ? targetPathEdit_->text().trimmed() : QString());
    context.insert(QStringLiteral("showIgnored"), showIgnoredButton_ != nullptr && showIgnoredButton_->isChecked());
    context.insert(QStringLiteral("workflowFilter"), CurrentWorkflowFilterCode());

    QJsonObject root;
    root.insert(QStringLiteral("product"), QStringLiteral("DiskLens"));
    root.insert(QStringLiteral("packageType"), QStringLiteral("featureHubDelivery"));
    root.insert(QStringLiteral("schemaVersion"), 1);
    root.insert(QStringLiteral("batchId"), currentBatchId_);
    root.insert(QStringLiteral("resultCapturedAt"), resultCapturedAt_);
    root.insert(QStringLiteral("exportedAt"), QDateTime::currentDateTime().toString(Qt::ISODate));
    root.insert(QStringLiteral("context"), context);
    root.insert(QStringLiteral("summary"), summary);
    root.insert(QStringLiteral("baseline"), baseline);
    root.insert(QStringLiteral("modules"), moduleArray);
    root.insert(QStringLiteral("findings"), findingArray);
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

void FeatureHubWidget::ExportFindings() {
    const QVector<FeatureFinding> visibleFindings = VisibleFindings();
    if (visibleFindings.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("导出"), QStringLiteral("当前视图没有可导出的工具箱结果。"));
        return;
    }
    QString selectedFilter;
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("导出空间工具箱结果"),
                                                      QStringLiteral("空间工具箱结果.csv"),
                                                      QStringLiteral("CSV 文件 (*.csv);;HTML 报表 (*.html)"),
                                                      &selectedFilter);
    if (path.isEmpty()) {
        return;
    }
    const bool asHtml = selectedFilter.startsWith(QStringLiteral("HTML")) || path.endsWith(QStringLiteral(".html"), Qt::CaseInsensitive);
    QStringList headers{QStringLiteral("批次"), QStringLiteral("模块"), QStringLiteral("项目"), QStringLiteral("等级"),
                        QStringLiteral("趋势"), QStringLiteral("复核状态"), QStringLiteral("处置状态"), QStringLiteral("状态"),
                        QStringLiteral("大小"), QStringLiteral("路径"), QStringLiteral("备注"), QStringLiteral("说明")};
    QVector<QStringList> rows;
    rows.reserve(visibleFindings.size());
    for (const FeatureFinding& finding : visibleFindings) {
        rows << (QStringList()
            << currentBatchId_
            << ModuleTitle(finding.module)
            << finding.title
            << SeverityTitle(SeverityForFinding(finding))
            << TrendTitleForFinding(finding)
            << (IsIgnoredFinding(finding) ? QStringLiteral("已忽略") : QStringLiteral("待复核"))
            << (IsCompletedFinding(finding) ? QStringLiteral("已处理") : QStringLiteral("待处理"))
            << finding.state
            << (finding.bytes > 0 ? FormatBytesText(finding.bytes) : QStringLiteral("-"))
            << finding.path
            << FindingNote(finding)
            << finding.detail);
    }
    if (!WriteTableReport(path, QStringLiteral("磁盘洞察 空间工具箱当前视图"), headers, rows, asHtml)) {
        QMessageBox::warning(this, QStringLiteral("导出"), QStringLiteral("无法写入导出文件。"));
    } else if (statusLabel_ != nullptr) {
        statusLabel_->setText(QStringLiteral("已导出当前视图：%1 条结果。").arg(rows.size()));
    }
}

void FeatureHubWidget::ExportProfessionalReport() {
    const QVector<FeatureFinding> visibleFindings = VisibleFindings();
    if (visibleFindings.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("专业报告"), QStringLiteral("当前视图没有可导出的工具箱结果。"));
        return;
    }

    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("导出空间工具箱专业报告"),
                                                      QStringLiteral("空间工具箱专业报告.html"),
                                                      QStringLiteral("HTML 报告 (*.html)"));
    if (path.isEmpty()) {
        return;
    }

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::warning(this, QStringLiteral("专业报告"), QStringLiteral("无法写入专业报告文件。"));
        return;
    }
    const QByteArray bytes = BuildProfessionalReportHtml(visibleFindings).toUtf8();
    const bool ok = file.write(bytes) == bytes.size();
    file.close();
    if (!ok) {
        QMessageBox::warning(this, QStringLiteral("专业报告"), QStringLiteral("专业报告写入不完整。"));
        return;
    }
    if (statusLabel_ != nullptr) {
        statusLabel_->setText(QStringLiteral("已导出专业报告：%1 条结果。").arg(visibleFindings.size()));
    }
}

void FeatureHubWidget::ExportDeliveryPackage() {
    const QVector<FeatureFinding> visibleFindings = VisibleFindings();
    if (visibleFindings.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("JSON 交付包"), QStringLiteral("当前视图没有可导出的工具箱结果。"));
        return;
    }

    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("导出空间工具箱 JSON 交付包"),
                                                      QStringLiteral("空间工具箱交付包.json"),
                                                      QStringLiteral("JSON 文件 (*.json)"));
    if (path.isEmpty()) {
        return;
    }

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::warning(this, QStringLiteral("JSON 交付包"), QStringLiteral("无法写入 JSON 交付包。"));
        return;
    }
    const QByteArray bytes = BuildDeliveryPackageJson(visibleFindings);
    const bool ok = file.write(bytes) == bytes.size();
    file.close();
    if (!ok) {
        QMessageBox::warning(this, QStringLiteral("JSON 交付包"), QStringLiteral("JSON 交付包写入不完整。"));
        return;
    }
    if (statusLabel_ != nullptr) {
        statusLabel_->setText(QStringLiteral("已导出 JSON 交付包：%1 条结果。").arg(visibleFindings.size()));
    }
}

void FeatureHubWidget::ExportTaskChecklist() {
    const QVector<FeatureFinding> visibleFindings = VisibleFindings();
    if (visibleFindings.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("任务清单"), QStringLiteral("当前视图没有可导出的处置任务。"));
        return;
    }

    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("导出空间治理任务清单"),
                                                      QStringLiteral("空间治理任务清单.md"),
                                                      QStringLiteral("Markdown 文件 (*.md)"));
    if (path.isEmpty()) {
        return;
    }

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::warning(this, QStringLiteral("任务清单"), QStringLiteral("无法写入任务清单文件。"));
        return;
    }
    const QByteArray bytes = BuildTaskChecklistMarkdown(visibleFindings).toUtf8();
    const bool ok = file.write(bytes) == bytes.size();
    file.close();
    if (!ok) {
        QMessageBox::warning(this, QStringLiteral("任务清单"), QStringLiteral("任务清单写入不完整。"));
        return;
    }
    if (statusLabel_ != nullptr) {
        statusLabel_->setText(QStringLiteral("已导出处置任务清单：%1 条结果。").arg(visibleFindings.size()));
    }
}

void FeatureHubWidget::UpdateActionState() {
    if (openPathButton_ == nullptr) {
        return;
    }
    QString path;
    if (resultTree_ != nullptr && resultTree_->currentItem() != nullptr) {
        path = resultTree_->currentItem()->data(0, kRolePath).toString();
    }
    openPathButton_->setEnabled(PathExists(path));
    if (copyPathButton_ != nullptr) {
        copyPathButton_->setEnabled(!path.isEmpty());
    }
    if (actionPlanButton_ != nullptr) {
        bool ok = false;
        const FeatureFinding finding = CurrentFinding(ok);
        actionPlanButton_->setEnabled(ok);
        if (completeButton_ != nullptr) {
            completeButton_->setEnabled(ok);
            completeButton_->setText(ok && IsCompletedFinding(finding) ? QStringLiteral("标记待处理") : QStringLiteral("标记处理"));
        }
        if (noteButton_ != nullptr) {
            noteButton_->setEnabled(ok);
        }
    }
    const bool hasVisibleFindings = !VisibleFindings().isEmpty();
    if (bulkPlanButton_ != nullptr) {
        bulkPlanButton_->setEnabled(hasVisibleFindings);
    }
    if (exportButton_ != nullptr) {
        exportButton_->setEnabled(hasVisibleFindings);
    }
    if (professionalReportButton_ != nullptr) {
        professionalReportButton_->setEnabled(hasVisibleFindings);
    }
    if (deliveryPackageButton_ != nullptr) {
        deliveryPackageButton_->setEnabled(hasVisibleFindings);
    }
    if (taskChecklistButton_ != nullptr) {
        taskChecklistButton_->setEnabled(hasVisibleFindings);
    }
    if (deliveryMenuButton_ != nullptr) {
        deliveryMenuButton_->setEnabled(hasVisibleFindings);
    }
    if (baselineButton_ != nullptr) {
        baselineButton_->setEnabled(!findings_.isEmpty());
    }
}

void FeatureHubWidget::SetBusy(bool busy, const QString& text) {
    scanning_.store(busy);
    if (scanAllButton_ != nullptr) {
        scanAllButton_->setEnabled(!busy);
    }
    if (scanCurrentButton_ != nullptr) {
        scanCurrentButton_->setEnabled(!busy);
    }
    if (cancelButton_ != nullptr) {
        cancelButton_->setEnabled(busy);
    }
    if (statusLabel_ != nullptr) {
        statusLabel_->setText(text);
    }
    RefreshModuleFilter();
}

void FeatureHubWidget::ReplaceFindings(QVector<FeatureFinding> findings, std::uint64_t requestId, bool cancelled) {
    if (requestId != requestId_.load()) {
        return;
    }
    findings_ = std::move(findings);
    resultCapturedAt_ = QDateTime::currentDateTime().toString(Qt::ISODate);
    currentBatchId_ = QStringLiteral("FH-%1-%2")
                          .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMddHHmmss")))
                          .arg(requestId);
    SaveResultCache();
    cancelFlag_.reset();
    SetBusy(false, cancelled
                       ? QStringLiteral("已取消：保留 %1 条部分工具箱结果。").arg(findings_.size())
                       : QStringLiteral("体检完成：已生成 %1 条工具箱结果。").arg(findings_.size()));
}

FeatureModule FeatureHubWidget::CurrentModule(bool& hasModule) const {
    hasModule = false;
    if (moduleList_ == nullptr || moduleList_->currentItem() == nullptr) {
        return FeatureModule::GrowthTrace;
    }
    const int value = moduleList_->currentItem()->data(Qt::UserRole).toInt();
    if (value < 0) {
        return FeatureModule::GrowthTrace;
    }
    hasModule = true;
    return IntToModule(value);
}

}  // namespace disk_lens::qt_ui
