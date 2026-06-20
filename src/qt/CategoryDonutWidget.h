#pragma once

#include "core/CategoryStats.h"

#include <QColor>
#include <QString>
#include <QWidget>

#include <cstdint>
#include <vector>

namespace disk_lens::qt_ui {

/**
 * @brief 分类环形图跟随主题使用的颜色集合。
 *
 * 数据色块(各分类扇区)固定彩色、不随主题变化(与 AppIcons/TreemapWidget 数据色块策略一致,
 * 参考 MainWindow.cpp 中“数据色块保持彩色”的既定取向);此处只描述背景、文字、轨道与图例描边,
 * 使其在浅色/暗色/蓝色皮肤下都协调。
 */
struct DonutColors {
    QColor background;      ///< 控件背景(填满 rect,也用于挖中心孔)。
    QColor trackBg;         ///< 环形轨道底色(扇区背后的整圈淡环)。
    QColor centerText;      ///< 中心总大小文字。
    QColor centerCaption;   ///< 中心说明文字。
    QColor labelText;       ///< 图例分类名。
    QColor labelValue;      ///< 图例大小/占比。
    QColor swatchBorder;    ///< 图例色块描边。
};

/**
 * @brief 类型占比环形图控件:把 8 类 + 其他的字节数画成环形 + 右侧图例。
 *
 * 纯 QPainter 自绘(镜像 TreemapWidget 画法),无 QSS、无 QtCharts。数据来自
 * core::ComputeExtensionCategories 的只读切片;分类颜色按 FileCategory 固定取色。
 * 无数据时显示居中提示,绝不抛异常或绘制残缺扇区。
 */
class CategoryDonutWidget : public QWidget {
    Q_OBJECT

public:
    /**
     * @brief 构造环形图控件。
     * @param parent 父级 Qt 控件。
     */
    explicit CategoryDonutWidget(QWidget* parent = nullptr);

    /**
     * @brief 应用主题颜色(SetColors 后自动重绘)。
     * @param colors 主题颜色集合。
     */
    void SetColors(const DonutColors& colors);

    /**
     * @brief 设置分类切片(应已剔除 0 字节、按大小降序);空列表显示 emptyMessage_。
     * @param slices 分类切片。
     */
    void SetCategories(std::vector<core::CategorySlice> slices);

    /**
     * @brief 清空并显示提示。
     * @param message 空状态提示文字。
     */
    void Clear(const QString& message);

protected:
    /**
     * @brief 绘制环形图与图例。
     * @param event 绘制事件参数。
     */
    void paintEvent(QPaintEvent* event) override;

    /**
     * @brief 尺寸变化时重新计算几何。
     * @param event 尺寸事件参数。
     */
    void resizeEvent(QResizeEvent* event) override;

private:
    /**
     * @brief 由控件 rect 推导环形与图例的几何参数。
     */
    struct Geometry {
        QRectF donutRect;   ///< 环形外接矩形。
        QPointF center;     ///< 圆心。
        qreal outerR = 0.0; ///< 外半径。
        qreal innerR = 0.0; ///< 内半径(中心孔)。
        QRectF legendRect;  ///< 图例区。
    };

    /**
     * @brief 根据控件尺寸重算 geometry_。
     */
    void RebuildLayout();

    QString emptyMessage_;
    std::vector<core::CategorySlice> slices_;
    DonutColors colors_;
    Geometry geometry_;
};

}  // namespace disk_lens::qt_ui
