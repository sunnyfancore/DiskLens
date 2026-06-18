#include "qt/TreemapWidget.h"

#include "core/Format.h"

#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPoint>

#include <algorithm>
#include <iterator>
#include <numeric>

namespace disk_lens::qt_ui {

namespace {

/**
 * @brief 将宽字符串转换为 QString。
 * @param value 宽字符串。
 * @return QString 文本。
 */
QString ToQString(const std::wstring& value) {
    return QString::fromWCharArray(value.c_str(), static_cast<int>(value.size()));
}

/**
 * @brief 获取空间图使用的稳定色板。
 * @param index 项目索引。
 * @return 对应颜色。
 */
QColor PaletteColor(std::size_t index) {
    static const QColor colors[] = {
        QColor(37, 99, 235),
        QColor(13, 148, 136),
        QColor(217, 119, 6),
        QColor(220, 38, 38),
        QColor(124, 58, 237),
        QColor(8, 145, 178),
        QColor(234, 88, 12),
        QColor(79, 70, 229),
        QColor(22, 163, 74),
        QColor(219, 39, 119),
    };
    return colors[index % std::size(colors)];
}

}  // namespace

TreemapWidget::TreemapWidget(QWidget* parent) : QWidget(parent) {
    setMouseTracking(true);
    setMinimumHeight(86);
    // 默认浅色取值，主窗口会在 ApplyStyle 后立即刷新为当前主题。
    colors_.background = QColor(255, 255, 255);
    colors_.emptyText = QColor(100, 116, 139);
    colors_.cardBg = QColor(248, 250, 252);
    colors_.cardHoverBg = QColor(239, 246, 255);
    colors_.cardBorder = QColor(226, 232, 240);
    colors_.nameText = QColor(15, 23, 42);
    colors_.sizeText = QColor(100, 116, 139);
    colors_.barTrack = QColor(226, 232, 240);
    Clear(QStringLiteral("扫描完成后显示空间占比图"));
}

void TreemapWidget::SetColors(const TreemapColors& colors) {
    colors_ = colors;
    update();
}

void TreemapWidget::SetRootNode(const core::ScanNode& node) {
    blocks_.clear();
    hoveredIndex_ = -1;

    std::vector<const core::ScanNode*> children;
    children.reserve(node.children.size());
    for (const auto& child : node.children) {
        if (child && child->totalBytes > 0) {
            children.push_back(child.get());
        }
    }

    std::sort(children.begin(), children.end(), [](const core::ScanNode* left, const core::ScanNode* right) {
        return left->totalBytes > right->totalBytes;
    });

    const std::size_t limit = std::min<std::size_t>(children.size(), 80);
    blocks_.reserve(limit);
    for (std::size_t index = 0; index < limit; ++index) {
        const core::ScanNode* child = children[index];
        blocks_.push_back(Block{
            ToQString(child->name),
            ToQString(child->path),
            ToQString(core::FormatBytes(child->totalBytes)),
            child->totalBytes,
            child->kind == core::NodeKind::Directory,
            QRect(),
            PaletteColor(index),
        });
    }

    emptyMessage_ = blocks_.empty() ? QStringLiteral("当前目录没有可展示的空间占用项") : QString();
    RebuildLayout();
    update();
}

void TreemapWidget::Clear(const QString& message) {
    blocks_.clear();
    hoveredIndex_ = -1;
    emptyMessage_ = message;
    setToolTip(QString());
    update();
}

void TreemapWidget::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(rect(), colors_.background);

    if (blocks_.empty()) {
        painter.setPen(colors_.emptyText);
        painter.drawText(rect(), Qt::AlignCenter, emptyMessage_);
        return;
    }

    const QFontMetrics metrics(font());
    const bool compactMode = width() < 180;
    if (compactMode) {
        const std::uint64_t totalBytes = std::accumulate(blocks_.begin(), blocks_.end(), std::uint64_t{0}, [](std::uint64_t total, const Block& block) {
            return total + block.bytes;
        });

        QFont nameFont = painter.font();
        nameFont.setPointSizeF(std::max(8.0, nameFont.pointSizeF() - 0.5));
        QFont sizeFont = nameFont;
        sizeFont.setPointSizeF(std::max(7.2, sizeFont.pointSizeF() - 1.0));

        for (std::size_t index = 0; index < blocks_.size(); ++index) {
            const Block& block = blocks_[index];
            if (block.rect.isEmpty()) {
                continue;
            }

            const bool hovered = static_cast<int>(index) == hoveredIndex_;
            QRectF cardRect = QRectF(block.rect).adjusted(0.5, 0.5, -0.5, -0.5);
            painter.setPen(colors_.cardBorder);
            painter.setBrush(hovered ? colors_.cardHoverBg : colors_.cardBg);
            painter.drawRoundedRect(cardRect, 7.0, 7.0);

            QRect colorRect(block.rect.left() + 8, block.rect.top() + 10, 7, 7);
            painter.setPen(Qt::NoPen);
            painter.setBrush(block.color);
            painter.drawEllipse(colorRect);

            painter.setFont(nameFont);
            painter.setPen(colors_.nameText);
            const int textLeft = block.rect.left() + 21;
            const int textWidth = std::max(24, block.rect.width() - 29);
            painter.drawText(QRect(textLeft, block.rect.top() + 5, textWidth, 16), Qt::AlignLeft | Qt::AlignVCenter, metrics.elidedText(block.name, Qt::ElideRight, textWidth));

            painter.setFont(sizeFont);
            painter.setPen(colors_.sizeText);
            painter.drawText(QRect(textLeft, block.rect.top() + 21, textWidth, 14), Qt::AlignLeft | Qt::AlignVCenter, metrics.elidedText(block.sizeText, Qt::ElideRight, textWidth));

            const QRect barTrack(block.rect.left() + 8, block.rect.bottom() - 7, block.rect.width() - 16, 4);
            painter.setPen(Qt::NoPen);
            painter.setBrush(colors_.barTrack);
            painter.drawRoundedRect(barTrack, 2.0, 2.0);
            const int barWidth = totalBytes == 0 ? 0 : std::max(2, static_cast<int>((static_cast<double>(block.bytes) / static_cast<double>(totalBytes)) * barTrack.width()));
            painter.setBrush(block.color);
            painter.drawRoundedRect(QRect(barTrack.left(), barTrack.top(), std::min(barWidth, barTrack.width()), barTrack.height()), 2.0, 2.0);
        }
        return;
    }

