#include "qt/MainWindow.h"
#include "qt/HeadlessCli.h"

#include <QApplication>
#include <QCoreApplication>
#include <QIcon>
#include <QString>

/**
 * @brief Qt 市场版桌面程序入口。
 * @param argc 命令行参数数量。
 * @param argv 命令行参数数组。
 * @return 进程退出码。
 */
int main(int argc, char* argv[]) {
    // D1:在任何 GUI 初始化前探测无头命令行模式，避免无谓创建窗口、设高 DPI。
    // --cli/--headless 进入 CLI；--help/-h 也走 CLI 路径以打印用法而非启动窗口。
    bool cliMode = false;
    for (int i = 1; i < argc; ++i) {
        const QString arg = QString::fromLocal8Bit(argv[i]);
        if (arg == QStringLiteral("--cli") || arg == QStringLiteral("--headless")
            || arg == QStringLiteral("--help") || arg == QStringLiteral("-h")) {
            cliMode = true;
            break;
        }
    }

    if (cliMode) {
        // 无头模式：仅 QCoreApplication（无事件循环），扫描同步完成后直接返回退出码。
        QCoreApplication application(argc, argv);
        application.setApplicationName(QStringLiteral("DiskLens"));
        return disk_lens::qt_ui::RunHeadlessCli(argc, argv);
    }

    // 1.25/1.5× 缩放下取整到清晰的 1× 而非整体放大到 2×,避免文字发糊、1px 线错位。
    // 必须在构造 QApplication 之前设置。
    QApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::RoundPreferFloor);

    QApplication application(argc, argv);
    application.setApplicationName(QStringLiteral("DiskLens"));
    application.setApplicationDisplayName(QStringLiteral("磁盘洞察"));
    application.setOrganizationName(QStringLiteral("SunnyFan"));
    application.setWindowIcon(QIcon(QStringLiteral(":/icons/app.ico")));
    // F2 系统托盘:关闭「最后一个可见窗口关闭即退出」默认行为。最小化到托盘时主窗口 hide()
    // (而非 close),真实退出改由 closeEvent 末尾显式 QCoreApplication::quit() 触发——否则从托盘
    // 「退出」时窗口已隐藏,close() 不会触发 lastWindowClosed,application.exec() 永不返回,进程僵死。
    application.setQuitOnLastWindowClosed(false);

    disk_lens::qt_ui::MainWindow window;
    window.show();

    return application.exec();
}
