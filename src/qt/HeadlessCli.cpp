#include "qt/HeadlessCli.h"

#include "qt/TableExport.h"
#include "core/DirectoryScanner.h"
#include "core/Format.h"
#include "core/ScanModels.h"

#include <QByteArray>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QLatin1Char>
#include <QString>
#include <QStringList>

#include <algorithm>
#include <cstdio>
#include <vector>

#include <windows.h>

namespace disk_lens::qt_ui {
namespace {

/**
 * @brief 为 GUI 子系统进程准备标准输出，使无头导出能写入调用方终端或重定向文件。
 *
 * 磁盘洞察是 GUI 子系统程序，默认无控制台。三种情形：
 *   - stdout 已是控制台句柄（从 cmd/PowerShell 直接启动并继承）：仅把控制台输出代码页设为 UTF-8。
 *   - stdout 重定向到文件/管道（> out.csv、| 等）：保持继承句柄不动，UTF-8 字节直写文件。
 *   - 无标准输出句柄（资源管理器双击等）：尽力附加父进程控制台并重定向 stdout/stderr。
 */
void PrepareConsoleForOutput() {
    const HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    const DWORD type = (hOut != nullptr && hOut != INVALID_HANDLE_VALUE)
        ? GetFileType(hOut)
        : FILE_TYPE_UNKNOWN;
    if (type == FILE_TYPE_CHAR) {
        // 已是交互控制台句柄：printf/fwrite 直达，仅需设 UTF-8 代码页。
        SetConsoleOutputCP(CP_UTF8);
    } else if (type != FILE_TYPE_DISK && type != FILE_TYPE_PIPE) {
        // 既非控制台也非重定向（双击启动等无句柄）：尽力附加父控制台。
        if (AttachConsole(ATTACH_PARENT_PROCESS)) {
            FILE* dummy = nullptr;
            freopen_s(&dummy, "CONOUT$", "w", stdout);
            freopen_s(&dummy, "CONOUT$", "w", stderr);
            SetConsoleOutputCP(CP_UTF8);
        }
    }
    // FILE_TYPE_DISK / FILE_TYPE_PIPE：重定向到文件或管道，保持继承句柄，UTF-8 字节直写。
}

QString WToQString(const std::wstring& value) {
    return QString::fromWCharArray(value.c_str(), static_cast<int>(value.size()));
}

/**
 * @brief 取文件名扩展名作为「类型」列（小写带点，与 GUI 扩展名统计键风格一致）。
 */
QString ExtensionTypeOf(const QString& name) {
    const int dot = name.lastIndexOf(QLatin1Char('.'));
    if (dot > 0 && dot < name.size() - 1) {
        return name.mid(dot).toLower();
    }
    return QStringLiteral("(无扩展名)");
}

QString FormatModifiedDate(std::int64_t msec) {
    if (msec <= 0) {
        return QStringLiteral("—");
    }
    return QDateTime::fromMSecsSinceEpoch(msec).toString(QStringLiteral("yyyy-MM-dd"));
}

void PrintStdErr(const QString& text) {
    const QByteArray utf8 = (text + QStringLiteral("\n")).toUtf8();
    std::fwrite(utf8.constData(), 1, static_cast<std::size_t>(utf8.size()), stderr);
    std::fflush(stderr);
}

void WriteUtf8StdOut(const QString& text) {
    const QByteArray utf8 = text.toUtf8();
    std::fwrite(utf8.constData(), 1, static_cast<std::size_t>(utf8.size()), stdout);
    std::fflush(stdout);
}

}  // namespace

int RunHeadlessCli(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    PrepareConsoleForOutput();

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral(
        "磁盘洞察 无头扫描 + 导出（不启动图形界面）。"));
    parser.addHelpOption();
    // main.cpp 在 GUI 初始化前已据 --cli/--headless 切入无头分支；这里把它们注册为已知
    // 选项，否则 QCommandLineParser::process() 会把 --cli 当作未知选项拒绝并 exit(1)。
    parser.addOption(QCommandLineOption(
        QStringList() << QStringLiteral("cli") << QStringLiteral("headless"),
        QStringLiteral("无头命令行模式（入口程序探测到时自动添加）。")));
    const QCommandLineOption scanOption(
        QStringList() << QStringLiteral("scan"),
        QStringLiteral("要扫描的目录路径（必需），例如 C:\\Users 或 D:\\"),
        QStringLiteral("路径"));
    const QCommandLineOption formatOption(
        QStringList() << QStringLiteral("format"),
        QStringLiteral("输出格式：csv（默认）或 html"),
        QStringLiteral("格式"), QStringLiteral("csv"));
    const QCommandLineOption outputOption(
        QStringList() << QStringLiteral("output"),
        QStringLiteral("输出文件路径；省略则输出到标准输出"),
        QStringLiteral("文件"));
    const QCommandLineOption topOption(
        QStringList() << QStringLiteral("top"),
        QStringLiteral("仅导出最大的 N 个文件（默认 1000）；0 表示全部"),
        QStringLiteral("N"), QStringLiteral("1000"));
    parser.addOption(scanOption);
    parser.addOption(formatOption);
    parser.addOption(outputOption);
    parser.addOption(topOption);
    // process() 处理 --help（打印用法并 exit(0)）与未知选项（打印错误并 exit(1)）。
    parser.process(QCoreApplication::arguments());