    for (std::size_t index = 0; index < blocks_.size(); ++index) {
        const Block& block = blocks_[index];
        QColor color = block.color;
        if (static_cast<int>(index) == hoveredIndex_) {
            color = color.lighter(112);
        }

        const QRectF blockRect = QRectF(block.rect).adjusted(1.5, 1.5, -1.5, -1.5);
        if (blockRect.width() <= 2.0 || blockRect.height() <= 2.0) {
            continue;
        }

        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        painter.drawRoundedRect(blockRect, 6.0, 6.0);

        painter.setPen(static_cast<int>(index) == hoveredIndex_ ? QColor(18, 24, 38, 180) : QColor(255, 255, 255, 105));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(blockRect.adjusted(0.5, 0.5, -0.5, -0.5), 6.0, 6.0);

        if (block.rect.width() < 86 || block.rect.height() < 34) {
            continue;
        }

        const QRect textRect = block.rect.adjusted(8, 5, -8, -5);
        painter.setPen(Qt::white);
        const QString name = metrics.elidedText(block.name, Qt::ElideRight, textRect.width());
        painter.drawText(textRect, Qt::AlignLeft | Qt::AlignTop, name);

        if (block.rect.height() >= 52) {
            QFont sizeFont = painter.font();
            sizeFont.setPointSizeF(std::max(7.5, sizeFont.pointSizeF() - 1.0));
            painter.setFont(sizeFont);
            painter.setPen(QColor(255, 255, 255, 215));
            const QString size = metrics.elidedText(block.sizeText, Qt::ElideRight, textRect.width());
            painter.drawText(textRect.adjusted(0, metrics.height() + 2, 0, 0), Qt::AlignLeft | Qt::AlignTop, size);
        }
    }
}

void TreemapWidget::mouseMoveEvent(QMouseEvent* event) {
    const int index = HitTest(event->pos());
    if (index == hoveredIndex_) {
        return;
    }

    hoveredIndex_ = index;
    if (hoveredIndex_ >= 0) {
        const Block& block = blocks_[static_cast<std::size_t>(hoveredIndex_)];
        setToolTip(QStringLiteral("%1\n%2\n%3").arg(block.name, block.sizeText, block.path));
    } else {
        setToolTip(QString());
    }
    update();
}

void TreemapWidget::resizeEvent(QResizeEvent* event) {
    Q_UNUSED(event);
    RebuildLayout();
}

void TreemapWidget::leaveEvent(QEvent* event) {
    Q_UNUSED(event);
    hoveredIndex_ = -1;
    setToolTip(QString());
    update();
}

void TreemapWidget::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) {
        return;
    }

    const int index = HitTest(event->pos());
    if (index < 0) {
        return;
    }

    emit PathActivated(blocks_[static_cast<std::size_t>(index)].path);
}

void TreemapWidget::RebuildLayout() {
    if (blocks_.empty()) {
        return;
    }

    QRect area = rect().adjusted(8, 8, -8, -8);
    if (width() < 180) {
        const int rowHeight = 43;
        const int gap = 6;
        int top = area.top();
        for (std::size_t index = 0; index < blocks_.size(); ++index) {
            Block& block = blocks_[index];
            if (top + rowHeight > area.bottom() + 1) {
                block.rect = QRect();
                continue;
            }
            block.rect = QRect(area.left(), top, area.width(), rowHeight);
            top += rowHeight + gap;
        }
        return;
    }

    const std::uint64_t totalBytes = std::accumulate(blocks_.begin(), blocks_.end(), std::uint64_t{0}, [](std::uint64_t total, const Block& block) {
        return total + block.bytes;
    });

    std::uint64_t consumedBytes = 0;
    for (std::size_t index = 0; index < blocks_.size(); ++index) {
        Block& block = blocks_[index];
        const bool last = index + 1 == blocks_.size();
        if (last || totalBytes == 0) {
            block.rect = area;
        } else if (area.width() >= area.height()) {
            const int width = std::max(2, static_cast<int>((static_cast<double>(block.bytes) / static_cast<double>(totalBytes - consumedBytes)) * area.width()));
            block.rect = QRect(area.left(), area.top(), std::min(width, area.width()), area.height());
            area.setLeft(block.rect.right() + 1);
        } else {
            const int height = std::max(2, static_cast<int>((static_cast<double>(block.bytes) / static_cast<double>(totalBytes - consumedBytes)) * area.height()));
            block.rect = QRect(area.left(), area.top(), area.width(), std::min(height, area.height()));
            area.setTop(block.rect.bottom() + 1);
        }
        consumedBytes += block.bytes;
    }
}

int TreemapWidget::HitTest(const QPoint& position) const {
    for (std::size_t index = 0; index < blocks_.size(); ++index) {
        if (blocks_[index].rect.contains(position)) {
            return static_cast<int>(index);
        }
    }
    return -1;
}

}  // namespace disk_lens::qt_ui
