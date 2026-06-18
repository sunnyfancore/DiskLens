#include "qt/MainWindow.h"

#include <QApplication>
#include <QIcon>

/**
 * @brief Qt 市场版桌面程序入口。
 * @param argc 命令行参数数量。
 * @param argv 命令行参数数组。
 * @return 进程退出码。
 */
int main(int argc, char* argv[]) {
    // 1.25/1.5× 缩放下取整到清晰的 1× 而非整体放大到 2×,避免文字发糊、1px 线错位。
    // 必须在构造 QApplication 之前设置。
    QApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::RoundPreferFloor);

    QApplication application(argc, argv);
    application.setApplicationName(QStringLiteral("DiskLens"));
    application.setApplicationDisplayName(QStringLiteral("磁盘洞察"));
    application.setOrganizationName(QStringLiteral("SunnyFan"));
    application.setWindowIcon(QIcon(QStringLiteral(":/icons/app.ico")));

    disk_lens::qt_ui::MainWindow window;
    window.show();

    return application.exec();
}
