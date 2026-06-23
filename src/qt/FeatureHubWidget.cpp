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
#include <QHash>
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
#include <QRegularExpression>
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
#include <objbase.h>
#include <ole2.h>
#include <olectl.h>
#include <shlobj.h>
// initguid 必须在 dskquota.h 之前:使 dskquota.h 内的 DEFINE_GUID 在本 TU 内"定义"CLSID_DiskQuotaControl /
// IID_IDiskQuotaControl 等,从而无需链接 dskquota.lib(不改 CMakeLists)。ole2.h/olectl.h 已在其前包含,
// 故 dskquota.h 内部的 #ifndef 守卫会跳过它们——确保 initguid 只作用于 dskquota 自有的冷门 GUID(不在
// uuid.lib 中),不与 ole2/olectl 的 GUID 重复定义。
#include <initguid.h>
#include <dskquota.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace disk_lens::qt_ui {
namespace {

/**
 * @brief 工具箱模块的分组类别(导览与分组用)。
 *
 * 渲染顺序按"空间紧张的用户最想要的先":Reclaim 清理回收 → Inventory 占用盘点
 * → Migrate 迁移搬家 → Audit 检查防护。分类仅用于列表分组与导览文案,**不影响枚举顺序、
 * 扫描顺序或稳定键**。
 */
enum class ModuleCategory {
    Reclaim,
    Inventory,
    Migrate,
    Audit,
};

/**
 * @brief 分类标签与导览简介。
 */
struct CategoryInfo {
    ModuleCategory category = ModuleCategory::Reclaim;
    QString label;
    QString blurb;
};

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
     * @brief 模块说明(悬停 tooltip / 结果说明用)。
     */
    QString description;

    /**
     * @brief 所属分组类别(列表分组用)。
     */
    ModuleCategory category = ModuleCategory::Reclaim;

    /**
     * @brief 模块用途(是什么):说明面板与导览对话框用。
     */
    QString purpose;

    /**
     * @brief 操作流程(怎么用):分步骤说明。
     */
    QString howToUse;

    /**
     * @brief 操作提示:路径输入指引、口径与注意事项(安全声明由面板统一追加,不重复写)。
     */
    QString tips;
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

    /**
     * @brief 目录树中最新的文件修改时间(毫秒,UTC epoch);0 表示无文件或未取到。用于备份新鲜度比对等。
     */
    qint64 latestMtimeMsec = 0;
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
        {FeatureModule::GrowthTrace, QStringLiteral("空间增长溯源"),
         QStringLiteral("列出近 7 天新写入或修改的大文件，及累计偏大的目录（按修改时间，非增量测量）"),
         ModuleCategory::Inventory,
         QStringLiteral("找出近 7 天新写入或明显变大的文件与目录，定位空间“涨在哪”。"),
         QStringLiteral("①选中本模块（可选填源路径缩小范围）\n②点“当前模块”体检\n③按“大小”列排序看大户。"),
         QStringLiteral("不填源路径则扫描用户目录与各盘根；按修改时间统计，非逐字节增量测量。")},
        {FeatureModule::SoftwareFootprint, QStringLiteral("软件体积管理器"),
         QStringLiteral("估算已安装软件及安装目录真实占用"),
         ModuleCategory::Inventory,
         QStringLiteral("估算已安装软件及其安装目录的真实占用。"),
         QStringLiteral("①选中本模块\n②点“当前模块”体检\n③按“大小”排序，定位最占地的软件。"),
         QStringLiteral("以安装目录为主、注册表估算为辅；嵌套目录已去重。")},
        {FeatureModule::AppMover, QStringLiteral("应用 / 游戏搬家"),
         QStringLiteral("识别适合迁移的大型应用和游戏目录并生成迁移计划（不校验目标盘空间、不执行迁移）"),
         ModuleCategory::Migrate,
         QStringLiteral("识别适合迁移的大型应用 / 游戏目录，生成迁移计划（含 robocopy / mklink 命令）。"),
         QStringLiteral("①填“目标路径”（要搬到哪个盘）\n②选中本模块体检\n③对结果点“处理方案”看迁移命令。"),
         QStringLiteral("不校验目标盘空间、不执行迁移；Steam / Epic 等多盘库自动识别。")},
        {FeatureModule::ArchiveAssistant, QStringLiteral("归档助手"),
         QStringLiteral("为长期未动资料生成迁移归档计划"),
         ModuleCategory::Reclaim,
         QStringLiteral("为长期未动的资料生成归档 / 迁移计划，腾出活跃空间。"),
         QStringLiteral("①填“源路径”（要整理的目录）与可选“目标路径”\n②体检\n③看处理方案。"),
         QStringLiteral("目录级汇总并按陈旧程度排序；只生成计划不移动。")},
        {FeatureModule::DownloadOrganizer, QStringLiteral("下载整理中心"),
         QStringLiteral("按类型统计下载和桌面文件的体积与数量并给出整理建议（只报告不移动；可在源路径追加目录）"),
         ModuleCategory::Reclaim,
         QStringLiteral("按类型统计下载与桌面文件的体积、数量，给整理建议。"),
         QStringLiteral("①可选填源路径（默认下载 / 桌面）\n②体检\n③按类型看可清理项。"),
         QStringLiteral("只报告不移动；可在源路径追加多个目录。")},
        {FeatureModule::PrivacyRadar, QStringLiteral("隐私文件雷达"),
         QStringLiteral("发现密钥、证书、合同和身份信息等敏感文件"),
         ModuleCategory::Audit,
         QStringLiteral("发现密钥、证书、合同、身份信息及浏览器凭据等敏感文件。"),
         QStringLiteral("①选中本模块体检\n②按“敏感文件 / 凭据风险”等级排查\n③导出清单集中处置。"),
         QStringLiteral("仅识别位置，不读取内容、不上传；含浏览器凭据库定向扫描。")},
        {FeatureModule::DeveloperSpace, QStringLiteral("开发环境空间中心"),
         QStringLiteral("定位 node_modules、构建产物、包缓存和虚拟环境"),
         ModuleCategory::Inventory,
         QStringLiteral("定位 node_modules、构建产物、包缓存和虚拟环境等开发占用。"),
         QStringLiteral("①可选填源路径（默认用户目录）\n②体检\n③按大小清理可重建的产物。"),
         QStringLiteral("区分可清理缓存与需保留的源码；包缓存按引擎识别。")},
        {FeatureModule::DockerWsl, QStringLiteral("Docker / WSL 空间管理"),
         QStringLiteral("枚举 WSL2 发行版与 Docker 虚拟磁盘并核算真实占用,提示压缩 / prune 途径"),
         ModuleCategory::Inventory,
         QStringLiteral("枚举 WSL2 发行版与 Docker 虚拟磁盘，核算真实占用并提示压缩 / prune 途径。"),
         QStringLiteral("①选中本模块体检\n②看 vhdx 实占与可回收量\n③按处理方案压缩 / prune。"),
         QStringLiteral("稀疏盘报实占；只读查询，绝不自动 prune。")},
        {FeatureModule::MediaOrganizer, QStringLiteral("照片 / 视频整理器"),
         QStringLiteral("按媒体类型、年代和体积生成整理建议"),
         ModuleCategory::Reclaim,
         QStringLiteral("按媒体类型、年代和体积生成照片 / 视频整理建议。"),
         QStringLiteral("①填源路径（照片 / 视频所在目录）\n②体检\n③按年代或体积折叠查看。"),
         QStringLiteral("互斥类型分组；只生成建议不移动。")},
        {FeatureModule::QuotaBudget, QStringLiteral("磁盘配额与预算"),
         QStringLiteral("给常用目录套参考预算并标记超额位置,另只读查询 NTFS 卷配额启用状态"),
         ModuleCategory::Audit,
         QStringLiteral("给常用目录套参考预算标记超额，并只读查询 NTFS 卷配额状态。"),
         QStringLiteral("①选中本模块体检\n②看哪些目录超预算\n③按需在系统配额入口调整。"),
         QStringLiteral("配额查询为只读；预算为参考值。")},
        {FeatureModule::BackupGap, QStringLiteral("备份缺口检查"),
         QStringLiteral("核对重要目录的备份完整性、时效与本地目标，并提示可能存在的异地副本"),
         ModuleCategory::Audit,
         QStringLiteral("核对重要目录的备份完整性、时效与本地目标，提示可能的异地副本。"),
         QStringLiteral("①填源路径（要核查的目录）与可选目标路径\n②体检\n③看陈旧 / 不完整 / 缺口。"),
         QStringLiteral("只核对不备份；暴露备份健康度供判断。")},
        {FeatureModule::FileUnlocker, QStringLiteral("文件占用识别器"),
         QStringLiteral("用 Windows Restart Manager 识别占用进程（仅识别不自动解锁）"),
         ModuleCategory::Audit,
         QStringLiteral("用 Windows Restart Manager 识别占用文件 / 目录的进程。"),
         QStringLiteral("①填源路径（被占用的文件 / 目录）\n②体检\n③看占用进程，自行决定关闭。"),
         QStringLiteral("仅识别占用、不自动解锁；按抽样披露，避免误报。")},
        {FeatureModule::TransferAssistant, QStringLiteral("大文件传输助手"),
         QStringLiteral("估算迁移体积、目标盘空间和执行计划"),
         ModuleCategory::Migrate,
         QStringLiteral("估算迁移体积、目标盘剩余空间并生成传输计划。"),
         QStringLiteral("①填源路径（要搬的大文件 / 目录）与目标路径\n②体检\n③看空间是否充足及执行步骤。"),
         QStringLiteral("源为空时自动发现大目录候选；FAT32 + >4GB 会提示限制。")},
        {FeatureModule::CloudSync, QStringLiteral("同步盘空间分析"),
         QStringLiteral("识别同步盘本地占用、冲突文件和大缓存"),
         ModuleCategory::Inventory,
         QStringLiteral("识别同步盘本地占用、冲突文件和大缓存。"),
         QStringLiteral("①选中本模块体检\n②看同步盘占用分布\n③处理冲突文件与大缓存。"),
         QStringLiteral("大缓存折叠明细；只读分析不改动同步状态。")},
        {FeatureModule::RestorePoint, QStringLiteral("系统镜像 / 恢复点管理"),
         QStringLiteral("核算 Windows.old、更新备份与恢复目录占用，并查询卷影副本存储（VSS/还原点）实际占用；可在系统保护入口调整。"),
         ModuleCategory::Reclaim,
         QStringLiteral("核算 Windows.old、更新备份、恢复目录及卷影副本（VSS）实际占用。"),
         QStringLiteral("①选中本模块体检\n②看各类系统残留与 VSS 占用\n③在系统保护入口调整。"),
         QStringLiteral("VSS 经 vssadmin 只读查询；查询失败优雅降级，不报错数字。")},
        {FeatureModule::BrowserCache, QStringLiteral("浏览器缓存中心"),
         QStringLiteral("识别 Chrome、Edge、Firefox 等浏览器缓存和离线数据"),
         ModuleCategory::Reclaim,
         QStringLiteral("识别 Chrome、Edge、Firefox 等浏览器的缓存和离线数据。"),
         QStringLiteral("①选中本模块体检\n②按大小看各浏览器缓存\n③按处理方案清理。"),
         QStringLiteral("区分可清理缓存与需保留的登录数据；只识别不清理。")},
        {FeatureModule::StartupFootprint, QStringLiteral("启动项体积检查"),
         QStringLiteral("盘点开机启动入口及其关联程序占用"),
         ModuleCategory::Inventory,
         QStringLiteral("盘点开机启动入口及其关联程序的真实占用。"),
         QStringLiteral("①选中本模块体检\n②看各启动项的安装目录占用\n③决定是否禁用。"),
         QStringLiteral("回溯 exe 安装目录估算；区分启用 / 禁用状态。")},
        {FeatureModule::MessengerCache, QStringLiteral("聊天缓存治理"),
         QStringLiteral("识别微信、企业微信、QQ、Teams 等聊天客户端的本地数据与缓存"),
         ModuleCategory::Reclaim,
         QStringLiteral("识别微信、企业微信、QQ、钉钉、飞书等聊天客户端的本地数据与缓存。"),
         QStringLiteral("①选中本模块体检\n②看各客户端缓存占用\n③区分可清理缓存与消息记录。"),
         QStringLiteral("按客户端与子目录拆分；消息记录保留、仅缓存可清。")},
        {FeatureModule::MailArchive, QStringLiteral("邮件归档库检查"),
         QStringLiteral("发现 PST、OST、MBOX 等邮件归档和离线邮箱文件"),
         ModuleCategory::Inventory,
         QStringLiteral("发现 PST、OST、MBOX 等邮件归档和离线邮箱文件。"),
         QStringLiteral("①选中本模块体检\n②看邮件库占用\n③按需在客户端中归档 / 压缩。"),
         QStringLiteral("稀疏 OST 报实占；只识别不改动邮箱。")},
        {FeatureModule::VirtualMachineImages, QStringLiteral("虚拟机镜像管理"),
         QStringLiteral("发现 VHD、VMDK、VDI、QCOW2 等大型虚拟磁盘"),
         ModuleCategory::Inventory,
         QStringLiteral("发现 VHD、VMDK、VDI、QCOW2 等大型虚拟磁盘文件。"),
         QStringLiteral("①选中本模块体检\n②看各虚拟盘占用\n③按需迁移或压缩。"),
         QStringLiteral("稀疏盘报实占；只识别不改动虚拟机。")},
    };
}

/**
 * @brief 返回分组分类的标签与导览简介(渲染顺序)。
 */
QVector<CategoryInfo> AllCategories() {
    return {
        {ModuleCategory::Reclaim, QStringLiteral("清理回收"), QStringLiteral("缓存、下载、旧资料——清掉能立刻腾出空间的项。")},
        {ModuleCategory::Inventory, QStringLiteral("占用盘点"), QStringLiteral("看清哪些软件、目录、虚拟盘在占地方。")},
        {ModuleCategory::Migrate, QStringLiteral("迁移搬家"), QStringLiteral("把大应用或大文件挪到其他盘。")},
        {ModuleCategory::Audit, QStringLiteral("检查防护"), QStringLiteral("体检备份、配额、隐私与文件占用，只看不改。")},
    };
}

/**
 * @brief 查模块所属分类(单一真相源,列表与说明面板共用,防漂移)。
 */
ModuleCategory CategoryOf(FeatureModule module) {
    for (const ModuleInfo& info : AllModules()) {
        if (info.module == module) {
            return info.category;
        }
    }
    return ModuleCategory::Reclaim;
}

/**
 * @brief 查模块完整元数据;未命中返回空标志。
 */
bool FindModuleInfo(FeatureModule module, ModuleInfo& out) {
    for (const ModuleInfo& info : AllModules()) {
        if (info.module == module) {
            out = info;
            return true;
        }
    }
    return false;
}

/**
 * @brief 统一的安全声明:工具箱只识别 / 生成方案 / 导出,不执行删除或迁移。
 */
