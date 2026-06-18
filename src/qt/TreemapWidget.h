#pragma once

#include "core/ScanModels.h"

#include <QColor>
#include <QRect>
#include <QString>
#include <QWidget>

#include <cstdint>
#include <vector>

namespace disk_lens::qt_ui {

/**
 * @brief 空间占比图跟随主题使用的颜色集合。
 *
 * 数据色块（PaletteColor）始终保持彩色以突出空间分布；这里只描述背景、文字与紧凑态卡片的底色，使其在浅色/暗色/蓝色皮肤下都协调。
 */
struct TreemapColors {
    /**
     * @brief 控件背景。
     */
    QColor background;

    /**
     * @brief 空状态提示文字颜色。
     */
    QColor emptyText;

    /**
     * @brief 紧凑态卡片背景。
     */
    QColor cardBg;

    /**
     * @brief 紧凑态卡片悬停背景。
     */
    QColor cardHoverBg;

    /**
     * @brief 紧凑态卡片描边。
     */
    QColor cardBorder;

    /**
     * @brief 紧凑态名称文字颜色。
     */
    QColor nameText;

    /**
     * @brief 紧凑态大小文字颜色。
     */
    QColor sizeText;

    /**
     * @brief 紧凑态占比进度条底色。
     */
    QColor barTrack;
};

/**
 * @brief 用于展示当前目录空间占比的轻量级矩形树图控件。
 */
class TreemapWidget : public QWidget {
    Q_OBJECT

public:
    /**
     * @brief 构造空间占比图控件。
     * @param parent 父级 Qt 控件。
     */
    explicit TreemapWidget(QWidget* parent = nullptr);

    /**
     * @brief 应用主题颜色，使空间图跟随浅色/暗色/蓝色皮肤。
     * @param colors 主题颜色集合。
     */
    void SetColors(const TreemapColors& colors);

    /**
     * @brief 设置当前要展示的扫描节点。
     * @param node 当前目录或文件节点。
     */
    void SetRootNode(const core::ScanNode& node);

    /**
     * @brief 清空空间占比图。
     * @param message 空状态提示文字。
     */
    void Clear(const QString& message);

signals:
    /**
     * @brief 用户点击空间块时发出。
     * @param path 被点击项目的完整路径。
     */
    void PathActivated(const QString& path);

protected:
    /**
     * @brief 绘制空间占比图。
     * @param event 绘制事件参数。
     */
    void paintEvent(QPaintEvent* event) override;

    /**
     * @brief 处理鼠标移动以更新提示。
     * @param event 鼠标事件参数。
     */
    void mouseMoveEvent(QMouseEvent* event) override;

    /**
     * @brief 处理控件尺寸变化并重新布局空间块。
     * @param event 尺寸事件参数。
     */
    void resizeEvent(QResizeEvent* event) override;

    /**
     * @brief 处理鼠标离开控件。
     * @param event 鼠标事件参数。
     */
    void leaveEvent(QEvent* event) override;

    /**
     * @brief 处理鼠标点击以触发项目选择。
     * @param event 鼠标事件参数。
     */
    void mousePressEvent(QMouseEvent* event) override;

private:
    /**
     * @brief 单个空间块的绘制数据。
     */
    struct Block {
        /**
         * @brief 显示名称。
         */
        QString name;

        /**
         * @brief 完整路径。
         */
        QString path;

        /**
         * @brief 格式化后的大小。
         */
        QString sizeText;

        /**
         * @brief 原始大小，单位为字节。
         */
        std::uint64_t bytes = 0;

        /**
         * @brief 是否为目录。
         */
        bool directory = false;

        /**
         * @brief 当前块绘制区域。
         */
        QRect rect;

        /**
         * @brief 当前块颜色。
         */
        QColor color;
    };

    /**
     * @brief 根据控件大小重新计算块位置。
     */
    void RebuildLayout();

    /**
     * @brief 查找指定位置所在的空间块。
     * @param position 鼠标位置。
     * @return 命中的块索引，没有命中时返回 -1。
     */
    int HitTest(const QPoint& position) const;

    /**
     * @brief 空状态提示文字。
     */
    QString emptyMessage_;

    /**
     * @brief 当前绘制块列表。
     */
    std::vector<Block> blocks_;

    /**
     * @brief 当前悬停块索引。
     */
    int hoveredIndex_ = -1;

    /**
     * @brief 当前主题颜色集合，默认为浅色取值，由主窗口在切肤时刷新。
     */
    TreemapColors colors_;
};

}  // namespace disk_lens::qt_ui
