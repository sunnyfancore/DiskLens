#pragma once

#include <QColor>
#include <QIcon>

namespace disk_lens::qt_ui::app_icons {

/**
 * @brief 自绘图标工具集。
 *
 * 项目约束「不使用任何第三方成熟产品的图标资源」，因此界面内的图标全部用 QPainter
 * 在 16 单位逻辑网格上手工绘制，按调用尺寸缩放并按屏幕 devicePixelRatio 渲染，保证 HiDPI
 * 下清晰。所有图标内部带缓存，树/表大量填充时不会反复重绘。
 *
 * 调色板分两类：
 * - 内容类图标（文件夹 / 文件 / 磁盘 / 信息）使用固定配色，在三套皮肤卡片底色（白色 / 深石板 /
 *   天空青卡片）上均清晰可读，因此无需随主题切换刷新。
 * - 动作类图标（播放 / 停止 / 刷新 / 删除 / 上级 / 打开目录）颜色由调用方注入，传入当前主题的
 *   强调色，主窗口会在 ApplyStyle 中按当前主题重新设置，实现跟随皮肤。
 */

/**
 * @brief 清空图标缓存。
 *
 * 像素图按绘制时的 devicePixelRatio 渲染并缓存。混 DPI 多显示器上,若窗口换接到不同 DPR 的屏幕,
 * 原缓存的像素图在新屏上会发虚。主窗口在 QWindow::screenChanged / QScreen::logicalDotsPerInchChanged
 * 时调用本函数清空,图标随后按新 DPR 重新渲染。
 */
void InvalidateCache();

/**
 * @brief 文件夹图标（蓝色，固定配色）。
 * @param px 图标逻辑边长，单位为设备无关像素。
 * @return 文件夹图标。
 */
QIcon folder(int px);

/**
 * @brief 文件图标（灰色描边，固定配色）。
 * @param px 图标逻辑边长，单位为设备无关像素。
 * @return 文件图标。
 */
QIcon fileGlyph(int px);

/**
 * @brief 磁盘 / 硬盘图标（灰色描边 + 强调色指示灯，固定配色）。
 * @param px 图标逻辑边长，单位为设备无关像素。
 * @return 磁盘图标。
 */
QIcon drive(int px);

/**
 * @brief 磁盘 / 硬盘图标（动作类，描边与指示灯颜色由调用方注入）。
 *
 * 与固定配色的 drive(int) 区分：用于左侧导航等需要随选中状态着色（未选次要文字色、选中强调色）
 * 的场景。调用方按当前主题在 ApplyStyle 中重新设置，实现跟随皮肤。
 * @param px 图标逻辑边长，单位为设备无关像素。
 * @param color 描边与指示灯颜色，一般传入当前主题强调色或次要文字色。
 * @return 磁盘图标。
 */
QIcon drive(int px, const QColor& color);

/**
 * @brief 信息提示图标（用于「更多项未展开」等提示行，固定配色）。
 * @param px 图标逻辑边长，单位为设备无关像素。
 * @return 信息图标。
 */
QIcon info(int px);

/**
 * @brief 返回上级图标（左侧箭头，固定配色，用于表格中的「..」父级行）。
 * @param px 图标逻辑边长，单位为设备无关像素。
 * @return 返回上级图标。
 */
QIcon arrowBack(int px);

/**
 * @brief 打开目录图标（动作类，描边颜色由调用方注入）。
 * @param px 图标逻辑边长，单位为设备无关像素。
 * @param color 描边颜色，一般传入当前主题强调色。
 * @return 打开目录图标。
 */
QIcon folderOpen(int px, const QColor& color);

/**
 * @brief 播放图标（动作类，颜色由调用方注入）。
 * @param px 图标逻辑边长，单位为设备无关像素。
 * @param color 填充颜色，一般传入当前主题强调色。
 * @return 播放图标。
 */
QIcon play(int px, const QColor& color);

/**
 * @brief 停止图标（动作类，颜色由调用方注入）。
 * @param px 图标逻辑边长，单位为设备无关像素。
 * @param color 填充颜色，一般传入当前主题强调色。
 * @return 停止图标。
 */
QIcon stop(int px, const QColor& color);

/**
 * @brief 刷新图标（动作类，颜色由调用方注入）。
 * @param px 图标逻辑边长，单位为设备无关像素。
 * @param color 描边颜色，一般传入当前主题强调色。
 * @return 刷新图标。
 */
QIcon refresh(int px, const QColor& color);

/**
 * @brief 删除（垃圾桶）图标（动作类，颜色由调用方注入）。
 * @param px 图标逻辑边长，单位为设备无关像素。
 * @param color 描边颜色，一般传入当前主题强调色。
 * @return 删除图标。
 */
QIcon trash(int px, const QColor& color);

/**
 * @brief 向上箭头图标（动作类，颜色由调用方注入）。
 * @param px 图标逻辑边长，单位为设备无关像素。
 * @param color 描边颜色，一般传入当前主题强调色。
 * @return 向上箭头图标。
 */
QIcon arrowUp(int px, const QColor& color);

/**
 * @brief 搜索（放大镜）图标（动作类，颜色由调用方注入）。
 * @param px 图标逻辑边长，单位为设备无关像素。
 * @param color 描边颜色，一般传入当前主题强调色。
 * @return 搜索图标。
 */
QIcon search(int px, const QColor& color);

}  // namespace disk_lens::qt_ui::app_icons
