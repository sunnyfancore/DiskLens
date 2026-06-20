#include "qt/CategoryDonutWidget.h"

#include "core/Format.h"

#include <QFont>
#include <QFontMetrics>
#include <QPainter>
#include <QPaintEvent>
#include <QPen>
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

// 分类固定色板(下标 = FileCategory 枚举值)。中明度饱和色,浅/暗皮肤下都清晰;
// 与 TreemapWidget::PaletteColor、AppIcons 调色同源,观感不突兀。
const QColor kCategoryColors[9] = {
    QColor(37, 99, 235),    // 图片   blue
    QColor(220, 38, 38),    // 视频   red
    QColor(124, 58, 237),   // 音频   violet
    QColor(8, 145, 178),    // 文档   cyan
    QColor(217, 119, 6),    // 压缩   amber
    QColor(234, 88, 12),    // 安装包 orange
    QColor(219, 39, 119),   // 游戏   pink
    QColor(5, 150, 105),    // 开发   green
    QColor(100, 116, 139),  // 其他   slate
};

QColor ColorForCategory(core::FileCategory category) {
    const int idx = static_cast<int>(category);
    if (idx < 0 || idx >= 9) {
        return kCategoryColors[8];
    }
    return kCategoryColors[idx];
}

}  // namespace

CategoryDonutWidget::CategoryDonutWidget(QWidget* parent) : QWidget(parent) {
    setMinimumSize(220, 220);
    // 默认浅色取值;主窗口 ApplyStyle 后会立即刷新为当前主题。
    colors_.background = QColor(255, 255, 255);
    colors_.trackBg = QColor(226, 232, 240);
    colors_.centerText = QColor(15, 23, 42);
    colors_.centerCaption = QColor(100, 116, 139);
    colors_.labelText = QColor(15, 23, 42);
    colors_.labelValue = QColor(100, 116, 139);
    colors_.swatchBorder = QColor(226, 232, 240);
    Clear(QStringLiteral("扫描完成后显示类型占比"));
}

void CategoryDonutWidget::SetColors(const DonutColors& colors) {
    colors_ = colors;
    update();
}

void CategoryDonutWidget::SetCategories(std::vector<core::CategorySlice> slices) {
    slices_ = std::move(slices);
    emptyMessage_.clear();
    RebuildLayout();
    update();
}

void CategoryDonutWidget::Clear(const QString& message) {
    slices_.clear();
    emptyMessage_ = message;
    update();
}

void CategoryDonutWidget::resizeEvent(QResizeEvent* event) {
    Q_UNUSED(event);
    RebuildLayout();
}

void CategoryDonutWidget::RebuildLayout() {
    geometry_ = Geometry();
    if (width() < 32 || height() < 32) {
        return;
    }

    const qreal margin = 12.0;
    const qreal availableW = static_cast<qreal>(width()) - 2.0 * margin;
    const qreal availableH = static_cast<qreal>(height()) - 2.0 * margin;
    if (availableW <= 24.0 || availableH <= 24.0) {
        return;
    }

    // 环形占左侧(约一半宽),图例占右侧。
    const qreal donutSide = std::min(availableH, availableW * 0.52);
    const qreal outerR = std::max(8.0, donutSide / 2.0);
    const qreal innerR = outerR * 0.62;
    const QPointF center(margin + donutSide / 2.0, margin + availableH / 2.0);
    const QRectF donutRect(center.x() - outerR, center.y() - outerR, outerR * 2.0, outerR * 2.0);
    const qreal legendLeft = margin + donutSide + 10.0;
    const QRectF legendRect(legendLeft, margin, std::max(0.0, static_cast<qreal>(width()) - legendLeft - margin), availableH);

    geometry_.donutRect = donutRect;
    geometry_.center = center;
    geometry_.outerR = outerR;
    geometry_.innerR = innerR;
    geometry_.legendRect = legendRect;
}

