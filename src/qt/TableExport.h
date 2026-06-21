#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

namespace disk_lens::qt_ui {

/**
 * @brief 把表头与行渲染为 CSV 文本（含表头行，不含 BOM）。
 * @param headers 表头。
 * @param rows 数据行（每行一组原始单元格）。
 * @return CSV 文本。
 *
 * GUI「导出当前列表」与 D1 无头命令行导出共用此渲染，确保两者转义、字段拼接完全一致。
 */
QString RenderCsvRows(const QStringList& headers, const QVector<QStringList>& rows);

/**
 * @brief 把表头与行渲染为带内联样式的 HTML 报表（完整文档，UTF-8 不含 BOM）。
 * @param title 报表标题。
 * @param headers 表头。
 * @param rows 数据行。
 * @return 完整 HTML 文档。
 */
QString RenderHtmlTable(const QString& title, const QStringList& headers, const QVector<QStringList>& rows);

/**
 * @brief 把表头与行写为报告文件（UTF-8 BOM + CSV 或 HTML）。
 * @param path 输出文件路径。
 * @param title 报表标题（HTML 用；CSV 忽略）。
 * @param headers 表头。
 * @param rows 数据行。
 * @param asHtml true=HTML 报表，false=CSV。
 * @return 写出成功返回 true；无法打开或写入失败返回 false。
 *
 * 这是 GUI 导出与 CLI 无头导出的共用出口：编码（UTF-8 BOM）、转义、渲染均在此统一，
 * 避免 GUI 与 CLI 两套写出逻辑漂移。
 */
bool WriteTableReport(const QString& path, const QString& title,
                      const QStringList& headers, const QVector<QStringList>& rows, bool asHtml);

}  // namespace disk_lens::qt_ui
