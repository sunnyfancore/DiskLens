#include "qt/FileAgeHistogramWidget.h"

#include "core/Format.h"

#include <QFont>
#include <QFontMetrics>
#include <QPainter>
#include <QPaintEvent>
#include <QPen>
#include <QPointF>
#include <QRectF>

#include <algorithm>
#include <numeric>

namespace disk_lens::qt_ui {

namespace {

/**
 * @brief 宽字符串转 QString。
 */
QString ToQString(const std::wstring& value) {
    return QString::fromWCharArray(value.c_str(), static_cast<int>(value.size()));
}

// 7 个分带(新→旧)的柱体固定色:由新到旧从青蓝渐变到红橙,直观表达数据"冷热"
// (越新越偏青/蓝=近期活跃,越旧越偏橙/红=长期冷数据)。与 CategoryDonutWidget 扇区、
// TreemapWidget 数据色块同属"数据色块保持彩色"取向,不随主题变化。
const QColor kBandColors[7] = {
    QColor(13, 148, 136),   // 7 天内      teal(最热)
    QColor(8, 145, 178),    // 7-30 天     cyan
    QColor(37, 99, 235),    // 30-90 天    blue
    QColor(124, 58, 237),   // 90-180 天   violet
    QColor(217, 119, 6),    // 180 天-1年  amber
    QColor(234, 88, 12),    // 1-3 年      orange
    QColor(220, 38, 38),    // >3 年       red(最冷)
};

QColor ColorForBand(int index) {
    if (index < 0 || index >= 7) {
        return kBandColors[6];
    }
    return kBandColors[index];
}

}  // namespace

FileAgeHistogramWidget::FileAgeHistogramWidget(QWidget* parent) : QWidget(parent) {
    setMinimumSize(320, 200);
    // 默认浅色取值;主窗口 ApplyStyle 后会立即刷新为当前主题。
    colors_.background = QColor(255, 255, 255);
    colors_.axisLine = QColor(226, 232, 240);
    colors_.barTopValue = QColor(15, 23, 42);
    colors_.bandLabel = QColor(15, 23, 42);
    colors_.countLabel = QColor(100, 116, 139);
    colors_.caption = QColor(100, 116, 139);
    Clear(QStringLiteral("扫描完成后显示文件年龄分布"));
}

void FileAgeHistogramWidget::SetColors(const HistogramColors& colors) {
    colors_ = colors;
    update();
}

void FileAgeHistogramWidget::SetBuckets(std::vector<core::AgeBucket> buckets) {
    buckets_ = std::move(buckets);
    emptyMessage_.clear();
    update();
}

void FileAgeHistogramWidget::Clear(const QString& message) {
    buckets_.clear();
    emptyMessage_ = message;
    update();
}

void FileAgeHistogramWidget::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(rect(), colors_.background);

    if (buckets_.empty()) {
        painter.setPen(colors_.caption);
        painter.drawText(rect(), Qt::AlignCenter | Qt::TextWordWrap, emptyMessage_);
        return;
    }

    const std::uint64_t totalBytes = std::accumulate(buckets_.begin(), buckets_.end(), std::uint64_t{0},
        [](std::uint64_t sum, const core::AgeBucket& bucket) { return sum + bucket.totalBytes; });
    if (totalBytes == 0) {
        painter.setPen(colors_.caption);
        painter.drawText(rect(), Qt::AlignCenter, QStringLiteral("暂无修改时间数据"));
        return;
    }

    const auto maxIt = std::max_element(buckets_.begin(), buckets_.end(),
        [](const core::AgeBucket& left, const core::AgeBucket& right) { return left.totalBytes < right.totalBytes; });
    const std::uint64_t maxBytes = (maxIt != buckets_.end()) ? maxIt->totalBytes : 0;
    if (maxBytes == 0) {
        return;  // totalBytes>0 已保证 maxBytes>0,此处仅为保险。
    }

    const qreal margin = 18.0;
    const qreal topPad = 26.0;     // 柱顶"大小"数值标签的预留高度。
    const qreal bottomPad = 64.0;  // 横轴分带名 + 文件数标签的预留高度。
    const qreal leftEdge = margin;
    const qreal rightEdge = static_cast<qreal>(width()) - margin;
    const qreal baseline = static_cast<qreal>(height()) - bottomPad;
    const qreal topLine = margin + topPad;
    const qreal plotW = rightEdge - leftEdge;
    const qreal plotH = baseline - topLine;
    if (plotW <= 8.0 || plotH <= 8.0) {
        return;  // 控件过小,跳过绘制(空状态已处理)。
    }

    const int n = static_cast<int>(buckets_.size());
    const qreal gap = 12.0;
    const qreal barW = (plotW - gap * static_cast<qreal>(n - 1)) / static_cast<qreal>(n);
    if (barW < 2.0) {
        return;  // 柱过窄(控件横向过窄),跳过绘制,避免负宽度。
    }

    // 横轴基线。
    painter.setPen(QPen(colors_.axisLine, 1.0));
    painter.drawLine(QPointF(leftEdge, baseline), QPointF(rightEdge, baseline));

    QFont boldFont = font();
    boldFont.setBold(true);
    QFont smallFont = font();
    smallFont.setPointSizeF(std::max(7.0, smallFont.pointSizeF() - 1.0));
    const QFontMetrics smallMetrics(smallFont);

    for (int i = 0; i < n; ++i) {
        const core::AgeBucket& bucket = buckets_[static_cast<std::size_t>(i)];
        const qreal x = leftEdge + static_cast<qreal>(i) * (barW + gap);
        const qreal h = (static_cast<double>(bucket.totalBytes) / static_cast<double>(maxBytes)) * plotH;
        const qreal labelSlotW = barW + gap;

        // 柱体。
        if (h > 0.5) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(ColorForBand(i));
            painter.drawRoundedRect(QRectF(x, baseline - h, barW, h), 4.0, 4.0);
        }

        // 柱顶"大小"数值标签(仅非零带;直接贴在该柱顶端上方)。
        if (bucket.totalBytes > 0) {
            painter.setPen(colors_.barTopValue);
            painter.setFont(boldFont);
            const QRectF valueRect(x - gap * 0.5, baseline - h - topPad, labelSlotW, topPad);
            painter.drawText(valueRect, Qt::AlignHCenter | Qt::AlignBottom, ToQString(core::FormatBytes(bucket.totalBytes)));
        }

        // 横轴分带名(小号,允许换行以容纳 "180 天-1 年" 这类较长名)。
        painter.setPen(colors_.bandLabel);
        painter.setFont(smallFont);
        const QRectF labelRect(x - gap * 0.5, baseline + 6.0, labelSlotW, 30.0);
        painter.drawText(labelRect, Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap,
                         ToQString(bucket.label));

        // 文件数(次色,过长则省略)。
        painter.setPen(colors_.countLabel);
        painter.setFont(smallFont);
        const QString countText = QString::number(static_cast<qulonglong>(bucket.fileCount)) + QStringLiteral(" 个");
        const QRectF countRect(x - gap * 0.5, baseline + 38.0, labelSlotW, 18.0);
        painter.drawText(countRect, Qt::AlignHCenter | Qt::AlignTop,
                         smallMetrics.elidedText(countText, Qt::ElideRight, static_cast<int>(labelSlotW)));
    }
}

}  // namespace disk_lens::qt_ui
