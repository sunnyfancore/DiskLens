#pragma once

#include "core/AgeStats.h"

#include <QColor>
#include <QString>
#include <QWidget>

#include <cstdint>
#include <vector>

namespace disk_lens::qt_ui {

/**
 * @brief 文件年龄直方图跟随主题使用的颜色集合。
 *
 * 柱体本身按"年龄"取固定渐变色(越新越青蓝=活跃,越旧越红=冷数据),不随主题变化
 * (与 CategoryDonutWidget 扇区固定彩色、TreemapWidget 数据色块策略一致);此处只描述
 * 背景、基线、文字、柱顶值标签的颜色,使其在浅色/暗色/蓝色皮肤下都协调。
 */
struct HistogramColors {
    QColor background;   ///< 控件背景。
    QColor axisLine;     ///< 横轴基线。
    QColor barTopValue;  ///< 柱顶"大小"数值标签。
    QColor bandLabel;    ///< 横轴分带名(7 天内 / … / >3 年)。
    QColor countLabel;   ///< 文件数标签。
    QColor caption;      ///< 空状态/说明文字。
};

/**
 * @brief 文件年龄分布直方图控件:把 7 个年龄分带的字节数画成竖直柱状 + 横轴分带名。
 *
 * 纯 QPainter 自绘(镜像 CategoryDonutWidget 画法),无 QSS、无 QtCharts。数据来自
 * core::ComputeAgeBuckets 的只读切片;柱高按各带字节数与最大带的比例。无数据时显示居中提示,
 * 绝不抛异常或绘制残缺柱。
 */
class FileAgeHistogramWidget : public QWidget {
    Q_OBJECT

public:
    /**
     * @brief 构造直方图控件。
     * @param parent 父级 Qt 控件。
     */
    explicit FileAgeHistogramWidget(QWidget* parent = nullptr);

    /**
     * @brief 应用主题颜色(SetColors 后自动重绘)。
     * @param colors 主题颜色集合。
     */
    void SetColors(const HistogramColors& colors);

    /**
     * @brief 设置分带(应与 core::ComputeAgeBuckets 返回顺序一致,固定 7 项);空列表显示 emptyMessage_。
     * @param buckets 分带统计。
     */
    void SetBuckets(std::vector<core::AgeBucket> buckets);

    /**
     * @brief 清空并显示提示。
     * @param message 空状态提示文字。
     */
    void Clear(const QString& message);

protected:
    /**
     * @brief 绘制直方图。
     * @param event 绘制事件参数。
     */
    void paintEvent(QPaintEvent* event) override;

private:
    QString emptyMessage_;
    std::vector<core::AgeBucket> buckets_;
    HistogramColors colors_;
};

}  // namespace disk_lens::qt_ui