    if (!parser.isSet(scanOption)) {
        PrintStdErr(QStringLiteral("错误：缺少 --scan <路径>。使用 --help 查看用法。"));
        return 2;
    }

    const QString scanPath = parser.value(scanOption).trimmed();
    if (scanPath.isEmpty()) {
        PrintStdErr(QStringLiteral("错误：--scan 路径为空。"));
        return 2;
    }
    const QFileInfo scanInfo(scanPath);
    if (!scanInfo.exists() || !scanInfo.isDir()) {
        PrintStdErr(QStringLiteral("错误：路径不存在或不是目录：%1")
                        .arg(QDir::toNativeSeparators(scanPath)));
        return 3;
    }

    const QString formatValue = parser.value(formatOption).trimmed().toLower();
    const bool asHtml = formatValue == QStringLiteral("html");
    if (!asHtml && formatValue != QStringLiteral("csv")) {
        PrintStdErr(QStringLiteral("错误：--format 仅支持 csv 或 html（得到 \"%1\"）。").arg(formatValue));
        return 2;
    }

    bool topOk = false;
    const long long topN = parser.value(topOption).toLongLong(&topOk);
    if (!topOk || topN < 0) {
        PrintStdErr(QStringLiteral("错误：--top 必须是非负整数（得到 \"%1\"）。").arg(parser.value(topOption)));
        return 2;
    }

    // 同步扫描（兼容引擎）。兼容引擎跨 NTFS/exFAT/FAT32/ReFS/网络盘始终正确，
    // 无 MFT 极速通道的校验边界问题；MFT 加速保留为 GUI 优势，CLI 以正确性优先。
    PrintStdErr(QStringLiteral("扫描中：%1 ...").arg(QDir::toNativeSeparators(scanPath)));
    core::DirectoryScanner scanner;
    const std::wstring wideRoot = scanPath.toStdWString();
    core::ScanResult result = scanner.Scan(wideRoot, nullptr);
    if (!result.root) {
        PrintStdErr(QStringLiteral("错误：扫描失败，未能读取该目录（权限不足或路径无效）。"));
        return 3;
    }

    // 收集全部文件节点，按大小降序（同大小按路径稳定排序），截断到 topN。
    std::vector<const core::ScanNode*> files;
    files.reserve(4096);
    std::vector<const core::ScanNode*> stack;
    stack.reserve(4096);
    stack.push_back(result.root.get());
    while (!stack.empty()) {
        const core::ScanNode* node = stack.back();
        stack.pop_back();
        if (node == nullptr) {
            continue;
        }
        if (node->kind == core::NodeKind::File) {
            files.push_back(node);
        }
        for (const auto& child : node->children) {
            stack.push_back(child.get());
        }
    }
    std::sort(files.begin(), files.end(),
              [](const core::ScanNode* a, const core::ScanNode* b) {
                  if (a->ownBytes != b->ownBytes) {
                      return a->ownBytes > b->ownBytes;
                  }
                  return a->path < b->path;
              });
    if (topN > 0 && static_cast<std::size_t>(topN) < files.size()) {
        files.resize(static_cast<std::size_t>(topN));
    }

    // 构建行（名称/大小/类型/路径/修改时间），镜像 GUI 结果表表头。
    const QStringList headers{
        QStringLiteral("名称"),
        QStringLiteral("大小"),
        QStringLiteral("类型"),
        QStringLiteral("路径"),
        QStringLiteral("修改时间"),
    };
    QVector<QStringList> rows;
    rows.reserve(static_cast<int>(files.size()));
    for (const core::ScanNode* file : files) {
        const QString name = WToQString(file->name);
        const QString path = QDir::toNativeSeparators(WToQString(file->path));
        rows << (QStringList()
            << name
            << WToQString(core::FormatBytes(file->ownBytes))
            << ExtensionTypeOf(name)
            << path
            << FormatModifiedDate(file->lastModifiedMsec));
    }

    const QString title = QStringLiteral("磁盘洞察 分析结果（%1 项）").arg(rows.size());
    PrintStdErr(QStringLiteral("完成：文件 %1 · 目录 %2 · 跳过 %3。正在导出 %4 项…")
                    .arg(static_cast<qulonglong>(result.fileCount))
                    .arg(static_cast<qulonglong>(result.directoryCount))
                    .arg(static_cast<qulonglong>(result.errorCount))
                    .arg(rows.size()));

    const QString outputPath = parser.value(outputOption);
    if (!outputPath.isEmpty()) {
        if (!WriteTableReport(outputPath, title, headers, rows, asHtml)) {
            PrintStdErr(QStringLiteral("错误：无法写入输出文件：%1")
                            .arg(QDir::toNativeSeparators(outputPath)));
            return 4;
        }
        PrintStdErr(QStringLiteral("已导出 %1 项 → %2")
                        .arg(rows.size())
                        .arg(QDir::toNativeSeparators(outputPath)));
        return 0;
    }

    // 标准输出：UTF-8 无 BOM（便于管道与重定向）；HTML 文档头已声明 charset=utf-8。
    WriteUtf8StdOut(asHtml ? RenderHtmlTable(title, headers, rows) : RenderCsvRows(headers, rows));
    return 0;
}

}  // namespace disk_lens::qt_ui
