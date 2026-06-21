#include "qt/TableExport.h"

#include <fstream>
#include <locale>

namespace disk_lens::qt_ui {
namespace {

/**
 * @brief 将文本转义为 CSV 字段（用双引号包裹，内部双引号翻倍）。
 */
QString EscapeCsv(const QString& value) {
    QString escaped = value;
    escaped.replace(QStringLiteral("\""), QStringLiteral("\"\""));
    return QStringLiteral("\"%1\"").arg(escaped);
}

/**
 * @brief 转义 HTML 文本（& < > " '）。
 */
QString EscapeHtml(const QString& value) {
    QString escaped = value;
    escaped.replace(QStringLiteral("&"), QStringLiteral("&amp;"));
    escaped.replace(QStringLiteral("<"), QStringLiteral("&lt;"));
    escaped.replace(QStringLiteral(">"), QStringLiteral("&gt;"));
    escaped.replace(QStringLiteral("\""), QStringLiteral("&quot;"));
    escaped.replace(QStringLiteral("'"), QStringLiteral("&#39;"));
    return escaped;
}

}  // namespace

QString RenderCsvRows(const QStringList& headers, const QVector<QStringList>& rows) {
    QStringList headerCells;
    headerCells.reserve(headers.size());
    for (const QString& header : headers) {
        headerCells << EscapeCsv(header);
    }
    QString out = headerCells.join(QStringLiteral(",")) + QStringLiteral("\n");
    for (const QStringList& row : rows) {
        QStringList cells;
        cells.reserve(row.size());
        for (const QString& cell : row) {
            cells << EscapeCsv(cell);
        }
        out += cells.join(QStringLiteral(",")) + QStringLiteral("\n");
    }
    return out;
}

QString RenderHtmlTable(const QString& title, const QStringList& headers, const QVector<QStringList>& rows) {
    QString out;
    out += QStringLiteral("<!DOCTYPE html><html><head><meta charset=\"utf-8\">");
    out += QStringLiteral("<title>") + EscapeHtml(title) + QStringLiteral("</title>");
    out += QStringLiteral("<style>");
    out += QStringLiteral("body{font-family:'Microsoft YaHei','Segoe UI',Arial,sans-serif;margin:24px;color:#222;}");
    out += QStringLiteral("h1{font-size:18px;margin:0 0 12px;}");
    out += QStringLiteral("table{border-collapse:collapse;width:100%;font-size:13px;}");
    out += QStringLiteral("th,td{border:1px solid #ddd;padding:6px 10px;text-align:left;vertical-align:top;}");
    out += QStringLiteral("th{background:#2c3e50;color:#fff;}");
    out += QStringLiteral("tr:nth-child(even){background:#f6f8fa;}");
    out += QStringLiteral("</style></head><body><h1>") + EscapeHtml(title) + QStringLiteral("</h1>");
    out += QStringLiteral("<table><thead><tr>");
    for (const QString& header : headers) {
        out += QStringLiteral("<th>") + EscapeHtml(header) + QStringLiteral("</th>");
    }
    out += QStringLiteral("</tr></thead><tbody>");
    for (const QStringList& row : rows) {
        out += QStringLiteral("<tr>");
        for (const QString& cell : row) {
            out += QStringLiteral("<td>") + EscapeHtml(cell) + QStringLiteral("</td>");
        }
        out += QStringLiteral("</tr>");
    }
    out += QStringLiteral("</tbody></table></body></html>");
    return out;
}

bool WriteTableReport(const QString& path, const QString& title,
                      const QStringList& headers, const QVector<QStringList>& rows, bool asHtml) {
    std::wofstream file(path.toStdWString(), std::ios::binary);
    if (!file.is_open()) {
        return false;
    }
    file.imbue(std::locale(".UTF-8"));
    file << L"\xfeff";  // UTF-8 BOM，Excel 双击打开含中文的 CSV 时不会乱码。
    file << (asHtml ? RenderHtmlTable(title, headers, rows) : RenderCsvRows(headers, rows)).toStdWString();
    return static_cast<bool>(file);
}

}  // namespace disk_lens::qt_ui