QString GuideSafetyNote() {
    return QStringLiteral("本工具箱只做识别、生成方案与导出报告，不会删除或迁移你的文件。");
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
    // 识别 rundll32 / regsvr32 启动器(支持全路径调用:Run 键常以 %SystemRoot%\system32\rundll32.exe 形式
    // 调用,故按"首 token 的文件名"判定而非裸名前缀)。真正的程序是其首个参数(DLL/控件),而非启动器本身。
    auto launcherFileName = [&text]() -> QString {
        if (text.startsWith(QLatin1Char('"'))) {
            const int closeQuote = text.indexOf(QLatin1Char('"'), 1);
            if (closeQuote > 1) {
                return QFileInfo(text.mid(1, closeQuote - 1)).fileName().toCaseFolded();
            }
        }
        const int firstSpace = text.indexOf(QLatin1Char(' '));
        const QString head = firstSpace > 0 ? text.left(firstSpace) : text;
        return QFileInfo(head).fileName().toCaseFolded();
    };
    const QString launcherName = launcherFileName();
    const bool isLauncher = launcherName == QStringLiteral("rundll32.exe") ||
                            launcherName == QStringLiteral("regsvr32.exe") ||
                            launcherName == QStringLiteral("rundll32") ||
                            launcherName == QStringLiteral("regsvr32");
    if (isLauncher) {
        // 剥离启动器 token(含其全路径),取剩余首个参数(DLL/控件)。
        QString rest;
        if (text.startsWith(QLatin1Char('"'))) {
            const int closeQuote = text.indexOf(QLatin1Char('"'), 1);
            rest = closeQuote > 1 ? text.mid(closeQuote + 1) : text.mid(1);
        } else {
            const int firstSpace = text.indexOf(QLatin1Char(' '));
            rest = firstSpace > 0 ? text.mid(firstSpace) : QString();
        }
        rest = rest.trimmed();
        if (rest.isEmpty()) {
            return {};  // 启动器无参数→无法确定目标。
        }
        if (rest.startsWith(QLatin1Char('"'))) {
            const int closeQuote = rest.indexOf(QLatin1Char('"'), 1);
            if (closeQuote > 1) {
                return QDir::toNativeSeparators(rest.mid(1, closeQuote - 1));
            }
        }
        // 未加引号时取到逗号或空格前(DLL 路径后常跟 ",入口函数")。
        int stop = rest.size();
        const int comma = rest.indexOf(QLatin1Char(','));
        const int space = rest.indexOf(QLatin1Char(' '));
        if (comma > 0) {
            stop = std::min(stop, comma);
        }
        if (space > 0) {
            stop = std::min(stop, space);
        }
        const QString firstArg = rest.left(stop).trimmed();
        return firstArg.isEmpty() ? QString() : QDir::toNativeSeparators(firstArg);
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
        summary.latestMtimeMsec = info.lastModified().toMSecsSinceEpoch();
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
            const qint64 mtime = item.lastModified().toMSecsSinceEpoch();
            if (mtime > summary.latestMtimeMsec) {
                summary.latestMtimeMsec = mtime;
            }
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
 * @brief 将磁盘文件路径归一化为去重键。
 * @param path 原始路径(可能来自注册表带 \\?\ 前缀,或来自文件枚举无前缀)。
 * @return 归一化键(剥离长路径前缀、统一反斜杠、大小写不敏感)。
 *
 * \\?\ 前缀会跳过 Win32 的 / → \ 归一化(见长路径前缀经验),故须先剥离前缀再做 toNativeSeparators,
 * 否则带前缀(WSL 注册表 BasePath)与不带前缀(文件系统枚举)的同一路径会被判为不同文件,导致跨来源
 * 重复计费。用于 WSL 发行版 vhdx 的模块内去重与跨模块(DockerWsl vs VirtualMachineImages)归属去重。
 */
QString NormalizeDiskPathKey(const QString& path) {
    QString s = path;
    if (s.startsWith(QStringLiteral("\\\\?\\"))) {
        s = s.mid(4);
    } else if (s.startsWith(QStringLiteral("\\\\.\\"))) {
        s = s.mid(4);
    }
    return QDir::toNativeSeparators(s).toCaseFolded();
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
 * @brief 将路径归一化为稳定键的路径分量。
 * @param path 原始路径。
 * @return 去长路径前缀、统一反斜杠、大小写不敏感、去尾分隔符后的路径键。
 *
 * 使同一目录/文件在不同写法下(\\?\ 前缀、/ vs \、尾部分隔符、大小写)生成同一键,避免忽略/基线标记
 * 因路径写法漂移而失配。
 */
QString NormalizeKeyPath(const QString& path) {
    QString s = path.trimmed();
    if (s.startsWith(QStringLiteral("\\\\?\\"))) {
        s = s.mid(4);
    } else if (s.startsWith(QStringLiteral("\\\\.\\"))) {
        s = s.mid(4);
    }
    s = QDir::toNativeSeparators(s).toCaseFolded();
    while (s.endsWith(QLatin1Char('\\')) || s.endsWith(QLatin1Char('/'))) {
        s.chop(1);
    }
    return s;
}

/**
 * @brief 生成结果稳定键，用于忽略列表与跨会话复核。
 * @param finding 检测结果。
 * @return 稳定键文本(v2 格式,不含 state)。
 *
 * v2 键格式:v2|module|title|path(去 state 维度)。原 v1 含 state,模块一旦改 title/state 文案(名实不符
 * 深改)就会使旧忽略/基线/已处理标记全部失配,标记反复漂移。去 state 后,只要同一目录/文件 + 同一标题
 * 即视为同一项,标记稳定。路径经 NormalizeKeyPath 归一化,标题/路径均不含 '|'(Windows 路径非法字符),
 * 故按 '|' 拆分迁移安全。
 */
QString FindingStableKey(const FeatureFinding& finding) {
    return QStringLiteral("v2|%1|%2|%3")
        .arg(ModuleToInt(finding.module))
        .arg(finding.title.trimmed().toCaseFolded())
        .arg(NormalizeKeyPath(finding.path));
}

/**
 * @brief 把旧版(v1,含 state)稳定键迁移为 v2(去 state)。
 * @param v1Key 旧键(module|title|path|state)或已是 v2 的键。
 * @return v2 键;非 v1/v2 格式的键返回空串(调用方应丢弃)。
 *
 * v1→v2:取前 3 段(module|title|path),丢弃 state,前缀 v2。模块/标题/路径均按与 FindingStableKey 完全
 * 相同的方式规范化(模块还原为已知整数,标题 trim+casefold,路径过 NormalizeKeyPath),使迁移后的键与新生键
 * 逐字节相同——与旧 v1 存储时是否已 casefold 无关,标记全部保留(而非"绝大多数")。
 */
QString MigrateV1KeyToV2(const QString& v1Key) {
    if (v1Key.startsWith(QStringLiteral("v2|"))) {
        return v1Key;  // 已是 v2。
    }
    const QStringList parts = v1Key.split(QLatin1Char('|'));
    if (parts.size() < 4) {
        return QString();  // 非 v1 键(段数不足),丢弃。
    }
    // 模块:还原为已知整数(与 FindingStableKey 的 ModuleToInt 渲染一致);解析失败或非已知值保留原 token。
    bool moduleOk = false;
    const int moduleInt = parts[0].toInt(&moduleOk);
    const QString moduleComponent = (moduleOk && IsKnownModuleValue(moduleInt)) ? QString::number(moduleInt) : parts[0];
    return QStringLiteral("v2|%1|%2|%3")
        .arg(moduleComponent)
        .arg(parts[1].trimmed().toCaseFolded())
        .arg(NormalizeKeyPath(parts[2]));
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
        state.contains(QStringLiteral("不足")) ||
        state.contains(QStringLiteral("不完整")) ||
        state.contains(QStringLiteral("陈旧"))) {
        return FindingSeverity::Warning;
    }
    // 健康状态(备份完整)与媒体类型盘点(媒体分组)不论体积多大都不应升级:bytes≥10GB 升 Notice 的规则是为
    // "可清理的大块垃圾"设计的;媒体分组是按类型的盘点直方图(大相册/视频库非可回收垃圾),不应被误显为提示级,
    // 否则健康的媒体库会把治理分压向 0。
    if (state.contains(QStringLiteral("备份完整")) || state.contains(QStringLiteral("媒体分组"))) {
        return FindingSeverity::Normal;
    }
    if (finding.bytes >= 10ULL * 1024ULL * 1024ULL * 1024ULL ||
        state.contains(QStringLiteral("虚拟磁盘")) ||
        state.contains(QStringLiteral("聊天缓存")) ||
        state.contains(QStringLiteral("聊天客户端数据")) ||
        state.contains(QStringLiteral("邮件归档")) ||
        state.contains(QStringLiteral("浏览器缓存")) ||
        state.contains(QStringLiteral("重型启动项"))) {
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
        // 近期大文件多为普通文件;仅当 NTFS 压缩/稀疏(实占<逻辑)时在明细补"逻辑/实占",提醒磁盘实际占用
        // 小于逻辑大小。bytes 保持逻辑大小不变(folderBytes 求和与"增长目录"口径一致、不扰动严重度/稳定键)。
        QString detail = QStringLiteral("近 7 天修改的大文件，可能是空间增长来源。");
        const AllocatedBytes ab = AllocatedBytesOnDisk(file.path);
        if (ab.sparse) {
            detail += QStringLiteral(" 该文件经 NTFS 压缩/稀疏,实占 %1(磁盘实际占用小于逻辑大小)。")
                          .arg(FormatAllocatedText(ab));
        }
        AddFinding(out, FeatureModule::GrowthTrace, file.name, QStringLiteral("近期写入"),
                   detail, file.path, file.bytes);
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
 * @param measuredInstallDirs 跨 hive 累计的“已实测安装目录”键(NormalizeKeyPath 后)。不同
 *        DisplayName 共享同一安装目录时(如某套件主程序与其更新器都指向同一根,或 32/64 位视图
 *        重复登记)只计一次,避免对同一目录重复实测抬升全局 totalBytes —— 系统导出/治理按逐项
 *        finding.bytes 求和且假设各 finding 互斥(见 D1/D2 不变式)。
 * @param cancelFlag 取消标志。
 */
void CollectInstalledSoftwareFromRegistry(const QString& registryRoot, QVector<FeatureFinding>& out,
                                          QSet<QString>& measuredInstallDirs,
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
        QString detail = publisher.isEmpty()
            ? QStringLiteral("来自系统卸载注册表。")
            : QStringLiteral("发布者：%1。").arg(publisher);
        const QString path = installLocation;
        std::uint64_t bytes = 0;

        // 安装目录存在 → 以**实测真实占用**为准。模块名为“软件体积管理器”、描述承诺“安装目录真实占用”,
        // 故目录在就量目录,而非直接采信注册表 EstimatedSize —— 该值由各安装器一次性写入、常不更新且
        // 不含用户数据,系统性偏低(原逻辑把它当主数,仅在其为 0 时才量目录,正是名实不符根因)。注册表
        // 估算值仅在目录缺失/实测为空时回退并注明来源(truth-in-labeling)。标题(name)与状态(已安装软件)
        // 均不变 → v2 稳定键不漂移、严重度关键词无扰动。
        if (PathExists(path)) {
            const QString pathKey = NormalizeKeyPath(path);
            if (!pathKey.isEmpty()) {
                // 去重:不仅精确同目录,还按“目录包含关系”判定(镜像 ComputeStartupProgramFootprint 的
                // seenDirs 逻辑)——若本目录与某已统计目录互为父子(如 Vendor\ 与 Vendor\Helper\,父目录递归
                // 统计已含子目录),则视为同一安装树,体积只在先计入的入口统计,免子树被重复累加进全局
                // totalBytes(系统导出/治理按逐项 bytes 求和假设互斥,见 D1/D2 不变式)。
                const QLatin1Char sep('\\');
                bool nestedWithSeen = false;
                for (const QString& seen : measuredInstallDirs) {
                    if (seen == pathKey || pathKey.startsWith(seen + sep) || seen.startsWith(pathKey + sep)) {
                        nestedWithSeen = true;
                        break;
                    }
                }
                if (nestedWithSeen) {
                    continue;
                }
                measuredInstallDirs.insert(pathKey);
            }
            const PathSizeSummary summary = ComputePathSizeLimited(path, 8000, cancelFlag);
            bytes = summary.bytes;
            if (summary.truncated) {
                detail += QStringLiteral(" 安装目录较大，已按上限估算（实际可能更高）。");
            }
            if (bytes == 0 && estimatedKb > 0) {
                // 目录在但实测为空/不可访问(权限/reparse/已卸载残留空目录),回退注册表估算值并注明。
                bytes = static_cast<std::uint64_t>(estimatedKb) * 1024ULL;
                detail += QStringLiteral(" 目录实测为空或不可访问，体积改用注册表估算值（可能不准）。");
            }
        } else if (estimatedKb > 0) {
            // 无安装目录(便携软件/部分卸载的残留注册表项),仅给注册表估算值并注明。
            bytes = static_cast<std::uint64_t>(estimatedKb) * 1024ULL;
            detail += QStringLiteral(" 无安装目录信息，体积为注册表估算值（可能不准）。");
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
    QSet<QString> measuredInstallDirs;
    CollectInstalledSoftwareFromRegistry(QStringLiteral("HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall"), out, measuredInstallDirs, cancelFlag);
    CollectInstalledSoftwareFromRegistry(QStringLiteral("HKEY_LOCAL_MACHINE\\SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall"), out, measuredInstallDirs, cancelFlag);
    CollectInstalledSoftwareFromRegistry(QStringLiteral("HKEY_CURRENT_USER\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall"), out, measuredInstallDirs, cancelFlag);
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
        const QString detail = QStringLiteral("可先复制到目标盘，再用 junction 保持原路径；当前版本生成计划，不直接执行迁移。"
                                              " 注：多为已安装程序目录，与“软件体积”模块盘点的同一批程序重叠，此处仅按“可迁移大目录”筛选，并非额外占用。");
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
 *
 * 为“长期未动资料”生成**目录级**迁移归档计划:按目录 rollup(归档以目录为单位迁移,而非逐文件),
 * 仅收“整棵子树最近一次修改也早于一年前”的陈旧大目录,按体积降序排列(先处理最大块),并对超出
 * 发射上限的候选明示。原实现扁平枚举文件、按文件系统次序取前 80、不排序、漏掉海量小文件陈旧目录、
 * 且上限静默截断——与“生成归档计划”名实不符。另保留“孤立大陈旧文件”检测(≥100MB 且超一年未改、
 * 且不在任何已发陈旧目录内)以补覆盖率、不回归既有文件级结果。
 *
 * @param out 输出结果。
 * @param sourcePath 源路径。
 * @param targetPath 目标路径。
 * @param cancelFlag 取消标志。
 */
void ScanArchiveAssistant(QVector<FeatureFinding>& out, const QString& sourcePath, const QString& targetPath,
                          std::shared_ptr<std::atomic_bool> cancelFlag) {
    const QString root = PathExists(sourcePath) ? sourcePath : QDir::homePath();
    const qint64 cutoff = QDateTime::currentMSecsSinceEpoch() - 365LL * 86400000LL;
    constexpr std::uint64_t kMinArchiveBytes = 100ULL * 1024ULL * 1024ULL;  // 100 MiB:值得单独归档的最小目录/文件
    constexpr int kMaxDepth = 4;              // 钻取最大深度(防极深目录树耗时)
    constexpr int kMaxDirEmitted = 40;        // 陈旧目录候选发射上限
    constexpr int kMaxTotalEmitted = 80;      // 目录+孤立文件合计发射上限(与既有规模一致)
    constexpr int kMaxVisitedDirs = 40000;    // 钻取访问目录数上限
    constexpr int kMaxVisitedFiles = 200000;  // 孤立文件扫描访问数上限
    const QLatin1Char sep('\\');

    const QString moveNote = targetPath.isEmpty()
        ? QStringLiteral("建议将整个目录移动到归档盘或离线介质。")
        : QStringLiteral("可整体归档到：%1。").arg(QDir::toNativeSeparators(targetPath));

    struct DirCandidate { QString path; std::uint64_t bytes; qint64 latestMtimeMsec; bool truncated; };
    QVector<DirCandidate> dirCandidates;

    // 整个源目录完全陈旧(用户选了某个陈旧文件夹)→ 单一候选,无需钻取,其内文件也全陈旧故无需再扫孤立文件。
    const PathSizeSummary rootSummary = ComputePathSizeLimited(root, 12000, cancelFlag);
    const bool rootFullyStale = rootSummary.bytes >= kMinArchiveBytes
        && rootSummary.latestMtimeMsec > 0 && rootSummary.latestMtimeMsec < cutoff;
    if (rootFullyStale) {
        dirCandidates.append({root, rootSummary.bytes, rootSummary.latestMtimeMsec, rootSummary.truncated});
    } else if (!IsCancelled(cancelFlag)) {
        // 限定深度钻取:<100MB 剪枝不下钻(子目录只会更小);“大且陈旧”收为候选且不再下钻(子树已由本目录
        // 代表,候选天然互斥);仅“大且活跃”才下钻,在其内部找陈旧子部分。每目录仅量一次。
        QStringList currentLevel;
        currentLevel.append(root);
        int visitedDirs = 0;
        for (int depth = 0; depth < kMaxDepth && !currentLevel.isEmpty() && !IsCancelled(cancelFlag); ++depth) {
            QStringList nextLevel;
            for (const QString& dir : currentLevel) {
                if (IsCancelled(cancelFlag) || visitedDirs >= kMaxVisitedDirs) {
                    break;
                }
                ++visitedDirs;
                const QFileInfoList entries = QDir(dir).entryInfoList(
                    QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
                for (const QFileInfo& entry : entries) {
                    if (IsCancelled(cancelFlag)) {
                        break;
                    }
                    const QString childPath = entry.absoluteFilePath();
                    const PathSizeSummary summary = ComputePathSizeLimited(childPath, 12000, cancelFlag);
                    if (summary.bytes < kMinArchiveBytes) {
                        continue;  // 小目录剪枝。
                    }
                    if (summary.latestMtimeMsec > 0 && summary.latestMtimeMsec < cutoff) {
                        dirCandidates.append({childPath, summary.bytes, summary.latestMtimeMsec, summary.truncated});
                    } else {
                        nextLevel.append(childPath);  // 大且活跃 → 下钻找陈旧子部分。
                    }
                }
            }
            currentLevel = nextLevel;
        }
    }

    // 按体积降序发射。钻取已不向陈旧目录下钻故候选本就互斥;此处贪心按包含关系再跳过(保险)防双计进全局
    // totalBytes(系统导出/治理假设各 finding 互斥,见 D1/D2/D3 不变式)。父目录 ≥ 子目录故父先入列。
    std::sort(dirCandidates.begin(), dirCandidates.end(),
              [](const DirCandidate& a, const DirCandidate& b) { return a.bytes > b.bytes; });
    QSet<QString> emittedRoots;
    int emittedDirs = 0;
    int droppedDirCandidates = 0;
    for (const DirCandidate& c : dirCandidates) {
        const QString key = NormalizeKeyPath(c.path);
        bool nested = false;
        for (const QString& seen : emittedRoots) {
            if (seen == key || key.startsWith(seen + sep) || seen.startsWith(key + sep)) {
                nested = true;
                break;
            }
        }
        if (nested) {
            continue;  // 已被更大/更外层候选包含,跳过(不计入 dropped,本就不会发)。
        }
        if (emittedDirs >= kMaxDirEmitted) {
            ++droppedDirCandidates;
            continue;
        }
        emittedRoots.insert(key);
        QString detail;
        if (c.truncated) {
            detail += QStringLiteral("目录较大，体积为下限估算（实际可能更高）。");
        }
        detail += QStringLiteral("整目录最近一次修改距今已超过一年，适合整体归档。") + moveNote;
        AddFinding(out, FeatureModule::ArchiveAssistant, QFileInfo(c.path).fileName(),
                   QStringLiteral("归档候选"), detail, c.path, c.bytes);
        ++emittedDirs;
    }

    // 孤立大陈旧文件(补覆盖率、不回归既有文件级结果):不在任何已发陈旧目录内、≥100MB 且超一年未改。
    // 文件标题(info.fileName())/状态("归档候选")/路径与旧实现一致 → v2 稳定键不漂移,既有标记保留。
    // 文件预算随已发目录数自适应(合计不超 80):已发目录越少文件预算越大,纯文件扫描可发满 80(恢复旧上限),
    // 避免在“无陈旧大目录、全是孤立大陈旧文件”的常见场景下把可操作候选砍半(覆盖率回归)。
    const int fileBudget = std::max(0, kMaxTotalEmitted - emittedDirs);
    int emittedFiles = 0;
    int droppedFileCandidates = 0;
    if (!IsCancelled(cancelFlag)) {
        QDirIterator iterator(root, QDir::Files | QDir::Hidden | QDir::System, QDirIterator::Subdirectories);
        int visitedFiles = 0;
        while (iterator.hasNext() && visitedFiles < kMaxVisitedFiles && !IsCancelled(cancelFlag)) {
            iterator.next();
            ++visitedFiles;
            const QFileInfo info = iterator.fileInfo();
            const std::uint64_t bytes = static_cast<std::uint64_t>(std::max<qint64>(0, info.size()));
            if (bytes < kMinArchiveBytes || info.lastModified().toMSecsSinceEpoch() > cutoff) {
                continue;
            }
            const QString filePath = info.absoluteFilePath();
            const QString normFile = NormalizeKeyPath(filePath);
            bool insideEmittedDir = false;
            for (const QString& seen : emittedRoots) {
                if (normFile.startsWith(seen + sep)) {
                    insideEmittedDir = true;
                    break;
                }
            }
            if (insideEmittedDir) {
                continue;  // 已含于某陈旧目录(体积已计入该目录),免双计。
            }
            if (emittedFiles >= fileBudget) {
                ++droppedFileCandidates;
                continue;
            }
            const QString detail = (targetPath.isEmpty()
                ? QStringLiteral("超过一年未修改的大文件，建议移动到归档盘或离线介质。")
                : QStringLiteral("超过一年未修改的大文件，可归档到：%1。").arg(QDir::toNativeSeparators(targetPath)));
            AddFinding(out, FeatureModule::ArchiveAssistant, info.fileName(), QStringLiteral("归档候选"),
                       detail, filePath, bytes);
            ++emittedFiles;
        }
    }

    if (emittedDirs == 0 && emittedFiles == 0) {
        AddFinding(out, FeatureModule::ArchiveAssistant, QStringLiteral("归档助手"),
                   QStringLiteral("未发现归档候选"),
                   QStringLiteral("所选目录下未发现超过 100 MB 且超过一年未修改的内容。"),
                   root, 0);
    } else if (droppedDirCandidates > 0 || droppedFileCandidates > 0) {
        // 超出发射上限的候选明示(不静默截断,免误以为“只有这些”)。
        AddFinding(out, FeatureModule::ArchiveAssistant, QStringLiteral("更多归档候选"),
                   QStringLiteral("归档候选"),
                   QStringLiteral("另有 %1 个陈旧大目录、%2 个大陈旧文件因较多未逐一列出，可缩小扫描范围或提高阈值后再查。")
                       .arg(droppedDirCandidates).arg(droppedFileCandidates),
                   root, 0);
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
    // 不在此扫描 WeChat Files / Tencent Files:那是聊天客户端目录(聊天接收的文件/图片/视频等用户
    // 数据 + 缓存),归属"聊天缓存"模块(MessengerCache)。原行为会与该模块重复计数,并把聊天接收
    // 文件误归类为"下载整理"。用户仍可通过源路径手动追加任意目录。
    if (PathExists(sourcePath)) {
        roots << sourcePath;
    }
    int emitted = 0;
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
            ++emitted;
        }
    }
    if (emitted == 0) {
        AddFinding(out, FeatureModule::DownloadOrganizer, QStringLiteral("下载整理中心"), QStringLiteral("未发现"),
                   QStringLiteral("下载和桌面目录未发现可整理文件；可在源路径手动追加目录。"));
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
    // OpenSSH 私钥无扩展名(id_rsa / id_ecdsa / id_ed25519 / id_dsa),公钥为 .pub 不敏感。
    // 仅当非 .pub 时按名命中,避免把 id_rsa.pub 等公钥误报为敏感(原 id_rsa 在通用 needle 清单内会误命中 .pub)。
    static const QStringList sshPrivateKeyNames{
        QStringLiteral("id_rsa"), QStringLiteral("id_ecdsa"),
        QStringLiteral("id_ed25519"), QStringLiteral("id_dsa")
    };
    if (suffix != QStringLiteral("pub")) {
        for (const QString& needle : sshPrivateKeyNames) {
            if (name.contains(needle)) {
                return true;
            }
        }
    }
    const QStringList needles{
        QStringLiteral("password"), QStringLiteral("passwd"), QStringLiteral("secret"),
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
    // 凭证目录:其存在本身即暗示密钥/凭证。原仅扫桌面/文档/图片/视频,漏 ~/.ssh 等最典型的私钥位置
    // (id_rsa needle 因 .ssh 未被遍历而成死代码)。补扫兑现"发现密钥"承诺;ExistingPaths 过滤不存在的目录。
    const QString home = QDir::homePath();
    roots << home + QStringLiteral("/.ssh");
    roots << home + QStringLiteral("/.aws");
    roots << home + QStringLiteral("/.gnupg");
    if (PathExists(sourcePath)) {
        roots << sourcePath;
    }
    roots.removeDuplicates();
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
                       QStringLiteral("建议确认存放位置是否安全（避免随同步盘、备份或公开项目外泄）。"),
                       info.absoluteFilePath(), static_cast<std::uint64_t>(std::max<qint64>(0, info.size())));
            ++emitted;
        }
    }

    // 浏览器凭据/密码库:Chromium 与 Firefox 的 profile 目录含巨大缓存子树(GPU Cache/Code Cache/Service Worker),
    // 上面递归 walk(emitted<120/visited<120000 per root)会先撞进缓存耗尽预算却到不了 profile 内的凭据文件
    // (Login Data/Network/Cookies 等)——故不把浏览器 User Data 加进递归根,改用外科式定点探测:枚举 profile
    // 子目录 + 直接按已知文件名 QFileInfo::exists 查。凭据文件虽小(几 KB~MB),但含保存的密码/加密密钥/会话 Cookie。
    // 严重度两级(免多浏览器机 ~20+ 凭据全 Critical 致 GovernanceScore 恒钳 0 失区分力):主密钥/密钥材料(Chromium
    // Local State、Firefox key4.db/key3.db)无之则加密库不可解、最敏感→Critical("敏感文件"含"敏感"→Critical);
    // 加密凭据库(Login Data/Cookies/logins.json 等,加密、缺主密钥即废数据)→Warning("凭据风险"含"风险"不含"敏感")。
    // 与上面 walk 的扫描根(桌面/文档/~.ssh 等,ImportantUserFolders 不含 LOCALAPPDATA/APPDATA)不相交,不产生同文件
    // 双发;但该不相交"依赖"上面 LooksSensitiveFile 对凭据文件名(Login Data/key4.db 等)视而不见——日后若拓宽
    // LooksSensitiveFile 认这些名,须同步从递归 walk 排除浏览器目录,否则同文件经两路径双发(键同则 BuildFindings 去重兜底)。
    {
        static const QHash<QString, QString> credDescriptions{
            // Chromium(每个 profile 内,或 User Data 根下的 Local State 主密钥)
            {QStringLiteral("Login Data"), QStringLiteral("浏览器保存的登录密码（加密存储，配合本机主密钥可解密，外泄即等同密码泄露）")},
            {QStringLiteral("Login Data For Account"), QStringLiteral("浏览器账户同步的登录密码（加密存储）")},
            {QStringLiteral("Web Data"), QStringLiteral("浏览器自动填充数据（含表单与支付信息，加密存储）")},
            {QStringLiteral("Cookies"), QStringLiteral("浏览器会话 Cookies（含登录态，可被复用冒充已登录）")},
            {QStringLiteral("Network/Cookies"), QStringLiteral("浏览器网络服务会话 Cookies（新版 Chromium 迁移至此，含登录态）")},
            {QStringLiteral("Local State"), QStringLiteral("浏览器主密钥文件（用于解密保存的密码/Cookies，严禁外泄或上传）")},
            // Firefox(每个 profile 内)
            {QStringLiteral("logins.json"), QStringLiteral("Firefox 保存的登录信息（加密，配合 key4.db 主密钥可解密）")},
            {QStringLiteral("key4.db"), QStringLiteral("Firefox 主密钥库（用于解密保存的密码，严禁外泄）")},
            {QStringLiteral("key3.db"), QStringLiteral("Firefox 旧版主密钥库（用于解密历史密码）")},
            {QStringLiteral("signons.sqlite"), QStringLiteral("Firefox 旧版密码库（已弃用，仍可能含历史密码）")},
            {QStringLiteral("cert9.db"), QStringLiteral("Firefox 证书库（含客户端证书与 CA 信任）")},
        };
        // 主密钥/密钥材料:Critical("敏感文件");其余加密凭据库 Warning。Warning 级状态须含"风险"且不含"敏感"
        //(否则 SeverityForFinding 先命中"敏感"判 Critical)——故用"凭据风险"。
        static const QSet<QString> masterKeyCreds{
            QStringLiteral("Local State"), QStringLiteral("key4.db"), QStringLiteral("key3.db"),
        };
        const QString localAppData = qEnvironmentVariable("LOCALAPPDATA");
        const QString roamingAppData = qEnvironmentVariable("APPDATA");
        const QString safeTail = QStringLiteral("。建议确认存放位置是否安全（避免随同步盘、备份或公开项目外泄）。");

        // 发射单个凭据文件 finding;文件不存在/非文件/已取消时返回 false 不计数。标题取文件名(Network/Cookies→末段
        // "Cookies");状态按是否主密钥分 Critical("敏感文件")/Warning("凭据风险");明细由文件名查 credDescriptions + 通用安全建议尾。
        auto emitCred = [&](const QString& dir, const QString& rel) -> bool {
            if (IsCancelled(cancelFlag)) {
                return false;
            }
            const QFileInfo fi(dir + QStringLiteral("/") + rel);
            if (!fi.exists() || !fi.isFile()) {
                return false;
            }
            const auto it = credDescriptions.constFind(rel);
            const QString detail = (it != credDescriptions.constEnd() ? it.value() : QStringLiteral("浏览器/客户端本地凭据文件（含敏感数据）")) + safeTail;
            const QString state = masterKeyCreds.contains(rel) ? QStringLiteral("敏感文件") : QStringLiteral("凭据风险");
            AddFinding(out, FeatureModule::PrivacyRadar, fi.fileName(), state, detail,
                       fi.absoluteFilePath(), static_cast<std::uint64_t>(std::max<qint64>(0, fi.size())));
            return true;
        };

        int emittedCreds = 0;
        // Chromium 系标准布局(User Data 根下 Local State 主密钥 + Default/Profile N 等子 profile):Chrome/Edge/Brave/Vivaldi/Yandex。
        const QStringList chromiumRoots{
            localAppData + QStringLiteral("/Google/Chrome/User Data"),
            localAppData + QStringLiteral("/Microsoft/Edge/User Data"),
            localAppData + QStringLiteral("/BraveSoftware/Brave-Browser/User Data"),
            localAppData + QStringLiteral("/Vivaldi/User Data"),
            localAppData + QStringLiteral("/Yandex/YandexBrowser/User Data"),
        };
        const QStringList chromiumRootCreds{ QStringLiteral("Local State") };
        const QStringList chromiumProfileCreds{
            QStringLiteral("Login Data"), QStringLiteral("Login Data For Account"),
            QStringLiteral("Web Data"), QStringLiteral("Cookies"), QStringLiteral("Network/Cookies"),
        };
        for (const QString& root : chromiumRoots) {
            if (emittedCreds >= 120 || IsCancelled(cancelFlag)) {
                break;
            }
            if (!PathExists(root)) {
                continue;
            }
            for (const QString& rel : chromiumRootCreds) {
                if (emittedCreds >= 120 || IsCancelled(cancelFlag)) {
                    break;
                }
                if (emitCred(root, rel)) {
                    ++emittedCreds;
                }
            }
            // 仅扫已知 profile 名(避免遍历 GrShaderCache/Snapshots/Crashpad 等非 profile 子目录);"."/".."/其它
            // 目录名一律被 isProfile 拒绝,即便 entryList 返回也无害。
            const QStringList profiles = QDir(root).entryList(QDir::Dirs | QDir::Hidden, QDir::Name);
            for (const QString& profile : profiles) {
                if (emittedCreds >= 120 || IsCancelled(cancelFlag)) {
                    break;
                }
                const bool isProfile = (profile == QStringLiteral("Default"))
                    || profile.startsWith(QStringLiteral("Profile "))
                    || profile == QStringLiteral("Guest Profile")
                    || profile == QStringLiteral("System Profile");
                if (!isProfile) {
                    continue;
                }
                const QString profilePath = root + QStringLiteral("/") + profile;
                for (const QString& rel : chromiumProfileCreds) {
                    if (emittedCreds >= 120 || IsCancelled(cancelFlag)) {
                        break;
                    }
                    if (emitCred(profilePath, rel)) {
                        ++emittedCreds;
                    }
                }
            }
        }

        // Opera / Opera GX(Chromium 内核但布局不同):无 User Data 父级,profile 目录即 ".../Opera Stable" /
        // ".../Opera GX Stable",Local State 与 Login Data/Cookies 等凭据文件**直接**在其内(无 per-profile 枚举)。
        const QStringList operaProfileCreds{
            QStringLiteral("Local State"), QStringLiteral("Login Data"), QStringLiteral("Login Data For Account"),
            QStringLiteral("Web Data"), QStringLiteral("Cookies"), QStringLiteral("Network/Cookies"),
        };
        const QStringList operaRoots{
            roamingAppData + QStringLiteral("/Opera Software/Opera Stable"),
            roamingAppData + QStringLiteral("/Opera Software/Opera GX Stable"),
        };
        for (const QString& root : operaRoots) {
            if (emittedCreds >= 120 || IsCancelled(cancelFlag)) {
                break;
            }
            if (!PathExists(root)) {
                continue;
            }
            for (const QString& rel : operaProfileCreds) {
                if (emittedCreds >= 120 || IsCancelled(cancelFlag)) {
                    break;
                }
                if (emitCred(root, rel)) {
                    ++emittedCreds;
                }
            }
        }

        // Firefox:Profiles 根下每个随机名目录即一个 profile,直接 glob(无需解析 profiles.ini)。Release 与 Developer
        // Edition / Nightly 各用独立 Profiles 目录(Firefox 67+ 多频道隔离),Beta 共用 Release 的(已覆盖)。
        const QStringList firefoxRoots{
            roamingAppData + QStringLiteral("/Mozilla/Firefox/Profiles"),
            roamingAppData + QStringLiteral("/Mozilla/Firefox/Developer Edition/Profiles"),
            roamingAppData + QStringLiteral("/Mozilla/Firefox/Nightly/Profiles"),
        };
        const QStringList firefoxProfileCreds{
            QStringLiteral("logins.json"), QStringLiteral("key4.db"), QStringLiteral("key3.db"),
            QStringLiteral("signons.sqlite"), QStringLiteral("cert9.db"),
        };
        for (const QString& firefoxRoot : firefoxRoots) {
            if (emittedCreds >= 120 || IsCancelled(cancelFlag)) {
                break;
            }
            if (!PathExists(firefoxRoot)) {
                continue;
            }
            const QStringList ffProfiles = QDir(firefoxRoot).entryList(QDir::Dirs, QDir::Name);
            for (const QString& profile : ffProfiles) {
                if (emittedCreds >= 120 || IsCancelled(cancelFlag)) {
                    break;
                }
                const QString profilePath = firefoxRoot + QStringLiteral("/") + profile;
                for (const QString& rel : firefoxProfileCreds) {
                    if (emittedCreds >= 120 || IsCancelled(cancelFlag)) {
                        break;
                    }
                    if (emitCred(profilePath, rel)) {
                        ++emittedCreds;
                    }
                }
            }
        }
    }
}

/**
 * @brief 判断目录名是否为开发空间热点。
 * @param name 目录名。
 * @return 命中开发热点时返回 true。
 */
bool IsDeveloperHotDirectory(const QString& name) {
    // 原集合只覆盖 node_modules/venv/构建产物 + .m2/.cargo/.gradle/.pnpm-store,遗漏多数最占空间的 Windows
    // 开发包缓存。补充:.nuget(NuGet 全局包)、.npm(npm 缓存)、.cache(pip/go-build 等通用缓存)、.yarn、
    // go-build、vendor(Go/PHP 依赖)、.sbt/.ivy2(Scala),兑现描述"包缓存"承诺。__pycache__ 等恒小目录不计入
    // (永远到不了 100 MiB 阈值,只会徒增无效 size 遍历)。
    static const QSet<QString> names{
        QStringLiteral("node_modules"), QStringLiteral(".venv"), QStringLiteral("venv"), QStringLiteral("target"),
        QStringLiteral("build"), QStringLiteral("dist"), QStringLiteral(".next"), QStringLiteral(".gradle"),
        QStringLiteral(".m2"), QStringLiteral(".cargo"), QStringLiteral(".pnpm-store"), QStringLiteral("packages"),
        QStringLiteral(".nuget"), QStringLiteral(".npm"), QStringLiteral(".cache"), QStringLiteral(".yarn"),
        QStringLiteral("go-build"), QStringLiteral("vendor"), QStringLiteral(".sbt"), QStringLiteral(".ivy2")
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
 * @brief 一个 WSL2 发行版的虚拟磁盘信息(Lxss 注册表解析结果)。
 */
struct WslDistroVhdx {
    /** 发行版名称(DistributionName;缺失时回退为注册表子键名)。 */
    QString distroName;
    /** 解析出的根文件系统 vhdx 路径(无法解析时为空,调用方应跳过)。 */
    QString vhdxPath;
    /** 是否为 Docker Desktop 的内部发行版(docker-desktop / docker-desktop-data)。 */
    bool isDockerData = false;
};

/**
 * @brief 枚举本机已注册的 WSL2 发行版及其虚拟磁盘。
 * @param cancelFlag 取消标志。
 * @return 发行版列表(含 Docker Desktop 的内部发行版)。
 *
 * 读取 HKCU\Software\Microsoft\Windows\CurrentVersion\Lxss 下每个 {GUID} 子键的 DistributionName /
 * BasePath / VhdFileName,组合出 ext4.vhdx 路径。这是 WSL2 的权威来源:Store 版 Ubuntu/Debian 等用户
 * 发行版,以及 Docker Desktop 的 docker-desktop / docker-desktop-data 两个内部发行版,都在此登记。
 * 纯本地只读注册表访问,不联网、不启动 wsl.exe。BasePath 可能带 \\?\ 长路径前缀,QFileInfo 与
 * GetCompressedFileSizeW 均支持,直接透传。
 */
QVector<WslDistroVhdx> EnumerateWslDistros(std::shared_ptr<std::atomic_bool> cancelFlag) {
    QVector<WslDistroVhdx> distros;
    QSettings lxss(
        QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Lxss"),
        QSettings::NativeFormat);
    const QStringList guids = lxss.childGroups();
    for (const QString& guid : guids) {
        if (IsCancelled(cancelFlag)) {
            break;
        }
        lxss.beginGroup(guid);
        const QString name = lxss.value(QStringLiteral("DistributionName")).toString();
        const QString basePath = lxss.value(QStringLiteral("BasePath")).toString();
        const QString vhdName = lxss.value(QStringLiteral("VhdFileName")).toString();
        lxss.endGroup();
        if (basePath.isEmpty()) {
            continue;  // 无 BasePath 的子键不是有效发行版。
        }
        // 解析 vhdx 路径:BasePath + VhdFileName(缺省 ext4.vhdx);失败依次回退到缺省名 / BasePath 本身。
        const QString vhd = vhdName.isEmpty() ? QStringLiteral("ext4.vhdx") : vhdName;
        QString vhdxPath = basePath + QStringLiteral("\\") + vhd;
        if (!QFileInfo::exists(vhdxPath)) {
            const QString defaultName = basePath + QStringLiteral("\\ext4.vhdx");
            if (vhdName.compare(QStringLiteral("ext4.vhdx"), Qt::CaseInsensitive) != 0 && QFileInfo::exists(defaultName)) {
                vhdxPath = defaultName;
            } else if (basePath.endsWith(QStringLiteral(".vhdx"), Qt::CaseInsensitive) && QFileInfo::exists(basePath)) {
                vhdxPath = basePath;  // 极少数布局:BasePath 直接就是 vhdx 文件。
            } else {
                vhdxPath.clear();  // 无法解析,跳过(不臆造体积)。
            }
        }

        WslDistroVhdx d;
        d.distroName = name.isEmpty() ? guid : name;
        d.vhdxPath = vhdxPath;
        const QString lower = d.distroName.toCaseFolded();
        d.isDockerData = (lower == QStringLiteral("docker-desktop") ||
                          lower == QStringLiteral("docker-desktop-data") ||
                          lower == QStringLiteral("docker-desktop-proxy"));
        distros.append(d);
    }
    return distros;
}

/**
 * @brief 扫描 Docker 和 WSL 空间管理模块。
 * @param out 输出结果。
 * @param cancelFlag 取消标志。
 *
 * 原实现仅盲扫候选目录下的 *.vhdx(逻辑大小)并把 docker.exe 探测当占位提示,既不知某个 vhdx 属于哪个
 * WSL 发行版 / Docker,又因逻辑大小高估占用。本实现:① 以 Lxss 注册表为权威来源枚举 WSL2 发行版(含
 * Docker Desktop 的内部发行版),按发行版 / Docker 归属命名并用 AllocatedBytesOnDisk 核算真实占用;
 * ② 对未登记的 Docker 目录 vhdx 做去重兜底扫描;③ 探测 Docker Engine(Windows 服务)的镜像层目录
 * (daemon.json 的 data-root 或 %ProgramData%\docker);④ docker.exe 探测改为如实告知可用 docker system df
 * 自查可回收明细——本工具不自动执行该子进程,以免弹出控制台窗口并依赖守护进程在线。全程只读,绝不 prune。
 */
void ScanDockerWsl(QVector<FeatureFinding>& out, std::shared_ptr<std::atomic_bool> cancelFlag) {
    QSet<QString> seenVhdx;  // 模块内去重(本函数内);跨模块去重见 BuildFindings 末尾。

    // ① WSL2 发行版(Lxss 注册表权威来源;含 Docker Desktop 内部发行版)。
    const QVector<WslDistroVhdx> distros = EnumerateWslDistros(cancelFlag);
    for (const WslDistroVhdx& d : distros) {
        if (IsCancelled(cancelFlag)) {
            break;
        }
        if (d.vhdxPath.isEmpty()) {
            continue;
        }
        seenVhdx << NormalizeDiskPathKey(d.vhdxPath);
        const AllocatedBytes ab = AllocatedBytesOnDisk(d.vhdxPath);
        const QString title = d.isDockerData ? QStringLiteral("Docker Desktop 数据盘") : d.distroName;
        // 状态含"虚拟磁盘"以命中 SeverityForFinding 的 Notice 关键词:vhdx 改报实占(allocated)后,
        // 稀疏盘可能跌破 10GB 升 Notice 的字节阈值,此处靠关键词保持与通用 vhdx 一致的提示级可见性。
        const QString state = d.isDockerData ? QStringLiteral("Docker 虚拟磁盘") : QStringLiteral("WSL 虚拟磁盘");
        const QString reclaimHint = d.isDockerData
            ? QStringLiteral("Docker 的镜像 / 容器 / 卷 / 构建缓存存储于该 WSL2 虚拟磁盘内。"
                             "可用 docker system df 查看可回收明细,按需 docker system prune;压缩磁盘需先 wsl --shutdown。")
            : QStringLiteral("WSL2 发行版 %1 的根文件系统虚拟磁盘。其内部空闲空间可在 wsl --terminate %1 "
                             "(或 wsl --shutdown)后用 Optimize-VHD / diskpart 的 compact vdisk 压缩回收。").arg(d.distroName);
        const QString detail = reclaimHint + QStringLiteral(" 实占 %1。实际可回收量取决于虚拟盘内部空闲度,"
                                                            "此处为当前占用而非可回收保证。").arg(FormatAllocatedText(ab));
        AddFinding(out, FeatureModule::DockerWsl, title, state, detail,
                   QDir::toNativeSeparators(d.vhdxPath), ab.allocated);
    }

    // ② 兜底:未在 Lxss 登记的 Docker 目录 vhdx(旧版 / 异常布局),与 ① 去重。
    const QString localAppData = qEnvironmentVariable("LOCALAPPDATA");
    if (!localAppData.isEmpty()) {
        const QString dockerDir = localAppData + QStringLiteral("/Docker");
        if (QFileInfo::exists(dockerDir) && !IsCancelled(cancelFlag)) {
            QDirIterator iterator(dockerDir, QStringList{QStringLiteral("*.vhdx")},
                                  QDir::Files | QDir::Hidden | QDir::System, QDirIterator::Subdirectories);
            while (iterator.hasNext() && !IsCancelled(cancelFlag)) {
                iterator.next();
                const QFileInfo info = iterator.fileInfo();
                const QString key = NormalizeDiskPathKey(info.absoluteFilePath());
                if (seenVhdx.contains(key)) {
                    continue;  // 已由 Lxss 枚举归属,跳过避免重复计费。
                }
                seenVhdx << key;
                const AllocatedBytes ab = AllocatedBytesOnDisk(info.absoluteFilePath());
                AddFinding(out, FeatureModule::DockerWsl, info.fileName(), QStringLiteral("Docker 虚拟磁盘"),
                           QStringLiteral("Docker 相关虚拟磁盘,未在 WSL 注册表中登记(可能为旧版布局)。"
                                          "可用 docker system df 查看明细;压缩磁盘需先 wsl --shutdown。实占 %1。")
                               .arg(FormatAllocatedText(ab)),
                           QDir::toNativeSeparators(info.absoluteFilePath()), ab.allocated);
            }
        }
    }

    // ③ Docker Engine(Windows 服务,非 Desktop)镜像层:daemon.json 的 data-root 覆盖时用之,否则默认
    //    %ProgramData%\docker。仅本地只读 JSON / 目录体积核算,不启动 dockerd。
    const QString programData = qEnvironmentVariable("ProgramData");
    if (!programData.isEmpty()) {
        const QString dockerRoot = programData + QStringLiteral("/docker");
        QString dataRoot = dockerRoot;
        QFile daemonConfig(dockerRoot + QStringLiteral("/config/daemon.json"));
        if (daemonConfig.open(QIODevice::ReadOnly)) {
            QByteArray raw = daemonConfig.readAll();
            // QJsonDocument::fromJson 不跳过 UTF-8 BOM(EF BB BF),带 BOM 的 daemon.json(手编 / 记事本
            // 另存)会被解析成空文档,从而漏读自定义 data-root。剥离首部 BOM 后再解析。
            if (raw.startsWith("\xEF\xBB\xBF")) {
                raw.remove(0, 3);
            }
            const QJsonDocument doc = QJsonDocument::fromJson(raw);
            const QJsonValue dataRootValue = doc.object().value(QStringLiteral("data-root"));
            if (dataRootValue.isString()) {
                const QString custom = dataRootValue.toString().trimmed();
                if (!custom.isEmpty()) {
                    dataRoot = custom;
                }
            }
        }
        if (QFileInfo::exists(dataRoot) && !IsCancelled(cancelFlag)) {
            const QString windowsFilter = dataRoot + QStringLiteral("/windowsfilter");
            const QString sizeTarget = QFileInfo::exists(windowsFilter) ? windowsFilter : dataRoot;
            const PathSizeSummary summary = ComputePathSizeLimited(sizeTarget, 200000, cancelFlag);
            if (summary.bytes > 0 || summary.files > 0) {
                const QString truncNote = summary.truncated ? QStringLiteral("(部分枚举,实占为下限)") : QString();
                AddFinding(out, FeatureModule::DockerWsl,
                           QStringLiteral("Docker Engine 数据"), QStringLiteral("Docker Engine 数据"),
                           QStringLiteral("Docker Engine(Windows 服务)的镜像 / 容器层位于 %1,实占 %2%3。"
                                          "可用 docker system df / docker system prune 管理。")
                               .arg(QDir::toNativeSeparators(sizeTarget), FormatBytesText(summary.bytes), truncNote),
                           QDir::toNativeSeparators(sizeTarget), summary.bytes);
            }
        }
    }

    // ④ docker.exe 探测:如实告知可用 docker system df 自查,不自动执行子进程(避免控制台窗口与守护进程依赖)。
    const QString dockerExe = QStandardPaths::findExecutable(QStringLiteral("docker.exe"));
    if (dockerExe.isEmpty()) {
        AddFinding(out, FeatureModule::DockerWsl, QStringLiteral("Docker CLI"), QStringLiteral("未检测到"),
                   QStringLiteral("未在 PATH 中找到 docker.exe。若已安装 Docker,WSL 发行版与数据盘仍按磁盘文件枚举。"));
    } else {
        AddFinding(out, FeatureModule::DockerWsl, QStringLiteral("Docker CLI"), QStringLiteral("可用"),
                   QStringLiteral("检测到 %1。可用 docker system df 查看镜像 / 容器 / 卷 / 构建缓存的可回收明细。"
                                  "本工具不自动执行该命令,以免弹出控制台窗口并依赖守护进程在线。").arg(dockerExe));
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
    // cache2/startupcache 是 Firefox 的实际磁盘缓存目录名(原 name 集只有 "cache",Firefox cache2 被静默漏掉,
    // 与"识别 Firefox"承诺不符)。cache2 为 Firefox 主磁盘缓存,startupcache 为启动缓存。
    static const QSet<QString> names{
        QStringLiteral("cache"), QStringLiteral("cache2"), QStringLiteral("startupcache"),
        QStringLiteral("code cache"), QStringLiteral("gpucache"),
        QStringLiteral("cachestorage"), QStringLiteral("shadercache"), QStringLiteral("blob_storage"),
        QStringLiteral("indexeddb")
    };
    return names.contains(foldedName) || foldedPath.contains(QStringLiteral("/service worker/cachestorage"));
}

/**
 * @brief 由缓存目录路径推断所属浏览器名称(供标题区分,兑现"识别 Chrome、Edge、Firefox")。
 * @param absolutePath 缓存目录绝对路径。
 * @return 浏览器短名;无法判定返回"浏览器"。
 *
 * Chromium 系(Chrome/Edge/Brave)共用 "User Data/Default/..." 布局,逐项标题原本只有目录名(Cache/GPUCache)
 * 无法区分浏览器;Firefox 在 Mozilla/Firefox/Profiles 下。按路径特征给出浏览器名,标题改为"目录名（浏览器）"。
 */
QString BrowserLabelFromPath(const QString& absolutePath) {
    const QString folded = QDir::fromNativeSeparators(absolutePath).toCaseFolded();
    if (folded.contains(QStringLiteral("/google/chrome/"))) {
        return QStringLiteral("Chrome");
    }
    if (folded.contains(QStringLiteral("/microsoft/edge/"))) {
        return QStringLiteral("Edge");
    }
    if (folded.contains(QStringLiteral("/bravesoftware/"))) {
        return QStringLiteral("Brave");
    }
    if (folded.contains(QStringLiteral("/mozilla/firefox/"))) {
        return QStringLiteral("Firefox");
    }
    return QStringLiteral("浏览器");
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
            // 标题保持 fileName() 不变(稳定键 v2|module|title|path 含 title,改标题会使既有忽略/已处理/基线/备注
            // 标记失配漂移)。浏览器归属放明细前缀,既区分 Chrome/Edge/Brave/Firefox 又不破坏标记稳定性。
            AddFinding(out, FeatureModule::BrowserCache, info.fileName(), QStringLiteral("浏览器缓存"),
                       QStringLiteral("（%1）建议在浏览器设置中清理缓存或站点离线数据，避免直接删除整个用户配置。")
                           .arg(BrowserLabelFromPath(info.absoluteFilePath())),
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
 * @brief 解析 Windows 快捷方式(.lnk)的目标路径(只读,不弹 UI、不联网)。
 * @param lnkPath 快捷方式文件路径。
 * @return 目标可执行文件路径;解析失败或入参为空返回空串。
 */
QString ResolveLnkTarget(const QString& lnkPath) {
    if (lnkPath.isEmpty()) {
        return {};
    }
    const QString native = QDir::toNativeSeparators(lnkPath);
    // 扫描跑在后台 std::thread 上,Qt 未为其初始化 COM 套间,这里按需初始化(线程内 ref-counted)。
    const HRESULT hrInit = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool weInitialized = (hrInit == S_OK);  // S_FALSE=本线程已初始化,不重复 Uninitialize。
    QString target;
    IShellLinkW* shellLink = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&shellLink));
    if (SUCCEEDED(hr) && shellLink != nullptr) {
        IPersistFile* persistFile = nullptr;
        hr = shellLink->QueryInterface(IID_PPV_ARGS(&persistFile));
        if (SUCCEEDED(hr) && persistFile != nullptr) {
            hr = persistFile->Load(reinterpret_cast<LPCOLESTR>(native.utf16()), STGM_READ);
            if (SUCCEEDED(hr)) {
                WCHAR path[MAX_PATH] = {};
                WIN32_FIND_DATAW findData{};
                // SLGP_RAWPATH 取原始存储路径;不调用 Resolve(避免可能的弹窗/网络/慢解析),快速只读。
                hr = shellLink->GetPath(path, MAX_PATH, &findData, SLGP_RAWPATH);
                if (SUCCEEDED(hr) && path[0] != L'\0') {
                    target = QString::fromWCharArray(path);
                }
            }
            persistFile->Release();
        }
        shellLink->Release();
    }
    if (weInitialized) {
        CoUninitialize();
    }
    return target;
}

/**
 * @brief 启动项"关联程序占用"计算结果。
 */
struct StartupProgramFootprint {
    /** 程序所在目录的占用字节数(excluded/duplicate 时为 0)。 */
    std::uint64_t bytes = 0;
    /** 程序所在目录(程序本体目录,exe 的直属父目录)。 */
    QString installDir;
    /** 目录是否因条目上限被截断(bytes 为估算下限)。 */
    bool truncated = false;
    /** 是否系统组件(System32/SysWOW64/Windows 根),不计入用户程序占用。 */
    bool excluded = false;
    /** 是否与既有启动入口同属一个程序目录(体积已计入该项,不重复统计)。 */
    bool duplicate = false;
};

/**
 * @brief 对解析出的可执行文件,统计其安装目录(程序本体目录)体积作为"关联程序占用"。
 *
 * 原 StartupFootprint 只量 .lnk(~1KB)或裸 exe 单文件,数百 MB 的自更新器/同步客户端启动项会被报成 1KB。
 * 本函数改为统计 exe 所在安装目录,更贴近"开机自启程序占用多少盘空间"。系统目录(System32/SysWOW64/
 * Windows 根)予以排除(统计它们会得到荒谬体积且与用户程序占用无关);同一程序目录只在首个入口计入体积。
 * @param exePath 可执行文件路径。
 * @param cancelFlag 取消标志。
 * @param seenDirs 已统计过的安装目录(跨文件夹+注册表共享去重);命中则 duplicate=true。
 * @return 体积结果。
 */
StartupProgramFootprint ComputeStartupProgramFootprint(const QString& exePath,
                                                       std::shared_ptr<std::atomic_bool> cancelFlag,
                                                       QSet<QString>& seenDirs) {
    StartupProgramFootprint fp;
    const QFileInfo info(exePath);
    if (exePath.isEmpty() || !info.exists() || !info.isFile()) {
        return fp;
    }
    const QString installDir = QDir::toNativeSeparators(info.absolutePath());
    const QString key = installDir.toCaseFolded();
    const QString systemRoot = qEnvironmentVariable("SystemRoot");
    const QString winDir = QDir::toNativeSeparators(
        systemRoot.isEmpty() ? QStringLiteral("C:\\Windows") : systemRoot).toCaseFolded();
    // 系统目录:凡位于 Windows 根或其任意子目录(system32/syswow64/Installer/assembly/Temp/OEM 等)的启动器
    // 均视为系统组件——统计这些目录(尤其 Installer)既慢又会得到与"用户程序占用"无关的庞大体积。
    const bool isSystem = (key == winDir || key.startsWith(winDir + QStringLiteral("\\")));
    if (isSystem) {
        fp.excluded = true;
        fp.installDir = installDir;
        return fp;  // 系统组件不计体积,也不进 seenDirs。
    }
    // 去重:不仅精确同目录,还按"目录包含关系"判定——若本目录与某已统计目录互为父子(如 App\ 与
    // App\updater\,父目录递归统计已含子目录),则视为同一程序,体积只在先计入的入口统计,避免子树被重复累加。
    const QLatin1Char sep('\\');
    for (const QString& seen : seenDirs) {
        if (seen == key || key.startsWith(seen + sep) || seen.startsWith(key + sep)) {
            fp.duplicate = true;
            fp.installDir = installDir;
            return fp;  // 与既有启动入口的安装目录相同或存在包含关系,不重复统计。
        }
    }
    seenDirs.insert(key);
    fp.installDir = installDir;
    const PathSizeSummary summary = ComputePathSizeLimited(installDir, 8000, cancelFlag);
    fp.bytes = summary.bytes;
    fp.truncated = summary.truncated;
    return fp;
}

/**
 * @brief 按"关联程序占用"体积给出启动项分级状态标签。
 * @param programBytes 程序目录占用字节。
 * @param excluded 系统组件。
 * @param duplicate 与既有项同目录。
 * @param unresolved 无法解析目标程序。
 * @return 状态文案。
 */
QString StartupFootprintState(std::uint64_t programBytes, bool excluded, bool duplicate, bool unresolved) {
    if (unresolved) {
        return QStringLiteral("启动项（无法解析）");
    }
    if (excluded) {
        return QStringLiteral("启动项（系统组件）");
    }
    if (duplicate) {
        return QStringLiteral("启动项（同程序）");
    }
    if (programBytes >= 100ULL * 1024ULL * 1024ULL) {
        return QStringLiteral("重型启动项");
    }
    if (programBytes >= 10ULL * 1024ULL * 1024ULL) {
        return QStringLiteral("中等启动项");
    }
    return QStringLiteral("轻量启动项");
}

/**
 * @brief 扫描启动文件夹中的启动入口。
 * @param out 输出结果。
 * @param folder 启动文件夹路径。
 * @param cancelFlag 取消标志。
 * @param seenDirs 跨入口共享的已统计程序目录集合(去重)。
 */
void ScanStartupFolder(QVector<FeatureFinding>& out, const QString& folder, std::shared_ptr<std::atomic_bool> cancelFlag,
                       QSet<QString>& seenDirs) {
    if (!PathExists(folder)) {
        return;
    }
    QDirIterator iterator(folder, QDir::Files | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
    while (iterator.hasNext() && !IsCancelled(cancelFlag)) {
        iterator.next();
        const QFileInfo info = iterator.fileInfo();
        QString executable;
        bool unresolved = false;
        if (info.suffix().toCaseFolded() == QLatin1String("lnk")) {
            executable = ResolveLnkTarget(info.absoluteFilePath());
            if (executable.isEmpty()) {
                unresolved = true;  // 快捷方式无法解析目标。
            }
        } else {
            executable = info.absoluteFilePath();  // 非 lnk:直接作为程序文件处理。
        }
        // 解析到目录(如快捷方式指向文件夹)或路径不存在,同样视为无法解析。
        if (!unresolved && !executable.isEmpty()) {
            const QFileInfo targetInfo(executable);
            if (!targetInfo.exists() || !targetInfo.isFile()) {
                unresolved = true;
            }
        }
        const StartupProgramFootprint fp = ComputeStartupProgramFootprint(executable, cancelFlag, seenDirs);
        const QString state = StartupFootprintState(fp.bytes, fp.excluded, fp.duplicate, unresolved);
        QString detail;
        std::uint64_t bytes = 0;
        if (unresolved) {
            detail = QStringLiteral("开机启动文件夹入口，无法解析出可执行的目标程序，不计体积。可在系统启动管理中禁用。");
        } else if (fp.excluded) {
            detail = QStringLiteral("开机启动文件夹入口，程序位于系统目录（%1），不计入用户程序占用。").arg(fp.installDir);
        } else if (fp.duplicate) {
            detail = QStringLiteral("开机启动文件夹入口，与上方某启动项同属程序目录（%1），体积已计入该项，不重复统计。").arg(fp.installDir);
        } else {
            detail = QStringLiteral("开机启动文件夹入口，关联程序目录：%1，约 %2%3；注：该目录与“软件体积管理器”盘点的同一批已安装程序重叠，此处仅反映该程序是否随开机自启，并非额外占用。")
                         .arg(fp.installDir, FormatBytesText(fp.bytes),
                              fp.truncated ? QStringLiteral("（条目较多，为估算下限）") : QString());
            bytes = fp.bytes;
        }
        AddFinding(out, FeatureModule::StartupFootprint, info.fileName(), state, detail, info.absoluteFilePath(), bytes);
    }
}

/**
 * @brief 扫描注册表 Run 启动项。
 * @param out 输出结果。
 * @param registryRoot 注册表 Run 项路径。
 * @param label 结果来源标签。
 * @param cancelFlag 取消标志。
 * @param seenDirs 跨入口共享的已统计程序目录集合(去重)。
 */
void ScanStartupRegistry(QVector<FeatureFinding>& out, const QString& registryRoot, const QString& label,
                         std::shared_ptr<std::atomic_bool> cancelFlag, QSet<QString>& seenDirs) {
    QSettings registry(registryRoot, QSettings::NativeFormat);
    const QStringList keys = registry.allKeys();
    for (const QString& key : keys) {
        if (IsCancelled(cancelFlag)) {
            break;
        }
        const QString command = registry.value(key).toString();
        if (key == QLatin1String(".") || command.trimmed().isEmpty()) {
            continue;  // 跳过默认值与空命令,避免误报"无法解析"。
        }
        const QString executable = ExecutablePathFromCommand(command);
        const StartupProgramFootprint fp = ComputeStartupProgramFootprint(executable, cancelFlag, seenDirs);
        const QFileInfo exeInfo(executable);
        const bool unresolved = executable.isEmpty() || !exeInfo.exists() || !exeInfo.isFile();
        const QString state = StartupFootprintState(fp.bytes, fp.excluded, fp.duplicate, unresolved);
        QString detail = QStringLiteral("启动命令：%1").arg(command);
        if (unresolved) {
            detail += QStringLiteral("；未能解析出有效的可执行文件路径，不计体积。");
        } else if (fp.excluded) {
            detail += QStringLiteral("；程序位于系统目录（%1），不计入用户程序占用。").arg(fp.installDir);
        } else if (fp.duplicate) {
            detail += QStringLiteral("；与上方某启动项同属程序目录（%1），体积已计入该项，不重复统计。").arg(fp.installDir);
        } else {
            detail += QStringLiteral("；关联程序目录：%1，约 %2%3；注：该目录与“软件体积管理器”盘点的同一批已安装程序重叠，此处仅反映该程序是否随开机自启，并非额外占用。")
                          .arg(fp.installDir, FormatBytesText(fp.bytes),
                               fp.truncated ? QStringLiteral("（条目较多，为估算下限）") : QString());
        }
        const QString entryPath = unresolved ? executable : exeInfo.absoluteFilePath();
        AddFinding(out, FeatureModule::StartupFootprint, QStringLiteral("%1 · %2").arg(label, key),
                   state, detail, entryPath, fp.bytes);
    }
}

/**
 * @brief 扫描启动项体积检查模块。
 * @param out 输出结果。
 * @param cancelFlag 取消标志。
 */
void ScanStartupFootprint(QVector<FeatureFinding>& out, std::shared_ptr<std::atomic_bool> cancelFlag) {
    // 跨文件夹+注册表共享:同一程序目录只在首个入口计入体积,避免重复计费。
    QSet<QString> seenDirs;
    const QString appData = qEnvironmentVariable("APPDATA");
    const QString programData = qEnvironmentVariable("ProgramData");
    ScanStartupFolder(out, appData + QStringLiteral("/Microsoft/Windows/Start Menu/Programs/Startup"), cancelFlag, seenDirs);
    ScanStartupFolder(out, programData + QStringLiteral("/Microsoft/Windows/Start Menu/Programs/Startup"), cancelFlag, seenDirs);

    ScanStartupRegistry(out, QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"),
                        QStringLiteral("当前用户"), cancelFlag, seenDirs);
    ScanStartupRegistry(out, QStringLiteral("HKEY_LOCAL_MACHINE\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"),
                        QStringLiteral("全局"), cancelFlag, seenDirs);
    ScanStartupRegistry(out, QStringLiteral("HKEY_LOCAL_MACHINE\\Software\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Run"),
                        QStringLiteral("全局 32 位"), cancelFlag, seenDirs);
    // 补 RunOnce(一次性自启,执行后自删)与 Explorer\Run(资源管理器启动后自启)两类常见自启向量。
    ScanStartupRegistry(out, QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\RunOnce"),
                        QStringLiteral("当前用户一次性"), cancelFlag, seenDirs);
    ScanStartupRegistry(out, QStringLiteral("HKEY_LOCAL_MACHINE\\Software\\Microsoft\\Windows\\CurrentVersion\\RunOnce"),
                        QStringLiteral("全局一次性"), cancelFlag, seenDirs);
    ScanStartupRegistry(out, QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Run"),
                        QStringLiteral("当前用户资源管理器"), cancelFlag, seenDirs);

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

        /**
         * @brief 是否为"用户数据型"目录:整目录含聊天接收的文件/图片/视频等需保留内容,不可当纯缓存整目录删除。
         */
        bool isUserData = false;
    };

    const QString localAppData = qEnvironmentVariable("LOCALAPPDATA");
    const QString roamingAppData = qEnvironmentVariable("APPDATA");
    const QString documents = StandardPathOrHome(QStandardPaths::DocumentsLocation);
    QVector<CacheCandidate> candidates{
        {QStringLiteral("微信文件"), documents + QStringLiteral("/WeChat Files"), true},
        {QStringLiteral("微信文件（4.x）"), documents + QStringLiteral("/xwechat_files"), true},
        {QStringLiteral("企业微信文件"), documents + QStringLiteral("/WXWork"), true},
        {QStringLiteral("QQ 文件"), documents + QStringLiteral("/Tencent Files"), true},
        {QStringLiteral("微信配置缓存"), roamingAppData + QStringLiteral("/Tencent/WeChat"), false},
        {QStringLiteral("QQ 缓存"), roamingAppData + QStringLiteral("/Tencent/QQ"), false},
        {QStringLiteral("Microsoft Teams"), roamingAppData + QStringLiteral("/Microsoft/Teams"), false},
        {QStringLiteral("新版 Teams"), localAppData + QStringLiteral("/Packages/MSTeams_8wekyb3d8bbwe"), false},
        {QStringLiteral("Slack"), roamingAppData + QStringLiteral("/Slack"), false},
        {QStringLiteral("Discord"), roamingAppData + QStringLiteral("/discord"), false},
        {QStringLiteral("Telegram Desktop"), roamingAppData + QStringLiteral("/Telegram Desktop"), false},
        // 以下四类客户端目录经多源 web 核实(知乎/CSDN/cnblogs/官方帮助中心/取证文章),路径均为 Windows 本地
        // 真实位置;PathExists 过滤不存在的目录,故版本/语言差异路径缺席即跳过,不产生误报。nature 按是否含
        // 不可替代用户数据保守标注:接收文件/聊天库/加密密钥目录标 data(勿整目录删),仅纯 Chromium 缓存标 cache。
        // 钉钉(DingTalk,阿里企业 IM):接收文件默认 Documents\钉钉,旧版/英文版 Documents\DingTalk(两名覆盖)。
        // %LOCALAPPDATA%\DingTalk 为 Electron/CEF 缓存(纯可清→cache);%APPDATA%\DingTalk 虽含表情/头像缓存,
        // 但同一 {uid}_v2 树亦含加密本地聊天库 DBFiles\dingtalk.db(钉钉云端仅存 180 天,更早历史仅此本地副本,
        // 误清将永久丢失)——属混合 data+cache,按 data 保守标注勿整目录删(与飞书 LarkShell/WhatsApp 商店版同口径)。
        {QStringLiteral("钉钉文件"), documents + QStringLiteral("/钉钉"), true},
        {QStringLiteral("钉钉文件（旧版）"), documents + QStringLiteral("/DingTalk"), true},
        {QStringLiteral("钉钉缓存"), localAppData + QStringLiteral("/DingTalk"), false},
        {QStringLiteral("钉钉配置缓存"), roamingAppData + QStringLiteral("/DingTalk"), true},
        // 飞书 / Lark(字节企业 IM):LarkShell 为数据+缓存根。Roaming 实例含 sdk_storage(运行时/日志,混合→保守 data);
        // Local 实例为 GPUCache/Crashpad/日志(纯缓存→cache)。
        {QStringLiteral("飞书数据（LarkShell）"), roamingAppData + QStringLiteral("/LarkShell"), true},
        {QStringLiteral("飞书缓存（LarkShell）"), localAppData + QStringLiteral("/LarkShell"), false},
        // WhatsApp:经典 Win32 安装版(APPDATA\WhatsApp,聊天库 messages.db + 接收媒体)与微软商店/UWP 版
        // (Packages\...WhatsAppDesktop...,包根含 LocalState 聊天库 + AC/TempState/LocalCache 缓存)均含不可替代数据。
        {QStringLiteral("WhatsApp 数据（经典版）"), roamingAppData + QStringLiteral("/WhatsApp"), true},
        {QStringLiteral("WhatsApp 数据（商店版）"), localAppData + QStringLiteral("/Packages/5319275A.WhatsAppDesktop_cv1g1gvanyjgm"), true},
        // Signal:加密消息库 db.sqlite + config.json(含加密密钥)+ 附件,全为不可替代用户数据。
        {QStringLiteral("Signal 数据"), roamingAppData + QStringLiteral("/Signal"), true},
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
        // 用户数据型目录(微信/企业微信/QQ 的 Files)整目录含聊天接收的文件/图片/视频,不可当"缓存"
        // 整目录删除——原统一标"聊天缓存"会误导用户清空致数据丢失。改标"聊天客户端数据"并明确勿整目录
        // 删;真正的配置/缓存目录仍标"聊天缓存"。
        QString detail = candidate.isUserData
            ? QStringLiteral("含聊天接收的文件/图片/视频等用户数据与部分缓存，请勿整目录删除；建议在客户端内迁移存储位置或仅清理缓存子目录。")
            : QStringLiteral("聊天客户端本地文件和缓存。建议优先在客户端内清理或迁移存储位置。");
        if (summary.truncated) {
            // 条目数超 ComputePathSizeLimited 枚举上限(30000),bytes 为部分累加下限;披露以免用户高估可回收量
            // (与 ScanDockerWsl/ScanSoftwareFootprint/ScanStartupFootprint 等模块同口径,本模块原吞掉此标记)。
            detail += QStringLiteral("（条目数较多，本机估算为下限，实际占用可能更高）");
        }
        AddFinding(out, FeatureModule::MessengerCache, candidate.name,
                   candidate.isUserData ? QStringLiteral("聊天客户端数据") : QStringLiteral("聊天缓存"),
                   detail, candidate.path, summary.bytes);
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
    // 新版 Outlook(OneOutlook / Monarch,Win11 新机默认邮件客户端)的离线缓存 OST 改用新目录,
    // 不再落到经典 Outlook 的 .../Microsoft/Outlook;不补扫则新版 Outlook 用户的 OST 不被发现
    // (与"邮件归档库检查"名实不符)。补桌面版与 MSIX 包缓存两处候选;PathExists 过滤不存在的目录,
    // 未装新版 Outlook 的机器无影响(候选路径不准也只是空扫描,不产生误报——*.ost 仅匹配真实文件)。
    roots << localAppData + QStringLiteral("/Microsoft/OutlookForWindows");
    roots << localAppData + QStringLiteral("/Packages/Microsoft.OutlookForWindows_8wekyb3d8bbwe/LocalCache");
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
            // PST/OST/MBOX 通常为普通文件(逻辑==实占);仅当 NTFS 压缩/稀疏(实占<逻辑)时在明细补"逻辑/实占",
            // 提醒删除回收的是实占量。bytes 保持逻辑大小不变(不扰动体积口径/严重度/稳定键),纯明细增补。
            QString detail = QStringLiteral("大型邮件归档或离线邮箱文件。处理前应先确认账户同步和备份状态。");
            const AllocatedBytes ab = AllocatedBytesOnDisk(info.absoluteFilePath());
            if (ab.sparse) {
                detail += QStringLiteral(" 该文件经 NTFS 压缩/稀疏,实占 %1(删除可回收实占量,非逻辑大小)。")
                              .arg(FormatAllocatedText(ab));
            }
            AddFinding(out, FeatureModule::MailArchive, info.fileName(), QStringLiteral("邮件归档"),
                       detail, info.absoluteFilePath(), bytes);
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
            const AllocatedBytes ab = AllocatedBytesOnDisk(info.absoluteFilePath());
            // 动态虚拟磁盘(vhdx/vmdk/qcow2 等)的逻辑大小常远超实占(按需扩张),按逻辑报"可回收"会严重高估。
            // 与 DockerWsl 一致改报实占(allocated):逻辑大小仅作发射门槛(免稀疏盘被挤出清单),bytes 用实占,
            // 明细用 FormatAllocatedText 同时披露逻辑/实占。状态含"虚拟磁盘"以命中 SeverityForFinding 的 Notice
            // 关键词——实占跌破 10GB 升 Notice 的字节阈值时,靠关键词保持与通用虚拟盘一致的提示级可见性。
            if (ab.logical < 1024ULL * 1024ULL * 1024ULL) {
                continue;
            }
            AddFinding(out, FeatureModule::VirtualMachineImages, info.fileName(), QStringLiteral("虚拟磁盘"),
                       QStringLiteral("大型虚拟机磁盘。建议通过对应虚拟化软件迁移、压缩或清理快照。实占 %1。"
                                      "实际可回收量取决于虚拟盘内部空闲度,此处为当前占用而非可回收保证。")
                           .arg(FormatAllocatedText(ab)),
                       info.absoluteFilePath(), ab.allocated);
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
    // 按描述"按媒体类型、年代和体积"三维度生成整理建议。原实现只做类型直方图,年代/体积维度缺失(详情甚至把
    // "可按年份整理"甩给用户)。为避免同一文件体积在多个交叉维度分组里被重复计入全局总量/治理分(系统假设各
    // finding 互斥、逐项 bytes 求和),此处以"类型"为唯一体积承载维度产出互斥 finding(求和=媒体总量),把
    // "年代(文件修改年跨度)"与"体积(大视频)"两维度折叠进各类型 finding 的明细——既兑现描述承诺,又不重复计费、
    // 不污染治理分。处置方案"按年份建目录 Photos\2026""大视频转归档盘"由明细的年代跨度与大视频计数支撑。
    static const QSet<QString> videoSuffixes{
        QStringLiteral("mp4"), QStringLiteral("mov"), QStringLiteral("mkv"), QStringLiteral("avi"), QStringLiteral("wmv")};
    constexpr std::uint64_t kLargeVideoThreshold = 100ull * 1024 * 1024;  // 100 MiB 及以上视为大视频(可转归档盘)。
    struct TypeBucket {
        std::uint64_t bytes = 0;
        int count = 0;
        int earliestYear = 0;  // 该类型文件修改年最早(0=无有效年)。
        int latestYear = 0;
        std::uint64_t largeVideoBytes = 0;
        int largeVideoCount = 0;
    };
    std::map<QString, TypeBucket> byExt;
    QDirIterator iterator(root, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    int visited = 0;
    bool truncated = false;
    while (iterator.hasNext()) {
        if (visited >= 150000) {
            truncated = true;  // 命中枚举上限,以下体积/计数为下限。
            break;
        }
        if (IsCancelled(cancelFlag)) {
            break;
        }
        iterator.next();
        ++visited;
        const QFileInfo info = iterator.fileInfo();
        const QString suffix = info.suffix().toLower();
        if (!IsMediaSuffix(suffix)) {
            continue;
        }
        const std::uint64_t bytes = static_cast<std::uint64_t>(std::max<qint64>(0, info.size()));
        TypeBucket& bucket = byExt[suffix.isEmpty() ? QStringLiteral("无扩展名") : suffix];
        bucket.bytes += bytes;
        bucket.count += 1;
        const int year = info.lastModified().date().year();
        if (year > 1900 && year < 2100) {  // 仅采纳有效修改年(排除 0/异常年)。
            if (bucket.earliestYear == 0 || year < bucket.earliestYear) {
                bucket.earliestYear = year;
            }
            if (year > bucket.latestYear) {
                bucket.latestYear = year;
            }
        }
        if (videoSuffixes.contains(suffix) && bytes >= kLargeVideoThreshold) {
            bucket.largeVideoBytes += bytes;
            bucket.largeVideoCount += 1;
        }
    }
    if (byExt.empty()) {
        AddFinding(out, FeatureModule::MediaOrganizer, QStringLiteral("未发现媒体"),
                   QStringLiteral("媒体分组"),
                   QStringLiteral("所选目录未发现常见照片 / 视频文件。"),
                   root, 0);
        return;
    }
    for (const auto& item : byExt) {
        const TypeBucket& bucket = item.second;
        QString detail = QStringLiteral("共 %1 个文件").arg(bucket.count);
        if (bucket.earliestYear > 0 && bucket.latestYear > 0) {
            // 年代维度:折叠进明细(文件修改年跨度),支撑"按年份整理"建议,不另发体积承载 finding。
            detail += bucket.earliestYear == bucket.latestYear
                ? QStringLiteral("，年代 %1").arg(bucket.earliestYear)
                : QStringLiteral("，年代 %1–%2").arg(bucket.earliestYear).arg(bucket.latestYear);
        }
        if (bucket.largeVideoCount > 0) {
            // 体积维度:大视频计数+合计折叠进明细(仅视频类型),支撑"大视频转归档盘"建议。
            detail += QStringLiteral("，含超过 100 MiB 的大视频 %1 个（约 %2，建议先转归档盘）")
                          .arg(bucket.largeVideoCount)
                          .arg(FormatBytesText(bucket.largeVideoBytes));
        }
        if (truncated) {
            detail += QStringLiteral("（枚举上限内，体积/计数为下限）");
        }
        AddFinding(out, FeatureModule::MediaOrganizer, QStringLiteral(".%1 媒体").arg(item.first),
                   QStringLiteral("媒体分组"), detail, root, bucket.bytes);
    }
}

/**
 * @brief NTFS 卷配额查询结果(IDiskQuotaControl 只读)。
 */
struct NtfsQuotaInfo {
    /** GetQuotaState 经 DISKQUOTA_STATE_MASK:0=禁用,1=跟踪,2=强制。 */
    DWORD state = 0;
    /** 卷默认配额上限(字节);<0 表示无上限。 */
    long long defaultLimit = -1;
    /** 卷默认配额警告阈值(字节);<0 表示无上限。 */
    long long defaultThreshold = -1;
};

/**
 * @brief 只读查询一个 NTFS 卷的配额状态。
 * @param volumeRoot 卷根路径(如 "C:\\")。
 * @param hasData [out] 是否成功取到配额数据(COM 不可用 / 非 NTFS / 初始化失败时为 false)。
 * @return 配额信息(hasData 为 false 时内容无意义)。
 *
 * 用 IDiskQuotaControl::Initialize(volumeRoot, FALSE) 只读打开卷,GetQuotaState 取启用状态,
 * GetDefaultQuotaLimit / GetDefaultQuotaThreshold 取卷默认配额。NTFS 配额在多数家用机器上默认关闭,
 * 此时 hasData=true 但 state=禁用。全程只读,绝不调用 Set* / AddUser* / DeleteUser 等写入接口。COM 在
 * 后台 std::thread 上按需 CoInitializeEx / CoUninitialize:weInitialized 仅在本次确实以 S_OK 初始化时
 * 反初始化,S_FALSE(同线程已初始化,如 lnk 解析)与 RPC_E_CHANGED_MODE(套间不符)均不反初始化、后者
 * 直接放弃查询以避免跨套间调用风险。任一 COM 步骤失败立即释放并返回 hasData=false,调用方降级到启发式预算。
 */
NtfsQuotaInfo QueryNtfsQuotaForVolume(const QString& volumeRoot, bool& hasData) {
    hasData = false;
    NtfsQuotaInfo info;
    const HRESULT hrInit = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (hrInit == RPC_E_CHANGED_MODE) {
        return info;  // 线程套间不符,放弃查询(不强制切换以免破坏同线程既有 COM 状态)。
    }
    const bool weInitialized = (hrInit == S_OK);  // S_FALSE=同线程已初始化,不反初始化。
    if (hrInit != S_OK && hrInit != S_FALSE) {
        return info;  // 其它失败码视为 COM 不可用。
    }

    IDiskQuotaControl* control = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_DiskQuotaControl, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_IDiskQuotaControl, reinterpret_cast<void**>(&control));
    if (FAILED(hr) || control == nullptr) {
        if (weInitialized) {
            CoUninitialize();
        }
        return info;
    }

    const std::wstring rootW = QDir::toNativeSeparators(volumeRoot).toStdWString();
    hr = control->Initialize(rootW.c_str(), FALSE);  // 只读。
    if (SUCCEEDED(hr)) {
        DWORD state = 0;
        if (SUCCEEDED(control->GetQuotaState(&state))) {
            info.state = state & DISKQUOTA_STATE_MASK;
            hasData = true;
            long long limit = -1;
            if (SUCCEEDED(control->GetDefaultQuotaLimit(&limit))) {
                info.defaultLimit = limit;
            }
            long long threshold = -1;
            if (SUCCEEDED(control->GetDefaultQuotaThreshold(&threshold))) {
                info.defaultThreshold = threshold;
            }
        }
    }
    control->Release();
    if (weInitialized) {
        CoUninitialize();
    }
    return info;
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
        // 说明里明确这是"建议预算(参考值)"——硬编码预算不可被读成"系统认定的配额"。NTFS 真实配额状态
        // 由下方 QueryNtfsQuotaForVolume 另行只读查询并在独立条目中如实报告。截断时 bytes 是下限估算,
        // 需告知用户实际可能更高。
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

    // NTFS 卷配额状态(只读 IDiskQuotaControl):对预算目录所在卷查询真实配额启用状态与卷默认上限,作为
    // 上方"参考预算"的补充。多数家用机器 NTFS 配额默认关闭→如实报"未启用";启用时报卷默认上限。任一卷
    // COM 查询失败静默降级(发中性"配额查询不可用"),不影响预算条目。按卷去重(预算目录常同在系统盘)。
    QSet<QString> queriedRoots;
    for (const Budget& budget : budgets) {
        if (IsCancelled(cancelFlag)) {
            break;
        }
        const QStorageInfo volume(budget.path);
        if (!volume.isValid() || volume.fileSystemType() != "NTFS") {
            continue;  // 仅 NTFS 支持配额;非 NTFS / 无效卷跳过。
        }
        const QString root = QDir::toNativeSeparators(volume.rootPath());
        if (root.isEmpty() || queriedRoots.contains(root)) {
            continue;
        }
        queriedRoots << root;

        bool hasData = false;
        const NtfsQuotaInfo quota = QueryNtfsQuotaForVolume(root, hasData);
        if (!hasData) {
            AddFinding(out, FeatureModule::QuotaBudget, QStringLiteral("%1 配额").arg(root),
                       QStringLiteral("配额查询不可用"),
                       QStringLiteral("无法通过 NTFS 配额接口读取 %1 的配额状态(可能权限不足或接口不可用)。"
                                      "上方目录预算为参考建议值,非系统强制限额。").arg(root));
            continue;
        }

        QString stateText;
        QString detail;
        const bool enforced = (quota.state == DISKQUOTA_STATE_ENFORCE);
        if (enforced) {
            stateText = QStringLiteral("NTFS 配额（强制）");
        } else if (quota.state == DISKQUOTA_STATE_TRACK) {
            stateText = QStringLiteral("NTFS 配额（跟踪）");
        } else {
            stateText = QStringLiteral("NTFS 配额（未启用）");
        }
        if (quota.state == DISKQUOTA_STATE_DISABLED) {
            detail = QStringLiteral("%1 的 NTFS 配额当前未启用,卷上用户不受系统配额限制。上方目录预算为参考"
                                    "建议值,可在「卷属性 → 配额」中启用 NTFS 配额由系统强制。").arg(root);
        } else {
            const QString limitText = (quota.defaultLimit < 0)
                ? QStringLiteral("无上限")
                : FormatBytesText(static_cast<std::uint64_t>(quota.defaultLimit));
            // 卷默认上限适用于"无显式条目的用户",并非当前用户的专属条目,如实标注以免误解。
            detail = QStringLiteral("%1 的 NTFS 配额已%2,卷默认配额上限 %3(适用于无显式条目的用户,非当前"
                                    "用户的专属条目)。上方目录预算为参考建议值。")
                         .arg(root,
                              enforced ? QStringLiteral("强制") : QStringLiteral("跟踪"),
                              limitText);
        }
        AddFinding(out, FeatureModule::QuotaBudget, QStringLiteral("%1 配额").arg(root), stateText, detail);
    }
}

/**
 * @brief 探测本机是否存在"异地/历史"备份类在场信号(仅本地存在性检测,不联网、不上传)。
 *
 * 无本地备份目标时,给用户一个"可能已有异地副本"的参考;空列表表示未探测到任何信号。
 * 读取本机目录/环境变量/本地注册表,均不触发任何网络访问,符合 LOCAL-ONLY 约束。
 * @return 探测到的备份在场信号(展示文本)列表。
 */
QStringList ProbeAlternativeBackupPresence() {
    QStringList found;
    const QString home = QDir::homePath();
    const QString oneDriveEnv = qEnvironmentVariable("OneDrive");
    if (!oneDriveEnv.isEmpty() && QFileInfo::exists(oneDriveEnv)) {
        found << QStringLiteral("OneDrive 同步");
    } else if (QFileInfo::exists(home + QStringLiteral("/OneDrive"))) {
        found << QStringLiteral("OneDrive 同步");
    }
    // iCloud:Windows 默认目录名带空格"iCloud Drive",旧版/部分版本为"iCloudDrive",两者都探测。
    if (QFileInfo::exists(home + QStringLiteral("/iCloud Drive")) ||
        QFileInfo::exists(home + QStringLiteral("/iCloudDrive"))) {
        found << QStringLiteral("iCloud 云盘");
    }
    if (QFileInfo::exists(home + QStringLiteral("/Google Drive"))) {
        found << QStringLiteral("Google Drive 同步");
    }
    if (QFileInfo::exists(home + QStringLiteral("/Dropbox"))) {
        found << QStringLiteral("Dropbox 同步");
    }
    // File History:HKCU 配置键存在子项/子值即视为"配置过"(本地注册表读取,不联网,只读不写)。
    // 注意:子键可能在停用后残留,故信号标注"已配置",最终是否仍有异地副本以模块级"可能"措辞兜底。
    const QSettings fileHistory(
        QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\FileHistory"),
        QSettings::NativeFormat);
    if (!fileHistory.childGroups().isEmpty() || !fileHistory.childKeys().isEmpty()) {
        found << QStringLiteral("Windows 文件历史（已配置）");
    }
    return found;
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
        // 无本地目标盘时不可判定备份缺口(止血:不再把每个重要目录打成 Critical「备份缺口」)。额外探测
        // 云盘/文件历史在场,给用户一个"可能已有异地副本"的参考,再引导其设置本地备份目标或确认同步状态。
        const QStringList alt = ProbeAlternativeBackupPresence();
        if (alt.isEmpty()) {
            AddFinding(out, FeatureModule::BackupGap, QStringLiteral("请选择备份目标"), QStringLiteral("等待输入"),
                       QStringLiteral("未设置本地备份目标盘，无法核对备份缺口。请在目标路径选择一个备份盘或目录后重新体检。"));
        } else {
            AddFinding(out, FeatureModule::BackupGap, QStringLiteral("未设本地备份目标"), QStringLiteral("等待输入"),
                       QStringLiteral("未设置本地备份目标盘，无法核对备份缺口。检测到本机存在：%1——这些可能已为资料提供"
                                      "异地副本，建议确认其同步状态后再决定是否补充本地备份。").arg(alt.join(QStringLiteral("、"))));
        }
        return;
    }
    // 以下阈值均为参考值,非权威标准;结论据此推断,非硬性判定。
    constexpr int kCompleteRatioPct = 50;                            // 目标体积 < 源 50% 视为不完整(参考阈值)。
    constexpr qint64 kStaleWindowMsec = 30LL * 24LL * 3600LL * 1000LL;  // 落后源 30 天视为陈旧(参考阈值)。
    for (const QString& path : ExistingPaths(important)) {
        if (IsCancelled(cancelFlag)) {
            break;
        }
        const PathSizeSummary source = ComputePathSizeLimited(path, 15000, cancelFlag);
        const QString backupDir = QDir(targetPath).filePath(QFileInfo(path).fileName());
        const QFileInfo backupInfo(backupDir);
        const QString title = QFileInfo(path).fileName().isEmpty() ? path : QFileInfo(path).fileName();
        QString state;
        QString detail = QStringLiteral("备份目标：%1。").arg(QDir::toNativeSeparators(targetPath));
        if (!backupInfo.isDir()) {
            // 目标下不存在同名目录(或同名项是文件而非目录)→真实缺口,与源是否截断无关。
            state = QStringLiteral("备份缺口");
            detail += QStringLiteral("目标路径下未发现同名备份目录；磁盘健康异常时应优先处理。");
            if (source.truncated) {
                detail += QStringLiteral("源目录条目较多，体积为估算下限。");
            }
        } else {
            const PathSizeSummary backup = ComputePathSizeLimited(backupDir, 15000, cancelFlag);
            if (IsCancelled(cancelFlag)) {
                break;  // 取消发生在两次计数之间:不再为本目录下结论,直接结束扫描。
            }
            const bool unreliable = (source.truncated || backup.truncated);
            if (unreliable) {
                // 部分计数不足以可靠判定完整度/新鲜度/是否为空——发中性"核对受限",不误报 Warning/Critical。
                state = QStringLiteral("备份核对受限");
                detail += QStringLiteral("源或目标目录条目较多，已按扫描上限估算，完整度、新鲜度与是否为空均无法可靠比对，建议人工核对。");
            } else {
                const bool sourceHasContent = (source.files > 0 || source.bytes > 0);
                const bool backupEmpty = (backup.files == 0 && backup.bytes == 0);
                const bool incomplete = (!backupEmpty && source.bytes > 0
                                         && backup.bytes * 100 < source.bytes * static_cast<std::uint64_t>(kCompleteRatioPct));
                const bool stale = (!backupEmpty && !incomplete && source.latestMtimeMsec > 0 && backup.latestMtimeMsec > 0
                                    && source.latestMtimeMsec - backup.latestMtimeMsec > kStaleWindowMsec);
                if (backupEmpty) {
                    if (sourceHasContent) {
                        state = QStringLiteral("备份缺口");
                        detail += QStringLiteral("目标下同名备份目录未发现任何文件。");
                    } else {
                        state = QStringLiteral("备份完整");
                        detail += QStringLiteral("源目录暂无可备份内容，无需核对。");
                    }
                } else if (incomplete) {
                    state = QStringLiteral("备份不完整");
                    const int pct = source.bytes > 0 ? static_cast<int>((backup.bytes * 100) / source.bytes) : 0;
                    detail += QStringLiteral("目标备份体积约为源的 %1%（低于 %2% 参考阈值），疑似不完整。")
                                  .arg(pct).arg(kCompleteRatioPct);
                } else if (stale) {
                    state = QStringLiteral("备份陈旧");
                    const qint64 days = (source.latestMtimeMsec - backup.latestMtimeMsec) / (24LL * 3600LL * 1000LL);
                    detail += QStringLiteral("目标备份最后更新早于源约 %1 天（超过 30 天参考阈值），建议刷新。").arg(days);
                } else {
                    state = QStringLiteral("备份完整");
                    detail += QStringLiteral("目标下存在同名备份目录，体积与时效接近源。");
                }
            }
        }
        AddFinding(out, FeatureModule::BackupGap, title, state, detail, path, source.bytes);
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
 * @brief 扫描文件占用识别器模块。
 *
 * 用 Windows Restart Manager **识别**(只读查询,不自动解锁)占用指定文件/目录的进程。目录扫描受抽样
 * 上限约束(Restart Manager 每文件一会话有开销),仅抽查前 kMaxSampleFiles 个文件;抽查未命中时明细如实
 * 标注“仅抽查样本”,避免对文件较多的目录误报“未发现占用”的绝对结论。
 *
 * @param out 输出结果。
 * @param sourcePath 源路径。
 * @param cancelFlag 取消标志。
 */
void ScanFileUnlocker(QVector<FeatureFinding>& out, const QString& sourcePath, std::shared_ptr<std::atomic_bool> cancelFlag) {
    if (!PathExists(sourcePath)) {
        AddFinding(out, FeatureModule::FileUnlocker, QStringLiteral("请选择文件或目录"), QStringLiteral("等待输入"),
                   QStringLiteral("设置源路径后可查询占用进程。"));
        return;
    }

    constexpr int kMaxSampleFiles = 30;  // Restart Manager 每文件一会话有开销,目录扫描仅抽查上限
    QStringList targets;
    bool truncated = false;
    const QFileInfo info(sourcePath);
    if (info.isFile()) {
        targets << info.absoluteFilePath();
    } else {
        QDirIterator iterator(sourcePath, QDir::Files | QDir::Hidden | QDir::System, QDirIterator::Subdirectories);
        while (iterator.hasNext() && targets.size() < kMaxSampleFiles && !IsCancelled(cancelFlag)) {
            iterator.next();
            targets << iterator.filePath();
        }
        // 循环结束时仍有更多文件 → 被抽样上限截断(目录文件多于上限),其后结论须就“样本”而非全量下。
        truncated = iterator.hasNext();
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
        // 仅识别占用进程,不自动解锁;释放需用户手动关闭对应进程(处置方案给出操作步骤)。
        AddFinding(out, FeatureModule::FileUnlocker, QFileInfo(target).fileName(), QStringLiteral("被占用"),
                   QStringLiteral("占用进程：%1。本工具仅识别占用、不自动解锁，如需释放请手动关闭对应进程。").arg(processes.join(QStringLiteral("、"))),
                   target, static_cast<std::uint64_t>(std::max<qint64>(0, QFileInfo(target).size())));
    }

    const QString sourceTitle = QFileInfo(sourcePath).fileName().isEmpty() ? sourcePath : QFileInfo(sourcePath).fileName();
    if (locked == 0) {
        // 抽样截断时不得断言“未发现占用”绝对结论——仅能就抽查样本下结论,免对文件较多的目录误报。
        const QString emptyDetail = truncated
            ? QStringLiteral("抽查的前 %1 个文件样本中未发现占用进程；目录文件较多，仅抽查样本，可能存在未被抽查的占用文件，可缩小到具体子目录或单文件再查。").arg(targets.size())
            : QStringLiteral("Restart Manager 未发现当前路径被进程占用。");
        AddFinding(out, FeatureModule::FileUnlocker, sourceTitle, QStringLiteral("未发现占用"), emptyDetail, sourcePath, 0);
    } else if (truncated) {
        // 命中占用但目录文件多于抽样上限 → 明示抽查范围(不静默截断),免误以为已排查全部文件。
        AddFinding(out, FeatureModule::FileUnlocker, QStringLiteral("更多文件未抽查"), QStringLiteral("抽查上限"),
                   QStringLiteral("目录文件较多，仅用 Restart Manager 抽查了前 %1 个文件。如需完整排查，请缩小源路径到具体子目录或单文件。").arg(targets.size()),
                   sourcePath, 0);
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
        // 大缓存检测(描述承诺"识别...大缓存",原实现完全缺失):浅层找名字含 cache/缓存/tmp/temp 的大子目录,
        // 折叠进根 finding 明细——不另发体积承载 finding,以免与根 finding 的"全目录体积"在全局总量里重复计入
        // (系统假设各 finding 互斥)。按目录包含关系去重,避免父子缓存目录在明细里重复。
        QString cacheNote;
        {
            const QString rootKey = QDir::toNativeSeparators(QDir(root).absolutePath()).toCaseFolded();
            QSet<QString> matchedRoots;
            QStringList cacheHits;
            QDirIterator it(root, QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden, QDirIterator::Subdirectories);
            int visited = 0;
            while (it.hasNext() && cacheHits.size() < 6 && visited < 40000 && !IsCancelled(cancelFlag)) {
                it.next();
                ++visited;
                const QFileInfo di = it.fileInfo();
                const QString fn = di.fileName().toCaseFolded();
                const bool cacheLike = fn.contains(QStringLiteral("cache")) || fn.contains(QStringLiteral("缓存")) ||
                    fn.contains(QStringLiteral("temp")) || fn == QStringLiteral("tmp") || fn == QStringLiteral(".tmp");
                if (!cacheLike) {
                    continue;
                }
                const QString candKey = QDir::toNativeSeparators(di.absoluteFilePath()).toCaseFolded();
                if (candKey == rootKey) {
                    continue;  // 跳过根自身。
                }
                bool nested = false;
                const QLatin1Char sep('\\');
                for (const QString& m : matchedRoots) {
                    if (candKey == m || candKey.startsWith(m + sep)) {
                        nested = true;  // 已在某已纳入缓存目录内,不重复计入明细。
                        break;
                    }
                }
                if (nested) {
                    continue;
                }
                const PathSizeSummary cs = ComputePathSizeLimited(di.absoluteFilePath(), 8000, cancelFlag);
                if (cs.bytes < 256ULL * 1024ULL * 1024ULL) {
                    continue;  // 未达 256 MiB 不算大缓存。
                }
                matchedRoots.insert(candKey);
                cacheHits << QStringLiteral("%1（约 %2）").arg(di.fileName()).arg(FormatBytesText(cs.bytes));
            }
            if (!cacheHits.isEmpty()) {
                cacheNote = QStringLiteral("；含大缓存：%1").arg(cacheHits.join(QStringLiteral("、")));
            }
        }
        AddFinding(out, FeatureModule::CloudSync, QFileInfo(root).fileName().isEmpty() ? root : QFileInfo(root).fileName(),
                   QStringLiteral("同步目录"),
                   QStringLiteral("本地同步目录占用估算；建议检查仅云端、冲突副本和重复下载。") + cacheNote,
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
 * @brief 一次 vssadmin list shadowstorage 查询的原始结果。
 *
 * vssadmin 按 Windows OEM 代码页(zh-CN=CP936/GBK、en-US=CP437)输出,高字节为本地化标签文字,
 * 但 ASCII 字节(数字/单位/百分号/GUID/盘符括号)在所有 OEM 代码页中与 ASCII 逐字节一致,故解析只在
 * ASCII 令牌上进行、与系统语言无关。raw 为未解码的原始字节;ran 表示进程正常结束(取得 exitCode,
 * 非超时/取消/启动失败);timedOut 表示等待超时已被强制终止。
 */
struct VssQueryResult {
    QByteArray raw;       // 原始 stdout(含 stderr 合并),均为 OEM 字节。
    DWORD exitCode = 0;   // 进程退出码。
    bool ran = false;     // 进程正常结束(拿到 exitCode,非超时)。
    bool timedOut = false;
};

/**
 * @brief 以 CREATE_NO_WINDOW(无控制台窗口闪烁)、可取消、5 秒超时运行 vssadmin list shadowstorage。
 *
 * 用 Win32 CreateProcess 而非 QProcess:GUI 进程启动控制台子进程默认会弹出控制台窗口,CREATE_NO_WINDOW
 * 抑制之(DockerWsl 同样因此不自动跑 docker.exe)。stdout/stderr 合并到单条管道,输出小(<2KB)故
 * "进程结束后一次读尽"无管道写满死锁风险。等待分片 100ms 轮询以便响应 cancelFlag;超时则
 * TerminateProcess + 短等待回收,ran=false 让上层降级——绝不阻塞 worker 线程或卡死有界 join。
 * 本进程已 requireAdministrator 清单提权,vssadmin 应有权限;若意外未提权返回 exitCode=2,上层降级提示。
 */
VssQueryResult QueryVssAdminShadowStorage(const std::shared_ptr<std::atomic_bool>& cancelFlag) {
    VssQueryResult result;
    QString sysRoot = QString::fromLocal8Bit(qgetenv("SystemRoot"));
    if (sysRoot.isEmpty()) {
        sysRoot = QStringLiteral("C:\\Windows");
    }
    const QString exe = sysRoot + QStringLiteral("\\System32\\vssadmin.exe");
    if (!QFileInfo::exists(exe)) {
        return result;  // 不存在 → ran=false,上层降级。
    }
    const QString cmdLine = QStringLiteral("\"%1\" list shadowstorage").arg(exe);

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = nullptr;
    HANDLE hRead = nullptr;
    HANDLE hWrite = nullptr;
    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) {
        return result;
    }
    SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);  // 读端不继承,避免本进程挂住读端。

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = hWrite;
    si.hStdError = hWrite;  // 合并 stderr(错误信息无 ASCII 数值三元组 → 上层结构判定降级)。
    PROCESS_INFORMATION pi{};

    // CreateProcessW 可能改写命令行缓冲,放入可变缓冲。
    const std::wstring wcmd = cmdLine.toStdWString();
    std::vector<wchar_t> cmdBuf(wcmd.begin(), wcmd.end());
    cmdBuf.push_back(L'\0');

    const BOOL ok = CreateProcessW(nullptr, cmdBuf.data(), nullptr, nullptr, TRUE /*InheritHandles*/,
                                   CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    CloseHandle(hWrite);  // 关闭本端写句柄,子进程退出后读端读到 EOF。
    if (!ok) {
        CloseHandle(hRead);
        return result;
    }

    constexpr DWORD kStepMs = 100;
    constexpr int kMaxSteps = 50;  // 100ms × 50 = 5 秒上限。
    bool finished = false;
    for (int i = 0; i < kMaxSteps; ++i) {
        if (IsCancelled(cancelFlag)) {
            break;
        }
        if (WaitForSingleObject(pi.hProcess, kStepMs) == WAIT_OBJECT_0) {
            finished = true;
            break;
        }
    }
    if (!finished) {
        result.timedOut = true;
        TerminateProcess(pi.hProcess, 1);
        WaitForSingleObject(pi.hProcess, 500);
    }
    GetExitCodeProcess(pi.hProcess, &result.exitCode);

    // 输出小,进程结束后一次读尽(无写满死锁风险)。
    char chunk[8192];
    DWORD readN = 0;
    while (ReadFile(hRead, chunk, sizeof(chunk), &readN, nullptr) && readN > 0) {
        result.raw.append(chunk, static_cast<int>(readN));
    }

    result.ran = finished;

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    CloseHandle(hRead);
    return result;
}

/**
 * @brief 一卷的卷影副本存储关联(已通过交叉校验)。
 */
struct VssAssociation {
    QString label;            // 盘符标签如 "C:",或 GUID 尾段(无盘符卷)。
    qint64 usedBytes = 0;     // 已用卷影副本存储(实际占用),作 finding 的 bytes。
    qint64 allocatedBytes = 0;
    qint64 maximumBytes = -1; // -1 = UNBOUNDED 无上限。
};

/**
 * @brief 把单位令牌换算为字节倍数;未知单位返回 -1。
 */
qint64 VssUnitMultiplier(const QString& unit) {
    const QString u = unit.toCaseFolded();
    if (u == QLatin1String("bytes") || u == QLatin1String("b")) {
        return 1;
    }
    if (u == QLatin1String("kb")) {
        return 1024LL;
    }
    if (u == QLatin1String("mb")) {
        return 1024LL * 1024;
    }
    if (u == QLatin1String("gb")) {
        return 1024LL * 1024 * 1024;
    }
    if (u == QLatin1String("tb")) {
        return 1024LL * 1024 * 1024 * 1024;
    }
    if (u == QLatin1String("pb")) {
        return 1024LL * 1024 * 1024 * 1024 * 1024;
    }
    return -1;
}

/**
 * @brief 数值(GB 等)×字节倍数换算,四舍五入;溢出返回 -1。
 */
qint64 CheckedVssMul(double value, qint64 mult) {
    if (mult < 0) {
        return -1;
    }
    const double product = value * static_cast<double>(mult);
    if (product < 0.0 || product > 9.0e18) {  // qint64 max ≈ 9.22e18。
        return -1;
    }
    return static_cast<qint64>(product + 0.5);
}

/**
 * @brief 从行表里取"已用"行上一行(存储卷行)的盘符/GUID 标签。
 */
QString ExtractStorageVolumeLabel(const QStringList& lines, int usedIdx) {
    const int volIdx = usedIdx - 1;
    if (volIdx < 0 || volIdx >= lines.size()) {
        return QStringLiteral("(未知卷)");
    }
    const QString& volLine = lines[volIdx];
    static const QRegularExpression driveRe(QStringLiteral("\\(([A-Za-z]:)\\)"));
    const QRegularExpressionMatch dm = driveRe.match(volLine);
    if (dm.hasMatch()) {
        return dm.captured(1);  // "C:"
    }
    static const QRegularExpression guidRe(QStringLiteral("Volume\\{([0-9A-Fa-f]{8})"));
    const QRegularExpressionMatch gm = guidRe.match(volLine);
    if (gm.hasMatch()) {
        return QStringLiteral("Volume{%1}").arg(gm.captured(1).toUpper());  // GUID 前 8 位。
    }
    return QStringLiteral("(未知卷)");
}

/**
 * @brief 把盘符标签解析为 QStorageInfo 卷容量;不可解返回 -1(不因此丢弃 finding)。
 */
qint64 ResolveVolumeBytesTotal(const QString& label) {
    if (label.size() < 2 || label[1] != QLatin1Char(':')) {
        return -1;  // 仅 "X:" 盘符可稳定解析;GUID 标签不解析。
    }
    const QStorageInfo vol(label + QStringLiteral("\\"));
    if (!vol.isValid()) {
        return -1;
    }
    const qint64 total = vol.bytesTotal();
    return total > 0 ? total : -1;
}

/**
 * @brief VSS 解析结果:通过的关联 + 是否曾见到数值行(用于区分"未配置" vs "数据不一致")。
 *
 * sawNumericLines=true 表示输出里出现过数值行但无任何三元组通过交叉校验(数据在场但不自洽),
 * 上层据此发"数据不一致"中性提示而非误报"未配置";=false 表示连数值行都没有(确实未配置 VSS)。
 */
struct VssParseResult {
    QVector<VssAssociation> assocs;
    bool sawNumericLines = false;
};

/**
 * @brief 解析 vssadmin list shadowstorage 原始字节为各卷卷影副本存储关联(仅返回通过交叉校验者)。
 *
 * 语言无关:raw 为 OEM 字节,QString::fromLatin1 逐字节映射成 QString(0x00-0x7F 映射为对应 Unicode
 * 码点,高字节标签映射为乱码但解析不依赖标签文字),再用仅含 ASCII 令牌的正则匹配。GBK 尾字节虽可能落在
 * ASCII 区,但本地化标签为纯中文不含"数字+单位+空格+(数字%)"此类 ASCII 多令牌序列,且 Used/Allocated/
 * Maximum 三行连续无标签行穿插,故不会误匹配。三条数值行按块内位置(Used→Allocated→Maximum)识别,
 * 与本地化标签无关。每三元组经交叉校验(used≤allocated≤maximum、百分比单调、对存储卷容量 5% 容差上限)
 * 方才采信;任一不满足则丢弃该卷(不发出可能错误的字节数),上层降级为中性提示——绝不发出未通过校验的数字。
 */
VssParseResult ParseVssShadowStorageRaw(const QByteArray& raw) {
    VssParseResult result;
    QVector<VssAssociation> out;
    if (raw.isEmpty()) {
        return result;
    }
    const QString buf = QString::fromLatin1(raw.constData(), raw.size());

    static const QRegularExpression numRe(
        QStringLiteral("([0-9]+(?:\\.[0-9]+)?)\\s+(bytes|B|KB|MB|GB|TB|PB)\\s*\\(([0-9]+)\\s*%\\)"));
    static const QRegularExpression unboundedRe(QStringLiteral("UNBOUNDED"),
                                                QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression pctRe(QStringLiteral("\\(([0-9]+)\\s*%\\)"));

    const QStringList lines = buf.split(QRegularExpression(QStringLiteral("[\r\n]+")), Qt::SkipEmptyParts);

    // 收集所有数值行(数值+单位+百分比,或 UNBOUNDED 行)及其行号、百分比。
    struct NumLine {
        double value;
        qint64 mult;
        int pct;
        bool unbounded;
        int idx;
    };
    QVector<NumLine> nums;
    for (int i = 0; i < lines.size(); ++i) {
        const QString& line = lines[i];
        const QRegularExpressionMatch m = numRe.match(line);
        if (m.hasMatch()) {
            const qint64 mult = VssUnitMultiplier(m.captured(2));
            if (mult < 0) {
                continue;  // 未知单位 → 跳过,不污染三元组(后续行号不再相邻即整体降级)。
            }
            nums.append({m.captured(1).toDouble(), mult, m.captured(3).toInt(), false, i});
            continue;
        }
        if (unboundedRe.match(line).hasMatch()) {
            int pct = 100;
            const QRegularExpressionMatch pm = pctRe.match(line);
            if (pm.hasMatch()) {
                pct = pm.captured(1).toInt();
            }
            nums.append({0.0, 1, pct, true, i});
        }
    }

    // 按位置每三条行号严格相邻者为一组 = 一个关联块的 Used/Allocated/Maximum。
    for (int k = 0; k + 2 < nums.size(); k += 3) {
        const NumLine& a = nums[k];      // Used
        const NumLine& b = nums[k + 1];  // Allocated
        const NumLine& c = nums[k + 2];  // Maximum
        if (b.idx != a.idx + 1 || c.idx != b.idx + 1) {
            break;  // 行号不再连续 = 结构异常,放弃后续(上层降级)。
        }
        const qint64 used = CheckedVssMul(a.value, a.mult);
        const qint64 allocated = CheckedVssMul(b.value, b.mult);
        const qint64 maximum = c.unbounded ? -1 : CheckedVssMul(c.value, c.mult);
        if (used < 0 || allocated < 0 || (!c.unbounded && maximum < 0)) {
            continue;  // 溢出 → 丢弃该卷。
        }
        // 交叉校验 1:体积单调(无上限时仅 used≤allocated)。
        if (used > allocated) {
            continue;
        }
        if (!c.unbounded && allocated > maximum) {
            continue;
        }
        // 交叉校验 2:百分比单调(无上限时 maximum 百分比通常 100,不约束上界)。
        if (a.pct > b.pct) {
            continue;
        }
        if (!c.unbounded && b.pct > c.pct) {
            continue;
        }
        const QString label = ExtractStorageVolumeLabel(lines, a.idx);
        // 交叉校验 3:对存储卷容量做 5% 容差上限校验(可解才校验,不可解不因之丢弃)。
        const qint64 cap = ResolveVolumeBytesTotal(label);
        if (cap > 0) {
            const qint64 tolerance = cap / 20;  // 5%。
            if (allocated > cap + tolerance || used > cap + tolerance) {
                continue;
            }
        }
        out.append({label, used, allocated, maximum});
    }
    result.assocs = out;
    result.sawNumericLines = !nums.isEmpty();
    return result;
}

/**
 * @brief 扫描系统镜像与恢复点模块。
 *
 * ① 核算升级/恢复残留目录(Windows.old/$WINDOWS.~BT/更新下载缓存/Recovery)占用;
 * ② 查询卷影副本存储(VSS/还原点)实际占用——以 vssadmin list shadowstorage 子进程取数,语言无关的
 *   ASCII 令牌位置解析 + 交叉校验,任何不确定(超时/退出码非零/无可信三元组)均降级为中性提示,
 *   绝不发出未校验或错误的字节数(PARAMOUNT:不可回归、不可呈现错误数字)。
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

    if (!IsCancelled(cancelFlag)) {
        const VssQueryResult qr = QueryVssAdminShadowStorage(cancelFlag);
        const VssParseResult parsed = ParseVssShadowStorageRaw(qr.raw);
        // 仅当进程正常结束且退出码为 0、且有通过交叉校验的三元组时才发字节数;
        // 非零退出码即使输出看似可解析也一律降级(失败的查询不可信,守"不可呈现错误数字")。
        const bool emitFindings = !parsed.assocs.isEmpty() && qr.ran && qr.exitCode == 0;
        if (emitFindings) {
            for (const VssAssociation& a : parsed.assocs) {
                if (IsCancelled(cancelFlag)) {
                    break;
                }
                const QString title = QStringLiteral("卷影副本存储（%1）").arg(a.label);
                const QString rootPath = (a.label.size() >= 2 && a.label[1] == QLatin1Char(':'))
                                             ? (a.label + QStringLiteral("\\"))
                                             : QString();
                const QString maxText = (a.maximumBytes < 0)
                                            ? QStringLiteral("无上限")
                                            : FormatBytesText(static_cast<std::uint64_t>(a.maximumBytes));
                const QString detail = QStringLiteral(
                    "卷 %1 的卷影副本（还原点 / 系统保护）存储：已用 %2 / 已分配 %3 / 上限 %4。"
                    "由系统保护策略维护，非可直接删除的垃圾；如需调整，请在「系统属性 › 系统保护」"
                    "修改上限或删除还原点（删除将丢失对应还原点）。")
                    .arg(a.label,
                         FormatBytesText(static_cast<std::uint64_t>(a.usedBytes)),
                         FormatBytesText(static_cast<std::uint64_t>(a.allocatedBytes)),
                         maxText);
                AddFinding(out, FeatureModule::RestorePoint, title, QStringLiteral("卷影副本存储"), detail, rootPath,
                           static_cast<std::uint64_t>(a.usedBytes));
            }
        } else if (!qr.ran) {
            // 超时/未启动/取消:中性提示,不报错不报数字。
            AddFinding(out, FeatureModule::RestorePoint, QStringLiteral("卷影副本存储"), QStringLiteral("查询不可用"),
                       QStringLiteral("未能完成卷影副本存储查询（vssadmin 调用超时或不可用）。"
                                      "可在「系统属性 › 系统保护」查看与调整。"));
        } else if (qr.exitCode == 2) {
            // 非提权(本应提权,意外路径):中性提示。
            AddFinding(out, FeatureModule::RestorePoint, QStringLiteral("卷影副本存储"), QStringLiteral("查询不可用"),
                       QStringLiteral("查询卷影副本存储需要管理员权限。请在「系统属性 › 系统保护」查看。"));
        } else if (qr.exitCode == 0) {
            // 干净退出但无可信三元组:据是否见过数值行区分"数据不一致"(在场但不自洽)与"未配置"。
            if (parsed.sawNumericLines) {
                AddFinding(out, FeatureModule::RestorePoint, QStringLiteral("卷影副本存储"), QStringLiteral("数据不一致"),
                           QStringLiteral("检测到卷影副本存储数据但未能通过自洽校验（已用≤已分配≤上限），"
                                          "为避免给出错误数字已跳过。请在「系统属性 › 系统保护」查看。"));
            } else {
                AddFinding(out, FeatureModule::RestorePoint, QStringLiteral("卷影副本存储"), QStringLiteral("未配置"),
                           QStringLiteral("本机当前未配置卷影副本存储（系统保护未启用或未占用空间）。"));
            }
        } else {
            // 非零退出且无可信数据:中性提示。
            AddFinding(out, FeatureModule::RestorePoint, QStringLiteral("卷影副本存储"), QStringLiteral("查询不可用"),
                       QStringLiteral("vssadmin 返回错误码 %1，未能解析卷影副本存储。"
                                      "可在「系统属性 › 系统保护」查看与调整。")
                           .arg(static_cast<int>(qr.exitCode)));
        }
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

    // 稳定键去重(v2 键不含 state):同一模块+标题+路径视为同一项,保留排序最前者(体积最大/最优先),
    // 移除其余重复(避免同一文件/目录因 state 文案不同被多次列出或重复计费)。std::unique 仅处理相邻,
    // 故用集合判定以适应任意顺序。
    {
        QSet<QString> seenKeys;
        out.erase(std::remove_if(out.begin(), out.end(),
                      [&seenKeys](const FeatureFinding& f) {
                          const QString key = FindingStableKey(f);
                          if (seenKeys.contains(key)) {
                              return true;
                          }
                          seenKeys.insert(key);
                          return false;
                      }),
                  out.end());
    }

    // 跨模块 vhdx 归属去重:`wsl --import` 导入的发行版常把 ext4.vhdx 放在 Documents 等位置,会被
    // DockerWsl(Lxss 注册表权威归属)与 VirtualMachineImages(Documents 通用 *.vhdx 扫描)同时报告,
    // 造成同一文件双计与归属冲突。两者现均报实占体积(D6 起 VirtualMachineImages 改用 allocated,与
    // DockerWsl 同口径),故去重主因不再是"口径不一"而是避免双计。以 DockerWsl 的归属为准,移除
    // VirtualMachineImages 中指向同一 vhdx 的重复项(DockerWsl 信息更丰富:命名发行版 + 实占 + 压缩建议)。
    {
        QSet<QString> dockerWslVhdx;
        for (const FeatureFinding& f : out) {
            if (f.module == FeatureModule::DockerWsl && !f.path.isEmpty()) {
                dockerWslVhdx << NormalizeDiskPathKey(f.path);
            }
        }
        if (!dockerWslVhdx.isEmpty()) {
            out.erase(std::remove_if(out.begin(), out.end(),
                          [&dockerWslVhdx](const FeatureFinding& f) {
                              return f.module == FeatureModule::VirtualMachineImages && !f.path.isEmpty() &&
                                     dockerWslVhdx.contains(NormalizeDiskPathKey(f.path));
                          }),
                      out.end());
        }
    }
    return out;
}

}  // namespace

FeatureHubWidget::FeatureHubWidget(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    // 首次使用向导条(可关闭,持久化 featureHub/onboardingSeen)。可见性在 LoadSettings 里按标志设置。
    onboardingBanner_ = new QFrame(this);
    onboardingBanner_->setObjectName(QStringLiteral("OnboardingBanner"));
    auto* onbLayout = new QVBoxLayout(onboardingBanner_);
    onbLayout->setContentsMargins(16, 12, 16, 12);
    onbLayout->setSpacing(6);
    auto* onbTitle = new QLabel(QStringLiteral("首次使用空间工具箱？"), onboardingBanner_);
    onbTitle->setObjectName(QStringLiteral("OnboardingTitle"));
    auto* onbBody = new QLabel(QStringLiteral(
        "这里把磁盘空间工具按“清理回收 / 占用盘点 / 迁移搬家 / 检查防护”四类整理。"
        "三步上手：①从左侧选一个模块（或先点“全部体检”看全貌）；②按需填源 / 目标路径；"
        "③点“当前模块”体检，看结果后用“处理方案 / 交付”导出清单。工具箱只识别与生成方案，不会删除或迁移你的文件。"), onboardingBanner_);
    onbBody->setObjectName(QStringLiteral("OnboardingBody"));
    onbBody->setWordWrap(true);
    auto* onbDismiss = new QPushButton(QStringLiteral("知道了"), onboardingBanner_);
    onbDismiss->setObjectName(QStringLiteral("OnboardingDismiss"));
    auto* onbRow = new QHBoxLayout();
    onbRow->addStretch(1);
    onbRow->addWidget(onbDismiss);
    onbLayout->addWidget(onbTitle);
    onbLayout->addWidget(onbBody);
    onbLayout->addLayout(onbRow);
    connect(onbDismiss, &QPushButton::clicked, this, &FeatureHubWidget::DismissOnboarding);

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

    layout->addWidget(onboardingBanner_);
    layout->addWidget(hero);
    layout->addWidget(splitter, 1);
    LoadWorkflowState();
    LoadResultCache();
    UpdateActionState();
    UpdateModuleGuide();
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
    helpButton_ = new QPushButton(QStringLiteral("导览"), toolbar);
    helpButton_->setToolTip(QStringLiteral("打开空间工具箱导览：分类说明、每个模块的用法与操作流程"));

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
    actionLayout->addWidget(helpButton_);
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
    connect(helpButton_, &QPushButton::clicked, this, &FeatureHubWidget::ShowGuideDialog);
    connect(sourcePathEdit_, &QLineEdit::editingFinished, this, &FeatureHubWidget::SaveSettings);
    connect(targetPathEdit_, &QLineEdit::editingFinished, this, &FeatureHubWidget::SaveSettings);

    return toolbar;
}

QWidget* FeatureHubWidget::CreateModuleList() {
    moduleList_ = new QListWidget(this);
    // 原 objectName "DirectoryTree" 与主窗口目录树共享 QSS;改为独立 "ModuleList" 以免分类标题/列表
    // 样式污染主树,并允许本页单独设置列表外观。
    moduleList_->setObjectName(QStringLiteral("ModuleList"));
    moduleList_->setUniformItemSizes(true);
    moduleList_->setIconSize(QSize(16, 16));

    auto* allItem = new QListWidgetItem(app_icons::info(16), QStringLiteral("全部能力"), moduleList_);
    allItem->setData(Qt::UserRole, -1);

    // 分类标题项用 Qt::ItemIsEnabled(启用但不可选):正常渲染、不灰化;鼠标点击不选中,
    // 键盘导航即便落在其上,CurrentModule 的 value<0 分支(UserRole=-2)会当作"全部能力"显示,
    // 等价于未选具体模块——无副作用、不崩。加粗字体区分于模块项。
    QFont headerFont = moduleList_->font();
    headerFont.setBold(true);

    const QVector<ModuleInfo> all = AllModules();
    for (const CategoryInfo& cat : AllCategories()) {
        int count = 0;
        for (const ModuleInfo& info : all) {
            if (info.category == cat.category) {
                ++count;
            }
        }
        auto* header = new QListWidgetItem(QStringLiteral("%1（%2）").arg(cat.label).arg(count), moduleList_);
        header->setFont(headerFont);
        header->setData(Qt::UserRole, -2);
        header->setFlags(Qt::ItemIsEnabled);

        for (const ModuleInfo& info : all) {
            if (info.category != cat.category) {
                continue;
            }
            auto* item = new QListWidgetItem(app_icons::folder(16), info.title, moduleList_);
            item->setToolTip(info.description);
            item->setData(Qt::UserRole, ModuleToInt(info.module));
        }
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

    // 模块说明面板(常驻,会用·核心):选中模块即显示"是什么/怎么用/提示",
    // 选中"全部能力"显示工具箱总览。置于指标行之上,树 stretch 1 不受影响。
    moduleGuideFrame_ = new QFrame(host);
    moduleGuideFrame_->setObjectName(QStringLiteral("ModuleGuide"));
    auto* guideLayout = new QVBoxLayout(moduleGuideFrame_);
    guideLayout->setContentsMargins(12, 8, 12, 8);
    guideLayout->setSpacing(3);
    guidePurposeLabel_ = new QLabel(moduleGuideFrame_);
    guidePurposeLabel_->setObjectName(QStringLiteral("ModuleGuidePurpose"));
    guidePurposeLabel_->setWordWrap(true);
    guidePurposeLabel_->setTextFormat(Qt::PlainText);
    guideHowToUseLabel_ = new QLabel(moduleGuideFrame_);
    guideHowToUseLabel_->setObjectName(QStringLiteral("ModuleGuideHowTo"));
    guideHowToUseLabel_->setWordWrap(true);
    guideHowToUseLabel_->setTextFormat(Qt::PlainText);
    guideTipsLabel_ = new QLabel(moduleGuideFrame_);
    guideTipsLabel_->setObjectName(QStringLiteral("ModuleGuideTips"));
    guideTipsLabel_->setWordWrap(true);
    guideTipsLabel_->setTextFormat(Qt::PlainText);
    guideLayout->addWidget(guidePurposeLabel_);
    guideLayout->addWidget(guideHowToUseLabel_);
    guideLayout->addWidget(guideTipsLabel_);

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

    layout->addWidget(moduleGuideFrame_);
    layout->addWidget(metricRow);
    layout->addWidget(resultTree_, 1);
    return host;
}

void FeatureHubWidget::UpdateModuleGuide() {
    if (guidePurposeLabel_ == nullptr || guideHowToUseLabel_ == nullptr || guideTipsLabel_ == nullptr) {
        return;
    }
    bool hasModule = false;
    const FeatureModule module = CurrentModule(hasModule);
    ModuleInfo info;
    if (hasModule && FindModuleInfo(module, info)) {
        guidePurposeLabel_->setText(QStringLiteral("是什么：") + info.purpose);
        guideHowToUseLabel_->setText(QStringLiteral("怎么用：") + info.howToUse);
        guideTipsLabel_->setText(QStringLiteral("提示：") + info.tips + QStringLiteral(" ") + GuideSafetyNote());
        return;
    }
    // 未选具体模块("全部能力"或分类标题):显示工具箱总览 + 三步上手。
    guidePurposeLabel_->setText(QStringLiteral("是什么：空间工具箱集中了清理回收、占用盘点、迁移搬家、检查防护四类只读工具，帮你找出磁盘上哪些东西占地方、能不能清 / 搬 / 查。"));
    guideHowToUseLabel_->setText(QStringLiteral("怎么用：①先点“全部体检”看全貌，或从左侧选一个模块细看\n②可选填源 / 目标路径（部分模块需要）\n③看结果后用“处理方案 / 方案包 / 交付”导出处置清单"));
    guideTipsLabel_->setText(QStringLiteral("提示：") + GuideSafetyNote());
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
    if (onboardingBanner_ != nullptr) {
        // 默认未看过→显示向导条;点过"知道了"→持久化 true 后隐藏。
        onboardingBanner_->setVisible(!settings.value(QStringLiteral("featureHub/onboardingSeen"), false).toBool());
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
    // 稳定键版本迁移:keyVersion<2 时,既有忽略/已处理/基线键与备注键均为含 state 的 v1 格式,需逐条
    // 迁移为 v2(去 state)。MigrateV1KeyToV2 按 FindingStableKey 同口径规范化模块/标题/路径,迁移键与新生键
    // 逐字节相同,既有标记全部保留(非清空);仅 state 段不同而塌缩的同键备注做合并,不丢文本。
    const bool migrate = settings.value(QStringLiteral("featureHub/keyVersion"), 0).toInt() < 2;
    const auto loadKeySet = [&settings, migrate](const char* settingKey) {
        QSet<QString> result;
        const QStringList raw = settings.value(QLatin1String(settingKey)).toStringList();
        for (const QString& key : raw) {
            const QString trimmed = key.trimmed();
            if (trimmed.isEmpty()) {
                continue;
            }
            const QString v2 = migrate ? MigrateV1KeyToV2(trimmed) : trimmed;
            if (!v2.isEmpty()) {
                result.insert(v2);
            }
        }
        return result;
    };
    ignoredFindingKeys_ = loadKeySet("featureHub/ignoredKeys");
    completedFindingKeys_ = loadKeySet("featureHub/completedKeys");
    baselineFindingKeys_ = loadKeySet("featureHub/baselineKeys");
    baselineCapturedAt_ = settings.value(QStringLiteral("featureHub/baselineCapturedAt")).toString();
    const int noteCount = settings.beginReadArray(QStringLiteral("featureHub/notes"));
    for (int index = 0; index < noteCount; ++index) {
        settings.setArrayIndex(index);
        QString key = settings.value(QStringLiteral("key")).toString();
        const QString note = settings.value(QStringLiteral("note")).toString();
        if (migrate) {
            key = MigrateV1KeyToV2(key);
        }
        if (!key.trimmed().isEmpty() && !note.trimmed().isEmpty()) {
            // 两条 v1 键仅 state 段不同会塌缩为同一 v2 键(用户先在 stateA 加备注,重扫 state 变化后再加备注):
            // 合并而非覆盖,避免静默丢弃旧备注(QMap::insert 会替值)。
            if (findingNotes_.contains(key)) {
                findingNotes_[key] = findingNotes_.value(key) + QStringLiteral("\n") + note;
            } else {
                findingNotes_.insert(key, note);
            }
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
    settings.setValue(QStringLiteral("featureHub/keyVersion"), 2);  // v2 键(去 state),持久化版本以停止重复迁移。
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
    // 镜像 MainWindow health worker 的 wait+detach 降级:轮询完成标志最多约 1 秒,worker 已结束则即时
    // join 回收;超时(极慢盘 / Restart Manager / 未来的 QProcess 阻塞调用)则 detach 解除阻塞,退出靠
    // quitting_ 守卫 + 回投前 QPointer 判空兜底,杜绝「关窗卡死」。预算取 1s(而非 health 的 2s):工具箱
    // cancel 先置且粒度更细(每模块间 / 每目录条目),正常扫描秒级 bail,1s 足够;串行后总关窗最坏 ~3s,不致假死。
    constexpr int kPollRounds = 10;   // 10 × 100ms = 1s。
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
    // 若 closeEvent 已调 RequestShutdownForQuit 完成收尾(join 或 detach 后 scanWorker_ 不再 joinable),
    // 跳过重复轮询——避免正常关窗后析构阶段再空等一次完整轮询预算,缩短进程退出耗时。
    // 仍 joinable(未经 closeEvent 的销毁场景,如标签页重建 / 直接销毁)时照常有界 join,防御不漏。
    quitting_.store(true);
    if (cancelFlag_ != nullptr) {
        cancelFlag_->store(true);
    }
    if (scanWorker_.joinable()) {
        JoinWorkerBounded();
    }
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
    UpdateModuleGuide();
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

void FeatureHubWidget::DismissOnboarding() {
    QSettings settings(QStringLiteral("SunnyFan"), QStringLiteral("DiskLens"));
    settings.setValue(QStringLiteral("featureHub/onboardingSeen"), true);
    if (onboardingBanner_ != nullptr) {
        onboardingBanner_->setVisible(false);
    }
}

void FeatureHubWidget::ShowGuideDialog() {
    // 与组件 B 同源生成(AllCategories + AllModules),分类与说明永不漂移。
    QString text;
    text += QStringLiteral("空间工具箱 · 导览\n\n");
    text += QStringLiteral("把磁盘空间工具按四类整理；所有模块只识别与生成方案，不会删除或迁移你的文件。\n\n");

    const QVector<ModuleInfo> all = AllModules();
    for (const CategoryInfo& cat : AllCategories()) {
        text += QStringLiteral("【%1】%2\n").arg(cat.label, cat.blurb);
        for (const ModuleInfo& info : all) {
            if (info.category != cat.category) {
                continue;
            }
            text += QStringLiteral("\n· %1\n").arg(info.title);
            text += QStringLiteral("  是什么：%1\n").arg(info.purpose);
            text += QStringLiteral("  怎么用：%1\n").arg(info.howToUse);
            text += QStringLiteral("  提示：%1\n").arg(info.tips);
        }
        text += QStringLiteral("\n");
    }
    text += QStringLiteral("\n") + GuideSafetyNote() + QStringLiteral("\n");

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("空间工具箱 · 导览"));
    dialog.resize(720, 600);

    auto* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(16, 14, 16, 14);
    layout->setSpacing(10);

    auto* textEdit = new QTextEdit(&dialog);
    textEdit->setReadOnly(true);
    textEdit->setPlainText(text);
    textEdit->setLineWrapMode(QTextEdit::WidgetWidth);
    layout->addWidget(textEdit, 1);

    auto* buttons = new QDialogButtonBox(&dialog);
    QPushButton* copyButton = buttons->addButton(QStringLiteral("复制"), QDialogButtonBox::ActionRole);
    QPushButton* closeButton = buttons->addButton(QStringLiteral("关闭"), QDialogButtonBox::RejectRole);
    closeButton->setDefault(true);
    connect(copyButton, &QPushButton::clicked, &dialog, [text]() {
        if (QApplication::clipboard() != nullptr) {
            QApplication::clipboard()->setText(text);
        }
    });
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);

    dialog.exec();
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
        lines << QStringLiteral("4. 对 ext4.vhdx 可用 Optimize-VHD(需 Hyper-V PowerShell 模块)或系统自带的 diskpart compact vdisk 压缩。");
        break;
    case FeatureModule::MediaOrganizer:
        lines << QStringLiteral("1. 按年份和媒体类型建立目录，例如 Photos\\2026、Videos\\2026。");
        lines << QStringLiteral("2. RAW+JPG 成对文件先不要拆散。");
        lines << QStringLiteral("3. 大视频建议先转移到归档盘，再用备份缺口检查确认有副本。");
        break;
    case FeatureModule::QuotaBudget:
        lines << QStringLiteral("1. 这里的“预算”是参考建议值；NTFS 卷配额的启用状态与卷默认上限已另行只读查询并列出（多为“未启用”）。");
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