void CategoryDonutWidget::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(rect(), colors_.background);

    if (slices_.empty()) {
        painter.setPen(colors_.centerCaption);
        painter.drawText(rect(), Qt::AlignCenter | Qt::TextWordWrap, emptyMessage_);
        return;
    }

    if (geometry_.outerR <= 0.0) {
        return;  // 几何过小,跳过绘制(空状态已处理)。
    }

    const std::uint64_t totalBytes = std::accumulate(slices_.begin(), slices_.end(), std::uint64_t{0},
        [](std::uint64_t sum, const core::CategorySlice& slice) { return sum + slice.totalBytes; });
    if (totalBytes == 0) {
        painter.setPen(colors_.centerCaption);
        painter.drawText(rect(), Qt::AlignCenter, QStringLiteral("无占用数据"));
        return;
    }

    // 轨道整圈(淡色),让极小扇区也可见、整体更精致。
    painter.setPen(Qt::NoPen);
    painter.setBrush(colors_.trackBg);
    painter.drawEllipse(geometry_.center, geometry_.outerR, geometry_.outerR);

    // 扇区:drawPie 用 1/16 度,0°=3 点钟、正值=逆时针。从 12 点钟(90°)起、顺时针铺开(负 span)。
    // 每段 span 取整会丢小数,且 <1/5760 的极小扇区会被跳过,累加后不足整圈(5760);最后再用
    // 末段颜色补一笔把缺口填回 12 点钟,否则首尾接缝会露出 trackBg 形成细缝(镜像 TreemapWidget
    // 让末块吞掉取整余量的做法)。
    const QRectF& pie = geometry_.donutRect;
    const int fullCircle16 = 5760;
    const int startAnchor16 = 90 * 16;
    int startAngle16 = startAnchor16;
    QColor lastColor = ColorForCategory(slices_.front().category);  // 兜底:循环中至少绘制一段(totalBytes>0)。
    for (const core::CategorySlice& slice : slices_) {
        if (slice.totalBytes == 0) {
            continue;
        }
        const int span16 = static_cast<int>((static_cast<double>(slice.totalBytes) / static_cast<double>(totalBytes)) * static_cast<double>(fullCircle16));
        if (span16 <= 0) {
            continue;
        }
        lastColor = ColorForCategory(slice.category);
        painter.setPen(Qt::NoPen);
        painter.setBrush(lastColor);
        painter.drawPie(pie, startAngle16, -span16);
        startAngle16 -= span16;
    }
    // 收尾:用最后绘制的扇区颜色补齐整圈余量(消除取整/跳过极小扇区造成的接缝)。
    const int remainder16 = startAngle16 - (startAnchor16 - fullCircle16);
    if (remainder16 > 0) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(lastColor);
        painter.drawPie(pie, startAngle16, -remainder16);
    }

    // 中心孔:用背景色填一个圆,把 pie 的实心盘挖成环。
    painter.setBrush(colors_.background);
    painter.setPen(Qt::NoPen);
    painter.drawEllipse(geometry_.center, geometry_.innerR, geometry_.innerR);

    // 中心文字:上半行总大小(粗体主色),下半行说明(次色)。
    const QRectF holeRect(geometry_.center.x() - geometry_.innerR, geometry_.center.y() - geometry_.innerR,
                          geometry_.innerR * 2.0, geometry_.innerR * 2.0);
    QFont totalFont = painter.font();
    totalFont.setBold(true);
    painter.setFont(totalFont);
    painter.setPen(colors_.centerText);
    painter.drawText(holeRect.adjusted(0, 0, 0, -holeRect.height() * 0.22), Qt::AlignCenter,
                     ToQString(core::FormatBytes(totalBytes)));
    QFont captionFont = totalFont;
    captionFont.setBold(false);
    captionFont.setPointSizeF(std::max(7.0, captionFont.pointSizeF() - 2.0));
    painter.setFont(captionFont);
    painter.setPen(colors_.centerCaption);
    painter.drawText(holeRect.adjusted(0, holeRect.height() * 0.20, 0, 0), Qt::AlignCenter,
                     QStringLiteral("总占用"));

    // 右侧图例:色块 + 分类名 + 大小与占比。
    const QFontMetrics metrics(font());
    const qreal rowH = 22.0;
    const qreal swatch = 12.0;
    const QRectF& legend = geometry_.legendRect;
    qreal y = legend.top();
    for (const core::CategorySlice& slice : slices_) {
        if (y + rowH > legend.bottom() + 1.0) {
            break;  // 高度不足则省略剩余行(分类≤9,通常放得下)。
        }
        const QColor col = ColorForCategory(slice.category);
        // 色块。
        painter.setPen(QPen(colors_.swatchBorder, 1.0));
        painter.setBrush(col);
        painter.drawRoundedRect(QRectF(legend.left(), y + (rowH - swatch) / 2.0, swatch, swatch), 3.0, 3.0);
        // 大小 + 占比(先算宽度,决定分类名可用宽度)。
        const double pct = (static_cast<double>(slice.totalBytes) / static_cast<double>(totalBytes)) * 100.0;
        const QString sizeText = ToQString(core::FormatBytes(slice.totalBytes))
                                 + QStringLiteral("  ") + QString::number(pct, 'f', 1) + QStringLiteral("%");
        const int sizeWidth = metrics.horizontalAdvance(sizeText);
        const int nameWidth = std::max(0, static_cast<int>(legend.width() - swatch - 8.0) - sizeWidth - 10);
        // 分类名(左对齐,过长省略)。
        painter.setPen(colors_.labelText);
        painter.drawText(QRectF(legend.left() + swatch + 8.0, y, nameWidth, rowH), Qt::AlignLeft | Qt::AlignVCenter,
                         metrics.elidedText(ToQString(slice.name), Qt::ElideRight, nameWidth));
        // 大小与占比(右对齐)。
        painter.setPen(colors_.labelValue);
        painter.drawText(QRectF(legend.left(), y, legend.width(), rowH), Qt::AlignRight | Qt::AlignVCenter, sizeText);
        y += rowH;
    }
}

}  // namespace disk_lens::qt_ui
