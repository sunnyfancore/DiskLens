#pragma once

namespace disk_lens::qt_ui {

/**
 * @brief 无头命令行模式入口：解析参数 → 同步扫描路径 → 导出 CSV/HTML。
 *
 * 不构造任何 GUI；扫描在调用线程同步完成（无事件循环）。失败返回非零退出码：
 *   - 0 成功
 *   - 2 参数错误（缺少 --scan / 非法 --format 或 --top）
 *   - 3 扫描失败（路径不存在 / 非目录 / 无法读取）
 *   - 4 写出失败（无法写入 --output 指定文件）
 *
 * @note 必须在已构造 QCoreApplication（或其派生类）后调用——参数解析依赖
 *       QCoreApplication::arguments() 与 QCommandLineParser。
 */
int RunHeadlessCli(int argc, char* argv[]);

}  // namespace disk_lens::qt_ui
