#include "qt/AppIcons.h"

#include <QGuiApplication>
#include <QHash>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QScreen>
#include <QString>

#include <cmath>
#include <functional>

namespace disk_lens::qt_ui::app_icons {

namespace {

/**
 * @brief 统一的 16 单位逻辑绘图网格，所有图标按此网格设计，再按目标尺寸缩放。
 */
constexpr qreal kGrid = 16.0;

/**
 * @brief 固定调色板：内容类图标使用，三套皮肤卡片底色上均清晰可读。
 */
const QColor kFolderBack(37, 99, 235);    // blue-600：文件夹背板与标签
const QColor kFolderFront(59, 130, 246);  // blue-500：文件夹正面
const QColor kGlyphStroke(100, 116, 139); // slate-500：文件 / 磁盘描边
const QColor kGlyphDetail(148, 163, 184); // slate-400：文件内文字线 / 信息环
const QColor kAccentDot(59, 130, 246);    // blue-500：磁盘指示灯

/**
 * @brief 取主屏幕 devicePixelRatio，HiDPI 下按其放大像素图以保证清晰。
 * @return 设备像素比，至少为 1。
 */
qreal PrimaryDpr() {
    QScreen* screen = QGuiApplication::primaryScreen();
    return (screen != nullptr) ? qMax(qreal(1), screen->devicePixelRatio()) : qreal(1);
}

// 图标缓存,按 (名称,尺寸,颜色) 键保存,避免目录树 / 表格大量填充时反复重绘。
// 提到文件作用域,以便 InvalidateCache() 在屏幕换接 / DPI 变化时清空,让图标按新 devicePixelRatio 重新渲染。
QHash<QString, QIcon> g_iconCache;

/**
 * @brief 渲染并缓存一个图标。
 *
 * 同一个「键」只绘制一次，避免目录树 / 表格填充时反复生成像素图。键由调用方拼接，需包含影响
 * 绘制的全部参数（名称、尺寸、颜色）。
 * @param key 缓存键。
 * @param px 图标逻辑边长。
 * @param paint 在已缩放到 16 单位网格上的 QPainter 上完成实际绘制。
 * @return 图标。
 */
QIcon MakeIcon(const QString& key, int px, const std::function<void(QPainter*)>& paint) {
    auto& cache = g_iconCache;
    const auto it = cache.constFind(key);
    if (it != cache.constEnd()) {
        return it.value();
    }

    // 取主屏 DPR 与 2.0 的较大者作为渲染基线。主屏 1× 而副屏 2×(混 DPI 多显示器)时,
    // 像素图按 2× 设备像素绘制:副屏 1:1 清晰,1× 主屏由 Qt 干净地下采样亦清晰。
    const qreal dpr = qMax(PrimaryDpr(), 2.0);
    QPixmap pixmap(static_cast<int>(px * dpr), static_cast<int>(px * dpr));
    pixmap.setDevicePixelRatio(dpr);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.scale(px / kGrid, px / kGrid);
    paint(&painter);
    painter.end();

    QIcon icon(pixmap);
    cache.insert(key, icon);
    return icon;
}

/**
 * @brief 把颜色转成缓存键片段（#RRGGBB）。
 * @param color 颜色。
 * @return 颜色的十六进制文本。
 */
QString ColorKey(const QColor& color) {
    return color.name();
}

/**
 * @brief 配置一组圆角连接的描边画笔。
 * @param color 描边颜色。
 * @param width 线宽。
 * @return 配置好的画笔。
 */
QPen RoundPen(const QColor& color, qreal width) {
    QPen pen(color, width);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    return pen;
}

}  // namespace

void InvalidateCache() {
    // 屏幕换接(混 DPI 多显示器)或显示缩放变化时清空,使图标按新的 devicePixelRatio 重新渲染,
    // 避免副屏上沿用主屏 DPR 缓存的像素图导致发虚。
    g_iconCache.clear();
}

QIcon folder(int px) {
    return MakeIcon(QStringLiteral("folder:%1").arg(px), px, [](QPainter* p) {
        // 背板与标签同色，构成顶沿一致的文件夹后半部分；正面下移覆盖，露出标签凸起。
        QPainterPath back;
        back.addRoundedRect(QRectF(1.6, 4.0, 12.8, 9.2), 2.4, 2.4);
        p->fillPath(back, kFolderBack);

        QPainterPath tab;
        tab.addRoundedRect(QRectF(1.6, 3.3, 5.6, 3.1), 1.6, 1.6);
        p->fillPath(tab, kFolderBack);

        QPainterPath front;
        front.addRoundedRect(QRectF(1.6, 6.0, 12.8, 7.4), 2.2, 2.2);
        p->fillPath(front, kFolderFront);
    });
}

QIcon fileGlyph(int px) {
    return MakeIcon(QStringLiteral("file:%1").arg(px), px, [](QPainter* p) {
        p->setBrush(Qt::NoBrush);
        p->setPen(RoundPen(kGlyphStroke, 1.4));

        // 页面轮廓 + 右上折角。
        QPainterPath page;
        page.moveTo(4.0, 2.2);
        page.lineTo(9.2, 2.2);
        page.lineTo(12.6, 5.6);
        page.lineTo(12.6, 13.8);
        page.lineTo(4.0, 13.8);
        page.closeSubpath();
        p->drawPath(page);

        // 折角内侧两条短线，区分翻折面。
        p->drawLine(QPointF(9.2, 2.2), QPointF(9.2, 5.6));
        p->drawLine(QPointF(9.2, 5.6), QPointF(12.6, 5.6));

        // 页面内的两行文字示意。
        p->setPen(RoundPen(kGlyphDetail, 1.2));
        p->drawLine(QPointF(5.8, 8.4), QPointF(10.8, 8.4));
        p->drawLine(QPointF(5.8, 10.8), QPointF(10.8, 10.8));
    });
}

QIcon drive(int px) {
    return MakeIcon(QStringLiteral("drive:%1").arg(px), px, [](QPainter* p) {
        p->setBrush(Qt::NoBrush);
        p->setPen(RoundPen(kGlyphStroke, 1.4));
        p->drawRoundedRect(QRectF(2.0, 3.6, 12.0, 8.8), 1.8, 1.8);

        // 顶部读盘槽。
        p->drawLine(QPointF(3.8, 7.0), QPointF(9.0, 7.0));

        // 右下强调色指示灯。
        p->setPen(Qt::NoPen);
        p->setBrush(kAccentDot);
        p->drawEllipse(QPointF(11.4, 9.6), 1.0, 1.0);
    });
}

QIcon drive(int px, const QColor& color) {
    return MakeIcon(QStringLiteral("drive:%1:%2").arg(px).arg(ColorKey(color)), px, [color](QPainter* p) {
        p->setBrush(Qt::NoBrush);
        p->setPen(RoundPen(color, 1.4));
        p->drawRoundedRect(QRectF(2.0, 3.6, 12.0, 8.8), 1.8, 1.8);

        // 顶部读盘槽。
        p->drawLine(QPointF(3.8, 7.0), QPointF(9.0, 7.0));

        // 右下指示灯，与描边同色，构成统一的着色外观。
        p->setPen(Qt::NoPen);
        p->setBrush(color);
        p->drawEllipse(QPointF(11.4, 9.6), 1.0, 1.0);
    });
}

QIcon info(int px) {
    return MakeIcon(QStringLiteral("info:%1").arg(px), px, [](QPainter* p) {
        p->setBrush(Qt::NoBrush);
        p->setPen(RoundPen(kGlyphDetail, 1.4));
        p->drawEllipse(QPointF(8.0, 8.0), 5.4, 5.4);

        // i 的圆点。
        p->setPen(Qt::NoPen);
        p->setBrush(kGlyphDetail);
        p->drawEllipse(QPointF(8.0, 5.3), 0.85, 0.85);

        // i 的竖线。
        p->setPen(RoundPen(kGlyphDetail, 1.5));
        p->drawLine(QPointF(8.0, 7.4), QPointF(8.0, 11.0));
    });
}

QIcon arrowBack(int px) {
    return MakeIcon(QStringLiteral("back:%1").arg(px), px, [](QPainter* p) {
        p->setBrush(Qt::NoBrush);
        p->setPen(RoundPen(kGlyphStroke, 1.8));

        // 水平箭杆 + 指向左侧的 V 形箭头。
        p->drawLine(QPointF(3.0, 8.0), QPointF(13.0, 8.0));
        p->drawLine(QPointF(3.0, 8.0), QPointF(6.8, 4.2));
        p->drawLine(QPointF(3.0, 8.0), QPointF(6.8, 11.8));
    });
}

QIcon folderOpen(int px, const QColor& color) {
    return MakeIcon(QStringLiteral("folderOpen:%1:%2").arg(px).arg(ColorKey(color)), px, [color](QPainter* p) {
        p->setBrush(Qt::NoBrush);
        p->setPen(RoundPen(color, 1.5));

        // 底部更宽的口袋轮廓 + 顶部标签下凹，表示打开的文件夹。
        QPainterPath outline;
        outline.moveTo(1.8, 12.8);
        outline.lineTo(4.2, 5.0);
        outline.lineTo(7.4, 5.0);
        outline.lineTo(8.9, 6.5);
        outline.lineTo(14.2, 6.5);
        outline.lineTo(12.0, 12.8);
        outline.closeSubpath();
        p->drawPath(outline);

        // 翻开的封面分隔线。
        p->drawLine(QPointF(3.6, 9.4), QPointF(13.0, 9.4));
    });
}

QIcon play(int px, const QColor& color) {
    return MakeIcon(QStringLiteral("play:%1:%2").arg(px).arg(ColorKey(color)), px, [color](QPainter* p) {
        QPainterPath tri;
        tri.moveTo(5.4, 3.6);
        tri.lineTo(5.4, 12.4);
        tri.lineTo(12.8, 8.0);
        tri.closeSubpath();
        p->fillPath(tri, color);
    });
}

QIcon stop(int px, const QColor& color) {
    return MakeIcon(QStringLiteral("stop:%1:%2").arg(px).arg(ColorKey(color)), px, [color](QPainter* p) {
        p->setPen(Qt::NoPen);
        p->setBrush(color);
        p->drawRoundedRect(QRectF(4.4, 4.4, 7.2, 7.2), 1.3, 1.3);
    });
}

QIcon refresh(int px, const QColor& color) {
    return MakeIcon(QStringLiteral("refresh:%1:%2").arg(px).arg(ColorKey(color)), px, [color](QPainter* p) {
        p->setBrush(Qt::NoBrush);
        p->setPen(RoundPen(color, 1.7));

        // 一段 300° 圆弧（留 60° 开口）。起点角度不影响最终外观。
        const QRectF arcRect(3.0, 3.0, 10.0, 10.0);
        const QPointF center(8.0, 8.0);
        QPainterPath arc;
        arc.arcMoveTo(arcRect, 60);
        arc.arcTo(arcRect, 60, 300);
        p->drawPath(arc);

        // 不依赖 Qt 圆弧角度方向约定：直接读取弧末端坐标，按「径向取垂直」算出切向，
        // 在末端放一个沿切向的三角形箭头，保证箭头始终贴合弧线、不会错位。
        const QPointF end = arc.currentPosition();
        QPointF radial = end - center;
        const qreal length = std::hypot(radial.x(), radial.y());
        if (length < 0.001) {
            return;
        }
        radial /= length;
        const QPointF tangent(radial.y(), -radial.x());
        const QPointF tip = end + tangent * 2.6;
        const QPointF base = end - tangent * 0.4;
        const QPointF side1 = base + radial * 2.2;
        const QPointF side2 = base - radial * 2.2;

        p->setPen(Qt::NoPen);
        p->setBrush(color);
        QPainterPath head;
        head.moveTo(tip);
        head.lineTo(side1);
        head.lineTo(side2);
        head.closeSubpath();
        p->drawPath(head);
    });
}

QIcon trash(int px, const QColor& color) {
    return MakeIcon(QStringLiteral("trash:%1:%2").arg(px).arg(ColorKey(color)), px, [color](QPainter* p) {
        p->setBrush(Qt::NoBrush);
        p->setPen(RoundPen(color, 1.5));

        // 盖子横条。
        p->drawLine(QPointF(3.4, 5.2), QPointF(12.6, 5.2));

        // 顶部把手。
        p->drawLine(QPointF(6.4, 5.2), QPointF(6.4, 4.0));
        p->drawLine(QPointF(6.4, 4.0), QPointF(9.6, 4.0));
        p->drawLine(QPointF(9.6, 4.0), QPointF(9.6, 5.2));

        // 梯形桶身。
        QPainterPath body;
        body.moveTo(4.6, 5.8);
        body.lineTo(5.6, 13.2);
        body.lineTo(10.4, 13.2);
        body.lineTo(11.4, 5.8);
        body.closeSubpath();
        p->drawPath(body);

        // 桶身两条竖纹。
        p->drawLine(QPointF(7.0, 6.8), QPointF(7.3, 12.2));
        p->drawLine(QPointF(9.0, 6.8), QPointF(8.7, 12.2));
    });
}

QIcon arrowUp(int px, const QColor& color) {
    return MakeIcon(QStringLiteral("up:%1:%2").arg(px).arg(ColorKey(color)), px, [color](QPainter* p) {
        p->setBrush(Qt::NoBrush);
        p->setPen(RoundPen(color, 1.8));

        // 箭杆。
        p->drawLine(QPointF(8.0, 3.4), QPointF(8.0, 13.0));

        // 箭头 V 形。
        p->drawLine(QPointF(8.0, 3.4), QPointF(4.2, 7.2));
        p->drawLine(QPointF(8.0, 3.4), QPointF(11.8, 7.2));
    });
}

QIcon search(int px, const QColor& color) {
    return MakeIcon(QStringLiteral("search:%1:%2").arg(px).arg(ColorKey(color)), px, [color](QPainter* p) {
        p->setBrush(Qt::NoBrush);
        p->setPen(RoundPen(color, 1.6));

        // 镜片圆环。
        p->drawEllipse(QPointF(6.8, 6.8), 3.7, 3.7);

        // 右下手柄。
        p->drawLine(QPointF(9.5, 9.5), QPointF(13.2, 13.2));
    });
}

}  // namespace disk_lens::qt_ui::app_icons
