#include "qt/MainWindow.h"

#include "app/resource.h"
#include "core/CategoryStats.h"
#include "core/AgeStats.h"
#include "core/Format.h"
#include "core/LongPath.h"
#include "qt/AppIcons.h"
#include "Version.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QCoreApplication>
#include <QColor>
#include <QDataStream>
#include <QDateEdit>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QFont>
#include <QFontMetrics>
#include <QFrame>
#include <QEventLoop>
#include <QGraphicsDropShadowEffect>
#include <QContextMenuEvent>
#include <QGridLayout>
#include <QGuiApplication>
#include <QScrollArea>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMetaObject>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QScreen>
#include <QSettings>
#include <QShortcut>
#include <QSizePolicy>
#include <QScrollBar>
#include <QSplitter>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStringList>
#include <QStyle>
#include <QStyleOptionViewItem>
#include <QStyledItemDelegate>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTableView>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QTreeWidget>
#include <QUrl>
#include <QVariant>
#include <QVBoxLayout>
#include <QWidget>
#include <QWindow>

#include <Windows.h>
#include <Shellapi.h>
#include <WinIoCtl.h>
#include <dwmapi.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <locale>
#include <map>
#include <set>
#include <memory>
#include <queue>
#include <system_error>
#include <thread>
#include <unordered_map>

namespace disk_lens::qt_ui {

namespace {

/**
 * @brief 快速搜索每次渲染到表格的批量大小。
 */
constexpr int kSearchPageSize = 1000;

/**
 * @brief 按像素宽度生成表格路径列的友好省略文本。
 * @param path 完整路径。
 * @param metrics 当前字体度量。
 * @param pixelWidth 可用像素宽度。
 * @return 适合当前列宽展示的路径文本。
 */
QString ElidePathForWidth(const QString& path, const QFontMetrics& metrics, int pixelWidth);

/**
 * @brief 判断文本是否像 Windows 文件系统路径。
 * @param value 待判断文本。
 * @return 是路径时返回 true。
 */
bool LooksLikeWindowsPath(const QString& value);

/**
 * @brief 根据完整路径生成列表路径列的展示文本。
 * @param fullPath 完整文件或目录路径。
 * @param isDirectory 当前条目是否为目录或磁盘。
 * @return 用于路径列展示的所在目录文本。
 */
QString ContainingDirectoryForDisplay(const QString& fullPath, bool isDirectory);

/**
 * @brief 将可执行文件资源中的图标应用到 Qt 窗口。
 * @param window 需要设置标题栏图标的窗口。
 */
void ApplyNativeWindowIcon(QWidget* window);

/**
 * @brief 显示带统一应用图标的消息框。
 * @param parent 父级窗口。
 * @param icon 消息框语义图标。
 * @param title 标题文本。
 * @param text 正文文本。
 * @param buttons 按钮集合。
 * @param defaultButton 默认按钮。
 * @return 用户点击的按钮。
 */
QMessageBox::StandardButton ShowAppMessageBox(
    QWidget* parent,
    QMessageBox::Icon icon,
    const QString& title,
    const QString& text,
    QMessageBox::StandardButtons buttons = QMessageBox::Ok,
    QMessageBox::StandardButton defaultButton = QMessageBox::NoButton);

/**
 * @brief 当前界面统一使用的主题颜色与度量令牌。
 *
 * 由 ApplyStyle 在每次切肤时刷新到 g_activeTokens，供样式表和自绘控件（表头、空间图、清理语义色）共同读取，保证三套皮肤视觉一致。
 */
struct ThemeTokens {
    /**
     * @brief 主窗口画布背景。
     */
    QString windowBg;

    /**
     * @brief 左侧功能导航栏背景。
     */
    QString navBg;

    /**
     * @brief 左侧功能导航栏右侧分隔线颜色。
     */
    QString navBorder;

    /**
     * @brief 卡片与面板背景。
     */
    QString cardBg;

    /**
     * @brief 卡片与面板统一描边颜色。
     */
    QString cardBorder;

    /**
     * @brief 卡片顶部高光描边，用于模拟微立体。
     */
    QString cardTopHi;

    /**
     * @brief 输入框与下拉框背景。
     */
    QString inputBg;

    /**
     * @brief 输入框与下拉框描边颜色。
     */
    QString inputBorder;

    /**
     * @brief 主要文字颜色。
     */
    QString textPrimary;

    /**
     * @brief 次要文字颜色。
     */
    QString textSecondary;

    /**
     * @brief 弱化文字颜色。
     */
    QString textMuted;

    /**
     * @brief 主题强调色。
     */
    QString accent;

    /**
     * @brief 强调色悬停态。
     */
    QString accentHover;

    /**
     * @brief 强调色按下态。
     */
    QString accentPressed;

    /**
     * @brief 强调色更深层，用于按下描边。
     */
    QString accentDeep;

    /**
     * @brief 强调色最浅填充，用于软背景与悬停。
     */
    QString accentSoft;

    /**
     * @brief 强调色软背景描边。
     */
    QString accentSoftBorder;

    /**
     * @brief 位于强调色填充上的文字颜色。
     */
    QString accentContrastText;

    /**
     * @brief 表头背景。
     */
    QString headerBg;

    /**
     * @brief 表头文字颜色。
     */
    QString headerText;

    /**
     * @brief 表头分隔线颜色。
     */
    QString headerLine;

    /**
     * @brief 表头排序箭头颜色。
     */
    QString sortArrow;

    /**
     * @brief 表格交替行背景。
     */
    QString altRow;

    /**
     * @brief 表格悬停行背景。
     */
    QString hoverRow;

    /**
     * @brief 表格选中行背景。
     */
    QString selectedRow;

    /**
     * @brief 表格选中行文字颜色。
     */
    QString selectedText;

    /**
     * @brief 安全/通过语义色。
     */
    QString good;

    /**
     * @brief 提醒/谨慎语义色。
     */
    QString warn;

    /**
     * @brief 危险/删除语义色。
     */
    QString danger;

    /**
     * @brief 加载遮罩半透明背景(rgba 字符串,随主题底色暗化内容)。
     */
    QString overlayScrim;

    /**
     * @brief 卡片圆角像素。
     */
    int cardRadius = 10;

    /**
     * @brief 按钮与输入框圆角像素。
     */
    int controlRadius = 6;

    /**
     * @brief 进度条与轨道圆角像素。
     */
    int trackRadius = 3;

    /**
     * @brief 胶囊型徽章圆角像素。
     */
    int pillRadius = 14;

    /**
     * @brief 表格行高像素。
     */
    int rowHeight = 28;

    // —— 间距令牌(4 的倍数体系,不随主题变化)——
    int spaceXs = 4;       // 图标与文字、紧凑组件细距。
    int spaceSm = 8;       // 卡片内常规间距、按钮组间距。
    int spaceMd = 12;      // 卡片分段间距、hero 间距。
    int spaceLg = 16;      // hero 内边距、主要间距。
    int spaceXl = 20;      // 对话框常规内边距。
    int spaceDialog = 24;  // 大对话框内边距。
    int space2xl = 32;     // 全屏空状态内边距。

    // —— 字号令牌 pt(不随主题变化)——
    int fsCaption = 9;   // 次要说明、版本号、页脚。
    int fsBody = 10;     // 正文。
    int fsLabel = 11;    // 按钮、导航、表单标签。
    int fsTitle = 13;    // 卡片标题、指标值。
    int fsH1 = 15;       // 强调大数值。
    int fsDisplay = 22;  // 首屏大字。

    // —— 控件尺寸令牌 px(不随主题变化)——
    int controlHeight = 28;        // 通用按钮/输入框最小高度。
    int navButtonHeight = 52;      // 导航按钮高度。
    int primaryButtonHeight = 40;  // 主操作按钮高度。
    int primaryButtonMinW = 132;   // 主操作按钮最小宽度。
};

/**
 * @brief 按主题名称解析对应的颜色与度量令牌。
 * @param themeName 主题名称，支持 light、dark、blue。
 * @return 解析后的主题令牌。
 */
ThemeTokens ResolveThemeTokens(const QString& themeName) {
    ThemeTokens t;
    t.cardRadius = 10;
    t.controlRadius = 6;
    t.trackRadius = 3;
    t.pillRadius = 14;
    t.rowHeight = 28;
    // 度量/字号/尺寸不随主题变化,三套主题取相同值。
    t.spaceXs = 4; t.spaceSm = 8; t.spaceMd = 12; t.spaceLg = 16;
    t.spaceXl = 20; t.spaceDialog = 24; t.space2xl = 32;
    t.fsCaption = 9; t.fsBody = 10; t.fsLabel = 11; t.fsTitle = 13;
    t.fsH1 = 15; t.fsDisplay = 22;
    t.controlHeight = 28; t.navButtonHeight = 52;
    t.primaryButtonHeight = 40; t.primaryButtonMinW = 132;

    if (themeName == QStringLiteral("dark")) {
        t.windowBg = QStringLiteral("#0f172a");
        t.navBg = QStringLiteral("#1e293b");
        t.navBorder = QStringLiteral("#334155");
        t.cardBg = QStringLiteral("#1e293b");
        t.cardBorder = QStringLiteral("#334155");
        t.cardTopHi = QStringLiteral("#3b485e");
        t.inputBg = QStringLiteral("#0f172a");
        t.inputBorder = QStringLiteral("#475569");
        t.textPrimary = QStringLiteral("#f1f5f9");
        t.textSecondary = QStringLiteral("#cbd5e1");
        t.textMuted = QStringLiteral("#64748b");
        t.accent = QStringLiteral("#3b82f6");
        t.accentHover = QStringLiteral("#60a5fa");
        t.accentPressed = QStringLiteral("#2563eb");
        t.accentDeep = QStringLiteral("#1d4ed8");
        t.accentSoft = QStringLiteral("#1e3a8a");
        t.accentSoftBorder = QStringLiteral("#1e40af");
        t.accentContrastText = QStringLiteral("#ffffff");
        t.headerBg = QStringLiteral("#1e293b");
        t.headerText = QStringLiteral("#cbd5e1");
        t.headerLine = QStringLiteral("#334155");
        t.sortArrow = QStringLiteral("#60a5fa");
        t.altRow = QStringLiteral("#1a2436");
        t.hoverRow = QStringLiteral("#27364f");
        t.selectedRow = QStringLiteral("#1e40af");
        t.selectedText = QStringLiteral("#ffffff");
        t.good = QStringLiteral("#34d399");
        t.warn = QStringLiteral("#fbbf24");
        t.danger = QStringLiteral("#f87171");
        t.overlayScrim = QStringLiteral("rgba(15, 23, 42, 110)");
        return t;
    }

    if (themeName == QStringLiteral("blue")) {
        t.windowBg = QStringLiteral("#eaf4ff");
        t.navBg = QStringLiteral("#ffffff");
        t.navBorder = QStringLiteral("#cfe4fb");
        t.cardBg = QStringLiteral("#ffffff");
        t.cardBorder = QStringLiteral("#cfe4fb");
        t.cardTopHi = QStringLiteral("#ffffff");
        t.inputBg = QStringLiteral("#ffffff");
        t.inputBorder = QStringLiteral("#a9d2f4");
        t.textPrimary = QStringLiteral("#0c4a6e");
        t.textSecondary = QStringLiteral("#475569");
        t.textMuted = QStringLiteral("#94a3b8");
        t.accent = QStringLiteral("#0ea5e9");
        t.accentHover = QStringLiteral("#0284c7");
        t.accentPressed = QStringLiteral("#0369a1");
        t.accentDeep = QStringLiteral("#075985");
        t.accentSoft = QStringLiteral("#e0f2fe");
        t.accentSoftBorder = QStringLiteral("#bae6fd");
        t.accentContrastText = QStringLiteral("#ffffff");
        t.headerBg = QStringLiteral("#f0f9ff");
        t.headerText = QStringLiteral("#075985");
        t.headerLine = QStringLiteral("#cfe4fb");
        t.sortArrow = QStringLiteral("#0ea5e9");
        t.altRow = QStringLiteral("#f0f9ff");
        t.hoverRow = QStringLiteral("#e0f2fe");
        t.selectedRow = QStringLiteral("#bae6fd");
        t.selectedText = QStringLiteral("#0c4a6e");
        t.good = QStringLiteral("#059669");
        t.warn = QStringLiteral("#d97706");
        t.danger = QStringLiteral("#dc2626");
        t.overlayScrim = QStringLiteral("rgba(15, 23, 42, 110)");
        return t;
    }

    // light（精致专业蓝，默认）
    t.windowBg = QStringLiteral("#f4f6fb");
    t.navBg = QStringLiteral("#ffffff");
    t.navBorder = QStringLiteral("#e2e8f0");
    t.cardBg = QStringLiteral("#ffffff");
    t.cardBorder = QStringLiteral("#e2e8f0");
    t.cardTopHi = QStringLiteral("#ffffff");
    t.inputBg = QStringLiteral("#ffffff");
    t.inputBorder = QStringLiteral("#cbd5e1");
    t.textPrimary = QStringLiteral("#0f172a");
    t.textSecondary = QStringLiteral("#475569");
    t.textMuted = QStringLiteral("#94a3b8");
    t.accent = QStringLiteral("#2563eb");
    t.accentHover = QStringLiteral("#1d4ed8");
    t.accentPressed = QStringLiteral("#1e40af");
    t.accentDeep = QStringLiteral("#1e3a8a");
    t.accentSoft = QStringLiteral("#eff6ff");
    t.accentSoftBorder = QStringLiteral("#dbeafe");
    t.accentContrastText = QStringLiteral("#ffffff");
    t.headerBg = QStringLiteral("#f8fafc");
    t.headerText = QStringLiteral("#334155");
    t.headerLine = QStringLiteral("#e2e8f0");
    t.sortArrow = QStringLiteral("#2563eb");
    t.altRow = QStringLiteral("#f8fafc");
    t.hoverRow = QStringLiteral("#f1f6ff");
    t.selectedRow = QStringLiteral("#dbeafe");
    t.selectedText = QStringLiteral("#0f172a");
    t.good = QStringLiteral("#059669");
    t.warn = QStringLiteral("#d97706");
    t.danger = QStringLiteral("#dc2626");
    t.overlayScrim = QStringLiteral("rgba(15, 23, 42, 110)");
    return t;
}

/**
 * @brief 当前生效的主题令牌，由 ApplyStyle 刷新，默认初始化为精致专业蓝。
 */
ThemeTokens g_activeTokens = ResolveThemeTokens(QStringLiteral("light"));

/**
 * @brief 面向专业界面的自绘表头，避免 Qt 原生排序箭头挤压文字。
 */
class ModernHeaderView : public QHeaderView {
public:
    /**
     * @brief 构造自绘表头。
     * @param orientation 表头方向。
     * @param parent 父级控件。
     */
    explicit ModernHeaderView(Qt::Orientation orientation, QWidget* parent = nullptr)
        : QHeaderView(orientation, parent) {
        setDefaultAlignment(Qt::AlignCenter);
        setHighlightSections(false);
        setSectionsClickable(true);
        setMinimumHeight(38);
    }

protected:
    /**
     * @brief 绘制单个表头分区。
     * @param painter 绘制器。
     * @param rect 分区矩形。
     * @param logicalIndex 逻辑列索引。
     */
    void paintSection(QPainter* painter, const QRect& rect, int logicalIndex) const override {
        if (painter == nullptr || !rect.isValid()) {
            return;
        }

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->fillRect(rect, QColor(g_activeTokens.headerBg));

        painter->setPen(QColor(g_activeTokens.headerLine));
        painter->drawLine(rect.topRight(), rect.bottomRight());
        painter->drawLine(rect.bottomLeft(), rect.bottomRight());

        const QString title = model() != nullptr
            ? model()->headerData(logicalIndex, orientation(), Qt::DisplayRole).toString()
            : QString();
        const bool sorted = isSortIndicatorShown() && sortIndicatorSection() == logicalIndex;
        QFont headerFont = painter->font();
        headerFont.setBold(true);
        painter->setFont(headerFont);

        const QFontMetrics metrics(headerFont);
        const int arrowWidth = sorted && rect.width() >= 72 ? 10 : 0;
        const int arrowGap = arrowWidth > 0 ? 6 : 0;
        const int horizontalPadding = rect.width() < 90 ? 6 : 12;
        const int availableWidth = std::max(0, rect.width() - horizontalPadding * 2 - arrowWidth - arrowGap);
        const QString text = metrics.elidedText(title, Qt::ElideRight, availableWidth);
        const int textWidth = std::min(metrics.horizontalAdvance(text), availableWidth);
        const int totalWidth = textWidth + arrowGap + arrowWidth;
        const int left = rect.left() + std::max(horizontalPadding, (rect.width() - totalWidth) / 2);
        QRect textRect(left, rect.top(), textWidth, rect.height());

        painter->setPen(QColor(g_activeTokens.headerText));
        painter->drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, text);

        if (arrowWidth > 0) {
            const int arrowLeft = std::min(textRect.right() + 1 + arrowGap, rect.right() - horizontalPadding - arrowWidth);
            const int centerY = rect.center().y();
            QPolygon arrow;
            if (sortIndicatorOrder() == Qt::AscendingOrder) {
                arrow << QPoint(arrowLeft, centerY + 4)
                      << QPoint(arrowLeft + 5, centerY - 3)
                      << QPoint(arrowLeft + 10, centerY + 4);
            } else {
                arrow << QPoint(arrowLeft, centerY - 3)
                      << QPoint(arrowLeft + 5, centerY + 4)
                      << QPoint(arrowLeft + 10, centerY - 3);
            }
            painter->setPen(Qt::NoPen);
            painter->setBrush(QColor(g_activeTokens.sortArrow));
            painter->drawPolygon(arrow);
        }

        painter->restore();
    }
};

/**
 * @brief 主题化树形控件：自绘展开 / 收起箭头。
 *
 * 默认 QTreeWidget 的分支三角在暗色 / 蓝色卡片底色上对比不足，用户难以察觉父级行可展开，常误以为
 * 必须双击。该子类重写 drawBranches，仅对「有子项」的行绘制清晰的主题着色箭头（收起 ▷ / 展开 ▽），
 * 单击分支区即可展开收起，符合「可点击展开」的预期；不绘制连线，保持现代简洁外观。
 */
class ThemedTreeWidget : public QTreeWidget {
public:
    using QTreeWidget::QTreeWidget;

protected:
    /**
     * @brief 在分支区绘制展开 / 收起箭头。
     * @param painter 绘制器。
     * @param rect 当前行分支区矩形（条目内容左侧）。
     * @param index 当前行模型索引。
     */
    void drawBranches(QPainter* painter, const QRect& rect, const QModelIndex& index) const override {
        if (painter == nullptr || !rect.isValid() || model() == nullptr || !model()->hasChildren(index)) {
            return;
        }

        const QColor color(g_activeTokens.textSecondary);
        const bool open = isExpanded(index);
        const int size = 10;
        const QPointF center(rect.right() - 11, rect.center().y());

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setPen(QPen(color, 1.8));
        painter->setBrush(Qt::NoBrush);

        QPolygonF chevron;
        if (open) {
            // 展开态：向下 V 形。
            chevron << QPointF(center.x() - size / 2.0, center.y() - size / 4.0)
                    << QPointF(center.x(), center.y() + size / 4.0)
                    << QPointF(center.x() + size / 2.0, center.y() - size / 4.0);
        } else {
            // 收起态：向右 > 形。
            chevron << QPointF(center.x() - size / 4.0, center.y() - size / 2.0)
                    << QPointF(center.x() + size / 4.0, center.y())
                    << QPointF(center.x() - size / 4.0, center.y() + size / 2.0);
        }
        painter->drawPolyline(chevron);
        painter->restore();
    }
};

/**
 * @brief 路径列绘制代理，使用路径语义省略代替 Qt 默认字符省略。
 */
class PathElideDelegate : public QStyledItemDelegate {
public:
    /**
     * @brief 构造路径列绘制代理。
     * @param parent 父级对象。
     */
    explicit PathElideDelegate(QObject* parent = nullptr) : QStyledItemDelegate(parent) {
    }

    /**
     * @brief 绘制路径单元格。
     * @param painter 绘制器。
     * @param option 单元格样式选项。
     * @param index 模型索引。
     */
    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        if (painter == nullptr) {
            return;
        }

        QStyleOptionViewItem backgroundOption(option);
        initStyleOption(&backgroundOption, index);
        const QString displayPath = index.data(Qt::DisplayRole).toString();
        backgroundOption.text.clear();
        backgroundOption.icon = QIcon();

        const QWidget* widget = option.widget;
        QStyle* style = widget != nullptr ? widget->style() : QApplication::style();
        style->drawControl(QStyle::CE_ItemViewItem, &backgroundOption, painter, widget);

        if (displayPath.isEmpty()) {
            return;
        }

        const QRect textRect = option.rect.adjusted(10, 0, -10, 0);
        const QString text = ElidePathForWidth(displayPath, option.fontMetrics, textRect.width());
        painter->save();
        painter->setClipRect(option.rect);
        const bool selected = option.state.testFlag(QStyle::State_Selected);
        painter->setPen(backgroundOption.palette.color(selected ? QPalette::HighlightedText : QPalette::Text));
        painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, text);
        painter->restore();
    }
};

/**
 * @brief 将宽字符串转换为 QString。
 * @param value 宽字符串。
 * @return QString 文本。
 */
QString ToQString(const std::wstring& value) {
    return QString::fromWCharArray(value.c_str(), static_cast<int>(value.size()));
}

/**
 * @brief 将 QString 转换为宽字符串。
 * @param value Qt 字符串。
 * @return 宽字符串。
 */
std::wstring ToWideString(const QString& value) {
    return value.toStdWString();
}

/**
 * @brief 把扫描根路径转成 QSettings 安全键段(字母数字保留,其余替换为 '_')。
 *
 * 路径含 ':'、'\\'、'/' 等,QSettings 键里这些字符会被当作分组分隔符或非法字符,
 * 故归一化为纯字母数字+下划线(如 "C:\Users" → "C__Users")。不同根路径几乎不会冲突。
 * @param path 扫描根路径。
 * @return 可直接拼接进 QSettings 键名的安全段。
 */
QString SanitizeSettingsKey(const QString& path) {
    QString out;
    out.reserve(path.size());
    for (const QChar& ch : path) {
        out += ch.isLetterOrNumber() ? ch : QLatin1Char('_');
    }
    return out.isEmpty() ? QStringLiteral("root") : out;
}

/**
 * @brief 将文本转义为 CSV 字段。
 * @param value 原始文本。
 * @return CSV 字段。
 */
QString EscapeCsv(const QString& value) {
    QString escaped = value;
    escaped.replace(QStringLiteral("\""), QStringLiteral("\"\""));
    return QStringLiteral("\"%1\"").arg(escaped);
}

/**
 * @brief 转义 HTML 文本（& < > " '）。
 * @param value 原始文本。
 * @return HTML 安全文本。
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

/**
 * @brief 把表头与行渲染为 CSV 文本（含表头行）。
 * @param headers 表头。
 * @param rows 数据行（每行一组原始单元格）。
 * @return CSV 文本（不含 BOM）。
 */
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

/**
 * @brief 把表头与行渲染为带内联样式的 HTML 报表。
 * @param title 报表标题。
 * @param headers 表头。
 * @param rows 数据行。
 * @return 完整 HTML 文档（UTF-8，不含 BOM）。
 */
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

/**
 * @brief 获取单调时钟毫秒数。
 * @return 当前单调时钟毫秒数。
 */
std::int64_t SteadyMilliseconds() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

/**
 * @brief 将窗口放到当前可用屏幕区域中央。
 * @param window 需要移动的窗口。
 */
void CenterWindowOnScreen(QWidget* window) {
    if (window == nullptr) {
        return;
    }

    QScreen* screen = window->screen();
    if (screen == nullptr) {
        screen = QGuiApplication::primaryScreen();
    }
    if (screen == nullptr) {
        return;
    }

    const QRect availableGeometry = screen->availableGeometry();
    QSize targetSize = window->size();
    if (targetSize.width() > availableGeometry.width() || targetSize.height() > availableGeometry.height()) {
        targetSize = targetSize.boundedTo(availableGeometry.size());
        window->resize(targetSize);
    }

    const QPoint centeredPosition(
        availableGeometry.left() + (availableGeometry.width() - window->width()) / 2,
        availableGeometry.top() + (availableGeometry.height() - window->height()) / 2);
    window->move(centeredPosition);
}

/**
 * @brief 获取 DiskLens 固定本地数据目录。
 * @return 本机用户目录下的 DiskLens 数据路径。
 */
QString DiskLensDataDirectory() {
    QString directory = qEnvironmentVariable("LOCALAPPDATA");
    if (directory.isEmpty()) {
        directory = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    } else {
        directory += QStringLiteral("/DiskLens");
    }

    QDir().mkpath(directory);
    return directory;
}

/**
 * @brief 将日期时间格式化为详情弹窗文本。
 * @param value 文件时间。
 * @return 格式化后的日期时间，无效时返回占位符。
 */
QString FormatDetailDateTime(const QDateTime& value) {
    return value.isValid() ? value.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")) : QStringLiteral("-");
}

/**
 * @brief 把 Unix epoch 毫秒格式化为表格用的绝对日期。
 * @param msec Unix epoch 毫秒。
 * @return "yyyy-MM-dd"；未知（≤0）时返回占位符。
 */
QString FormatModifiedDate(std::int64_t msec) {
    if (msec <= 0) {
        return QStringLiteral("—");
    }
    return QDateTime::fromMSecsSinceEpoch(msec).toString(QStringLiteral("yyyy-MM-dd"));
}

/**
 * @brief 将布尔状态格式化为中文文本。
 * @param value 布尔值。
 * @return 是或否。
 */
QString FormatYesNo(bool value) {
    return value ? QStringLiteral("是") : QStringLiteral("否");
}

/**
 * @brief 获取 Windows 文件属性摘要。
 * @param path 文件或目录路径。
 * @return 文件属性摘要文本。
 */
QString FileAttributeSummary(const QString& path) {
    if (path.isEmpty()) {
        return QStringLiteral("-");
    }

    const std::wstring nativePath = QDir::toNativeSeparators(path).toStdWString();
    const DWORD attributes = GetFileAttributesW(nativePath.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        return QStringLiteral("不可读取");
    }

    QStringList values;
    if ((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        values << QStringLiteral("目录");
    }
    if ((attributes & FILE_ATTRIBUTE_READONLY) != 0) {
        values << QStringLiteral("只读");
    }
    if ((attributes & FILE_ATTRIBUTE_HIDDEN) != 0) {
        values << QStringLiteral("隐藏");
    }
    if ((attributes & FILE_ATTRIBUTE_SYSTEM) != 0) {
        values << QStringLiteral("系统");
    }
    if ((attributes & FILE_ATTRIBUTE_ARCHIVE) != 0) {
        values << QStringLiteral("归档");
    }
    if ((attributes & FILE_ATTRIBUTE_COMPRESSED) != 0) {
        values << QStringLiteral("压缩");
    }
    if ((attributes & FILE_ATTRIBUTE_ENCRYPTED) != 0) {
        values << QStringLiteral("加密");
    }
    if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
        values << QStringLiteral("重解析点");
    }
    if (values.isEmpty()) {
        values << QStringLiteral("普通");
    }
    return values.join(QStringLiteral("、"));
}

/**
 * @brief 获取最近扫描结果缓存文件路径。
 * @return 缓存文件完整路径。
 */
QString CacheFilePath() {
    return DiskLensDataDirectory() + QStringLiteral("/last-scan.ndmcache");
}

/**
 * @brief 获取全系统快速搜索索引缓存文件路径。
 * @return 索引缓存文件完整路径。
 */
QString SearchIndexCacheFilePath() {
    return DiskLensDataDirectory() + QStringLiteral("/system-search.ndmindex");
}

/**
 * @brief 枚举本机固定磁盘根路径。
 * @return 固定磁盘根路径列表，例如 C:\。
 */
QStringList EnumerateFixedDriveRoots() {
    wchar_t buffer[512]{};
    const DWORD bufferLength = static_cast<DWORD>(sizeof(buffer) / sizeof(buffer[0]));
    const DWORD length = GetLogicalDriveStringsW(bufferLength, buffer);
    QStringList roots;
    if (length == 0 || length > bufferLength) {
        roots << QStringLiteral("C:\\");
        return roots;
    }

    const wchar_t* current = buffer;
    while (*current != L'\0') {
        const std::wstring root(current);
        if (GetDriveTypeW(root.c_str()) == DRIVE_FIXED) {
            roots << QString::fromStdWString(root);
        }
        current += root.size() + 1;
    }

    if (roots.isEmpty()) {
        roots << QStringLiteral("C:\\");
    }
    return roots;
}

/**
 * @brief 生成快速搜索使用的小写检索键。
 * @param name 名称。
 * @param path 路径。
 * @return 合并后的检索键。
 */
QString MakeSearchKey(const QString& name, const QString& path) {
    return name.toCaseFolded() + QLatin1Char('\n') + path.toCaseFolded();
}

/**
 * @brief 生成表格路径列使用的紧凑显示文本。
 * @param path 完整路径。
 * @return 保留盘符、父目录和文件名的中间省略路径。
 */
QString CompactDisplayPath(const QString& path) {
    constexpr qsizetype maxPathTextLength = 110;
    QString normalized = QDir::toNativeSeparators(path);
    if (normalized.size() <= maxPathTextLength || normalized.size() < 8) {
        return normalized;
    }

    QString rootText;
    if (normalized.size() >= 3 && normalized[1] == QLatin1Char(':')) {
        rootText = normalized.left(3);
    } else if (normalized.startsWith(QStringLiteral("\\\\"))) {
        const QStringList parts = normalized.split(QLatin1Char('\\'), Qt::SkipEmptyParts);
        if (parts.size() >= 2) {
            rootText = QStringLiteral("\\\\") + parts.at(0) + QLatin1Char('\\') + parts.at(1) + QLatin1Char('\\');
        }
    }

    const QString tailSource = rootText.isEmpty() ? normalized : normalized.mid(rootText.size());
    const QStringList parts = tailSource.split(QLatin1Char('\\'), Qt::SkipEmptyParts);
    if (parts.size() <= 3) {
        return normalized;
    }

    QStringList tailParts;
    const int keepParts = std::min(4, static_cast<int>(parts.size()));
    for (int index = parts.size() - keepParts; index < parts.size(); ++index) {
        tailParts << parts.at(index);
    }
    QString compact = rootText + QStringLiteral("...\\") + tailParts.join(QLatin1Char('\\'));

    while (compact.size() > maxPathTextLength && tailParts.size() > 2) {
        tailParts.removeFirst();
        compact = rootText + QStringLiteral("...\\") + tailParts.join(QLatin1Char('\\'));
    }

    if (compact.size() <= maxPathTextLength) {
        return compact;
    }

    const QString fileName = parts.last();
    const QString parentName = parts.size() >= 2 ? parts.at(parts.size() - 2) : QString();
    const qsizetype availableFileChars = std::max<qsizetype>(32, maxPathTextLength - rootText.size() - parentName.size() - 8);
    return rootText + QStringLiteral("...\\") +
           (parentName.isEmpty() ? QString() : parentName + QLatin1Char('\\')) +
           fileName.right(availableFileChars);
}

/**
 * @brief 按像素宽度生成表格路径列的友好省略文本。
 * @param path 完整路径。
 * @param metrics 当前字体度量。
 * @param pixelWidth 可用像素宽度。
 * @return 适合当前列宽展示的路径文本。
 */
QString ElidePathForWidth(const QString& path, const QFontMetrics& metrics, int pixelWidth) {
    const QString normalized = QDir::toNativeSeparators(path);
    if (normalized.isEmpty() || pixelWidth <= 0 || metrics.horizontalAdvance(normalized) <= pixelWidth) {
        return normalized;
    }

    return metrics.elidedText(normalized, Qt::ElideRight, pixelWidth);
}

bool LooksLikeWindowsPath(const QString& value) {
    const QString normalized = QDir::toNativeSeparators(value.trimmed());
    return (normalized.size() >= 2 && normalized.at(1) == QLatin1Char(':')) ||
           normalized.startsWith(QStringLiteral("\\\\"));
}

QString ContainingDirectoryForDisplay(const QString& fullPath, bool isDirectory) {
    QString normalized = QDir::toNativeSeparators(fullPath.trimmed());
    if (normalized.isEmpty() || !LooksLikeWindowsPath(normalized)) {
        return normalized;
    }

    while (normalized.size() > 3 && normalized.endsWith(QLatin1Char('\\'))) {
        normalized.chop(1);
    }

    if (normalized.size() == 2 && normalized.at(1) == QLatin1Char(':')) {
        return normalized + QLatin1Char('\\');
    }
    if (normalized.size() == 3 && normalized.at(1) == QLatin1Char(':') && normalized.at(2) == QLatin1Char('\\')) {
        return normalized;
    }

    const int slashIndex = normalized.lastIndexOf(QLatin1Char('\\'));
    if (slashIndex < 0) {
        return normalized;
    }

    if (normalized.startsWith(QStringLiteral("\\\\"))) {
        const QStringList parts = normalized.split(QLatin1Char('\\'), Qt::SkipEmptyParts);
        if (parts.size() <= 2) {
            return normalized;
        }
        if (parts.size() == 3 && isDirectory) {
            return QStringLiteral("\\\\") + parts.at(0) + QLatin1Char('\\') + parts.at(1);
        }
    }

    if (slashIndex == 2 && normalized.size() >= 3 && normalized.at(1) == QLatin1Char(':')) {
        return normalized.left(3);
    }

    Q_UNUSED(isDirectory);
    return normalized.left(slashIndex);
}

void ApplyNativeWindowIcon(QWidget* window) {
    if (window == nullptr) {
        return;
    }

    window->setWindowIcon(QIcon(QStringLiteral(":/icons/app.ico")));

    const HWND handle = reinterpret_cast<HWND>(window->winId());
    if (handle == nullptr) {
        return;
    }

    HINSTANCE instance = GetModuleHandleW(nullptr);
    const auto loadIcon = [instance](int width, int height) -> HICON {
        return static_cast<HICON>(LoadImageW(
            instance,
            MAKEINTRESOURCEW(IDI_APP_ICON),
            IMAGE_ICON,
            width,
            height,
            LR_SHARED));
    };

    if (HICON smallIcon = loadIcon(GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON))) {
        SendMessageW(handle, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(smallIcon));
    }
    if (HICON largeIcon = loadIcon(GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON))) {
        SendMessageW(handle, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(largeIcon));
    }
}

QMessageBox::StandardButton ShowAppMessageBox(
    QWidget* parent,
    QMessageBox::Icon icon,
    const QString& title,
    const QString& text,
    QMessageBox::StandardButtons buttons,
    QMessageBox::StandardButton defaultButton) {
    QMessageBox box(icon, title, text, buttons, parent);
    if (defaultButton != QMessageBox::NoButton) {
        box.setDefaultButton(defaultButton);
    }

    ApplyNativeWindowIcon(&box);
    QTimer::singleShot(0, &box, [&box]() {
        ApplyNativeWindowIcon(&box);
    });
    return static_cast<QMessageBox::StandardButton>(box.exec());
}

/**
 * @brief 向数据流写入宽字符串。
 * @param stream 目标数据流。
 * @param value 宽字符串。
 */
void WriteWideString(QDataStream& stream, const std::wstring& value) {
    stream << QString::fromStdWString(value);
}

/**
 * @brief 从数据流读取宽字符串。
 * @param stream 来源数据流。
 * @return 宽字符串。
 */
std::wstring ReadWideString(QDataStream& stream) {
    QString value;
    stream >> value;
    return value.toStdWString();
}

/**
 * @brief 递归写入扫描节点。
 * @param stream 目标数据流。
 * @param node 扫描节点。
 */
void WriteScanNode(QDataStream& stream, const core::ScanNode& node) {
    WriteWideString(stream, node.name);
    WriteWideString(stream, node.path);
    stream << static_cast<quint32>(node.kind);
    stream << static_cast<quint64>(node.ownBytes);
    stream << static_cast<quint64>(node.totalBytes);
    stream << static_cast<qint64>(node.lastModifiedMsec);
    stream << static_cast<quint32>(node.children.size());

    for (const auto& child : node.children) {
        WriteScanNode(stream, *child);
    }
}

/**
 * @brief 递归读取扫描节点。
 * @param stream 来源数据流。
 * @return 扫描节点。
 */
std::unique_ptr<core::ScanNode> ReadScanNode(QDataStream& stream, quint32 version) {
    auto node = std::make_unique<core::ScanNode>();
    node->name = ReadWideString(stream);
    node->path = ReadWideString(stream);

    quint32 kind = 0;
    quint64 ownBytes = 0;
    quint64 totalBytes = 0;
    quint32 childCount = 0;
    stream >> kind >> ownBytes >> totalBytes;
    // v2 起新增最后修改时间字段；v1 缓存没有该字段，读默认 0。
    if (version >= 2) {
        qint64 lastModifiedMsec = 0;
        stream >> lastModifiedMsec;
        node->lastModifiedMsec = static_cast<std::int64_t>(lastModifiedMsec);
    }
    stream >> childCount;

    node->kind = static_cast<core::NodeKind>(kind);
    node->ownBytes = static_cast<std::uint64_t>(ownBytes);
    node->totalBytes = static_cast<std::uint64_t>(totalBytes);
    node->children.reserve(childCount);

    for (quint32 index = 0; index < childCount; ++index) {
        node->children.push_back(ReadScanNode(stream, version));
    }

    return node;
}

/**
 * @brief 保存最近一次扫描结果缓存。
 * @param result 扫描结果。
 * @param usedNtfsMft 是否使用 NTFS 极速通道。
 */
void SaveScanCache(const core::ScanResult& result, bool usedNtfsMft) {
    QFile file(CacheFilePath());
    if (!file.open(QIODevice::WriteOnly)) {
        return;
    }

    QDataStream stream(&file);
    stream.setVersion(QDataStream::Qt_6_0);
    stream << static_cast<quint32>(0x4E444D43);
    stream << static_cast<quint32>(2);
    stream << static_cast<quint8>(usedNtfsMft ? 1 : 0);
    stream << static_cast<quint64>(result.fileCount);
    stream << static_cast<quint64>(result.directoryCount);
    stream << static_cast<quint64>(result.errorCount);
    stream << static_cast<quint32>(result.extensions.size());

    for (const auto& entry : result.extensions) {
        WriteWideString(stream, entry.second.extension);
        stream << static_cast<quint64>(entry.second.totalBytes);
        stream << static_cast<quint64>(entry.second.fileCount);
    }

    stream << static_cast<quint8>(result.root ? 1 : 0);
    if (result.root) {
        WriteScanNode(stream, *result.root);
    }
}

/**
 * @brief 加载最近一次扫描结果缓存。
 * @param usedNtfsMft 输出缓存是否来自 NTFS 极速通道。
 * @return 扫描结果，失败时返回 nullptr。
 */
std::unique_ptr<core::ScanResult> LoadScanCache(bool& usedNtfsMft) {
    QFile file(CacheFilePath());
    if (!file.open(QIODevice::ReadOnly)) {
        return nullptr;
    }

    QDataStream stream(&file);
    stream.setVersion(QDataStream::Qt_6_0);

    quint32 magic = 0;
    quint32 version = 0;
    quint8 usedFlag = 0;
    stream >> magic >> version >> usedFlag;
    if (magic != 0x4E444D43 || (version != 1 && version != 2) || stream.status() != QDataStream::Ok) {
        return nullptr;
    }

    auto result = std::make_unique<core::ScanResult>();
    quint64 fileCount = 0;
    quint64 directoryCount = 0;
    quint64 errorCount = 0;
    quint32 extensionCount = 0;
    stream >> fileCount >> directoryCount >> errorCount >> extensionCount;

    result->fileCount = static_cast<std::uint64_t>(fileCount);
    result->directoryCount = static_cast<std::uint64_t>(directoryCount);
    result->errorCount = static_cast<std::uint64_t>(errorCount);

    for (quint32 index = 0; index < extensionCount; ++index) {
        core::ExtensionSummary summary;
        summary.extension = ReadWideString(stream);
        quint64 totalBytes = 0;
        quint64 summaryFileCount = 0;
        stream >> totalBytes >> summaryFileCount;
        summary.totalBytes = static_cast<std::uint64_t>(totalBytes);
        summary.fileCount = static_cast<std::uint64_t>(summaryFileCount);
        result->extensions[summary.extension] = summary;
    }

    quint8 hasRoot = 0;
    stream >> hasRoot;
    if (hasRoot != 0) {
        result->root = ReadScanNode(stream, version);
    }

    if (stream.status() != QDataStream::Ok || !result->root) {
        return nullptr;
    }

    usedNtfsMft = usedFlag != 0;
    return result;
}

/**
 * @brief 获取卷已用空间，供 NTFS 极速结果做合理性校验。
 * @param rootPath 卷根路径，例如 C:\。
 * @return 已用空间字节数，失败时返回 0。
 */
std::uint64_t QueryVolumeUsedBytes(const std::wstring& rootPath) {
    ULARGE_INTEGER freeBytesAvailable{};
    ULARGE_INTEGER totalBytes{};
    ULARGE_INTEGER totalFreeBytes{};
    if (!GetDiskFreeSpaceExW(rootPath.c_str(), &freeBytesAvailable, &totalBytes, &totalFreeBytes)) {
        return 0;
    }

    return totalBytes.QuadPart > totalFreeBytes.QuadPart ? totalBytes.QuadPart - totalFreeBytes.QuadPart : 0;
}

/**
 * @brief 卷空间信息。
 */
struct VolumeSpaceInfo {
    /**
     * @brief 查询是否成功。
     */
    bool valid = false;

    /**
     * @brief 卷总容量。
     */
    std::uint64_t totalBytes = 0;

    /**
     * @brief 当前用户可用空间。
     */
    std::uint64_t freeBytes = 0;
};

/**
 * @brief 查询指定路径所在卷的总容量和可用空间。
 * @param rootPath 扫描路径或卷根路径。
 * @return 查询到的卷空间信息。
 */
VolumeSpaceInfo QueryVolumeSpaceInfo(const std::wstring& rootPath) {
    ULARGE_INTEGER freeBytesAvailable{};
    ULARGE_INTEGER totalBytes{};
    ULARGE_INTEGER totalFreeBytes{};
    if (!GetDiskFreeSpaceExW(rootPath.c_str(), &freeBytesAvailable, &totalBytes, &totalFreeBytes)) {
        return {};
    }

    return VolumeSpaceInfo{
        true,
        static_cast<std::uint64_t>(totalBytes.QuadPart),
        static_cast<std::uint64_t>(freeBytesAvailable.QuadPart),
    };
}

/**
 * @brief 判断 NTFS 极速扫描结果是否明显无效。
 * @param result 扫描结果。
 * @param rootPath 卷根路径。
 * @return 明显无效时返回 true。
 */
bool IsNtfsResultSuspiciouslySmall(const core::ScanResult& result, const std::wstring& rootPath) {
    if (!result.root) {
        return true;
    }
    if (result.root->totalBytes == 0 && (result.fileCount > 0 || result.directoryCount > 0)) {
        return false;
    }

    const std::uint64_t usedBytes = QueryVolumeUsedBytes(rootPath);
    if (usedBytes == 0) {
        return false;
    }

    if (result.root->totalBytes >= usedBytes) {
        return false;
    }

    const std::uint64_t visibleBytes = result.root->totalBytes;
    const std::uint64_t minimumPlausibleBytes = usedBytes * 20ULL / 100ULL;
    return usedBytes > 10ULL * 1024ULL * 1024ULL * 1024ULL &&
           visibleBytes < minimumPlausibleBytes &&
           result.fileCount + result.directoryCount < 1000ULL;
}

/**
 * @brief 判断路径是否为磁盘根路径。
 * @param rootPath 待判断路径。
 * @return 是磁盘根路径时返回 true。
 */
bool IsDriveRootPath(const std::wstring& rootPath) {
    return rootPath.size() == 3 && rootPath[1] == L':' && (rootPath[2] == L'\\' || rootPath[2] == L'/');
}

/**
 * @brief 查询卷文件系统名称。
 * @param rootPath 卷根路径，例如 C:\。
 * @return 文件系统名称，查询失败时返回空文本。
 */
QString QueryVolumeFileSystemName(const std::wstring& rootPath) {
    wchar_t fileSystemName[MAX_PATH]{};
    if (!GetVolumeInformationW(
            rootPath.c_str(),
            nullptr,
            0,
            nullptr,
            nullptr,
            nullptr,
            fileSystemName,
            MAX_PATH)) {
        return QString();
    }

    return QString::fromWCharArray(fileSystemName);
}

/**
 * @brief 生成 NTFS 极速扫描不可用的诊断原因。
 * @param rootPath 扫描根路径。
 * @param elevated 当前进程是否已拥有管理员权限。
 * @return 不可用原因；返回空文本表示可以尝试 NTFS MFT 极速扫描。
 */
QString DiagnoseNtfsFastScanBlocker(const std::wstring& rootPath, bool elevated) {
    if (!IsDriveRootPath(rootPath)) {
        return QStringLiteral("NTFS 极速扫描只能直接扫描盘符根目录，例如 C:\\ 或 D:\\；当前选择的是子目录或非盘符路径");
    }

    const UINT driveType = GetDriveTypeW(rootPath.c_str());
    if (driveType != DRIVE_FIXED && driveType != DRIVE_REMOVABLE) {
        return QStringLiteral("当前位置不是本地磁盘分区，网络盘、虚拟位置或特殊挂载点不能直接读取 NTFS MFT");
    }

    const QString fileSystemName = QueryVolumeFileSystemName(rootPath);
    if (fileSystemName.isEmpty()) {
        return QStringLiteral("无法读取卷文件系统信息，可能被权限、加密、驱动或安全软件拦截");
    }
    if (fileSystemName.compare(QStringLiteral("NTFS"), Qt::CaseInsensitive) != 0) {
        return QStringLiteral("当前分区文件系统是 %1，不是 NTFS；固态硬盘如果格式化为 exFAT、FAT32 或 ReFS，也不能使用 MFT 极速扫描")
            .arg(fileSystemName);
    }

    if (!elevated) {
        return QStringLiteral("当前程序没有管理员权限，Windows 不允许直接读取卷设备和 NTFS MFT");
    }

    return QString();
}

/**
 * @brief 递归计算路径占用大小。
 * @param path 要统计的路径。
 * @return 路径大小，失败时忽略不可访问项。
 */
std::uint64_t CalculatePathBytes(const std::filesystem::path& path) {
    // std::filesystem 在 MSVC 下默认仍受 MAX_PATH 限制,加 \\?\ 前缀后可越过;迭代产生的子路径会继承该前缀。
    const std::filesystem::path longPath(core::MakeLongPath(path.wstring()));

    std::error_code error;
    if (std::filesystem::is_regular_file(longPath, error)) {
        return std::filesystem::file_size(longPath, error);
    }

    std::uint64_t total = 0;
    if (!std::filesystem::exists(longPath, error)) {
        return 0;
    }

    std::filesystem::recursive_directory_iterator iterator(
        longPath,
        std::filesystem::directory_options::skip_permission_denied,
        error);
    const std::filesystem::recursive_directory_iterator end;
    while (!error && iterator != end) {
        if (std::filesystem::is_regular_file(iterator->path(), error)) {
            total += std::filesystem::file_size(iterator->path(), error);
        }
        iterator.increment(error);
    }

    return total;
}

/**
 * @brief 删除路径，可选择是否移入回收站。
 * @param path 要删除的路径。
 * @param allowUndo 是否允许通过回收站撤销。
 * @return 成功时返回 true。
 */
bool DeletePathWithShell(const QString& path, bool allowUndo) {
    std::wstring nativePath = QDir::toNativeSeparators(path).toStdWString();
    // SHFileOperationW 既受 MAX_PATH 限制、又拒绝 \\?\ 前缀;超长路径直接判失败,
    // 由调用方计入"未清理"——绝不在此回退成永久删除(那样会绕过回收站造成数据丢失)。
    // 完整支持(长路径回收站)见后续 IFileOperation 改造。
    if (nativePath.size() > MAX_PATH) {
        return false;
    }
    nativePath.push_back(L'\0');
    nativePath.push_back(L'\0');

    SHFILEOPSTRUCTW operation{};
    operation.wFunc = FO_DELETE;
    operation.pFrom = nativePath.c_str();
    operation.fFlags = FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT;
    if (allowUndo) {
        operation.fFlags |= FOF_ALLOWUNDO;
    }
    return SHFileOperationW(&operation) == 0 && !operation.fAnyOperationsAborted;
}

/**
 * @brief 把路径移动到回收站。
 * @param path 要删除的路径。
 * @return 成功时返回 true。
 */
bool RecyclePath(const QString& path) {
    return DeletePathWithShell(path, true);
}

/**
 * @brief 永久删除路径。
 * @param path 要删除的路径。
 * @return 成功时返回 true。
 */
bool PermanentlyDeletePath(const QString& path) {
    return DeletePathWithShell(path, false);
}

/**
 * @brief 判断路径是否是系统关键位置。
 * @param path 要检查的路径。
 * @return 属于关键位置时返回 true。
 */
bool IsProtectedManualDeletePath(const QString& path) {
    if (path.trimmed().isEmpty()) {
        return true;
    }

    const QString normalized = QDir::toNativeSeparators(QDir::cleanPath(path)).toCaseFolded();
    QFileInfo info(normalized);
    if (info.isRoot()) {
        return true;
    }

    QStringList protectedPaths;
    protectedPaths << QDir::toNativeSeparators(QDir::cleanPath(QDir::rootPath())).toCaseFolded();
    protectedPaths << QStringLiteral("c:\\windows");
    protectedPaths << QStringLiteral("c:\\program files");
    protectedPaths << QStringLiteral("c:\\program files (x86)");
    protectedPaths << QStringLiteral("c:\\programdata");
    const QString userProfile = qEnvironmentVariable("USERPROFILE");
    if (!userProfile.isEmpty()) {
        protectedPaths << QDir::toNativeSeparators(QDir::cleanPath(userProfile)).toCaseFolded();
    }
    protectedPaths << QDir::toNativeSeparators(QCoreApplication::applicationDirPath()).toCaseFolded();

    for (const QString& protectedPath : protectedPaths) {
        if (protectedPath.isEmpty()) {
            continue;
        }
        if (normalized == protectedPath) {
            return true;
        }
    }

    if (normalized.endsWith(QStringLiteral("\\system volume information")) ||
        normalized.endsWith(QStringLiteral("\\$recycle.bin"))) {
        return true;
    }

    return false;
}

/**
 * @brief 垃圾清理候选行。
 */
struct CleanupRow {
    /**
     * @brief 名称。
     */
    QString name;

    /**
     * @brief 格式化大小。
     */
    QString size;

    /**
     * @brief 类型。
     */
    QString type;

    /**
     * @brief 清理来源大类。
     */
    QString section;

    /**
     * @brief 完整路径。
     */
    QString path;

    /**
     * @brief 原始大小，单位为字节。
     */
    std::uint64_t bytes = 0;
};

/**
 * @brief 获取清理类别所属大类。
 * @param category 清理类别。
 * @return 展示用大类名称。
 */
QString CleanupSectionForCategory(const QString& category) {
    if (category.contains(QStringLiteral("Chrome")) ||
        category.contains(QStringLiteral("Edge")) ||
        category.contains(QStringLiteral("Firefox")) ||
        category.contains(QStringLiteral("网络缓存"))) {
        return QStringLiteral("浏览器缓存");
    }
    if (category.contains(QStringLiteral("Windows")) ||
        category.contains(QStringLiteral("系统")) ||
        category.contains(QStringLiteral("更新")) ||
        category.contains(QStringLiteral("预读取")) ||
        category.contains(QStringLiteral("错误报告"))) {
        return QStringLiteral("系统清理");
    }
    if (category.contains(QStringLiteral("npm")) ||
        category.contains(QStringLiteral("pip")) ||
        category.contains(QStringLiteral("NuGet")) ||
        category.contains(QStringLiteral("Gradle")) ||
        category.contains(QStringLiteral("Maven")) ||
        category.contains(QStringLiteral("Yarn")) ||
        category.contains(QStringLiteral("pnpm")) ||
        category.contains(QStringLiteral("Docker")) ||
        category.contains(QStringLiteral("JetBrains")) ||
        category.contains(QStringLiteral("Cargo"))) {
        return QStringLiteral("开发工具");
    }
    if (category.contains(QStringLiteral("最近访问")) ||
        category.contains(QStringLiteral("跳转列表"))) {
        return QStringLiteral("隐私痕迹");
    }
    if (category.contains(QStringLiteral("Shader")) ||
        category.contains(QStringLiteral("DirectX")) ||
        category.contains(QStringLiteral("显卡"))) {
        return QStringLiteral("图形缓存");
    }
    return QStringLiteral("应用缓存");
}

/**
 * @brief 判断清理类别是否默认勾选。
 * @param risk 风险级别。
 * @return 默认应勾选时返回 true。
 */
bool CleanupRiskCheckedByDefault(const QString& risk) {
    return risk == QStringLiteral("安全");
}

/**
 * @brief 获取清理大类的展示顺序。
 * @param section 清理大类名称。
 * @return 排序权重，数值越小越靠前。
 */
int CleanupSectionOrder(const QString& section) {
    if (section == QStringLiteral("浏览器缓存")) {
        return 10;
    }
    if (section == QStringLiteral("系统清理")) {
        return 20;
    }
    if (section == QStringLiteral("应用缓存")) {
        return 30;
    }
    if (section == QStringLiteral("图形缓存")) {
        return 40;
    }
    if (section == QStringLiteral("隐私痕迹")) {
        return 50;
    }
    if (section == QStringLiteral("开发工具")) {
        return 60;
    }
    return 100;
}

/**
 * @brief 生成垃圾清理分组标题文本。
 * @param section 清理大类名称。
 * @param bytes 大类累计可释放空间。
 * @param count 大类包含的清理项数量。
 * @return 面向用户展示的分组标题。
 */
QString FormatCleanupSectionTitle(const QString& section, std::uint64_t bytes, int count) {
    return QStringLiteral("  %1    %2 项    %3")
        .arg(section)
        .arg(count)
        .arg(ToQString(core::FormatBytes(bytes)));
}

/**
 * @brief 添加指定目录下的顶层清理候选项。
 * @param directoryPath 目录路径。
 * @param category 清理类别。
 * @param rows 输出候选行。
 * @param maxRows 最大候选数量。
 */
void CollectCleanupRows(const QString& directoryPath, const QString& category, std::vector<CleanupRow>& rows, std::size_t maxRows) {
    if (rows.size() >= maxRows || directoryPath.isEmpty()) {
        return;
    }

    std::error_code error;
    const std::filesystem::path directory = directoryPath.toStdWString();
    if (!std::filesystem::exists(directory, error) || !std::filesystem::is_directory(directory, error)) {
        return;
    }

    std::filesystem::directory_iterator iterator(directory, std::filesystem::directory_options::skip_permission_denied, error);
    const std::filesystem::directory_iterator end;
    while (!error && iterator != end && rows.size() < maxRows) {
        const std::filesystem::path itemPath = iterator->path();
        const std::uint64_t bytes = CalculatePathBytes(itemPath);
        if (bytes > 0) {
            rows.push_back(CleanupRow{
                QString::fromStdWString(itemPath.filename().wstring()),
                ToQString(core::FormatBytes(bytes)),
                category,
                CleanupSectionForCategory(category),
                QString::fromStdWString(itemPath.wstring()),
                bytes,
            });
        }
        iterator.increment(error);
    }
}

/**
 * @brief 添加整个目录作为一个清理候选项。
 * @param directoryPath 目录路径。
 * @param category 清理类别。
 * @param rows 输出候选行。
 * @param maxRows 最大候选数量。
 */
void CollectCleanupDirectory(const QString& directoryPath, const QString& category, std::vector<CleanupRow>& rows, std::size_t maxRows) {
    if (rows.size() >= maxRows || directoryPath.isEmpty()) {
        return;
    }

    std::error_code error;
    const std::filesystem::path directory = directoryPath.toStdWString();
    if (!std::filesystem::exists(directory, error) || !std::filesystem::is_directory(directory, error)) {
        return;
    }

    const std::uint64_t bytes = CalculatePathBytes(directory);
    if (bytes == 0) {
        return;
    }

    rows.push_back(CleanupRow{
        QString::fromStdWString(directory.filename().wstring()),
        ToQString(core::FormatBytes(bytes)),
        category,
        CleanupSectionForCategory(category),
        QString::fromStdWString(directory.wstring()),
        bytes,
    });
}

/**
 * @brief 添加匹配指定前缀的文件作为清理候选项。
 * @param directoryPath 要扫描的目录。
 * @param filePrefix 文件名前缀。
 * @param category 清理类别。
 * @param rows 输出候选行。
 * @param maxRows 最大候选数量。
 */
void CollectPrefixedFiles(const QString& directoryPath, const QString& filePrefix, const QString& category, std::vector<CleanupRow>& rows, std::size_t maxRows) {
    if (rows.size() >= maxRows || directoryPath.isEmpty()) {
        return;
    }

    std::error_code error;
    const std::filesystem::path directory = directoryPath.toStdWString();
    if (!std::filesystem::exists(directory, error) || !std::filesystem::is_directory(directory, error)) {
        return;
    }

    std::filesystem::directory_iterator iterator(directory, std::filesystem::directory_options::skip_permission_denied, error);
    const std::filesystem::directory_iterator end;
    while (!error && iterator != end && rows.size() < maxRows) {
        const std::filesystem::path itemPath = iterator->path();
        const QString fileName = QString::fromStdWString(itemPath.filename().wstring());
        if (!fileName.startsWith(filePrefix, Qt::CaseInsensitive)) {
            iterator.increment(error);
            continue;
        }

        const std::uint64_t bytes = CalculatePathBytes(itemPath);
        if (bytes > 0) {
            rows.push_back(CleanupRow{
                fileName,
                ToQString(core::FormatBytes(bytes)),
                category,
                CleanupSectionForCategory(category),
                QString::fromStdWString(itemPath.wstring()),
                bytes,
            });
        }
        iterator.increment(error);
    }
}

/**
 * @brief 在父目录的每个子目录下收集指定相对缓存目录。
 * @param parentDirectoryPath 父目录路径。
 * @param relativeCachePath 子目录内的相对缓存路径。
 * @param category 清理类别。
 * @param rows 输出候选行。
 * @param maxRows 最大候选数量。
 */
void CollectProfileCacheDirectories(const QString& parentDirectoryPath, const QString& relativeCachePath, const QString& category, std::vector<CleanupRow>& rows, std::size_t maxRows) {
    if (rows.size() >= maxRows || parentDirectoryPath.isEmpty() || relativeCachePath.isEmpty()) {
        return;
    }

    std::error_code error;
    const std::filesystem::path parentDirectory = parentDirectoryPath.toStdWString();
    if (!std::filesystem::exists(parentDirectory, error) || !std::filesystem::is_directory(parentDirectory, error)) {
        return;
    }

    std::filesystem::directory_iterator iterator(parentDirectory, std::filesystem::directory_options::skip_permission_denied, error);
    const std::filesystem::directory_iterator end;
    while (!error && iterator != end && rows.size() < maxRows) {
        if (std::filesystem::is_directory(iterator->path(), error)) {
            const QString candidatePath = QString::fromStdWString((iterator->path() / relativeCachePath.toStdWString()).wstring());
            CollectCleanupDirectory(candidatePath, category, rows, maxRows);
        }
        iterator.increment(error);
    }
}

/**
 * @brief 为系统已用但目录枚举不可见的空间补充占位节点。
 * @param result 扫描结果。
 * @param rootPath 扫描根路径。
 */
void AddReservedSpacePlaceholder(core::ScanResult& result, const std::wstring& rootPath) {
    if (!result.root || !IsDriveRootPath(rootPath)) {
        return;
    }

    const std::uint64_t usedBytes = QueryVolumeUsedBytes(rootPath);
    if (usedBytes <= result.root->totalBytes) {
        return;
    }

    const std::uint64_t reservedBytes = usedBytes - result.root->totalBytes;
    if (reservedBytes < 64ULL * 1024ULL * 1024ULL) {
        return;
    }

    auto reserved = std::make_unique<core::ScanNode>();
    reserved->name = L"(系统保留/无法访问)";
    reserved->path = rootPath + L"(系统保留/无法访问)";
    reserved->kind = core::NodeKind::File;
    reserved->ownBytes = reservedBytes;
    reserved->totalBytes = reservedBytes;

    result.root->totalBytes = usedBytes;
    result.root->children.push_back(std::move(reserved));
    std::sort(result.root->children.begin(), result.root->children.end(), [](const auto& left, const auto& right) {
        return left->totalBytes > right->totalBytes;
    });
}

}  // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("磁盘洞察"));
    setWindowIcon(QIcon(QStringLiteral(":/icons/app.ico")));
    setMenuWidget(CreateApplicationMenu());

    rootWidget_ = new QWidget(this);
    auto* shellLayout = new QHBoxLayout(rootWidget_);
    shellLayout->setContentsMargins(10, 10, 12, 12);
    shellLayout->setSpacing(10);

    auto* contentPanel = new QWidget(rootWidget_);
    auto* layout = new QVBoxLayout(contentPanel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);

    layout->addWidget(CreateCommandBar());
    layout->addWidget(CreateWorkspace(), 1);
    layout->addWidget(CreateInfoBar());
    shellLayout->addWidget(CreateModuleSidebar());
    shellLayout->addWidget(contentPanel, 1);

    loadingOverlay_ = qobject_cast<QFrame*>(CreateLoadingOverlay(rootWidget_));
    setCentralWidget(rootWidget_);
    // 设置最小尺寸，避免窗口过小时控件错位/裁剪，符合桌面商业软件规范。
    setMinimumSize(980, 600);
    // 允许从资源管理器拖入文件夹直接扫描。子控件(目录树/表格/Treemap/空状态遮罩)
    // 均未开启 acceptDrops,拖放事件会冒泡到本顶层窗口统一处理,与遮罩无冲突。
    setAcceptDrops(true);
    for (QPushButton* button : findChildren<QPushButton*>()) {
        button->setCursor(Qt::PointingHandCursor);
    }
    ApplyNativeWindowIcon(this);
    UpdateLoadingOverlayGeometry();
    InstallEmptyStateOverlays();
    ApplyStyle();
    InitializeEmptyState();
    LoadUiSettings();

    searchDebounceTimer_ = new QTimer(this);
    searchDebounceTimer_->setSingleShot(true);
    searchDebounceTimer_->setInterval(180);
    busyAnimationTimer_ = new QTimer(this);
    busyAnimationTimer_->setInterval(120);

    // E2 实时文件夹监控:监视已扫描根及一级子目录,变化经防抖触发自动重扫。
    // folderWatcher_ 父对象为 this,随主窗口析构自动销毁;directoryChanged 经事件循环投递到 UI 线程。
    folderWatcher_ = new QFileSystemWatcher(this);
    connect(folderWatcher_, &QFileSystemWatcher::directoryChanged, this, &MainWindow::ScheduleWatcherRescan);
    watchDebounceTimer_ = new QTimer(this);
    watchDebounceTimer_->setSingleShot(true);
    watchDebounceTimer_->setInterval(500);
    connect(watchDebounceTimer_, &QTimer::timeout, this, &MainWindow::OnWatchDebounceTimeout);

    InstallShortcuts();

    directoryTree_->setContextMenuPolicy(Qt::CustomContextMenu);
    directoryView_->setContextMenuPolicy(Qt::CustomContextMenu);
    directoryTable_->setContextMenuPolicy(Qt::CustomContextMenu);
    largeFilesView_->setContextMenuPolicy(Qt::CustomContextMenu);
    staleFilesView_->setContextMenuPolicy(Qt::CustomContextMenu);
    typeStatsTable_->setContextMenuPolicy(Qt::CustomContextMenu);
    duplicateTree_->setContextMenuPolicy(Qt::CustomContextMenu);
    searchView_->setContextMenuPolicy(Qt::CustomContextMenu);
    cleanupTree_->setContextMenuPolicy(Qt::CustomContextMenu);

    // 启动后默认聚焦扫描位置选择框，便于立即选择磁盘并开始分析。
    QTimer::singleShot(0, this, [this]() {
        if (driveCombo_ != nullptr) {
            driveCombo_->setFocus();
        }
    });

    connect(scanButton_, &QPushButton::clicked, this, [this]() {
        if (scanning_.load()) {
            return;
        }
        const QString rootPath = driveCombo_ != nullptr ? driveCombo_->currentText().trimmed() : QString();
        SetInfoBar(
            QStringLiteral("准备扫描"),
            latestResult_ ? latestResult_->fileCount : 0,
            latestResult_ ? latestResult_->directoryCount : 0,
            rootPath);
        if (!rootPath.isEmpty()) {
            PrimeLoadingFeedback(QStringLiteral("扫描中"), rootPath);
        }
        FlushImmediateFeedback();
        QTimer::singleShot(0, this, &MainWindow::StartScan);
    });
    connect(stopButton_, &QPushButton::clicked, this, [this]() {
        RunAfterClickFeedback(QStringLiteral("正在停止"), QStringLiteral("正在请求扫描线程停止"), [this]() {
            StopScan();
        });
    });
    connect(boostButton_, &QPushButton::clicked, this, [this]() {
        RunAfterClickFeedback(QStringLiteral("准备极速模式"), QStringLiteral("检查管理员权限并重启"), [this]() {
            RestartAsAdministrator();
        });
    });
    connect(browseButton_, &QPushButton::clicked, this, [this]() {
        RunAfterClickFeedback(QStringLiteral("选择扫描位置"), QStringLiteral("正在打开目录选择窗口"), [this]() {
            BrowseScanLocation();
        });
    });
    connect(directoryView_, &QTableView::doubleClicked, this, [this](const QModelIndex&) {
        ActivateDirectoryTableRow();
    });
    connect(largeFilesView_, &QTableView::doubleClicked, this, [this](const QModelIndex&) {
        OpenSelectedPath();
    });
    connect(staleFilesView_, &QTableView::doubleClicked, this, [this](const QModelIndex&) {
        OpenSelectedPath();
    });
    connect(duplicateTree_, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item, int) {
        if (item == nullptr || item->childCount() > 0) {
            return;  // 组节点不响应双击,仅成员行在资源管理器中定位。
        }
        const QString path = item->data(0, Qt::UserRole + 1).toString();
        if (!path.isEmpty()) {
            RevealPathInExplorer(path);
        }
    });
    connect(searchView_, &QTableView::doubleClicked, this, [this](const QModelIndex&) {
        OpenSelectedPath();
    });
    connect(searchView_->horizontalHeader(), &QHeaderView::sectionClicked, this, [this](int section) {
        if (searchModel_ == nullptr || searchView_ == nullptr) {
            return;
        }
        QHeaderView* header = searchView_->horizontalHeader();
        const bool sameSection = header->sortIndicatorSection() == section;
        const Qt::SortOrder order = sameSection && header->sortIndicatorOrder() == Qt::AscendingOrder
            ? Qt::DescendingOrder
            : Qt::AscendingOrder;
        header->setSortIndicatorShown(true);
        header->setSortIndicator(section, order);
        if (!searchResultRows_.isEmpty()) {
            std::stable_sort(searchResultRows_.begin(), searchResultRows_.end(), [section, order](const ResultRow& left, const ResultRow& right) {
                int comparison = 0;
                switch (section) {
                case 1:
                    comparison = left.bytes < right.bytes ? -1 : (left.bytes > right.bytes ? 1 : 0);
                    break;
                case 2:
                    comparison = QString::localeAwareCompare(left.type, right.type);
                    break;
                case 3:
                    comparison = QString::localeAwareCompare(left.fullPath.isEmpty() ? left.displayPath : left.fullPath,
                                                             right.fullPath.isEmpty() ? right.displayPath : right.fullPath);
                    break;
                case 4:
                    comparison = left.modifiedMsec < right.modifiedMsec ? -1 : (left.modifiedMsec > right.modifiedMsec ? 1 : 0);
                    break;
                case 0:
                default:
                    comparison = QString::localeAwareCompare(left.name, right.name);
                    break;
                }
                if (comparison == 0) {
                    comparison = left.bytes < right.bytes ? 1 : (left.bytes > right.bytes ? -1 : 0);
                }
                return order == Qt::AscendingOrder ? comparison < 0 : comparison > 0;
            });
            const QString keyword = searchEdit_ != nullptr ? searchEdit_->text().trimmed() : QString();
            RenderVisibleSearchResults(keyword);
        } else {
            searchModel_->sort(section, order);
        }
    });
    connect(cleanupTree_, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item, int) {
        if (item == nullptr) {
            return;
        }
        const std::size_t groupIndex = CleanupGroupIndexFromItem(item);
        if (groupIndex >= cleanupGroups_.size()) {
            return;
        }
        if (item->childCount() > 0) {
            item->setExpanded(!item->isExpanded());
        }
    });
    connect(searchEdit_, &QLineEdit::textChanged, this, &MainWindow::ScheduleSearch);
    connect(searchDebounceTimer_, &QTimer::timeout, this, &MainWindow::PopulateSearchTable);
    connect(searchIndexButton_, &QPushButton::clicked, this, [this]() {
        RunAfterClickFeedback(QStringLiteral("准备重建索引"), QStringLiteral("将扫描所有固定磁盘"), [this]() {
            StartSystemSearchIndex();
        });
    });
    connect(searchLoadMoreButton_, &QPushButton::clicked, this, [this]() {
        RunAfterClickFeedback(QStringLiteral("加载更多结果"), QStringLiteral("正在追加搜索结果"), [this]() {
            LoadMoreSearchResults();
        });
    });
    connect(searchView_->verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int value) {
        if (searchView_ == nullptr || searchAutoLoadPending_ || searchResultRows_.isEmpty()) {
            return;
        }

        QScrollBar* scrollBar = searchView_->verticalScrollBar();
        if (scrollBar == nullptr || value < scrollBar->maximum() - 3 || searchVisibleResultCount_ >= searchResultRows_.size()) {
            return;
        }

        searchAutoLoadPending_ = true;
        QTimer::singleShot(80, this, [this]() {
            searchAutoLoadPending_ = false;
            LoadMoreSearchResults();
        });
    });
    connect(busyAnimationTimer_, &QTimer::timeout, this, &MainWindow::AdvanceBusyAnimation);
    connect(cleanupScanButton_, &QPushButton::clicked, this, [this]() {
        RunAfterClickFeedback(QStringLiteral("准备清理体检"), QStringLiteral("正在收集可清理位置"), [this]() {
            ScanCleanupCandidates();
        });
    });
    connect(cleanupDeleteButton_, &QPushButton::clicked, this, [this]() {
        RunAfterClickFeedback(QStringLiteral("准备清理"), QStringLiteral("正在汇总选中的清理项"), [this]() {
            DeleteSelectedCleanupItems();
        });
    });
    connect(tabs_, &QTabWidget::currentChanged, this, [this](int) {
        if (tabs_ != nullptr && tabs_->currentWidget() != nullptr && tabs_->currentWidget()->isAncestorOf(searchView_)) {
            EnsureSearchIndexCacheLoading();
        }
        UpdateModuleChrome();
        PopulateCurrentDeferredTab();
    });
    connect(directoryTree_, &QTreeWidget::customContextMenuRequested, this, &MainWindow::ShowTreeContextMenu);
    connect(directoryView_, &QTableView::customContextMenuRequested, this, [this](const QPoint& position) {
        const QModelIndex index = directoryView_->indexAt(position);
        if (index.isValid()) {
            directoryView_->selectionModel()->select(index, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        }
        QMenu menu(this);
        const ResultRow* row = directoryModel_ != nullptr ? directoryModel_->RowAt(directoryView_->currentIndex().row()) : nullptr;
        const QString path = row != nullptr ? row->fullPath : QString();
        if (row != nullptr && row->isParentRow) {
            menu.addAction(QStringLiteral("返回上级"), this, &MainWindow::ActivateDirectoryTableRow);
        } else if (row != nullptr && row->isDirectory) {
            menu.addAction(QStringLiteral("进入目录"), this, &MainWindow::ActivateDirectoryTableRow);
        }
        AddPathActions(menu, path, true, row != nullptr ? row->bytes : 0, row != nullptr);
        menu.addSeparator();
        menu.addAction(QStringLiteral("导出当前列表"), this, &MainWindow::ExportCurrentTable);
        menu.exec(directoryView_->viewport()->mapToGlobal(position));
    });
    connect(directoryTable_, &QTableWidget::customContextMenuRequested, this, [this](const QPoint& position) {
        ShowTableContextMenu(directoryTable_, position);
    });
    connect(largeFilesView_, &QTableView::customContextMenuRequested, this, [this](const QPoint& position) {
        const QModelIndex index = largeFilesView_->indexAt(position);
        if (index.isValid()) {
            largeFilesView_->selectionModel()->select(index, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        }
        const ResultRow* row = largeFilesModel_ != nullptr ? largeFilesModel_->RowAt(largeFilesView_->currentIndex().row()) : nullptr;
        const QString path = row != nullptr ? row->fullPath : QString();
        QMenu menu(this);
        AddPathActions(menu, path, false, row != nullptr ? row->bytes : 0, row != nullptr);
        menu.addSeparator();
        menu.addAction(QStringLiteral("导出当前列表"), this, &MainWindow::ExportCurrentTable);
        menu.exec(largeFilesView_->viewport()->mapToGlobal(position));
    });
    connect(staleFilesView_, &QTableView::customContextMenuRequested, this, [this](const QPoint& position) {
        const QModelIndex index = staleFilesView_->indexAt(position);
        if (index.isValid()) {
            staleFilesView_->selectionModel()->select(index, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        }
        const ResultRow* row = staleFilesModel_ != nullptr ? staleFilesModel_->RowAt(staleFilesView_->currentIndex().row()) : nullptr;
        const QString path = row != nullptr ? row->fullPath : QString();
        QMenu menu(this);
        AddPathActions(menu, path, false, row != nullptr ? row->bytes : 0, row != nullptr);
        menu.addSeparator();
        menu.addAction(QStringLiteral("导出当前列表"), this, &MainWindow::ExportCurrentTable);
        menu.exec(staleFilesView_->viewport()->mapToGlobal(position));
    });
    connect(typeStatsTable_, &QTableWidget::customContextMenuRequested, this, [this](const QPoint& position) {
        ShowTypeStatsContextMenu(position);
    });
    connect(duplicateTree_, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint& position) {
        ShowDuplicateContextMenu(position);
    });
    connect(searchView_, &QTableView::customContextMenuRequested, this, [this](const QPoint& position) {
        const QModelIndex index = searchView_->indexAt(position);
        if (index.isValid()) {
            searchView_->selectionModel()->select(index, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        }
        const ResultRow* row = searchModel_ != nullptr ? searchModel_->RowAt(searchView_->currentIndex().row()) : nullptr;
        const QString path = row != nullptr ? row->fullPath : QString();
        QMenu menu(this);
        AddPathActions(menu, path, false, row != nullptr ? row->bytes : 0, row != nullptr);
        menu.addSeparator();
        menu.addAction(QStringLiteral("导出当前列表"), this, &MainWindow::ExportCurrentTable);
        menu.exec(searchView_->viewport()->mapToGlobal(position));
    });
    connect(cleanupTree_, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint& position) {
        ShowCleanupContextMenu(position);
    });
    connect(cleanupTree_, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* item, int column) {
        if (cleanupTreeUpdating_ || item == nullptr || column != 0) {
            return;
        }

        cleanupTreeUpdating_ = true;
        if (item->childCount() > 0) {
            for (int index = 0; index < item->childCount(); ++index) {
                QTreeWidgetItem* child = item->child(index);
                if (child != nullptr && child->data(0, Qt::UserRole).isValid()) {
                    child->setCheckState(0, item->checkState(0));
                }
            }
        } else if (QTreeWidgetItem* parent = item->parent()) {
            int checkedCount = 0;
            int partialCount = 0;
            int checkableCount = 0;
            for (int index = 0; index < parent->childCount(); ++index) {
                QTreeWidgetItem* child = parent->child(index);
                if (child == nullptr || !child->data(0, Qt::UserRole).isValid()) {
                    continue;
                }
                ++checkableCount;
                const Qt::CheckState state = child->checkState(0);
                if (state == Qt::Checked) {
                    ++checkedCount;
                } else if (state == Qt::PartiallyChecked) {
                    ++partialCount;
                }
            }
            if (checkableCount > 0 && checkedCount == checkableCount) {
                parent->setCheckState(0, Qt::Checked);
            } else if (checkedCount > 0 || partialCount > 0) {
                parent->setCheckState(0, Qt::PartiallyChecked);
            } else {
                parent->setCheckState(0, Qt::Unchecked);
            }
        }
        cleanupTreeUpdating_ = false;
        UpdateCleanupSelectionSummary();
    });
    connect(directoryTree_, &QTreeWidget::itemExpanded, this, [this](QTreeWidgetItem* item) {
        if (item == nullptr || item->data(0, Qt::UserRole + 1).toBool()) {
            return;
        }

        const auto address = item->data(0, Qt::UserRole).toULongLong();
        auto* node = reinterpret_cast<const core::ScanNode*>(address);
        if (node == nullptr) {
            return;
        }

        qDeleteAll(item->takeChildren());
        PopulateTreeItem(item, *node);
        item->setData(0, Qt::UserRole + 1, true);
    });
    connect(directoryTree_, &QTreeWidget::itemSelectionChanged, this, [this]() {
        auto* item = directoryTree_->currentItem();
        if (item == nullptr) {
            return;
        }

        const auto address = item->data(0, Qt::UserRole).toULongLong();
        auto* node = reinterpret_cast<const core::ScanNode*>(address);
        if (node == nullptr) {
            return;
        }

        SelectNodeDetails(*node);
    });

    QTimer::singleShot(250, this, [this]() {
        SetInfoBar(
            IsRunningAsAdministrator() ? QStringLiteral("已是极速权限") : QStringLiteral("就绪"),
            0,
            0,
            IsRunningAsAdministrator() ? QStringLiteral("默认管理员启动，可直接使用 NTFS 极速扫描") : QStringLiteral("请选择扫描位置"));
    });
    QTimer::singleShot(500, this, &MainWindow::LoadLastScanCacheAsync);
}

void MainWindow::resizeEvent(QResizeEvent* event) {
    QMainWindow::resizeEvent(event);
    UpdateLoadingOverlayGeometry();
}

void MainWindow::closeEvent(QCloseEvent* event) {
    DisarmWatcher();  // E2:关闭时先拆 watcher,防御性(父对象析构本也会销毁)。
    SaveUiSettings();
    QMainWindow::closeEvent(event);
}

void MainWindow::showEvent(QShowEvent* event) {
    QMainWindow::showEvent(event);
    if (screenHooksWired_ || windowHandle() == nullptr) {
        return;
    }
    screenHooksWired_ = true;

    // 混 DPI 多显示器:窗口换接到不同 DPR 的屏幕,或某屏缩放变化时,刷新动作 / 导航图标,
    // 使其按当前屏幕 devicePixelRatio 重新渲染。目录树 / 表格项图标由 MakeIcon 的高 DPR 渲染基线保持清晰。
    connect(windowHandle(), &QWindow::screenChanged, this, [this](QScreen* next) {
        RefreshIconsForScreenChange();
        if (next != nullptr) {
            connect(next, &QScreen::logicalDotsPerInchChanged, this, [this]() { RefreshIconsForScreenChange(); }, Qt::UniqueConnection);
        }
    });

    if (QScreen* current = windowHandle()->screen()) {
        connect(current, &QScreen::logicalDotsPerInchChanged, this, [this]() { RefreshIconsForScreenChange(); }, Qt::UniqueConnection);
    }
}

void MainWindow::RefreshIconsForScreenChange() {
    app_icons::InvalidateCache();
    ApplyActionIcons();
}

/**
 * @brief 显示目录树右键菜单。
 * @param position 鼠标在目录树视口中的位置。
 */
void MainWindow::ShowTreeContextMenu(const QPoint& position) {
    QTreeWidgetItem* item = directoryTree_->itemAt(position);
    if (item != nullptr) {
        directoryTree_->setCurrentItem(item);
    }

    const core::ScanNode* node = SelectedTreeNode();
    if (node == nullptr) {
        return;
    }

    const QString path = ToQString(node->path);
    QMenu menu(this);
    AddPathActions(menu, path, true, node->totalBytes, true);
    menu.addSeparator();
    menu.addAction(QStringLiteral("导出当前列表"), this, &MainWindow::ExportCurrentTable);
    menu.exec(directoryTree_->viewport()->mapToGlobal(position));
}

/**
 * @brief 显示表格右键菜单。
 * @param table 触发菜单的表格。
 * @param position 鼠标在表格视口中的位置。
 */
void MainWindow::ShowTableContextMenu(QTableWidget* table, const QPoint& position) {
    if (table == nullptr) {
        return;
    }

    QTableWidgetItem* item = table->itemAt(position);
    if (item != nullptr) {
        table->setCurrentCell(item->row(), item->column());
    }

    const QString path = SelectedTablePath(table);
    std::uint64_t scannedBytes = 0;
    bool hasScannedBytes = false;
    QTableWidgetItem* nameItem = table->currentRow() >= 0 ? table->item(table->currentRow(), 0) : nullptr;
    if (nameItem != nullptr) {
        const auto address = nameItem->data(Qt::UserRole).toULongLong();
        const auto* node = reinterpret_cast<const core::ScanNode*>(address);
        if (node != nullptr) {
            scannedBytes = node->totalBytes;
            hasScannedBytes = true;
        }
    }

    QMenu menu(this);
    if (table == directoryTable_) {
        const bool isParentRow = nameItem != nullptr && nameItem->data(Qt::UserRole + 2).toBool();
        const bool isDirectory = nameItem != nullptr && nameItem->data(Qt::UserRole + 1).toBool();
        if (isParentRow) {
            menu.addAction(QStringLiteral("返回上级"), this, &MainWindow::ActivateDirectoryTableRow);
        } else if (isDirectory) {
            menu.addAction(QStringLiteral("进入目录"), this, &MainWindow::ActivateDirectoryTableRow);
        }
    }
    AddPathActions(menu, path, true, scannedBytes, hasScannedBytes);
    menu.addSeparator();
    menu.addAction(QStringLiteral("导出当前列表"), this, &MainWindow::ExportCurrentTable);

    menu.exec(table->viewport()->mapToGlobal(position));
}

void MainWindow::ShowTypeStatsContextMenu(const QPoint& position) {
    if (typeStatsTable_ == nullptr) {
        return;
    }

    QTableWidgetItem* item = typeStatsTable_->itemAt(position);
    if (item != nullptr) {
        typeStatsTable_->setCurrentCell(item->row(), item->column());
    }

    QTableWidgetItem* extensionItem = typeStatsTable_->currentRow() >= 0 ? typeStatsTable_->item(typeStatsTable_->currentRow(), 0) : nullptr;
    QTableWidgetItem* sizeItem = typeStatsTable_->currentRow() >= 0 ? typeStatsTable_->item(typeStatsTable_->currentRow(), 1) : nullptr;
    QTableWidgetItem* countItem = typeStatsTable_->currentRow() >= 0 ? typeStatsTable_->item(typeStatsTable_->currentRow(), 2) : nullptr;
    const QString extension = extensionItem != nullptr ? extensionItem->text() : QString();
    const QString size = sizeItem != nullptr ? sizeItem->text() : QString();
    const QString count = countItem != nullptr ? countItem->text() : QString();

    QMenu menu(this);
    QAction* searchAction = menu.addAction(QStringLiteral("搜索该类型"), this, [this, extension]() {
        if (extension.isEmpty() || searchEdit_ == nullptr || tabs_ == nullptr || searchView_ == nullptr) {
            return;
        }
        const QString keyword = extension == QStringLiteral("(无扩展名)") ? QString() : extension;
        tabs_->setCurrentWidget(searchView_->parentWidget());
        searchEdit_->setText(keyword);
        searchEdit_->setFocus();
        ScheduleSearch();
    });
    menu.addSeparator();
    QAction* copyExtensionAction = menu.addAction(QStringLiteral("复制扩展名"), this, [extension]() {
        QApplication::clipboard()->setText(extension);
    });
    QAction* copySummaryAction = menu.addAction(QStringLiteral("复制统计信息"), this, [extension, size, count]() {
        QApplication::clipboard()->setText(QStringLiteral("%1\t%2\t%3").arg(extension, size, count));
    });
    menu.addSeparator();
    menu.addAction(QStringLiteral("导出当前列表"), this, &MainWindow::ExportCurrentTable);

    const bool hasExtension = !extension.isEmpty() && extension != QStringLiteral("正在生成类型统计") && extension != QStringLiteral("等待扫描完成");
    searchAction->setEnabled(hasExtension && extension != QStringLiteral("(无扩展名)"));
    copyExtensionAction->setEnabled(hasExtension);
    copySummaryAction->setEnabled(hasExtension);
    menu.exec(typeStatsTable_->viewport()->mapToGlobal(position));
}

void MainWindow::ShowCleanupContextMenu(const QPoint& position) {
    if (cleanupTree_ == nullptr) {
        return;
    }

    QTreeWidgetItem* item = cleanupTree_->itemAt(position);
    if (item != nullptr) {
        cleanupTree_->setCurrentItem(item);
    }

    QTreeWidgetItem* currentItem = cleanupTree_->currentItem();
    const std::size_t groupIndex = CleanupGroupIndexFromItem(currentItem);
    const bool hasGroup = groupIndex < cleanupGroups_.size();
    const QString selectedPath = currentItem != nullptr ? currentItem->data(0, Qt::UserRole + 1).toString() : QString();
    const std::uint64_t selectedBytes = currentItem != nullptr ? currentItem->data(0, Qt::UserRole + 2).toULongLong() : 0;
    const bool hasPath = !selectedPath.isEmpty();

    QMenu menu(this);
    QAction* toggleAction = menu.addAction(QStringLiteral("展开/折叠类别"), this, [currentItem]() {
        if (currentItem != nullptr && currentItem->childCount() > 0) {
            currentItem->setExpanded(!currentItem->isExpanded());
        }
    });
    QAction* detailAction = menu.addAction(QStringLiteral("查看清理明细"), this, [this, groupIndex]() {
        if (groupIndex >= cleanupGroups_.size()) {
            return;
        }
        const CleanupGroup& group = cleanupGroups_[groupIndex];
        QStringList preview;
        const std::size_t maxPreview = std::min<std::size_t>(group.paths.size(), 40);
        for (std::size_t index = 0; index < maxPreview; ++index) {
            preview << QDir::toNativeSeparators(group.paths[index]);
        }
        if (group.paths.size() > maxPreview) {
            preview << QStringLiteral("... 另有 %1 项").arg(static_cast<qulonglong>(group.paths.size() - maxPreview));
        }
        ShowAppMessageBox(
            this,
            QMessageBox::Information,
            QStringLiteral("清理明细"),
            QStringLiteral("%1\n\n风险：%2\n建议：%3\n预计释放：%4\n\n%5")
                .arg(group.name, group.risk, group.recommendation, ToQString(core::FormatBytes(group.bytes)), preview.join(QStringLiteral("\n"))));
    });
    QAction* copySummaryAction = menu.addAction(QStringLiteral("复制类别摘要"), this, [this, groupIndex]() {
        if (groupIndex >= cleanupGroups_.size()) {
            return;
        }
        const CleanupGroup& group = cleanupGroups_[groupIndex];
        QApplication::clipboard()->setText(QStringLiteral("%1\t%2\t%3\t%4\t%5 项")
                                               .arg(group.section,
                                                   group.name,
                                                   ToQString(core::FormatBytes(group.bytes)),
                                                   group.risk)
                                               .arg(static_cast<qulonglong>(group.paths.size())));
    });
    if (hasPath) {
        menu.addSeparator();
        menu.addAction(QStringLiteral("打开所在位置"), this, [this, selectedPath]() {
            RevealPathInExplorer(selectedPath);
        });
        menu.addAction(QStringLiteral("查看详情"), this, [this, selectedPath, selectedBytes]() {
            ShowPathDetails(selectedPath, selectedBytes, selectedBytes > 0);
        });
        menu.addAction(QStringLiteral("系统属性"), this, [this, selectedPath]() {
            ShowPathProperties(selectedPath);
        });
        menu.addAction(QStringLiteral("复制完整路径"), this, [selectedPath]() {
            QApplication::clipboard()->setText(selectedPath);
        });
    }
    menu.addSeparator();
    menu.addAction(QStringLiteral("只勾选安全项"), this, [this]() {
        SetCleanupCheckedMode(QStringLiteral("safe"));
    });
    menu.addAction(QStringLiteral("全部取消勾选"), this, [this]() {
        SetCleanupCheckedMode(QStringLiteral("none"));
    });
    menu.addAction(QStringLiteral("恢复默认选择"), this, [this]() {
        SetCleanupCheckedMode(QStringLiteral("default"));
    });
    menu.addSeparator();
    menu.addAction(QStringLiteral("导出当前列表"), this, &MainWindow::ExportCurrentTable);

    toggleAction->setEnabled(currentItem != nullptr && currentItem->childCount() > 0);
    detailAction->setEnabled(hasGroup);
    copySummaryAction->setEnabled(hasGroup);
    menu.exec(cleanupTree_->viewport()->mapToGlobal(position));
}

/**
 * @brief 获取清理树条目对应的清理分组下标。
 * @param item 清理树条目。
 * @return 分组下标，无效时返回 cleanupGroups_.size()。
 */
std::size_t MainWindow::CleanupGroupIndexFromItem(const QTreeWidgetItem* item) const {
    if (item == nullptr) {
        return cleanupGroups_.size();
    }

    QVariant groupData = item->data(0, Qt::UserRole);
    if (!groupData.isValid() && item->parent() != nullptr) {
        groupData = item->parent()->data(0, Qt::UserRole);
    }
    if (!groupData.isValid()) {
        return cleanupGroups_.size();
    }
    return static_cast<std::size_t>(groupData.toULongLong());
}

/**
 * @brief 统计清理树中已勾选项目并刷新底部汇总。
 */
void MainWindow::UpdateCleanupSelectionSummary() {
    if (cleanupTree_ == nullptr) {
        return;
    }

    std::uint64_t selectedBytes = 0;
    std::uint64_t selectedItems = 0;
    for (int row = 0; row < cleanupTree_->topLevelItemCount(); ++row) {
        QTreeWidgetItem* groupItem = cleanupTree_->topLevelItem(row);
        const std::size_t groupIndex = CleanupGroupIndexFromItem(groupItem);
        if (groupIndex >= cleanupGroups_.size() || groupItem->isHidden()) {
            continue;
        }

        const CleanupGroup& group = cleanupGroups_[groupIndex];
        if (groupItem->checkState(0) == Qt::Checked) {
            selectedBytes += group.bytes;
            selectedItems += static_cast<std::uint64_t>(group.paths.size());
            continue;
        }
        if (groupItem->checkState(0) != Qt::PartiallyChecked) {
            continue;
        }
        for (int childIndex = 0; childIndex < groupItem->childCount(); ++childIndex) {
            QTreeWidgetItem* child = groupItem->child(childIndex);
            if (child == nullptr || child->checkState(0) != Qt::Checked || !child->data(0, Qt::UserRole + 1).isValid()) {
                continue;
            }
            selectedBytes += child->data(0, Qt::UserRole + 2).toULongLong();
            ++selectedItems;
        }
    }

    if (cleanupSelectedLabel_ != nullptr) {
        cleanupSelectedLabel_->setText(QStringLiteral("已选中 %1 · %2 项")
                                           .arg(ToQString(core::FormatBytes(selectedBytes)))
                                           .arg(static_cast<qulonglong>(selectedItems)));
    }
    if (cleanupDeleteButton_ != nullptr) {
        cleanupDeleteButton_->setEnabled(selectedBytes > 0 && selectedItems > 0);
    }
    if (cleanupInfoLabel_ != nullptr) {
        std::uint64_t totalBytes = 0;
        std::uint64_t totalItems = 0;
        for (const CleanupGroup& group : cleanupGroups_) {
            totalBytes += group.bytes;
            totalItems += static_cast<std::uint64_t>(group.paths.size());
        }
        cleanupInfoLabel_->setText(QStringLiteral("垃圾清理 · 可回收 %1 · 共 %2 项 · 已选 %3 / %4 项")
                                       .arg(ToQString(core::FormatBytes(totalBytes)))
                                       .arg(static_cast<qulonglong>(totalItems))
                                       .arg(ToQString(core::FormatBytes(selectedBytes)))
                                       .arg(static_cast<qulonglong>(selectedItems)));
    }
}

void MainWindow::ApplyCleanupSectionFilter(const QString& section) {
    cleanupSectionFilter_ = section;
    for (QPushButton* button : cleanupSectionButtons_) {
        if (button == nullptr) {
            continue;
        }
        button->setChecked(button->property("section").toString() == section);
    }

    if (cleanupTree_ != nullptr) {
        for (int row = 0; row < cleanupTree_->topLevelItemCount(); ++row) {
            QTreeWidgetItem* groupItem = cleanupTree_->topLevelItem(row);
            const std::size_t groupIndex = CleanupGroupIndexFromItem(groupItem);
            if (groupItem == nullptr || groupIndex >= cleanupGroups_.size()) {
                continue;
            }

            const QString groupSection = cleanupGroups_[groupIndex].section;
            const bool softwareMatch = section == QStringLiteral("软件缓存") &&
                (groupSection == QStringLiteral("浏览器缓存") || groupSection == QStringLiteral("应用缓存"));
            const bool visible = section.isEmpty() || groupSection == section || softwareMatch;
            groupItem->setHidden(!visible);
        }
    }

    UpdateCleanupSelectionSummary();
}

/**
 * @brief 按指定模式批量设置清理树勾选状态。
 * @param mode 选择模式，支持 all、safe、none 和 default。
 */
void MainWindow::SetCleanupCheckedMode(const QString& mode) {
    if (cleanupTree_ == nullptr) {
        return;
    }

    cleanupTreeUpdating_ = true;
    for (int row = 0; row < cleanupTree_->topLevelItemCount(); ++row) {
        QTreeWidgetItem* groupItem = cleanupTree_->topLevelItem(row);
        const std::size_t groupIndex = CleanupGroupIndexFromItem(groupItem);
        if (groupItem == nullptr || groupIndex >= cleanupGroups_.size() || groupItem->isHidden()) {
            continue;
        }

        const CleanupGroup& group = cleanupGroups_[groupIndex];
        Qt::CheckState targetState = Qt::Unchecked;
        if (mode == QStringLiteral("all")) {
            targetState = Qt::Checked;
        } else if (mode == QStringLiteral("safe")) {
            targetState = group.risk == QStringLiteral("安全") ? Qt::Checked : Qt::Unchecked;
        } else if (mode == QStringLiteral("default")) {
            targetState = group.checkedByDefault ? Qt::Checked : Qt::Unchecked;
        }

        groupItem->setCheckState(0, targetState);
        for (int childIndex = 0; childIndex < groupItem->childCount(); ++childIndex) {
            QTreeWidgetItem* child = groupItem->child(childIndex);
            if (child != nullptr && child->data(0, Qt::UserRole).isValid()) {
                child->setCheckState(0, targetState);
            }
        }
    }
    cleanupTreeUpdating_ = false;
    UpdateCleanupSelectionSummary();
}

/**
 * @brief 获取当前目录树选中的扫描节点。
 * @return 当前扫描节点指针，未选中时返回 nullptr。
 */
const core::ScanNode* MainWindow::SelectedTreeNode() const {
    const QTreeWidgetItem* item = directoryTree_ != nullptr ? directoryTree_->currentItem() : nullptr;
    if (item == nullptr) {
        return nullptr;
    }

    const auto address = item->data(0, Qt::UserRole).toULongLong();
    return reinterpret_cast<const core::ScanNode*>(address);
}

/**
 * @brief 获取指定表格当前行的路径文本。
 * @param table 要读取的表格。
 * @return 路径文本，不存在时返回空字符串。
 */
QString MainWindow::SelectedTablePath(QTableWidget* table) const {
    if (table == nullptr || table->currentRow() < 0) {
        return QString();
    }

    QTableWidgetItem* item = table->item(table->currentRow(), 3);
    if (item == nullptr) {
        return QString();
    }

    const QString fullPath = item->data(Qt::UserRole).toString();
    return fullPath.isEmpty() ? item->text() : fullPath;
}

/**
 * @brief 将路径复制到系统剪贴板。
 * @param path 要复制的完整路径。
 */
void MainWindow::CopyPathToClipboard(const QString& path) {
    if (path.isEmpty()) {
        return;
    }

    QApplication::clipboard()->setText(path);
    SetInfoBar(QStringLiteral("已复制路径"), latestResult_ ? latestResult_->fileCount : 0, latestResult_ ? latestResult_->directoryCount : 0, path);
}

void MainWindow::CopyNameToClipboard(const QString& path) {
    if (path.isEmpty()) {
        return;
    }

    const QString name = QFileInfo(path).fileName();
    QApplication::clipboard()->setText(name.isEmpty() ? path : name);
    SetInfoBar(QStringLiteral("已复制名称"), latestResult_ ? latestResult_->fileCount : 0, latestResult_ ? latestResult_->directoryCount : 0, path);
}

void MainWindow::CopyParentPathToClipboard(const QString& path) {
    if (path.isEmpty()) {
        return;
    }

    const QString parentPath = QFileInfo(path).absolutePath();
    QApplication::clipboard()->setText(QDir::toNativeSeparators(parentPath));
    SetInfoBar(QStringLiteral("已复制所在目录"), latestResult_ ? latestResult_->fileCount : 0, latestResult_ ? latestResult_->directoryCount : 0, parentPath);
}

void MainWindow::OpenPathDirectly(const QString& path) {
    if (path.isEmpty()) {
        return;
    }

    if (!QFileInfo::exists(path)) {
        SetInfoBar(QStringLiteral("路径不可用"), latestResult_ ? latestResult_->fileCount : 0, latestResult_ ? latestResult_->directoryCount : 0, path);
        return;
    }

    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    SetInfoBar(QStringLiteral("已打开"), latestResult_ ? latestResult_->fileCount : 0, latestResult_ ? latestResult_->directoryCount : 0, path);
}

/**
 * @brief 使用 Windows 资源管理器定位路径。
 * @param path 要定位的完整路径。
 */
void MainWindow::RevealPathInExplorer(const QString& path) {
    if (path.isEmpty()) {
        return;
    }

    if (!QFileInfo::exists(path)) {
        SetInfoBar(QStringLiteral("路径不可用"), latestResult_ ? latestResult_->fileCount : 0, latestResult_ ? latestResult_->directoryCount : 0, path);
        return;
    }

    const QString normalizedPath = QDir::toNativeSeparators(path);
    const std::wstring arguments = QStringLiteral("/select,\"%1\"").arg(normalizedPath).toStdWString();
    ShellExecuteW(nullptr, L"open", L"explorer.exe", arguments.c_str(), nullptr, SW_SHOWNORMAL);
}

void MainWindow::ShowPathProperties(const QString& path) {
    if (path.isEmpty()) {
        return;
    }

    if (!QFileInfo::exists(path)) {
        SetInfoBar(QStringLiteral("路径不可用"), latestResult_ ? latestResult_->fileCount : 0, latestResult_ ? latestResult_->directoryCount : 0, path);
        return;
    }

    SHELLEXECUTEINFOW info{};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_INVOKEIDLIST;
    const std::wstring widePath = QDir::toNativeSeparators(path).toStdWString();
    info.lpVerb = L"properties";
    info.lpFile = widePath.c_str();
    info.nShow = SW_SHOWNORMAL;
    ShellExecuteExW(&info);
}

void MainWindow::ShowPathDetails(const QString& path, std::uint64_t scannedBytes, bool hasScannedBytes) {
    if (path.isEmpty()) {
        SetInfoBar(QStringLiteral("请选择项目"), 0, 0, QStringLiteral("没有选中路径"));
        return;
    }

    const QFileInfo fileInfo(path);
    const bool exists = fileInfo.exists();
    const bool isDirectory = exists && fileInfo.isDir();
    const bool isFile = exists && fileInfo.isFile();
    const QString nativePath = QDir::toNativeSeparators(path);
    const QString displayName = fileInfo.fileName().isEmpty() ? nativePath : fileInfo.fileName();
    const QString itemType = !exists
        ? QStringLiteral("路径不可用")
        : (isDirectory ? QStringLiteral("文件夹") : (isFile ? QStringLiteral("文件") : QStringLiteral("特殊项目")));
    const std::uint64_t detailBytes = hasScannedBytes
        ? scannedBytes
        : (isFile ? static_cast<std::uint64_t>(std::max<qint64>(0, fileInfo.size())) : 0);
    const QString sizeText = (hasScannedBytes || isFile) ? ToQString(core::FormatBytes(detailBytes)) : QStringLiteral("-");
    const QFile::Permissions permissions = fileInfo.permissions();

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("项目详情"));
    dialog.setWindowIcon(windowIcon());
    dialog.resize(640, 520);

    auto* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(20, 18, 20, 16);
    layout->setSpacing(12);

    auto* titleLabel = new QLabel(displayName, &dialog);
    titleLabel->setObjectName(QStringLiteral("AboutTitle"));
    titleLabel->setWordWrap(true);
    auto* pathLabel = new QLabel(nativePath, &dialog);
    pathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    pathLabel->setWordWrap(true);
    pathLabel->setToolTip(nativePath);

    auto* detailTable = new QTableWidget(&dialog);
    detailTable->setColumnCount(2);
    detailTable->setHorizontalHeaderLabels({QStringLiteral("项目"), QStringLiteral("内容")});
    detailTable->verticalHeader()->setVisible(false);
    detailTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    detailTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    detailTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    detailTable->setSelectionMode(QAbstractItemView::SingleSelection);
    detailTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    detailTable->setAlternatingRowColors(true);
    detailTable->setShowGrid(false);

    const auto addDetailRow = [detailTable](const QString& key, const QString& value) {
        const int row = detailTable->rowCount();
        detailTable->insertRow(row);
        auto* keyItem = new QTableWidgetItem(key);
        auto* valueItem = new QTableWidgetItem(value);
        keyItem->setToolTip(key);
        valueItem->setToolTip(value);
        detailTable->setItem(row, 0, keyItem);
        detailTable->setItem(row, 1, valueItem);
    };

    addDetailRow(QStringLiteral("名称"), displayName);
    addDetailRow(QStringLiteral("类型"), itemType);
    addDetailRow(QStringLiteral("大小"), sizeText);
    addDetailRow(QStringLiteral("完整路径"), nativePath);
    addDetailRow(QStringLiteral("所在目录"), QDir::toNativeSeparators(fileInfo.absolutePath()));
    addDetailRow(QStringLiteral("扩展名"), fileInfo.suffix().isEmpty() ? QStringLiteral("-") : fileInfo.suffix());
    addDetailRow(QStringLiteral("存在"), FormatYesNo(exists));
    addDetailRow(QStringLiteral("可读"), FormatYesNo(exists && fileInfo.isReadable()));
    addDetailRow(QStringLiteral("可写"), FormatYesNo(exists && fileInfo.isWritable()));
    addDetailRow(QStringLiteral("可执行"), FormatYesNo(exists && fileInfo.isExecutable()));
    addDetailRow(QStringLiteral("权限"), QStringLiteral("读取：%1　写入：%2　执行：%3")
                                          .arg(FormatYesNo((permissions & QFileDevice::ReadUser) != 0),
                                               FormatYesNo((permissions & QFileDevice::WriteUser) != 0),
                                               FormatYesNo((permissions & QFileDevice::ExeUser) != 0)));
    addDetailRow(QStringLiteral("创建时间"), FormatDetailDateTime(fileInfo.birthTime()));
    addDetailRow(QStringLiteral("修改时间"), FormatDetailDateTime(fileInfo.lastModified()));
    addDetailRow(QStringLiteral("访问时间"), FormatDetailDateTime(fileInfo.lastRead()));
    addDetailRow(QStringLiteral("属性"), FileAttributeSummary(path));
    addDetailRow(QStringLiteral("扫描大小来源"), hasScannedBytes ? QStringLiteral("当前扫描结果") : (isFile ? QStringLiteral("文件系统") : QStringLiteral("-")));
    detailTable->resizeRowsToContents();

    auto* buttons = new QDialogButtonBox(&dialog);
    QPushButton* copyButton = buttons->addButton(QStringLiteral("复制路径"), QDialogButtonBox::ActionRole);
    QPushButton* openButton = buttons->addButton(QStringLiteral("打开"), QDialogButtonBox::ActionRole);
    QPushButton* revealButton = buttons->addButton(QStringLiteral("打开所在位置"), QDialogButtonBox::ActionRole);
    QPushButton* propertiesButton = buttons->addButton(QStringLiteral("系统属性"), QDialogButtonBox::ActionRole);
    QPushButton* closeButton = buttons->addButton(QStringLiteral("关闭"), QDialogButtonBox::AcceptRole);
    openButton->setEnabled(exists);
    revealButton->setEnabled(exists);
    propertiesButton->setEnabled(exists);

    connect(copyButton, &QPushButton::clicked, this, [this, path]() {
        CopyPathToClipboard(path);
    });
    connect(openButton, &QPushButton::clicked, this, [this, path]() {
        OpenPathDirectly(path);
    });
    connect(revealButton, &QPushButton::clicked, this, [this, path]() {
        RevealPathInExplorer(path);
    });
    connect(propertiesButton, &QPushButton::clicked, this, [this, path]() {
        ShowPathProperties(path);
    });
    connect(closeButton, &QPushButton::clicked, &dialog, &QDialog::accept);

    layout->addWidget(titleLabel);
    layout->addWidget(pathLabel);
    layout->addWidget(detailTable, 1);
    layout->addWidget(buttons);
    ApplyNativeWindowIcon(&dialog);
    QTimer::singleShot(0, &dialog, [&dialog]() {
        ApplyNativeWindowIcon(&dialog);
    });
    dialog.exec();
}

void MainWindow::MovePathToRecycleBin(const QString& path) {
    if (path.isEmpty()) {
        return;
    }

    const QFileInfo fileInfo(path);
    if (!fileInfo.exists()) {
        SetInfoBar(QStringLiteral("路径不可用"), latestResult_ ? latestResult_->fileCount : 0, latestResult_ ? latestResult_->directoryCount : 0, path);
        return;
    }
    if (IsProtectedManualDeletePath(path)) {
        ShowAppMessageBox(this, QMessageBox::Information, QStringLiteral("受保护位置"), QStringLiteral("该位置属于系统或应用关键路径，已禁止通过手动操作移入回收站。"));
        return;
    }

    const QString displayName = fileInfo.fileName().isEmpty() ? path : fileInfo.fileName();
    const QMessageBox::StandardButton choice = ShowAppMessageBox(
        this,
        QMessageBox::Question,
        QStringLiteral("移入回收站"),
        QStringLiteral("确认将“%1”移入回收站吗？\n\n路径：%2\n\n操作完成后建议重新扫描当前位置。").arg(displayName, QDir::toNativeSeparators(path)),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);
    if (choice != QMessageBox::Yes) {
        return;
    }

    if (RecyclePath(path)) {
        SetInfoBar(QStringLiteral("已移入回收站"), latestResult_ ? latestResult_->fileCount : 0, latestResult_ ? latestResult_->directoryCount : 0, path);
    } else {
        ShowAppMessageBox(this, QMessageBox::Warning, QStringLiteral("移入回收站失败"), QStringLiteral("无法移动该项目，可能正在使用或权限不足。"));
    }
}

void MainWindow::AddPathActions(QMenu& menu, const QString& path, bool includeDirectOpen, std::uint64_t scannedBytes, bool hasScannedBytes) {
    QAction* openAction = nullptr;
    if (includeDirectOpen) {
        openAction = menu.addAction(QStringLiteral("打开"), this, [this, path]() {
            OpenPathDirectly(path);
        });
    }
    QAction* revealAction = menu.addAction(QStringLiteral("打开所在位置"), this, [this, path]() {
        RevealPathInExplorer(path);
    });
    menu.addSeparator();
    QAction* copyNameAction = menu.addAction(QStringLiteral("复制名称"), this, [this, path]() {
        CopyNameToClipboard(path);
    });
    QAction* copyPathAction = menu.addAction(QStringLiteral("复制完整路径"), this, [this, path]() {
        CopyPathToClipboard(path);
    });
    QAction* copyParentAction = menu.addAction(QStringLiteral("复制所在目录"), this, [this, path]() {
        CopyParentPathToClipboard(path);
    });
    menu.addSeparator();
    QAction* detailsAction = menu.addAction(QStringLiteral("查看详情"), this, [this, path, scannedBytes, hasScannedBytes]() {
        ShowPathDetails(path, scannedBytes, hasScannedBytes);
    });
    detailsAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+I")));
    QAction* propertiesAction = menu.addAction(QStringLiteral("属性"), this, [this, path]() {
        ShowPathProperties(path);
    });
    propertiesAction->setShortcut(QKeySequence(QStringLiteral("Alt+Return")));
    QAction* rescanAction = menu.addAction(QStringLiteral("扫描此位置"), this, [this, path]() {
        RescanPath(path);
    });
    menu.addSeparator();
    QAction* recycleAction = menu.addAction(QStringLiteral("移入回收站"), this, [this, path]() {
        MovePathToRecycleBin(path);
    });
    recycleAction->setShortcut(QKeySequence::Delete);

    const bool hasPath = !path.isEmpty();
    const bool existingPath = hasPath && QFileInfo::exists(path);
    if (openAction != nullptr) {
        openAction->setEnabled(existingPath);
    }
    revealAction->setEnabled(existingPath);
    copyNameAction->setEnabled(hasPath);
    copyPathAction->setEnabled(hasPath);
    copyParentAction->setEnabled(hasPath);
    detailsAction->setEnabled(hasPath);
    propertiesAction->setEnabled(existingPath);
    rescanAction->setEnabled(existingPath && QFileInfo(path).isDir());
    recycleAction->setEnabled(existingPath && !IsProtectedManualDeletePath(path));
    if (existingPath && IsProtectedManualDeletePath(path)) {
        recycleAction->setToolTip(QStringLiteral("系统或应用关键位置已受保护"));
    }
}

/**
 * @brief 将扫描位置切换为指定路径并立即重新扫描。
 * @param path 新的扫描路径。
 */
void MainWindow::RescanPath(const QString& path) {
    if (path.isEmpty()) {
        return;
    }

    driveCombo_->setCurrentText(path);
    StartScan();
}

void MainWindow::BrowseScanLocation() {
    const QString startPath = driveCombo_ != nullptr ? driveCombo_->currentText().trimmed() : QString();
    const QString path = QFileDialog::getExistingDirectory(this, QStringLiteral("选择扫描位置"), startPath);
    if (path.isEmpty() || driveCombo_ == nullptr) {
        return;
    }

    const QString nativePath = QDir::toNativeSeparators(path);
    if (driveCombo_->findText(nativePath) < 0) {
        driveCombo_->insertItem(0, nativePath);
    }
    driveCombo_->setCurrentText(nativePath);
}

void MainWindow::GoToParentDirectory() {
    if (latestResult_ == nullptr || latestResult_->root == nullptr || currentDirectoryNode_ == nullptr) {
        return;
    }

    auto parentIterator = parentByNode_.find(currentDirectoryNode_);
    const core::ScanNode* parent = parentIterator != parentByNode_.end()
        ? parentIterator->second
        : FindParentNode(*latestResult_->root, *currentDirectoryNode_);
    if (parent == nullptr) {
        return;
    }

    SelectNodeDetails(*parent);
}

const core::ScanNode* MainWindow::FindNodeByPath(const core::ScanNode& node, const QString& path) const {
    if (ToQString(node.path).compare(path, Qt::CaseInsensitive) == 0) {
        return &node;
    }

    for (const auto& child : node.children) {
        if (!child) {
            continue;
        }

        const core::ScanNode* found = FindNodeByPath(*child, path);
        if (found != nullptr) {
            return found;
        }
    }

    return nullptr;
}

void MainWindow::SelectNodeDetails(const core::ScanNode& node) {
    if (node.kind == core::NodeKind::Directory) {
        PopulateDirectoryTable(node);
        if (treemapWidget_ != nullptr) {
            treemapWidget_->SetRootNode(node);
        }
        if (treemapHint_ != nullptr) {
            const QString path = ToQString(node.path);
            treemapHint_->setText(QStringLiteral("空间概览\n%1").arg(QFontMetrics(treemapHint_->font()).elidedText(path, Qt::ElideMiddle, 140)));
            treemapHint_->setToolTip(path);
        }
        tabs_->setCurrentWidget(directoryView_);
        return;
    }

    if (directoryModel_ != nullptr) {
        const QString path = ToQString(node.path);
        directoryModel_->SetRows(QVector<ResultRow>{
            ResultRow{
                ToQString(node.name),
                ToQString(core::FormatBytes(node.totalBytes)),
                QStringLiteral("文件"),
                ContainingDirectoryForDisplay(path, false),
                path,
                MakeSearchKey(ToQString(node.name), path),
                node.totalBytes,
                reinterpret_cast<quint64>(&node),
                false,
                false,
                FormatModifiedDate(node.lastModifiedMsec),
                node.lastModifiedMsec,
            }
        });
    }
    if (treemapWidget_ != nullptr) {
        treemapWidget_->Clear(QStringLiteral("文件没有下级空间占比图"));
    }
    if (treemapHint_ != nullptr) {
        const QString path = ToQString(node.path);
        treemapHint_->setText(QStringLiteral("空间概览\n%1").arg(QFontMetrics(treemapHint_->font()).elidedText(path, Qt::ElideMiddle, 140)));
        treemapHint_->setToolTip(path);
    }
    tabs_->setCurrentWidget(directoryView_);
}

void MainWindow::ActivateDirectoryTableRow() {
    if (directoryView_ != nullptr && tabs_ != nullptr && tabs_->currentWidget() == directoryView_) {
        const ResultRow* row = directoryModel_ != nullptr ? directoryModel_->RowAt(directoryView_->currentIndex().row()) : nullptr;
        if (row == nullptr) {
            return;
        }

        if (row->isParentRow && latestResult_ && latestResult_->root && currentDirectoryNode_ != nullptr) {
            auto parentIterator = parentByNode_.find(currentDirectoryNode_);
            const core::ScanNode* parent = parentIterator != parentByNode_.end() ? parentIterator->second : FindParentNode(*latestResult_->root, *currentDirectoryNode_);
            if (parent != nullptr) {
                SelectNodeDetails(*parent);
            }
            return;
        }

        auto* node = reinterpret_cast<const core::ScanNode*>(row->nodeAddress);
        if (node == nullptr) {
            if (!row->fullPath.isEmpty()) {
                RevealPathInExplorer(row->fullPath);
            }
            return;
        }

        if (node->kind == core::NodeKind::Directory) {
            SelectNodeDetails(*node);
        } else {
            RevealPathInExplorer(ToQString(node->path));
        }
        return;
    }

    if (directoryTable_ == nullptr || directoryTable_->currentRow() < 0) {
        return;
    }

    QTableWidgetItem* nameItem = directoryTable_->item(directoryTable_->currentRow(), 0);
    if (nameItem == nullptr) {
        return;
    }

    const bool isParentRow = nameItem->data(Qt::UserRole + 2).toBool();
    if (isParentRow && latestResult_ && latestResult_->root && currentDirectoryNode_ != nullptr) {
        auto parentIterator = parentByNode_.find(currentDirectoryNode_);
        const core::ScanNode* parent = parentIterator != parentByNode_.end() ? parentIterator->second : FindParentNode(*latestResult_->root, *currentDirectoryNode_);
        if (parent != nullptr) {
            SelectNodeDetails(*parent);
        }
        return;
    }

    const auto address = nameItem->data(Qt::UserRole).toULongLong();
    auto* node = reinterpret_cast<const core::ScanNode*>(address);
    if (node == nullptr) {
        OpenSelectedPath();
        return;
    }

    if (node->kind == core::NodeKind::Directory) {
        SelectNodeDetails(*node);
    } else {
        RevealPathInExplorer(ToQString(node->path));
    }
}

const core::ScanNode* MainWindow::FindParentNode(const core::ScanNode& current, const core::ScanNode& target) const {
    for (const auto& child : current.children) {
        if (!child) {
            continue;
        }

        if (child.get() == &target) {
            return &current;
        }

        const core::ScanNode* found = FindParentNode(*child, target);
        if (found != nullptr) {
            return found;
        }
    }

    return nullptr;
}

void MainWindow::LoadLastScanCacheAsync() {
    if (latestResult_ != nullptr || scanning_.load()) {
        return;
    }

    SetInfoBar(QStringLiteral("正在加载上次结果"), 0, 0, QStringLiteral("后台读取缓存，不会自动重新扫描"));
    std::thread([this]() {
        bool cachedUsedNtfsMft = false;
        auto cachedResult = LoadScanCache(cachedUsedNtfsMft);
        if (!cachedResult || !cachedResult->root) {
            QMetaObject::invokeMethod(this, [this]() {
                SetInfoBar(QStringLiteral("就绪"), 0, 0, QStringLiteral("未找到可用的上次扫描结果"));
            }, Qt::QueuedConnection);
            return;
        }

        auto* rawResult = cachedResult.release();
        QMetaObject::invokeMethod(this, [this, rawResult, cachedUsedNtfsMft]() {
            latestResult_.reset(rawResult);
            lastScanUsedNtfsMft_ = cachedUsedNtfsMft;
            lastScanModeText_ = cachedUsedNtfsMft ? QStringLiteral("NTFS 极速（缓存）") : QStringLiteral("兼容（缓存）");
            lastScanModeDetail_ = QStringLiteral("已加载上次扫描结果，点击“开始扫描”可刷新");
            PopulateScanResult();
            SetInfoBar(
                QStringLiteral("已加载上次结果"),
                latestResult_ ? latestResult_->fileCount : 0,
                latestResult_ ? latestResult_->directoryCount : 0,
                lastScanModeDetail_);
            // E2:恢复缓存结果非新扫描,无耗时基线;给 30s 冷却避免恢复后外部搅动立刻触发重扫。
            watcherCooldownUntilMsec_ = QDateTime::currentMSecsSinceEpoch() + 30000;
            ReevaluateWatcher();  // E2:启动恢复缓存后,若已启用则按该根 arm watcher。
        }, Qt::QueuedConnection);
    }).detach();
}

void MainWindow::RebuildParentIndex() {
    parentByNode_.clear();
    if (latestResult_ == nullptr || latestResult_->root == nullptr) {
        return;
    }

    parentByNode_.reserve(static_cast<std::size_t>(std::min<std::uint64_t>(latestResult_->directoryCount + latestResult_->fileCount, 3000000)));
    IndexParentNodes(*latestResult_->root, nullptr);
}

void MainWindow::IndexParentNodes(const core::ScanNode& node, const core::ScanNode* parent) {
    parentByNode_[&node] = parent;
    for (const auto& child : node.children) {
        if (child) {
            IndexParentNodes(*child, &node);
        }
    }
}

void MainWindow::LoadUiSettings() {
    QSettings settings(QStringLiteral("SunnyFan"), QStringLiteral("DiskLens"));

    const QByteArray geometry = settings.value(QStringLiteral("ui/geometry")).toByteArray();
    if (!geometry.isEmpty()) {
        if (!restoreGeometry(geometry)) {
            resize(1280, 820);
            CenterWindowOnScreen(this);
        }
    } else {
        resize(1280, 820);
        CenterWindowOnScreen(this);
    }

    const QByteArray splitterState = settings.value(QStringLiteral("ui/workspaceSplitter/v2")).toByteArray();
    if (workspaceSplitter_ != nullptr && !splitterState.isEmpty()) {
        workspaceSplitter_->restoreState(splitterState);
    }
    currentTheme_ = settings.value(QStringLiteral("ui/theme"), QStringLiteral("light")).toString();
    ApplyStyle();
    UpdateModuleChrome();

    // 清理/去重默认选项持久化(默认与各复选框构造时一致:隐私/开发默认勾选,
    // 深度清理/永久删除默认不勾——故未设置过的用户行为零变化)。复选框在
    // CreateWorkspace(构造函数内,先于本函数)已创建,此处可安全恢复。
    if (cleanupPrivacyCheckBox_ != nullptr) {
        cleanupPrivacyCheckBox_->setChecked(settings.value(QStringLiteral("cleanup/privacy"), true).toBool());
    }
    if (cleanupDeveloperCheckBox_ != nullptr) {
        cleanupDeveloperCheckBox_->setChecked(settings.value(QStringLiteral("cleanup/developer"), true).toBool());
    }
    if (cleanupDeepCleanCheckBox_ != nullptr) {
        cleanupDeepCleanCheckBox_->setChecked(settings.value(QStringLiteral("cleanup/deepClean"), false).toBool());
    }
    if (duplicatePermanentCheckBox_ != nullptr) {
        duplicatePermanentCheckBox_->setChecked(settings.value(QStringLiteral("dedup/permanentDelete"), false).toBool());
    }

    // E2:实时文件夹监控开关,默认关闭(未设置过的用户行为零变化)。
    liveWatchEnabled_ = settings.value(QStringLiteral("watch/liveEnabled"), false).toBool();

    const QStringList recentPaths = settings.value(QStringLiteral("scan/recentPaths")).toStringList();
    if (driveCombo_ != nullptr) {
        for (const QString& path : recentPaths) {
            if (!path.isEmpty() && driveCombo_->findText(path) < 0) {
                driveCombo_->addItem(path);
            }
        }
        const QString lastPath = settings.value(QStringLiteral("scan/lastPath")).toString();
        if (!lastPath.isEmpty()) {
            driveCombo_->setCurrentText(lastPath);
        }
    }

    const QList<QPair<QString, QTableView*>> views = {
        {QStringLiteral("directory"), directoryView_},
        {QStringLiteral("largeFiles"), largeFilesView_},
        {QStringLiteral("staleFiles"), staleFilesView_},
        {QStringLiteral("search"), searchView_},
    };
    for (const auto& pair : views) {
        QTableView* view = pair.second;
        if (view == nullptr) {
            continue;
        }
        for (int column = 0; column < view->model()->columnCount(); ++column) {
            const int width = settings.value(QStringLiteral("columns/%1/%2").arg(pair.first).arg(column), -1).toInt();
            if (width > 24) {
                view->setColumnWidth(column, width);
            }
        }
    }
}

void MainWindow::SaveUiSettings() const {
    QSettings settings(QStringLiteral("SunnyFan"), QStringLiteral("DiskLens"));
    settings.setValue(QStringLiteral("ui/geometry"), saveGeometry());
    if (workspaceSplitter_ != nullptr) {
        settings.setValue(QStringLiteral("ui/workspaceSplitter/v2"), workspaceSplitter_->saveState());
    }
    settings.setValue(QStringLiteral("ui/theme"), currentTheme_);

    if (driveCombo_ != nullptr) {
        const QString currentPath = driveCombo_->currentText().trimmed();
        settings.setValue(QStringLiteral("scan/lastPath"), currentPath);

        QStringList recentPaths;
        if (!currentPath.isEmpty()) {
            recentPaths << currentPath;
        }
        for (int index = 0; index < driveCombo_->count() && recentPaths.size() < 12; ++index) {
            const QString path = driveCombo_->itemText(index).trimmed();
            if (!path.isEmpty() && !recentPaths.contains(path, Qt::CaseInsensitive)) {
                recentPaths << path;
            }
        }
        settings.setValue(QStringLiteral("scan/recentPaths"), recentPaths);
    }

    // 清理/去重默认选项(与 LoadUiSettings 对称)。
    settings.setValue(QStringLiteral("cleanup/privacy"), cleanupPrivacyCheckBox_ != nullptr && cleanupPrivacyCheckBox_->isChecked());
    settings.setValue(QStringLiteral("cleanup/developer"), cleanupDeveloperCheckBox_ != nullptr && cleanupDeveloperCheckBox_->isChecked());
    settings.setValue(QStringLiteral("cleanup/deepClean"), cleanupDeepCleanCheckBox_ != nullptr && cleanupDeepCleanCheckBox_->isChecked());
    settings.setValue(QStringLiteral("dedup/permanentDelete"), duplicatePermanentCheckBox_ != nullptr && duplicatePermanentCheckBox_->isChecked());

    // E2 实时文件夹监控开关(与 LoadUiSettings / 首选项对话框 accept 三处对称)。
    settings.setValue(QStringLiteral("watch/liveEnabled"), liveWatchEnabled_);

    const QList<QPair<QString, QTableView*>> views = {
        {QStringLiteral("directory"), directoryView_},
        {QStringLiteral("largeFiles"), largeFilesView_},
        {QStringLiteral("staleFiles"), staleFilesView_},
        {QStringLiteral("search"), searchView_},
    };
    for (const auto& pair : views) {
        QTableView* view = pair.second;
        if (view == nullptr) {
            continue;
        }
        for (int column = 0; column < view->model()->columnCount(); ++column) {
            settings.setValue(QStringLiteral("columns/%1/%2").arg(pair.first).arg(column), view->columnWidth(column));
        }
    }
}

void MainWindow::RunAfterClickFeedback(const QString& stateText, const QString& detailText, std::function<void()> action) {
    if (!stateText.isEmpty()) {
        // 仅在磁盘分析页用全局信息栏反馈;搜索/清理页有自己的状态标签(searchScopeLabel_、
        // cleanupStatusLabel_),写到这里会串到文件扫描的信息栏。
        QWidget* currentPage = tabs_ != nullptr ? tabs_->currentWidget() : nullptr;
        const bool onDiskAnalysisPage = currentPage == directoryView_
            || currentPage == largeFilesView_
            || currentPage == staleFilesView_
            || currentPage == typeStatsPage_
            || currentPage == duplicatePage_
            || currentPage == ageHistogramWidget_;
        if (onDiskAnalysisPage) {
            SetInfoBar(
                stateText,
                latestResult_ ? latestResult_->fileCount : 0,
                latestResult_ ? latestResult_->directoryCount : 0,
                detailText);
            FlushImmediateFeedback();
        }
    }

    QTimer::singleShot(16, this, [this, action = std::move(action)]() mutable {
        if (!action) {
            return;
        }
        action();
    });
}

void MainWindow::ShowShortcutHelp() {
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("快捷键与操作说明"));
    dialog.setWindowIcon(windowIcon());
    dialog.resize(760, 560);

    auto* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(22, 20, 22, 16);
    layout->setSpacing(12);

    auto* titleLabel = new QLabel(QStringLiteral("快捷键与操作说明"), &dialog);
    titleLabel->setObjectName(QStringLiteral("AboutTitle"));
    auto* descriptionLabel = new QLabel(
        QStringLiteral("常用操作都可以通过快捷键完成；右键文件、目录或搜索结果可查看详情、定位、复制路径和执行安全操作。"), &dialog);
    descriptionLabel->setWordWrap(true);

    auto* table = new QTableWidget(&dialog);
    table->setColumnCount(3);
    table->setHorizontalHeaderLabels({QStringLiteral("功能"), QStringLiteral("快捷键"), QStringLiteral("说明")});
    table->verticalHeader()->setVisible(false);
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setAlternatingRowColors(true);
    table->setShowGrid(false);

    const auto addHelpRow = [table](const QString& feature, const QString& shortcut, const QString& description) {
        const int row = table->rowCount();
        table->insertRow(row);
        auto* featureItem = new QTableWidgetItem(feature);
        auto* shortcutItem = new QTableWidgetItem(shortcut);
        auto* descriptionItem = new QTableWidgetItem(description);
        featureItem->setToolTip(feature);
        shortcutItem->setToolTip(shortcut);
        descriptionItem->setToolTip(description);
        table->setItem(row, 0, featureItem);
        table->setItem(row, 1, shortcutItem);
        table->setItem(row, 2, descriptionItem);
    };

    addHelpRow(QStringLiteral("开始或刷新扫描"), QStringLiteral("F5 / Ctrl+R"), QStringLiteral("按当前扫描位置重新分析磁盘。"));
    addHelpRow(QStringLiteral("停止扫描"), QStringLiteral("Esc"), QStringLiteral("扫描过程中立即请求停止，界面保持可操作。"));
    addHelpRow(QStringLiteral("选择扫描位置"), QStringLiteral("Ctrl+B"), QStringLiteral("打开目录选择窗口。"));
    addHelpRow(QStringLiteral("聚焦位置输入"), QStringLiteral("Ctrl+L"), QStringLiteral("快速切到顶部扫描位置。"));
    addHelpRow(QStringLiteral("快速搜索"), QStringLiteral("Ctrl+F"), QStringLiteral("切到全系统快速搜索并选中搜索框。"));
    addHelpRow(QStringLiteral("重建搜索索引"), QStringLiteral("Ctrl+Shift+F"), QStringLiteral("重新构建全系统固定磁盘索引。"));
    addHelpRow(QStringLiteral("打开所在位置"), QStringLiteral("Enter / Ctrl+O"), QStringLiteral("在资源管理器中定位当前选中项。"));
    addHelpRow(QStringLiteral("直接打开"), QStringLiteral("Ctrl+Enter"), QStringLiteral("直接打开当前文件或目录。"));
    addHelpRow(QStringLiteral("查看详情"), QStringLiteral("Ctrl+I"), QStringLiteral("显示软件内详情窗口，包含路径、大小、时间、权限与属性。"));
    addHelpRow(QStringLiteral("系统属性"), QStringLiteral("Alt+Enter"), QStringLiteral("打开 Windows 原生属性窗口。"));
    addHelpRow(QStringLiteral("复制完整路径"), QStringLiteral("Ctrl+C"), QStringLiteral("在表格或目录树中复制当前项目完整路径。"));
    addHelpRow(QStringLiteral("复制名称"), QStringLiteral("Ctrl+Shift+C"), QStringLiteral("复制当前文件或目录名称。"));
    addHelpRow(QStringLiteral("移入回收站"), QStringLiteral("Delete"), QStringLiteral("对选中项执行受保护的回收站删除。"));
    addHelpRow(QStringLiteral("返回上级目录"), QStringLiteral("Backspace / Alt+Left"), QStringLiteral("目录内容页返回当前目录的上级。"));
    addHelpRow(QStringLiteral("导出当前列表"), QStringLiteral("Ctrl+E"), QStringLiteral("导出当前可见列表为 CSV。"));
    addHelpRow(QStringLiteral("切换标签页"), QStringLiteral("Ctrl+1 至 Ctrl+9 / Ctrl+Tab"), QStringLiteral("快速切换目录内容、大文件、类型统计、疑似重复、长期未动、快速搜索、垃圾清理、磁盘健康、文件年龄。"));
    addHelpRow(QStringLiteral("查看说明"), QStringLiteral("F1"), QStringLiteral("打开本说明窗口。"));
    table->resizeRowsToContents();

    auto* buttons = new QDialogButtonBox(&dialog);
    QPushButton* okButton = buttons->addButton(QStringLiteral("知道了"), QDialogButtonBox::AcceptRole);
    connect(okButton, &QPushButton::clicked, &dialog, &QDialog::accept);

    layout->addWidget(titleLabel);
    layout->addWidget(descriptionLabel);
    layout->addWidget(table, 1);
    layout->addWidget(buttons);
    ApplyNativeWindowIcon(&dialog);
    QTimer::singleShot(0, &dialog, [&dialog]() {
        ApplyNativeWindowIcon(&dialog);
    });
    dialog.exec();
}

void MainWindow::InstallShortcuts() {
    const auto appShortcutAllowed = []() {
        QWidget* focusWidget = QApplication::focusWidget();
        return qobject_cast<QLineEdit*>(focusWidget) == nullptr;
    };

    auto* refreshShortcut = new QShortcut(QKeySequence(QStringLiteral("F5")), this);
    connect(refreshShortcut, &QShortcut::activated, this, &MainWindow::StartScan);

    auto* rescanShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+R")), this);
    connect(rescanShortcut, &QShortcut::activated, this, &MainWindow::StartScan);

    auto* stopShortcut = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    connect(stopShortcut, &QShortcut::activated, this, [this]() {
        if (scanning_.load()) {
            StopScan();
        }
    });

    auto* copyShortcut = new QShortcut(QKeySequence::Copy, this);
    connect(copyShortcut, &QShortcut::activated, this, [this, appShortcutAllowed]() {
        if (!appShortcutAllowed()) {
            return;
        }
        CopyPathToClipboard(CurrentSelectedPath());
    });

    auto* copyNameShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+C")), this);
    connect(copyNameShortcut, &QShortcut::activated, this, [this, appShortcutAllowed]() {
        if (!appShortcutAllowed()) {
            return;
        }
        CopyNameToClipboard(CurrentSelectedPath());
    });

    auto* locationShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+L")), this);
    connect(locationShortcut, &QShortcut::activated, this, [this]() {
        if (driveCombo_ != nullptr) {
            driveCombo_->setFocus();
            driveCombo_->showPopup();
        }
    });

    auto* browseShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+B")), this);
    connect(browseShortcut, &QShortcut::activated, this, &MainWindow::BrowseScanLocation);

    auto* searchShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+F")), this);
    connect(searchShortcut, &QShortcut::activated, this, [this]() {
        if (tabs_ != nullptr && searchView_ != nullptr) {
            tabs_->setCurrentWidget(searchView_->parentWidget());
        }
        if (searchEdit_ != nullptr) {
            searchEdit_->setFocus();
            searchEdit_->selectAll();
        }
    });

    auto* openShortcut = new QShortcut(QKeySequence(Qt::Key_Return), this);
    connect(openShortcut, &QShortcut::activated, this, &MainWindow::OpenSelectedPath);

    auto* revealShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+O")), this);
    connect(revealShortcut, &QShortcut::activated, this, &MainWindow::OpenSelectedPath);

    auto* directOpenShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+Return")), this);
    connect(directOpenShortcut, &QShortcut::activated, this, [this]() {
        OpenPathDirectly(CurrentSelectedPath());
    });

    auto* detailShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+I")), this);
    connect(detailShortcut, &QShortcut::activated, this, [this]() {
        bool hasBytes = false;
        const std::uint64_t bytes = CurrentSelectedScannedBytes(hasBytes);
        ShowPathDetails(CurrentSelectedPath(), bytes, hasBytes);
    });

    auto* propertiesShortcut = new QShortcut(QKeySequence(QStringLiteral("Alt+Return")), this);
    connect(propertiesShortcut, &QShortcut::activated, this, [this]() {
        ShowPathProperties(CurrentSelectedPath());
    });

    auto* deleteShortcut = new QShortcut(QKeySequence(Qt::Key_Delete), this);
    connect(deleteShortcut, &QShortcut::activated, this, [this, appShortcutAllowed]() {
        if (!appShortcutAllowed()) {
            return;
        }
        MovePathToRecycleBin(CurrentSelectedPath());
    });

    auto* backShortcut = new QShortcut(QKeySequence(Qt::Key_Backspace), this);
    connect(backShortcut, &QShortcut::activated, this, [this, appShortcutAllowed]() {
        if (appShortcutAllowed()) {
            GoToParentDirectory();
        }
    });

    auto* altLeftShortcut = new QShortcut(QKeySequence(QStringLiteral("Alt+Left")), this);
    connect(altLeftShortcut, &QShortcut::activated, this, &MainWindow::GoToParentDirectory);

    auto* exportShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+E")), this);
    connect(exportShortcut, &QShortcut::activated, this, &MainWindow::ExportCurrentTable);

    auto* rebuildIndexShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+F")), this);
    connect(rebuildIndexShortcut, &QShortcut::activated, this, &MainWindow::StartSystemSearchIndex);

    auto* nextTabShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+Tab")), this);
    connect(nextTabShortcut, &QShortcut::activated, this, [this]() {
        if (tabs_ == nullptr || tabs_->count() == 0) {
            return;
        }
        tabs_->setCurrentIndex((tabs_->currentIndex() + 1) % tabs_->count());
    });

    auto* previousTabShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+Tab")), this);
    connect(previousTabShortcut, &QShortcut::activated, this, [this]() {
        if (tabs_ == nullptr || tabs_->count() == 0) {
            return;
        }
        tabs_->setCurrentIndex((tabs_->currentIndex() + tabs_->count() - 1) % tabs_->count());
    });

    for (int index = 0; index < 6; ++index) {
        auto* tabShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+%1").arg(index + 1)), this);
        connect(tabShortcut, &QShortcut::activated, this, [this, index]() {
            if (tabs_ != nullptr && index < tabs_->count()) {
                tabs_->setCurrentIndex(index);
            }
        });
    }

    // 文件年龄直方图页(追加在 index 8):单独绑定 Ctrl+9,用 setCurrentWidget(指针)而非
    // setCurrentIndex(下标),避免与既有 0..7 索引/隐藏 tab 产生耦合,回归面最小。
    auto* ageTabShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+9")), this);
    connect(ageTabShortcut, &QShortcut::activated, this, [this]() {
        if (tabs_ != nullptr && ageHistogramWidget_ != nullptr) {
            tabs_->setCurrentWidget(ageHistogramWidget_);
        }
    });

    auto* helpShortcut = new QShortcut(QKeySequence(QStringLiteral("F1")), this);
    connect(helpShortcut, &QShortcut::activated, this, &MainWindow::ShowShortcutHelp);
}

QWidget* MainWindow::CreateApplicationMenu() {
    auto* frame = new QFrame(this);
    frame->setObjectName(QStringLiteral("MenuStrip"));
    frame->setFixedHeight(31);
    auto* outerLayout = new QVBoxLayout(frame);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->setSpacing(0);

    // 系统窗口标题栏（Windows 标题栏）与下方菜单条之间的分割线：位于菜单条最顶部。
    // setMenuWidget 的 QFrame 自身边框在部分皮肤下不绘制，故用独立分割条控件确保可见。
    auto* topDivider = new QFrame(frame);
    topDivider->setObjectName(QStringLiteral("TopDivider"));
    topDivider->setFixedHeight(1);
    outerLayout->addWidget(topDivider);

    auto* menuRow = new QWidget(frame);
    auto* layout = new QHBoxLayout(menuRow);
    layout->setContentsMargins(12, 0, 12, 0);
    layout->setSpacing(4);
    outerLayout->addWidget(menuRow);

    const auto createMenuButton = [frame, layout](const QString& text, QMenu* menu) {
        auto* button = new QPushButton(text, frame);
        button->setObjectName(QStringLiteral("MenuStripButton"));
        button->setMenu(menu);
        button->setCursor(Qt::PointingHandCursor);
        layout->addWidget(button);
        return button;
    };

    QMenu* fileMenu = new QMenu(QStringLiteral("文件"), frame);
    QAction* scanAction = fileMenu->addAction(QStringLiteral("开始扫描"), this, &MainWindow::StartScan);
    scanAction->setShortcut(QKeySequence(QStringLiteral("F5")));
    QAction* stopAction = fileMenu->addAction(QStringLiteral("停止扫描"), this, &MainWindow::StopScan);
    stopAction->setShortcut(QKeySequence(Qt::Key_Escape));
    fileMenu->addSeparator();
    QAction* browseAction = fileMenu->addAction(QStringLiteral("选择扫描位置..."), this, &MainWindow::BrowseScanLocation);
    browseAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+B")));
    fileMenu->addSeparator();
    QAction* exportAction = fileMenu->addAction(QStringLiteral("导出当前列表"), this, &MainWindow::ExportCurrentTable);
    exportAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+E")));
    fileMenu->addSeparator();
    QAction* prefsAction = fileMenu->addAction(QStringLiteral("首选项..."), this, &MainWindow::ShowPreferencesDialog);
    prefsAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+,")));
    fileMenu->addSeparator();
    fileMenu->addAction(QStringLiteral("退出"), this, &QWidget::close);
    createMenuButton(QStringLiteral("文件"), fileMenu);

    QMenu* actionMenu = new QMenu(QStringLiteral("操作"), frame);
    QAction* revealAction = actionMenu->addAction(QStringLiteral("打开所在位置"), this, &MainWindow::OpenSelectedPath);
    revealAction->setShortcut(QKeySequence(Qt::Key_Return));
    QAction* directOpenAction = actionMenu->addAction(QStringLiteral("直接打开"), this, [this]() {
        OpenPathDirectly(CurrentSelectedPath());
    });
    directOpenAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Return")));
    actionMenu->addSeparator();
    QAction* copyPathAction = actionMenu->addAction(QStringLiteral("复制完整路径"), this, [this]() {
        CopyPathToClipboard(CurrentSelectedPath());
    });
    copyPathAction->setShortcut(QKeySequence::Copy);
    QAction* copyNameAction = actionMenu->addAction(QStringLiteral("复制名称"), this, [this]() {
        CopyNameToClipboard(CurrentSelectedPath());
    });
    copyNameAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+C")));
    actionMenu->addSeparator();
    QAction* detailsAction = actionMenu->addAction(QStringLiteral("查看详情"), this, [this]() {
        bool hasBytes = false;
        const std::uint64_t bytes = CurrentSelectedScannedBytes(hasBytes);
        ShowPathDetails(CurrentSelectedPath(), bytes, hasBytes);
    });
    detailsAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+I")));
    QAction* propertiesAction = actionMenu->addAction(QStringLiteral("系统属性"), this, [this]() {
        ShowPathProperties(CurrentSelectedPath());
    });
    propertiesAction->setShortcut(QKeySequence(QStringLiteral("Alt+Return")));
    actionMenu->addSeparator();
    QAction* recycleAction = actionMenu->addAction(QStringLiteral("移入回收站"), this, [this]() {
        MovePathToRecycleBin(CurrentSelectedPath());
    });
    recycleAction->setShortcut(QKeySequence(Qt::Key_Delete));
    createMenuButton(QStringLiteral("操作"), actionMenu);

    QMenu* viewMenu = new QMenu(QStringLiteral("视图"), frame);
    QAction* directoryTabAction = viewMenu->addAction(QStringLiteral("目录内容"), this, [this]() {
        if (tabs_ != nullptr && directoryView_ != nullptr) {
            tabs_->setCurrentWidget(directoryView_);
        }
    });
    directoryTabAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+1")));
    QAction* largeFilesTabAction = viewMenu->addAction(QStringLiteral("大文件"), this, [this]() {
        if (tabs_ != nullptr && largeFilesView_ != nullptr) {
            tabs_->setCurrentWidget(largeFilesView_);
        }
    });
    largeFilesTabAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+2")));
    QAction* typeStatsTabAction = viewMenu->addAction(QStringLiteral("类型统计"), this, [this]() {
        if (tabs_ != nullptr && typeStatsPage_ != nullptr) {
            tabs_->setCurrentWidget(typeStatsPage_);
        }
    });
    typeStatsTabAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+3")));
    QAction* duplicateTabAction = viewMenu->addAction(QStringLiteral("疑似重复"), this, [this]() {
        if (tabs_ != nullptr && duplicatePage_ != nullptr) {
            tabs_->setCurrentWidget(duplicatePage_);
        }
    });
    duplicateTabAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+4")));
    QAction* staleFilesTabAction = viewMenu->addAction(QStringLiteral("长期未动"), this, [this]() {
        if (tabs_ != nullptr && staleFilesView_ != nullptr) {
            tabs_->setCurrentWidget(staleFilesView_);
        }
    });
    staleFilesTabAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+5")));
    viewMenu->addSeparator();
    viewMenu->addAction(QStringLiteral("快速搜索"), this, [this]() {
        if (tabs_ != nullptr && searchView_ != nullptr) {
            tabs_->setCurrentWidget(searchView_->parentWidget());
        }
        if (searchEdit_ != nullptr) {
            searchEdit_->setFocus();
            searchEdit_->selectAll();
        }
    })->setShortcut(QKeySequence(QStringLiteral("Ctrl+F")));
    QAction* searchTabAction = viewMenu->addAction(QStringLiteral("快速搜索页"), this, [this]() {
        if (tabs_ != nullptr && searchView_ != nullptr) {
            tabs_->setCurrentWidget(searchView_->parentWidget());
        }
    });
    searchTabAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+6")));
    QAction* cleanupTabAction = viewMenu->addAction(QStringLiteral("垃圾清理"), this, [this]() {
        if (tabs_ != nullptr && cleanupTree_ != nullptr) {
            tabs_->setCurrentWidget(cleanupTree_->parentWidget());
        }
    });
    cleanupTabAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+7")));
    QAction* healthTabAction = viewMenu->addAction(QStringLiteral("磁盘健康"), this, [this]() {
        if (tabs_ != nullptr && healthPage_ != nullptr) {
            tabs_->setCurrentWidget(healthPage_);
        }
    });
    healthTabAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+8")));
    createMenuButton(QStringLiteral("视图"), viewMenu);

    QMenu* toolsMenu = new QMenu(QStringLiteral("工具"), frame);
    QAction* rebuildIndexAction = toolsMenu->addAction(QStringLiteral("重建全系统搜索索引"), this, &MainWindow::StartSystemSearchIndex);
    rebuildIndexAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+F")));
    QAction* rescanAction = toolsMenu->addAction(QStringLiteral("重新扫描当前位置"), this, [this]() {
        if (driveCombo_ != nullptr) {
            RescanPath(driveCombo_->currentText().trimmed());
        }
    });
    rescanAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+R")));
    QAction* parentAction = toolsMenu->addAction(QStringLiteral("返回上级目录"), this, &MainWindow::GoToParentDirectory);
    parentAction->setShortcut(QKeySequence(Qt::Key_Backspace));
    toolsMenu->addSeparator();
    QMenu* themeMenu = toolsMenu->addMenu(QStringLiteral("主题皮肤"));
    themeMenu->addAction(QStringLiteral("浅色专业"), this, [this]() {
        SetTheme(QStringLiteral("light"));
    });
    themeMenu->addAction(QStringLiteral("暗色大师"), this, [this]() {
        SetTheme(QStringLiteral("dark"));
    });
    themeMenu->addAction(QStringLiteral("蓝色清爽"), this, [this]() {
        SetTheme(QStringLiteral("blue"));
    });
    createMenuButton(QStringLiteral("工具"), toolsMenu);

    QMenu* helpMenu = new QMenu(QStringLiteral("帮助"), frame);
    QAction* shortcutHelpAction = helpMenu->addAction(QStringLiteral("快捷键与操作说明"), this, &MainWindow::ShowShortcutHelp);
    shortcutHelpAction->setShortcut(QKeySequence(QStringLiteral("F1")));
    helpMenu->addSeparator();
    helpMenu->addAction(QStringLiteral("关于"), this, [this]() {
        QDialog dialog(this);
        dialog.setWindowTitle(QStringLiteral("关于磁盘洞察"));
        dialog.setWindowIcon(windowIcon());
        dialog.setMinimumWidth(440);

        auto* layout = new QVBoxLayout(&dialog);
        layout->setContentsMargins(28, 24, 28, 20);
        layout->setSpacing(12);

        // 头部：应用图标 + 标题与版本。
        auto* headerLayout = new QHBoxLayout();
        headerLayout->setSpacing(16);
        headerLayout->setAlignment(Qt::AlignLeft);
        auto* iconLabel = new QLabel(&dialog);
        iconLabel->setPixmap(windowIcon().pixmap(48, 48));
        auto* titleBlock = new QVBoxLayout();
        titleBlock->setSpacing(2);
        auto* titleLabel = new QLabel(QStringLiteral("磁盘洞察 DiskLens"), &dialog);
        titleLabel->setObjectName(QStringLiteral("AboutTitle"));
        auto* versionLabel = new QLabel(QStringLiteral("版本 ") + QStringLiteral(DISKLENS_VERSION_STRING), &dialog);
        versionLabel->setObjectName(QStringLiteral("AboutVersion"));
        titleBlock->addWidget(titleLabel);
        titleBlock->addWidget(versionLabel);
        headerLayout->addWidget(iconLabel, 0, Qt::AlignTop);
        headerLayout->addLayout(titleBlock);
        layout->addLayout(headerLayout);

        auto* separator = new QFrame(&dialog);
        separator->setFrameShape(QFrame::HLine);
        separator->setObjectName(QStringLiteral("AboutSeparator"));
        layout->addWidget(separator);

        auto* descriptionLabel = new QLabel(
            QStringLiteral("面向大容量磁盘的空间分析、全系统快速搜索与安全清理工具。\n本地执行、便携发布、可解释的清理建议。"), &dialog);
        descriptionLabel->setWordWrap(true);
        auto* detailLabel = new QLabel(
            QStringLiteral("作者：捌両&nbsp;&nbsp;|&nbsp;&nbsp;官网：<a href=\"https://sunnyfan.cn\">https://sunnyfan.cn</a>"), &dialog);
        detailLabel->setTextFormat(Qt::RichText);
        detailLabel->setTextInteractionFlags(Qt::TextBrowserInteraction);
        detailLabel->setOpenExternalLinks(true);

        auto* buttons = new QDialogButtonBox(&dialog);
        QPushButton* visitButton = buttons->addButton(QStringLiteral("访问官网"), QDialogButtonBox::ActionRole);
        QPushButton* okButton = buttons->addButton(QStringLiteral("确定"), QDialogButtonBox::AcceptRole);
        connect(visitButton, &QPushButton::clicked, this, []() {
            QDesktopServices::openUrl(QUrl(QStringLiteral("https://sunnyfan.cn")));
        });
        connect(okButton, &QPushButton::clicked, &dialog, &QDialog::accept);

        layout->addWidget(descriptionLabel);
        layout->addWidget(detailLabel);
        layout->addStretch(1);
        layout->addWidget(buttons);
        ApplyNativeWindowIcon(&dialog);
        QTimer::singleShot(0, &dialog, [&dialog]() {
            ApplyNativeWindowIcon(&dialog);
        });
        dialog.exec();
    });
    createMenuButton(QStringLiteral("帮助"), helpMenu);
    layout->addStretch(1);
    return frame;
}

QString MainWindow::CurrentSelectedPath() const {
    if (tabs_ != nullptr && tabs_->currentWidget() == directoryView_) {
        const ResultRow* row = directoryModel_ != nullptr ? directoryModel_->RowAt(directoryView_->currentIndex().row()) : nullptr;
        return row != nullptr ? row->fullPath : QString();
    }
    if (tabs_ != nullptr && tabs_->currentWidget() == largeFilesView_) {
        const ResultRow* row = largeFilesModel_ != nullptr ? largeFilesModel_->RowAt(largeFilesView_->currentIndex().row()) : nullptr;
        return row != nullptr ? row->fullPath : QString();
    }
    if (tabs_ != nullptr && tabs_->currentWidget() != nullptr && tabs_->currentWidget()->isAncestorOf(searchView_)) {
        const ResultRow* row = searchModel_ != nullptr ? searchModel_->RowAt(searchView_->currentIndex().row()) : nullptr;
        return row != nullptr ? row->fullPath : QString();
    }
    if (QTableWidget* table = CurrentTable()) {
        return SelectedTablePath(table);
    }
    if (const core::ScanNode* node = SelectedTreeNode()) {
        return ToQString(node->path);
    }
    return QString();
}

std::uint64_t MainWindow::CurrentSelectedScannedBytes(bool& hasBytes) const {
    hasBytes = false;

    const ResultRow* row = nullptr;
    if (tabs_ != nullptr && tabs_->currentWidget() == directoryView_) {
        row = directoryModel_ != nullptr ? directoryModel_->RowAt(directoryView_->currentIndex().row()) : nullptr;
    } else if (tabs_ != nullptr && tabs_->currentWidget() == largeFilesView_) {
        row = largeFilesModel_ != nullptr ? largeFilesModel_->RowAt(largeFilesView_->currentIndex().row()) : nullptr;
    } else if (tabs_ != nullptr && tabs_->currentWidget() != nullptr && tabs_->currentWidget()->isAncestorOf(searchView_)) {
        row = searchModel_ != nullptr ? searchModel_->RowAt(searchView_->currentIndex().row()) : nullptr;
    }
    if (row != nullptr) {
        hasBytes = true;
        return row->bytes;
    }

    if (QTableWidget* table = CurrentTable()) {
        QTableWidgetItem* item = table->currentRow() >= 0 ? table->item(table->currentRow(), 0) : nullptr;
        if (item != nullptr) {
            const auto address = item->data(Qt::UserRole).toULongLong();
            const auto* node = reinterpret_cast<const core::ScanNode*>(address);
            if (node != nullptr) {
                hasBytes = true;
                return node->totalBytes;
            }
        }
    }

    if (const core::ScanNode* node = SelectedTreeNode()) {
        hasBytes = true;
        return node->totalBytes;
    }

    return 0;
}

QWidget* MainWindow::CreateCommandBar() {
    auto* frame = new QFrame(this);
    frame->setObjectName(QStringLiteral("CommandBar"));
    commandBar_ = frame;

    auto* layout = new QHBoxLayout(frame);
    layout->setContentsMargins(14, 11, 14, 11);
    layout->setSpacing(10);

    driveCombo_ = new QComboBox(frame);
    driveCombo_->setEditable(false);
    driveCombo_->addItems(EnumerateFixedDriveRoots());
    driveCombo_->setMinimumWidth(230);
    driveCombo_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    driveCombo_->setToolTip(QStringLiteral("选择要扫描的磁盘或最近使用的位置"));

    browseButton_ = new QPushButton(QStringLiteral("浏览..."), frame);
    browseButton_->setToolTip(QStringLiteral("选择一个文件夹作为扫描位置"));

    scanButton_ = new QPushButton(QStringLiteral("开始扫描"), frame);
    scanButton_->setObjectName(QStringLiteral("PrimaryButton"));
    stopButton_ = new QPushButton(QStringLiteral("停止"), frame);
    stopButton_->setEnabled(false);
    boostButton_ = new QPushButton(QStringLiteral("极速模式"), frame);

    auto* modeLabel = new QLabel(QStringLiteral("兼容扫描 · 极速模式优先读取 NTFS MFT"), frame);
    modeLabel->setObjectName(QStringLiteral("ModeBadge"));
    modeLabel->setMinimumWidth(0);
    modeLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);

    layout->addWidget(driveCombo_, 1);
    layout->addWidget(browseButton_);
    layout->addWidget(scanButton_);
    layout->addWidget(stopButton_);
    layout->addWidget(boostButton_);
    layout->addSpacing(4);
    layout->addWidget(modeLabel, 2);

    return frame;
}

/**
 * @brief 创建应用最左侧的主功能导航栏。
 * @return 主功能导航栏控件。
 */
QWidget* MainWindow::CreateModuleSidebar() {
    auto* frame = new QFrame(this);
    frame->setObjectName(QStringLiteral("ModuleNav"));
    frame->setFixedWidth(160);

    auto* layout = new QVBoxLayout(frame);
    layout->setContentsMargins(12, 16, 12, 12);
    layout->setSpacing(8);

    const auto createModuleButton = [frame](const QString& text, const QString& hint) {
        auto* button = new QPushButton(text, frame);
        button->setObjectName(QStringLiteral("ModuleNavButton"));
        button->setCheckable(true);
        button->setToolTip(hint);
        button->setIconSize(QSize(22, 22));
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        return button;
    };

    diskModuleButton_ = createModuleButton(QStringLiteral("磁盘分析"), QStringLiteral("查看磁盘目录、大文件、类型统计和空间占用"));
    searchModuleButton_ = createModuleButton(QStringLiteral("文件搜索"), QStringLiteral("全系统快速搜索文件名、扩展名和路径片段"));
    cleanupModuleButton_ = createModuleButton(QStringLiteral("垃圾清理"), QStringLiteral("扫描可清理项目并按安全等级处理"));
    healthModuleButton_ = createModuleButton(QStringLiteral("磁盘健康"), QStringLiteral("读取物理盘 SMART / NVMe 健康日志,查看温度、通电时长和健康评估"));

    layout->addWidget(diskModuleButton_);
    layout->addWidget(searchModuleButton_);
    layout->addWidget(cleanupModuleButton_);
    layout->addWidget(healthModuleButton_);
    layout->addStretch(1);

    // 页脚分割线与品牌版本信息。
    auto* footerDivider = new QFrame(frame);
    footerDivider->setObjectName(QStringLiteral("NavDivider"));
    footerDivider->setFixedHeight(1);
    layout->addWidget(footerDivider);

    auto* footerLabel = new QLabel(QStringLiteral("v") + QStringLiteral(DISKLENS_VERSION_STRING) + QStringLiteral(" · SunnyFan"), frame);
    footerLabel->setObjectName(QStringLiteral("ModuleNavFooter"));
    footerLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    layout->addWidget(footerLabel);

    connect(diskModuleButton_, &QPushButton::clicked, this, [this]() {
        if (tabs_ != nullptr && directoryView_ != nullptr) {
            tabs_->setCurrentWidget(directoryView_);
        }
        UpdateModuleChrome();
    });
    connect(searchModuleButton_, &QPushButton::clicked, this, [this]() {
        if (tabs_ != nullptr && searchView_ != nullptr) {
            tabs_->setCurrentWidget(searchView_->parentWidget());
        }
        if (searchEdit_ != nullptr) {
            searchEdit_->setFocus();
            searchEdit_->selectAll();
        }
        UpdateModuleChrome();
    });
    connect(cleanupModuleButton_, &QPushButton::clicked, this, [this]() {
        if (tabs_ != nullptr && cleanupTree_ != nullptr) {
            tabs_->setCurrentWidget(cleanupTree_->parentWidget());
        }
        UpdateModuleChrome();
    });
    connect(healthModuleButton_, &QPushButton::clicked, this, [this]() {
        if (tabs_ != nullptr && healthPage_ != nullptr) {
            tabs_->setCurrentWidget(healthPage_);
        }
        if (!healthQuerying_ && healthInfos_.empty()) {
            RefreshDiskHealth();
        }
        UpdateModuleChrome();
    });

    return frame;
}

QWidget* MainWindow::CreateWorkspace() {
    auto* outerSplitter = new QSplitter(this);
    outerSplitter->setObjectName(QStringLiteral("WorkspaceSplitter"));
    workspaceSplitter_ = outerSplitter;

    directoryTree_ = new QTreeWidget(outerSplitter);
    directoryTree_->setObjectName(QStringLiteral("DirectoryTree"));
    directoryTree_->setHeaderLabels({QStringLiteral("目录"), QStringLiteral("大小")});
    directoryTree_->setHeader(new ModernHeaderView(Qt::Horizontal, directoryTree_));
    directoryTree_->setMinimumWidth(245);
    directoryTree_->setIndentation(8);
    directoryTree_->setUniformRowHeights(true);
    directoryTree_->setAnimated(true);
    directoryTree_->setIconSize(QSize(16, 16));
    directoryTree_->header()->setStretchLastSection(false);
    directoryTree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    directoryTree_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);

    auto* rightPanel = new QWidget(outerSplitter);
    auto* rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(8);

    auto* metrics = new QWidget(rightPanel);
    metricsPanel_ = metrics;
    auto* metricsLayout = new QGridLayout(metrics);
    metricsLayout->setContentsMargins(0, 0, 0, 0);
    metricsLayout->setHorizontalSpacing(8);
    metricsLayout->addWidget(CreateMetric(QStringLiteral("总占用"), QStringLiteral("--"), &totalMetricLabel_), 0, 0);
    metricsLayout->addWidget(CreateMetric(QStringLiteral("文件"), QStringLiteral("0"), &fileMetricLabel_), 0, 1);
    metricsLayout->addWidget(CreateMetric(QStringLiteral("目录"), QStringLiteral("0"), &directoryMetricLabel_), 0, 2);
    metricsLayout->addWidget(CreateMetric(QStringLiteral("扫描模式"), QStringLiteral("兼容"), &modeMetricLabel_), 0, 3);
    // 总占用是核心指标，用强调色突出焦点层次。
    if (totalMetricLabel_ != nullptr) {
        totalMetricLabel_->setObjectName(QStringLiteral("MetricValueAccent"));
    }
    metricsLayout->setColumnStretch(0, 1);
    metricsLayout->setColumnStretch(1, 1);
    metricsLayout->setColumnStretch(2, 1);
    metricsLayout->setColumnStretch(3, 1);

    // 磁盘增长告警横幅:置于度量面板第二行(跨 4 列),默认隐藏;显著增长时由 EvaluateGrowthAlert 显示。
    growthAlertFrame_ = new QFrame(metrics);
    growthAlertFrame_->setObjectName(QStringLiteral("GrowthAlert"));
    auto* growthLayout = new QHBoxLayout(growthAlertFrame_);
    growthLayout->setContentsMargins(12, 8, 8, 8);
    growthLayout->setSpacing(10);
    auto* growthTitle = new QLabel(QStringLiteral("⚠  增长告警"), growthAlertFrame_);
    growthTitle->setObjectName(QStringLiteral("GrowthAlertTitle"));
    growthAlertBodyLabel_ = new QLabel(growthAlertFrame_);
    growthAlertBodyLabel_->setObjectName(QStringLiteral("GrowthAlertBody"));
    growthAlertBodyLabel_->setTextFormat(Qt::PlainText);
    growthAlertBodyLabel_->setWordWrap(true);
    auto* growthClose = new QPushButton(QStringLiteral("×"), growthAlertFrame_);
    growthClose->setObjectName(QStringLiteral("GrowthAlertClose"));
    growthClose->setCursor(Qt::PointingHandCursor);
    growthClose->setToolTip(QStringLiteral("关闭告警(下次扫描会重新评估)"));
    growthLayout->addWidget(growthTitle);
    growthLayout->addWidget(growthAlertBodyLabel_, 1);
    growthLayout->addWidget(growthClose);
    metricsLayout->addWidget(growthAlertFrame_, 1, 0, 1, 4);
    growthAlertFrame_->setVisible(false);
    connect(growthClose, &QPushButton::clicked, this, [this]() {
        if (growthAlertFrame_ != nullptr) {
            growthAlertFrame_->setVisible(false);
        }
    });

    tabs_ = new QTabWidget(rightPanel);
    tabs_->setObjectName(QStringLiteral("ModuleTabs"));
    tabs_->setTabPosition(QTabWidget::North);
    tabs_->setDocumentMode(true);
    tabs_->tabBar()->setDrawBase(false);
    directoryModel_ = new ResultTableModel(this);
    directoryView_ = new QTableView(rightPanel);
    directoryView_->setModel(directoryModel_);
    directoryView_->setHorizontalHeader(new ModernHeaderView(Qt::Horizontal, directoryView_));
    directoryView_->setItemDelegateForColumn(3, new PathElideDelegate(directoryView_));
    directoryView_->setSelectionBehavior(QAbstractItemView::SelectRows);
    directoryView_->setSelectionMode(QAbstractItemView::SingleSelection);
    directoryView_->setAlternatingRowColors(true);
    directoryView_->setShowGrid(false);
    directoryView_->setIconSize(QSize(16, 16));
    directoryView_->setTextElideMode(Qt::ElideMiddle);
    directoryView_->verticalHeader()->setVisible(false);
    directoryView_->verticalHeader()->setDefaultSectionSize(28);
    directoryView_->horizontalHeader()->setStretchLastSection(false);
    directoryView_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    directoryView_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    directoryView_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    directoryView_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    directoryView_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    directoryView_->setColumnWidth(0, 230);
    directoryView_->setColumnWidth(1, 110);
    directoryView_->setColumnWidth(2, 90);
    directoryView_->setColumnWidth(3, 520);
    directoryView_->setSortingEnabled(true);
    directoryView_->sortByColumn(1, Qt::DescendingOrder);
    directoryTable_ = CreateResultTable();
    directoryTable_->hide();
    largeFilesModel_ = new ResultTableModel(this);
    largeFilesView_ = new QTableView(rightPanel);
    largeFilesView_->setModel(largeFilesModel_);
    largeFilesView_->setHorizontalHeader(new ModernHeaderView(Qt::Horizontal, largeFilesView_));
    largeFilesView_->setItemDelegateForColumn(3, new PathElideDelegate(largeFilesView_));
    largeFilesView_->setSelectionBehavior(QAbstractItemView::SelectRows);
    largeFilesView_->setSelectionMode(QAbstractItemView::SingleSelection);
    largeFilesView_->setAlternatingRowColors(true);
    largeFilesView_->setShowGrid(false);
    largeFilesView_->setIconSize(QSize(16, 16));
    largeFilesView_->setTextElideMode(Qt::ElideMiddle);
    largeFilesView_->verticalHeader()->setVisible(false);
    largeFilesView_->verticalHeader()->setDefaultSectionSize(28);
    largeFilesView_->horizontalHeader()->setStretchLastSection(false);
    largeFilesView_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    largeFilesView_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    largeFilesView_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    largeFilesView_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    largeFilesView_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    largeFilesView_->setColumnWidth(0, 230);
    largeFilesView_->setColumnWidth(1, 110);
    largeFilesView_->setColumnWidth(2, 90);
    largeFilesView_->setColumnWidth(3, 520);
    largeFilesView_->setSortingEnabled(true);
    largeFilesView_->sortByColumn(1, Qt::DescendingOrder);
    largeFilesTable_ = CreateResultTable();
    largeFilesTable_->hide();
    typeStatsTable_ = CreateResultTable();
    typeStatsTable_->setHorizontalHeaderLabels({
        QStringLiteral("扩展名"),
        QStringLiteral("总大小"),
        QStringLiteral("文件数"),
        QStringLiteral("说明")
    });
    staleFilesModel_ = new ResultTableModel(this);
    staleFilesView_ = new QTableView(rightPanel);
    staleFilesView_->setModel(staleFilesModel_);
    staleFilesView_->setHorizontalHeader(new ModernHeaderView(Qt::Horizontal, staleFilesView_));
    staleFilesView_->setItemDelegateForColumn(3, new PathElideDelegate(staleFilesView_));
    staleFilesView_->setSelectionBehavior(QAbstractItemView::SelectRows);
    staleFilesView_->setSelectionMode(QAbstractItemView::SingleSelection);
    staleFilesView_->setAlternatingRowColors(true);
    staleFilesView_->setShowGrid(false);
    staleFilesView_->setIconSize(QSize(16, 16));
    staleFilesView_->setTextElideMode(Qt::ElideMiddle);
    staleFilesView_->verticalHeader()->setVisible(false);
    staleFilesView_->verticalHeader()->setDefaultSectionSize(28);
    staleFilesView_->horizontalHeader()->setStretchLastSection(false);
    staleFilesView_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    staleFilesView_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    staleFilesView_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    staleFilesView_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    staleFilesView_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    staleFilesView_->setColumnWidth(0, 230);
    staleFilesView_->setColumnWidth(1, 110);
    staleFilesView_->setColumnWidth(2, 90);
    staleFilesView_->setColumnWidth(3, 460);
    staleFilesView_->setSortingEnabled(true);
    staleFilesView_->sortByColumn(4, Qt::AscendingOrder);

    // 类型统计页:左侧扩展名表格 + 右侧分类占比环形图(可拖拽分隔)。
    // 用 QSplitter 包裹后,作为 QTabWidget 的页控件;下方所有“当前页==类型统计”的判断都改用 typeStatsPage_。
    auto* typeStatsSplitter = new QSplitter(Qt::Horizontal);
    typeStatsSplitter->setChildrenCollapsible(false);
    typeStatsSplitter->addWidget(typeStatsTable_);
    typeStatsDonut_ = new CategoryDonutWidget(typeStatsSplitter);
    typeStatsSplitter->addWidget(typeStatsDonut_);
    typeStatsSplitter->setStretchFactor(0, 3);
    typeStatsSplitter->setStretchFactor(1, 2);
    typeStatsSplitter->setSizes(QList<int>{480, 320});
    typeStatsPage_ = typeStatsSplitter;

    tabs_->addTab(directoryView_, QStringLiteral("目录内容"));
    tabs_->addTab(largeFilesView_, QStringLiteral("大文件"));
    tabs_->addTab(typeStatsPage_, QStringLiteral("类型统计"));
    tabs_->addTab(CreateDuplicateTab(), QStringLiteral("疑似重复"));
    tabs_->addTab(staleFilesView_, QStringLiteral("长期未动"));
    tabs_->addTab(CreateSearchTab(), QStringLiteral("快速搜索"));
    tabs_->addTab(CreateCleanupTab(), QStringLiteral("垃圾清理"));
    tabs_->addTab(CreateHealthTab(), QStringLiteral("磁盘健康"));
    // 文件年龄直方图页(追加在末尾=index 8,不改动既有 0..7 的索引与隐藏 tab,回归面最小)。
    // 控件本身就是页(沿用 largeFilesView_/staleFilesView_ 的"控件即页"做法),故页签判定直接用它。
    ageHistogramWidget_ = new FileAgeHistogramWidget(tabs_);
    tabs_->addTab(ageHistogramWidget_, QStringLiteral("文件年龄"));
    tabs_->tabBar()->setTabVisible(5, false);
    tabs_->tabBar()->setTabVisible(6, false);
    tabs_->tabBar()->setTabVisible(7, false);

    rightLayout->addWidget(metrics);
    rightLayout->addWidget(tabs_, 1);

    QWidget* treemapPanel = CreateTreemapPanel();
    treemapPanel_ = treemapPanel;

    outerSplitter->addWidget(directoryTree_);
    outerSplitter->addWidget(rightPanel);
    outerSplitter->addWidget(treemapPanel);
    outerSplitter->setStretchFactor(0, 0);
    outerSplitter->setStretchFactor(1, 1);
    outerSplitter->setStretchFactor(2, 0);
    outerSplitter->setSizes({280, 905, 170});

    return outerSplitter;
}

QWidget* MainWindow::CreateInfoBar() {
    infoBarStack_ = new QStackedWidget(this);
    infoBarStack_->setObjectName(QStringLiteral("InfoBarStack"));

    // 磁盘分析模块：扫描统计（沿用原有结构与全部 SetInfoBar 调用）。
    auto* frame = new QFrame(infoBarStack_);
    frame->setObjectName(QStringLiteral("InfoBar"));

    auto* layout = new QGridLayout(frame);
    layout->setContentsMargins(12, 8, 12, 8);
    layout->setHorizontalSpacing(18);

    busyIndicatorLabel_ = new QLabel(QStringLiteral("●"), frame);
    busyIndicatorLabel_->setObjectName(QStringLiteral("BusyIndicator"));
    busyIndicatorLabel_->setFixedWidth(14);
    busyIndicatorLabel_->setAlignment(Qt::AlignCenter);
    busyIndicatorLabel_->setVisible(false);
    stateLabel_ = new QLabel(QStringLiteral("就绪"), frame);
    filesLabel_ = new QLabel(QStringLiteral("文件 0"), frame);
    directoriesLabel_ = new QLabel(QStringLiteral("目录 0"), frame);
    speedLabel_ = new QLabel(QStringLiteral("速度 0/秒"), frame);
    elapsedLabel_ = new QLabel(QStringLiteral("耗时 0.0 秒"), frame);
    pathLabel_ = new QLabel(QStringLiteral("请选择扫描位置"), frame);
    pathLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    pathLabel_->setMinimumWidth(0);
    pathLabel_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);

    layout->addWidget(busyIndicatorLabel_, 0, 0);
    layout->addWidget(stateLabel_, 0, 1);
    layout->addWidget(filesLabel_, 0, 2);
    layout->addWidget(directoriesLabel_, 0, 3);
    layout->addWidget(speedLabel_, 0, 4);
    layout->addWidget(elapsedLabel_, 0, 5);
    layout->addWidget(pathLabel_, 0, 6);
    layout->setColumnStretch(6, 1);
    infoBarStack_->addWidget(frame);

    // 文件搜索模块：命中数 / 关键字 / 耗时。
    auto* searchFrame = new QFrame(infoBarStack_);
    searchFrame->setObjectName(QStringLiteral("InfoBar"));
    auto* searchLayout = new QHBoxLayout(searchFrame);
    searchLayout->setContentsMargins(12, 8, 12, 8);
    searchLayout->setSpacing(14);
    searchInfoLabel_ = new QLabel(QStringLiteral("文件搜索 · 输入关键字开始搜索"), searchFrame);
    searchLayout->addWidget(searchInfoLabel_);
    searchLayout->addStretch(1);
    infoBarStack_->addWidget(searchFrame);

    // 垃圾清理模块：可回收 / 项数 / 已选。
    auto* cleanupFrame = new QFrame(infoBarStack_);
    cleanupFrame->setObjectName(QStringLiteral("InfoBar"));
    auto* cleanupLayout = new QHBoxLayout(cleanupFrame);
    cleanupLayout->setContentsMargins(12, 8, 12, 8);
    cleanupLayout->setSpacing(14);
    cleanupInfoLabel_ = new QLabel(QStringLiteral("垃圾清理 · 暂未扫描"), cleanupFrame);
    cleanupLayout->addWidget(cleanupInfoLabel_);
    cleanupLayout->addStretch(1);
    infoBarStack_->addWidget(cleanupFrame);

    // 磁盘健康模块:盘数 / 状态汇总。
    auto* healthFrame = new QFrame(infoBarStack_);
    healthFrame->setObjectName(QStringLiteral("InfoBar"));
    auto* healthLayout = new QHBoxLayout(healthFrame);
    healthLayout->setContentsMargins(12, 8, 12, 8);
    healthLayout->setSpacing(14);
    healthInfoLabel_ = new QLabel(QStringLiteral("磁盘健康 · 尚未读取"), healthFrame);
    healthLayout->addWidget(healthInfoLabel_);
    healthLayout->addStretch(1);
    infoBarStack_->addWidget(healthFrame);

    infoBarStack_->setCurrentIndex(0);
    return infoBarStack_;
}

QWidget* MainWindow::CreateLoadingOverlay(QWidget* parent) {
    auto* overlay = new QFrame(parent);
    overlay->setObjectName(QStringLiteral("LoadingOverlay"));
    overlay->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    overlay->setVisible(false);

    auto* overlayLayout = new QVBoxLayout(overlay);
    overlayLayout->setContentsMargins(24, 24, 24, 24);
    overlayLayout->addStretch(1);

    auto* card = new QFrame(overlay);
    card->setObjectName(QStringLiteral("LoadingCard"));
    card->setMaximumWidth(420);
    card->setMinimumWidth(320);

    // 浮层卡片柔和投影，强化"悬浮"质感（单元素，无性能顾虑）。
    auto* cardShadow = new QGraphicsDropShadowEffect(card);
    cardShadow->setBlurRadius(28);
    cardShadow->setOffset(0, 6);
    cardShadow->setColor(QColor(15, 23, 42, 90));
    card->setGraphicsEffect(cardShadow);

    auto* cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(28, 24, 28, 24);
    cardLayout->setSpacing(10);

    loadingSpinnerLabel_ = new QLabel(QStringLiteral("◐"), card);
    loadingSpinnerLabel_->setObjectName(QStringLiteral("LoadingSpinner"));
    loadingSpinnerLabel_->setAlignment(Qt::AlignCenter);
    loadingTitleLabel_ = new QLabel(QStringLiteral("正在处理"), card);
    loadingTitleLabel_->setObjectName(QStringLiteral("LoadingTitle"));
    loadingTitleLabel_->setAlignment(Qt::AlignCenter);
    loadingDetailLabel_ = new QLabel(QStringLiteral("请稍候"), card);
    loadingDetailLabel_->setObjectName(QStringLiteral("LoadingDetail"));
    loadingDetailLabel_->setAlignment(Qt::AlignCenter);
    loadingDetailLabel_->setWordWrap(true);
    loadingProgressBar_ = new QProgressBar(card);
    loadingProgressBar_->setObjectName(QStringLiteral("LoadingProgress"));
    loadingProgressBar_->setRange(0, 0);
    loadingProgressBar_->setTextVisible(false);
    loadingProgressBar_->setFixedHeight(6);

    cardLayout->addWidget(loadingSpinnerLabel_);
    cardLayout->addWidget(loadingTitleLabel_);
    cardLayout->addWidget(loadingDetailLabel_);
    cardLayout->addWidget(loadingProgressBar_);

    auto* row = new QHBoxLayout();
    row->addStretch(1);
    row->addWidget(card);
    row->addStretch(1);
    overlayLayout->addLayout(row);
    overlayLayout->addStretch(1);

    return overlay;
}

QTableWidget* MainWindow::CreateResultTable() {
    auto* table = new QTableWidget(this);
    table->setHorizontalHeader(new ModernHeaderView(Qt::Horizontal, table));
    table->setItemDelegateForColumn(3, new PathElideDelegate(table));
    table->setColumnCount(4);
    table->setHorizontalHeaderLabels({
        QStringLiteral("名称"),
        QStringLiteral("大小"),
        QStringLiteral("类型"),
        QStringLiteral("路径")
    });
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setAlternatingRowColors(true);
    table->setShowGrid(false);
    table->setIconSize(QSize(16, 16));
    table->setTextElideMode(Qt::ElideMiddle);
    table->verticalHeader()->setVisible(false);
    table->verticalHeader()->setDefaultSectionSize(28);
    table->horizontalHeader()->setStretchLastSection(false);
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    table->setColumnWidth(0, 230);
    table->setColumnWidth(1, 110);
    table->setColumnWidth(2, 90);
    table->setColumnWidth(3, 520);
    return table;
}

QWidget* MainWindow::CreateSearchTab() {
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    auto* searchBar = new QFrame(page);
    searchBar->setObjectName(QStringLiteral("InlineToolBar"));
    auto* searchLayout = new QHBoxLayout(searchBar);
    searchLayout->setContentsMargins(10, 8, 10, 8);
    searchLayout->setSpacing(8);

    auto* label = new QLabel(QStringLiteral("搜索"), searchBar);
    searchEdit_ = new QLineEdit(searchBar);
    searchEdit_->setPlaceholderText(QStringLiteral("全系统搜索文件名、扩展名或路径片段"));
    searchIndexButton_ = new QPushButton(QStringLiteral("重建索引"), searchBar);
    searchLoadMoreButton_ = new QPushButton(QStringLiteral("加载更多"), searchBar);
    searchLoadMoreButton_->setToolTip(QStringLiteral("继续显示当前关键字的更多匹配结果"));
    searchLoadMoreButton_->hide();
    searchScopeLabel_ = new QLabel(QStringLiteral("范围：全系统固定磁盘"), searchBar);
    searchScopeLabel_->setObjectName(QStringLiteral("ModeBadge"));
    searchLayout->addWidget(label);
    searchLayout->addWidget(searchEdit_, 1);
    searchLayout->addWidget(searchIndexButton_);
    searchLayout->addWidget(searchLoadMoreButton_);
    searchLayout->addWidget(searchScopeLabel_);

    searchModel_ = new ResultTableModel(this);
    searchView_ = new QTableView(page);
    searchView_->setModel(searchModel_);
    searchView_->setHorizontalHeader(new ModernHeaderView(Qt::Horizontal, searchView_));
    searchView_->setItemDelegateForColumn(3, new PathElideDelegate(searchView_));
    searchView_->setSelectionBehavior(QAbstractItemView::SelectRows);
    searchView_->setSelectionMode(QAbstractItemView::SingleSelection);
    searchView_->setAlternatingRowColors(true);
    searchView_->setShowGrid(false);
    searchView_->setIconSize(QSize(16, 16));
    searchView_->setTextElideMode(Qt::ElideMiddle);
    searchView_->verticalHeader()->setVisible(false);
    searchView_->verticalHeader()->setDefaultSectionSize(28);
    searchView_->horizontalHeader()->setStretchLastSection(false);
    searchView_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    searchView_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    searchView_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    searchView_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    searchView_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    searchView_->setColumnWidth(0, 260);
    searchView_->setColumnWidth(1, 110);
    searchView_->setColumnWidth(2, 90);
    searchView_->setColumnWidth(3, 760);
    searchView_->horizontalHeader()->setSectionsClickable(true);
    searchTable_ = CreateResultTable();
    searchTable_->hide();

    auto* filterBar = new QFrame(page);
    filterBar->setObjectName(QStringLiteral("InlineToolBar"));
    auto* filterLayout = new QHBoxLayout(filterBar);
    filterLayout->setContentsMargins(10, 0, 10, 0);
    filterLayout->setSpacing(8);

    searchTimeFilterCombo_ = new QComboBox(filterBar);
    searchTimeFilterCombo_->addItem(QStringLiteral("全部"), QVariant(0));
    searchTimeFilterCombo_->addItem(QStringLiteral("今天"), QVariant(1));
    searchTimeFilterCombo_->addItem(QStringLiteral("近 7 天"), QVariant(7));
    searchTimeFilterCombo_->addItem(QStringLiteral("近 30 天"), QVariant(30));
    searchTimeFilterCombo_->addItem(QStringLiteral("近一年"), QVariant(365));
    searchTimeFilterCombo_->addItem(QStringLiteral("自定义…"), QVariant(-1));

    auto* dateRangeWidget = new QWidget(filterBar);
    dateRangeWidget->setObjectName(QStringLiteral("SearchDateRange"));
    auto* dateRangeLayout = new QHBoxLayout(dateRangeWidget);
    dateRangeLayout->setContentsMargins(0, 0, 0, 0);
    dateRangeLayout->setSpacing(4);
    searchStartDateEdit_ = new QDateEdit(QDate::currentDate().addDays(-30), dateRangeWidget);
    searchStartDateEdit_->setCalendarPopup(true);
    searchStartDateEdit_->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
    searchStartDateEdit_->setMaximumDate(QDate::currentDate());
    searchEndDateEdit_ = new QDateEdit(QDate::currentDate(), dateRangeWidget);
    searchEndDateEdit_->setCalendarPopup(true);
    searchEndDateEdit_->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
    searchEndDateEdit_->setMaximumDate(QDate::currentDate());
    dateRangeLayout->addWidget(searchStartDateEdit_);
    dateRangeLayout->addWidget(new QLabel(QStringLiteral("至"), dateRangeWidget));
    dateRangeLayout->addWidget(searchEndDateEdit_);
    dateRangeWidget->hide();

    searchSizeFilterCombo_ = new QComboBox(filterBar);
    searchSizeFilterCombo_->addItem(QStringLiteral("全部"));
    searchSizeFilterCombo_->addItem(QStringLiteral("< 10 MB"));
    searchSizeFilterCombo_->addItem(QStringLiteral("10–100 MB"));
    searchSizeFilterCombo_->addItem(QStringLiteral("100 MB–1 GB"));
    searchSizeFilterCombo_->addItem(QStringLiteral("> 1 GB"));

    searchTypeFilterCombo_ = new QComboBox(filterBar);
    searchTypeFilterCombo_->addItem(QStringLiteral("全部"));
    searchTypeFilterCombo_->addItem(QStringLiteral("仅文件"));
    searchTypeFilterCombo_->addItem(QStringLiteral("仅目录"));

    searchClearFilterButton_ = new QPushButton(QStringLiteral("清除筛选"), filterBar);
    searchClearFilterButton_->setObjectName(QStringLiteral("SecondaryButton"));

    filterLayout->addWidget(new QLabel(QStringLiteral("时间"), filterBar));
    filterLayout->addWidget(searchTimeFilterCombo_);
    filterLayout->addWidget(dateRangeWidget);
    filterLayout->addSpacing(10);
    filterLayout->addWidget(new QLabel(QStringLiteral("大小"), filterBar));
    filterLayout->addWidget(searchSizeFilterCombo_);
    filterLayout->addSpacing(10);
    filterLayout->addWidget(new QLabel(QStringLiteral("类型"), filterBar));
    filterLayout->addWidget(searchTypeFilterCombo_);
    filterLayout->addStretch(1);
    filterLayout->addWidget(searchClearFilterButton_);

    connect(searchTimeFilterCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, dateRangeWidget](int) {
        if (searchTimeFilterCombo_ != nullptr) {
            dateRangeWidget->setVisible(searchTimeFilterCombo_->currentData().toInt() == -1);
        }
        ScheduleSearch();
    });
    connect(searchSizeFilterCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) { ScheduleSearch(); });
    connect(searchTypeFilterCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) { ScheduleSearch(); });
    connect(searchStartDateEdit_, &QDateEdit::dateChanged, this, [this](const QDate&) { ScheduleSearch(); });
    connect(searchEndDateEdit_, &QDateEdit::dateChanged, this, [this](const QDate&) { ScheduleSearch(); });
    connect(searchClearFilterButton_, &QPushButton::clicked, this, [this, dateRangeWidget]() {
        if (searchTimeFilterCombo_ != nullptr) {
            const QSignalBlocker blocker(searchTimeFilterCombo_);
            searchTimeFilterCombo_->setCurrentIndex(0);
        }
        if (searchSizeFilterCombo_ != nullptr) {
            const QSignalBlocker blocker(searchSizeFilterCombo_);
            searchSizeFilterCombo_->setCurrentIndex(0);
        }
        if (searchTypeFilterCombo_ != nullptr) {
            const QSignalBlocker blocker(searchTypeFilterCombo_);
            searchTypeFilterCombo_->setCurrentIndex(0);
        }
        dateRangeWidget->hide();
        ScheduleSearch();
    });

    layout->addWidget(searchBar);
    layout->addWidget(filterBar);
    layout->addWidget(searchView_, 1);
    return page;
}

QWidget* MainWindow::CreateCleanupTab() {
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    auto* hero = new QFrame(page);
    hero->setObjectName(QStringLiteral("CleanupHero"));
    auto* heroLayout = new QVBoxLayout(hero);
    heroLayout->setContentsMargins(16, 12, 16, 12);
    heroLayout->setSpacing(8);

    auto* titleLabel = new QLabel(QStringLiteral("系统清理体检"), hero);
    titleLabel->setObjectName(QStringLiteral("CleanupTitle"));
    cleanupStatusLabel_ = new QLabel(QStringLiteral("先扫描可清理项，安全项会默认勾选，谨慎项需手动确认"), hero);
    cleanupStatusLabel_->setObjectName(QStringLiteral("CleanupStatus"));

    // 主操作「扫描垃圾」：加大尺寸的强调主按钮，置于头部右侧，作为本页首要动作。
    cleanupScanButton_ = new QPushButton(QStringLiteral("扫描垃圾"), hero);
    cleanupScanButton_->setObjectName(QStringLiteral("PrimaryButton"));
    cleanupScanButton_->setMinimumHeight(g_activeTokens.primaryButtonHeight);
    cleanupScanButton_->setMinimumWidth(g_activeTokens.primaryButtonMinW);
    QFont scanButtonFont = cleanupScanButton_->font();
    scanButtonFont.setPointSize(g_activeTokens.fsLabel);
    cleanupScanButton_->setFont(scanButtonFont);

    cleanupPrivacyCheckBox_ = new QCheckBox(QStringLiteral("隐私痕迹"), hero);
    cleanupPrivacyCheckBox_->setChecked(true);
    cleanupPrivacyCheckBox_->setToolTip(QStringLiteral("扫描最近访问记录、跳转列表、浏览器缓存和应用痕迹"));
    cleanupDeveloperCheckBox_ = new QCheckBox(QStringLiteral("开发缓存"), hero);
    cleanupDeveloperCheckBox_->setChecked(true);
    cleanupDeveloperCheckBox_->setToolTip(QStringLiteral("扫描 npm、pip、NuGet、Gradle、Maven 等依赖缓存"));
    cleanupDeepCleanCheckBox_ = new QCheckBox(QStringLiteral("深度清理：直接删除"), hero);
    cleanupDeepCleanCheckBox_->setToolTip(QStringLiteral("不进入回收站，释放空间更彻底；建议确认项目后再开启"));
    cleanupSummaryLabel_ = new QLabel(QStringLiteral("安全项默认勾选，谨慎项需手动确认"), hero);
    cleanupSummaryLabel_->setObjectName(QStringLiteral("ModeBadge"));

    // 头部行：标题 + 状态（左）与主操作按钮（右）。
    auto* titleBlock = new QVBoxLayout();
    titleBlock->setSpacing(2);
    titleBlock->addWidget(titleLabel);
    titleBlock->addWidget(cleanupStatusLabel_);
    auto* headerLayout = new QHBoxLayout();
    headerLayout->setSpacing(16);
    headerLayout->addLayout(titleBlock);
    headerLayout->addStretch(1);
    headerLayout->addWidget(cleanupScanButton_);
    heroLayout->addLayout(headerLayout);

    // 选项行：扫描范围勾选项（左）与汇总说明（右）。
    auto* optionsLayout = new QHBoxLayout();
    optionsLayout->setSpacing(14);
    optionsLayout->addWidget(cleanupPrivacyCheckBox_);
    optionsLayout->addWidget(cleanupDeveloperCheckBox_);
    optionsLayout->addWidget(cleanupDeepCleanCheckBox_);
    optionsLayout->addStretch(1);
    optionsLayout->addWidget(cleanupSummaryLabel_);
    heroLayout->addLayout(optionsLayout);

    auto* sectionPanel = new QFrame(page);
    sectionPanel->setObjectName(QStringLiteral("CleanupSectionPanel"));
    auto* sectionLayout = new QHBoxLayout(sectionPanel);
    sectionLayout->setContentsMargins(10, 8, 10, 8);
    sectionLayout->setSpacing(8);
    cleanupSectionValueLabels_.clear();
    cleanupSectionButtons_.clear();
    cleanupSectionFilter_.clear();

    /**
     * @brief 创建垃圾清理分类筛选按钮。
     * @param title 分类标题。
     * @param section 分类筛选值。
     * @return 构建完成的按钮。
     */
    auto createCleanupSectionButton = [this, sectionPanel](const QString& title, const QString& section) -> QPushButton* {
        auto* button = new QPushButton(sectionPanel);
        button->setObjectName(QStringLiteral("CleanupSectionButton"));
        button->setCheckable(true);
        button->setProperty("section", section);
        button->setText(title + QStringLiteral("\n0 B"));
        button->setToolTip(section.isEmpty() ? QStringLiteral("显示全部清理类别") : QStringLiteral("只显示%1类别").arg(title));
        auto* valueLabel = new QLabel(QStringLiteral("0 B"), sectionPanel);
        valueLabel->setObjectName(QStringLiteral("CleanupSectionValue"));
        valueLabel->hide();
        cleanupSectionValueLabels_.push_back(valueLabel);
        cleanupSectionButtons_.push_back(button);
        connect(button, &QPushButton::clicked, this, [this, section]() {
            ApplyCleanupSectionFilter(section);
        });
        return button;
    };

    sectionLayout->addWidget(createCleanupSectionButton(QStringLiteral("全部"), QString()));
    sectionLayout->addWidget(createCleanupSectionButton(QStringLiteral("软件缓存"), QStringLiteral("软件缓存")));
    sectionLayout->addWidget(createCleanupSectionButton(QStringLiteral("系统清理"), QStringLiteral("系统清理")));
    sectionLayout->addWidget(createCleanupSectionButton(QStringLiteral("隐私痕迹"), QStringLiteral("隐私痕迹")));
    sectionLayout->addWidget(createCleanupSectionButton(QStringLiteral("图形缓存"), QStringLiteral("图形缓存")));
    sectionLayout->addWidget(createCleanupSectionButton(QStringLiteral("开发工具"), QStringLiteral("开发工具")));
    sectionLayout->addStretch(1);

    // 归类说明：预计可释放 / 安全建议 / 需确认 三块紧凑小卡，置于分类筛选行右侧。
    const auto createMetricChip = [sectionPanel](const QString& title, const QString& valueObjectName,
                                                 const QString& initialValue, QLabel*& outValue) {
        auto* chip = new QFrame(sectionPanel);
        chip->setObjectName(QStringLiteral("CleanupMetricChip"));
        auto* chipLayout = new QVBoxLayout(chip);
        chipLayout->setContentsMargins(12, 5, 12, 5);
        chipLayout->setSpacing(0);
        auto* chipTitle = new QLabel(title, chip);
        chipTitle->setObjectName(QStringLiteral("MetricTitle"));
        outValue = new QLabel(initialValue, chip);
        outValue->setObjectName(valueObjectName);
        chipLayout->addWidget(chipTitle);
        chipLayout->addWidget(outValue);
        return chip;
    };
    sectionLayout->addWidget(createMetricChip(QStringLiteral("预计可释放"), QStringLiteral("CleanupTotal"), QStringLiteral("--"), cleanupTotalLabel_));
    sectionLayout->addWidget(createMetricChip(QStringLiteral("安全建议"), QStringLiteral("CleanupGood"), QStringLiteral("0 项"), cleanupSafeCountLabel_));
    sectionLayout->addWidget(createMetricChip(QStringLiteral("需确认"), QStringLiteral("CleanupWarn"), QStringLiteral("0 项"), cleanupAttentionCountLabel_));

    if (!cleanupSectionButtons_.empty()) {
        cleanupSectionButtons_.front()->setChecked(true);
    }

    cleanupTree_ = new ThemedTreeWidget(page);
    cleanupTree_->setObjectName(QStringLiteral("CleanupTree"));
    cleanupTree_->setHeader(new ModernHeaderView(Qt::Horizontal, cleanupTree_));
    cleanupTree_->setColumnCount(4);
    cleanupTree_->setHeaderLabels({
        QStringLiteral("清理项"),
        QStringLiteral("可释放"),
        QStringLiteral("建议"),
        QStringLiteral("说明")
    });
    cleanupTree_->setRootIsDecorated(true);
    cleanupTree_->setAnimated(true);
    cleanupTree_->setAlternatingRowColors(true);
    cleanupTree_->setUniformRowHeights(false);
    cleanupTree_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    cleanupTree_->setIndentation(22);
    cleanupTree_->setIconSize(QSize(16, 16));
    cleanupTree_->header()->setStretchLastSection(false);
    cleanupTree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    cleanupTree_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    cleanupTree_->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    cleanupTree_->header()->setSectionResizeMode(3, QHeaderView::Stretch);
    cleanupTree_->header()->resizeSection(0, 300);
    cleanupTree_->header()->resizeSection(1, 120);
    cleanupTree_->header()->resizeSection(2, 110);

    auto* bottomBar = new QFrame(page);
    bottomBar->setObjectName(QStringLiteral("CleanupBottomBar"));
    auto* bottomLayout = new QHBoxLayout(bottomBar);
    bottomLayout->setContentsMargins(16, 10, 16, 10);
    bottomLayout->setSpacing(12);

    auto* selectAllButton = new QPushButton(QStringLiteral("全选"), bottomBar);
    auto* selectNoneButton = new QPushButton(QStringLiteral("全不选"), bottomBar);
    auto* selectDefaultButton = new QPushButton(QStringLiteral("恢复默认"), bottomBar);
    cleanupSelectedLabel_ = new QLabel(QStringLiteral("已选中 0 B"), bottomBar);
    cleanupSelectedLabel_->setObjectName(QStringLiteral("CleanupSelected"));
    cleanupDeleteButton_ = new QPushButton(QStringLiteral("一键清理"), bottomBar);
    cleanupDeleteButton_->setObjectName(QStringLiteral("PrimaryButton"));
    cleanupDeleteButton_->setEnabled(false);

    bottomLayout->addWidget(selectAllButton);
    bottomLayout->addWidget(selectNoneButton);
    bottomLayout->addWidget(selectDefaultButton);
    bottomLayout->addStretch(1);
    bottomLayout->addWidget(cleanupSelectedLabel_);
    bottomLayout->addWidget(cleanupDeleteButton_);

    connect(selectAllButton, &QPushButton::clicked, this, [this]() {
        SetCleanupCheckedMode(QStringLiteral("all"));
    });
    connect(selectNoneButton, &QPushButton::clicked, this, [this]() {
        SetCleanupCheckedMode(QStringLiteral("none"));
    });
    connect(selectDefaultButton, &QPushButton::clicked, this, [this]() {
        SetCleanupCheckedMode(QStringLiteral("default"));
    });

    layout->addWidget(hero);
    layout->addWidget(sectionPanel);
    layout->addWidget(cleanupTree_, 1);
    layout->addWidget(bottomBar);
    return page;
}

QWidget* MainWindow::CreateTreemapPanel() {
    auto* panel = new QFrame(this);
    panel->setObjectName(QStringLiteral("TreemapPanel"));
    panel->setMinimumWidth(160);
    panel->setMaximumWidth(220);
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);

    treemapHint_ = new QLabel(QStringLiteral("空间占比图"), panel);
    treemapHint_->setObjectName(QStringLiteral("TreemapHint"));
    treemapHint_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    treemapHint_->setWordWrap(true);
    treemapHint_->setMinimumWidth(0);
    treemapHint_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);

    treemapWidget_ = new TreemapWidget(panel);
    connect(treemapWidget_, &TreemapWidget::PathActivated, this, [this](const QString& path) {
        if (!latestResult_ || !latestResult_->root) {
            return;
        }

        const core::ScanNode* node = FindNodeByPath(*latestResult_->root, path);
        if (node != nullptr) {
            SelectNodeDetails(*node);
        }
    });

    layout->addWidget(treemapHint_);
    layout->addWidget(treemapWidget_, 1);

    return panel;
}

QWidget* MainWindow::CreateMetric(const QString& title, const QString& value, QLabel** valueTarget) {
    auto* frame = new QFrame(this);
    frame->setObjectName(QStringLiteral("Metric"));

    auto* layout = new QVBoxLayout(frame);
    layout->setContentsMargins(12, 9, 12, 9);
    layout->setSpacing(2);

    auto* titleLabel = new QLabel(title, frame);
    titleLabel->setObjectName(QStringLiteral("MetricTitle"));
    auto* valueLabel = new QLabel(value, frame);
    valueLabel->setObjectName(QStringLiteral("MetricValue"));
    valueLabel->setTextFormat(Qt::RichText);
    if (valueTarget != nullptr) {
        *valueTarget = valueLabel;
    }

    layout->addWidget(titleLabel);
    layout->addWidget(valueLabel);
    return frame;
}

/**
 * @brief 根据当前页签切换磁盘分析专属区域。
 */
void MainWindow::UpdateModuleChrome() {
    if (tabs_ == nullptr) {
        return;
    }

    QWidget* currentPage = tabs_->currentWidget();
    const bool isDiskAnalysisPage =
        currentPage == directoryView_ ||
        currentPage == largeFilesView_ ||
        currentPage == staleFilesView_ ||
        currentPage == typeStatsPage_ ||
        currentPage == duplicatePage_ ||
        currentPage == ageHistogramWidget_;

    if (directoryTree_ != nullptr) {
        directoryTree_->setVisible(isDiskAnalysisPage);
    }
    if (metricsPanel_ != nullptr) {
        metricsPanel_->setVisible(isDiskAnalysisPage);
    }
    if (treemapPanel_ != nullptr) {
        treemapPanel_->setVisible(isDiskAnalysisPage);
    }
    if (commandBar_ != nullptr) {
        commandBar_->setVisible(isDiskAnalysisPage);
    }
    if (tabs_->tabBar() != nullptr) {
        tabs_->tabBar()->setVisible(isDiskAnalysisPage);
    }
    if (diskModuleButton_ != nullptr) {
        diskModuleButton_->setChecked(isDiskAnalysisPage);
    }
    if (searchModuleButton_ != nullptr) {
        searchModuleButton_->setChecked(currentPage != nullptr && searchView_ != nullptr && currentPage->isAncestorOf(searchView_));
    }
    if (cleanupModuleButton_ != nullptr) {
        cleanupModuleButton_->setChecked(currentPage != nullptr && cleanupTree_ != nullptr && currentPage->isAncestorOf(cleanupTree_));
    }
    if (healthModuleButton_ != nullptr) {
        healthModuleButton_->setChecked(currentPage != nullptr && healthPage_ != nullptr && currentPage == healthPage_);
    }
    if (infoBarStack_ != nullptr) {
        const bool isSearchPage = currentPage != nullptr && searchView_ != nullptr && currentPage->isAncestorOf(searchView_);
        const bool isCleanupPage = currentPage != nullptr && cleanupTree_ != nullptr && currentPage->isAncestorOf(cleanupTree_);
        const bool isHealthPage = currentPage != nullptr && healthPage_ != nullptr && currentPage == healthPage_;
        if (isDiskAnalysisPage) {
            infoBarStack_->setCurrentIndex(0);
        } else if (isSearchPage) {
            infoBarStack_->setCurrentIndex(1);
            UpdateSearchInfoBar();
        } else if (isCleanupPage) {
            infoBarStack_->setCurrentIndex(2);
        } else if (isHealthPage) {
            infoBarStack_->setCurrentIndex(3);
        } else {
            infoBarStack_->setCurrentIndex(0);
        }
    }
    if (workspaceSplitter_ != nullptr) {
        workspaceSplitter_->setSizes(isDiskAnalysisPage ? QList<int>{280, 905, 170} : QList<int>{0, 1055, 0});
    }

    ReevaluateWatcher();
}

bool MainWindow::IsOnDiskAnalysisPage() const {
    if (tabs_ == nullptr) {
        return false;
    }
    // 精确复刻 UpdateModuleChrome 的 isDiskAnalysisPage 判定,二者须同步。
    QWidget* currentPage = tabs_->currentWidget();
    return currentPage == directoryView_ ||
           currentPage == largeFilesView_ ||
           currentPage == staleFilesView_ ||
           currentPage == typeStatsPage_ ||
           currentPage == duplicatePage_ ||
           currentPage == ageHistogramWidget_;
}

QStringList MainWindow::ComputeWatchPaths() const {
    if (latestResult_ == nullptr || latestResult_->root == nullptr) {
        return {};
    }
    // 归一为原生分隔符并去尾分隔符(保留裸盘根如 C:\),与 driveCombo 文本对齐。
    QString root = QDir::toNativeSeparators(ToQString(latestResult_->root->path));
    while (root.length() > 3 && root.endsWith(QLatin1Char('\\'))) {
        root.chop(1);
    }
    // 裸盘根(形如 C:\、D:\)不监视:监视它及其顶层目录会让盘中任意写入都触发
    // 全盘 MFT 重扫,形成稳态死循环。这是路线图"根+顶层子目录"的必要安全收敛。
    if (root.length() == 3 && root.at(0).isLetter() && root.at(1) == QLatin1Char(':') && root.at(2) == QLatin1Char('\\')) {
        return {};
    }
    QStringList paths;
    paths << root;
    for (const auto& child : latestResult_->root->children) {
        if (child && child->kind == core::NodeKind::Directory) {
            QString childPath = QDir::toNativeSeparators(ToQString(child->path));
            while (childPath.length() > 3 && childPath.endsWith(QLatin1Char('\\'))) {
                childPath.chop(1);
            }
            if (!childPath.isEmpty() && !paths.contains(childPath)) {
                paths << childPath;
            }
        }
    }
    return paths;
}

void MainWindow::DisarmWatcher() {
    if (folderWatcher_ != nullptr) {
        const QStringList watched = folderWatcher_->directories();
        if (!watched.isEmpty()) {
            folderWatcher_->removePaths(watched);
        }
    }
    currentWatchPaths_.clear();
}

void MainWindow::ReevaluateWatcher() {
    if (folderWatcher_ == nullptr) {
        return;
    }
    // 扫描进行中绝不改动 watcher 路径(硬约束);扫完由 HandleScanFinished 再次调用重新 arm。
    if (scanning_.load()) {
        return;
    }
    const bool shouldWatch = liveWatchEnabled_ &&
                             IsOnDiskAnalysisPage() &&
                             latestResult_ != nullptr &&
                             latestResult_->root != nullptr;
    if (!shouldWatch) {
        DisarmWatcher();
        watchedRootPath_.clear();
        return;
    }
    const QStringList desired = ComputeWatchPaths();
    if (currentWatchPaths_ == desired) {
        return;  // 幂等:目标状态未变(含同为空)则不动 watcher。
    }
    DisarmWatcher();
    if (!desired.isEmpty()) {
        folderWatcher_->addPaths(desired);  // 不可监视的路径(网络/无权限)会被 Qt 静默跳过,尽力而为。
    }
    currentWatchPaths_ = desired;
    watchedRootPath_ = desired.isEmpty() ? QString() : desired.first();
}

void MainWindow::ScheduleWatcherRescan() {
    if (watchDebounceTimer_ != nullptr) {
        watchDebounceTimer_->start();  // 单次定时器:重复 start() 重置间隔,自然聚合一波 directoryChanged。
    }
}

void MainWindow::OnWatchDebounceTimeout() {
    if (!liveWatchEnabled_) {
        return;
    }
    if (!IsOnDiskAnalysisPage()) {
        return;
    }
    if (watchedRootPath_.isEmpty()) {
        return;
    }
    // 扫描进行中:排队到本次扫完(scanning_ 由完成 lambda 在 UI 线程清零,届时定时器重试)。
    if (scanning_.load()) {
        watchDebounceTimer_->start();
        return;
    }
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    // 冷却:防外部搅动(AV/OneDrive/索引器)导致的稳态重扫循环,窗口=max(30s, 2×上次扫描耗时)。
    if (now < watcherCooldownUntilMsec_) {
        const qint64 remaining = watcherCooldownUntilMsec_ - now;
        // 上限放宽到 1h:长冷却(如 10 分钟扫描→20 分钟冷却)下,旧 60s 上限会让防抖定时器每分钟空转唤醒;1h 上限使唤醒次数大幅减少。
        watchDebounceTimer_->start(static_cast<int>(qBound(qint64(200), remaining, qint64(3600000))));
        return;
    }
    lastWatcherRescanMsec_ = now;
    RescanPath(watchedRootPath_);
}

/**
 * @brief 设置当前皮肤并刷新样式。
 * @param themeName 目标皮肤名称，支持 light、dark、blue。
 */
void MainWindow::SetTheme(const QString& themeName) {
    currentTheme_ = themeName.isEmpty() ? QStringLiteral("light") : themeName;
    ApplyStyle();
    UpdateModuleChrome();
    SetInfoBar(QStringLiteral("已切换皮肤"),
               latestResult_ != nullptr ? latestResult_->fileCount : 0,
               latestResult_ != nullptr ? latestResult_->directoryCount : 0,
               currentTheme_);
}

void MainWindow::ApplyStyle() {
    // 刷新当前主题令牌；样式表和自绘控件（表头、空间图、清理语义色）共用这一套取值，
    // 三套皮肤（light/dark/blue）共用同一段样式结构，仅令牌取值不同。
    g_activeTokens = ResolveThemeTokens(currentTheme_);
    const ThemeTokens& t = g_activeTokens;

    // 同步空间占比图主题颜色，使其在浅色/暗色/蓝色皮肤下都协调（数据色块保持彩色）。
    if (treemapWidget_ != nullptr) {
        TreemapColors treemapColors;
        treemapColors.background = QColor(t.cardBg);
        treemapColors.emptyText = QColor(t.textMuted);
        treemapColors.cardBg = QColor(t.altRow);
        treemapColors.cardHoverBg = QColor(t.hoverRow);
        treemapColors.cardBorder = QColor(t.cardBorder);
        treemapColors.nameText = QColor(t.textPrimary);
        treemapColors.sizeText = QColor(t.textSecondary);
        treemapColors.barTrack = QColor(t.cardBorder);
        treemapWidget_->SetColors(treemapColors);
    }

    // 同步分类环形图主题颜色(扇区固定彩色,这里只刷新背景/文字/轨道)。
    if (typeStatsDonut_ != nullptr) {
        qt_ui::DonutColors donutColors;
        donutColors.background = QColor(t.cardBg);
        donutColors.trackBg = QColor(t.cardBorder);
        donutColors.centerText = QColor(t.textPrimary);
        donutColors.centerCaption = QColor(t.textSecondary);
        donutColors.labelText = QColor(t.textPrimary);
        donutColors.labelValue = QColor(t.textSecondary);
        donutColors.swatchBorder = QColor(t.cardBorder);
        typeStatsDonut_->SetColors(donutColors);
    }

    // 同步文件年龄直方图主题颜色(柱体固定彩色,这里只刷新背景/基线/文字)。
    if (ageHistogramWidget_ != nullptr) {
        qt_ui::HistogramColors histColors;
        histColors.background = QColor(t.cardBg);
        histColors.axisLine = QColor(t.cardBorder);
        histColors.barTopValue = QColor(t.textPrimary);
        histColors.bandLabel = QColor(t.textPrimary);
        histColors.countLabel = QColor(t.textSecondary);
        histColors.caption = QColor(t.textSecondary);
        ageHistogramWidget_->SetColors(histColors);
    }

    // 样式表以 @token 形式占位，末尾用当前主题令牌统一替换，避免散落硬编码颜色。
    // 注意：样式表体积较大，单条字符串字面量会触发 MSVC 的 C2026（单字面量 16380 字节上限）。
    // 这里用 QString::fromUtf8 包裹「相邻原始字符串字面量」拼接，每段都远低于上限，便于后续继续扩充。
    QString style = QString::fromUtf8(R"(
        QMainWindow {
            background: @windowBg;
            font-family: "Microsoft YaHei UI", "Segoe UI";
            font-size: @fsBody;
            color: @textPrimary;
        }
        QLabel {
            color: @textPrimary;
            background: transparent;
        }
        QToolTip {
            background: @cardBg;
            color: @textPrimary;
            border: 1px solid @cardBorder;
            padding: 4px 6px;
        }

        /* 卡片与面板：浮于画布之上，统一描边 + 顶部高光模拟微立体 */
        QFrame#CommandBar, QFrame#InfoBar,
        QFrame#Metric, QFrame#CleanupHero, QFrame#TreemapPanel {
            background: @cardBg;
            border: 1px solid @cardBorder;
            border-top: 1px solid @cardTopHi;
            border-radius: @cardRadius;
        }
        QFrame#ModuleNav {
            background: @navBg;
            border: none;
            border-right: 1px solid @navBorder;
            border-radius: 0;
        }

        /* 侧边栏细分隔线（页脚上方） */
        QFrame#NavDivider {
            background: @navBorder;
            border: none;
        }

        /* 左侧功能导航 */
        QPushButton#ModuleNavButton {
            min-height: @navButtonHeight;
            padding: 0 14px 0 12px;
            border: none;
            border-left: 3px solid transparent;
            border-radius: @controlRadius;
            background: transparent;
            color: @textSecondary;
            font-size: @fsLabel;
            font-weight: 600;
            text-align: left;
        }
        QPushButton#ModuleNavButton:hover {
            background: @accentSoft;
            color: @accent;
        }
        QPushButton#ModuleNavButton:checked {
            background: @accentSoft;
            border-left: 3px solid @accent;
            color: @accent;
            font-weight: 700;
        }

        /* 顶部应用菜单条 */
        QFrame#MenuStrip {
            background: @navBg;
            border: none;
            border-radius: 0;
        }
        /* 系统窗口标题栏与菜单条之间的分割线（菜单条最顶部） */
        QFrame#TopDivider {
            background: @inputBorder;
            border: none;
        }
        QPushButton#MenuStripButton {
            min-height: @controlHeight;
            padding: 0 12px;
            border: none;
            border-radius: 0;
            background: transparent;
            color: @textSecondary;
            font-weight: 500;
        }
        QPushButton#MenuStripButton:hover,
        QPushButton#MenuStripButton:pressed {
            background: @accentSoft;
            color: @accent;
        }
        QPushButton#MenuStripButton::menu-indicator {
            image: none;
            width: 0;
        }

        /* 命令栏标签与徽章 */
        QLabel#ToolbarLabel {
            color: @textSecondary;
            font-weight: 600;
            padding-right: 2px;
        }
        QLabel#ModeBadge {
            color: @accent;
            background: @accentSoft;
            border: 1px solid @accentSoftBorder;
            border-radius: @pillRadius;
            padding: 5px 12px;
        }

        /* 垃圾清理 Hero 与指标卡 */
        QFrame#CleanupMetricChip {
            background: @accentSoft;
            border: 1px solid @accentSoftBorder;
            border-radius: @controlRadius;
        }
        QLabel#CleanupTitle {
            color: @textPrimary;
            font-size: @fsTitle;
            font-weight: 700;
        }
        QLabel#CleanupStatus {
            color: @textSecondary;
        }
        QLabel#CleanupTotal {
            color: @accent;
            font-size: @fsLabel;
            font-weight: 700;
        }
        QLabel#CleanupGood {
            color: @good;
            font-size: @fsLabel;
            font-weight: 700;
        }
        QLabel#CleanupWarn {
            color: @warn;
            font-size: @fsLabel;
            font-weight: 700;
        }
        QFrame#CleanupSectionPanel {
            background: @accentSoft;
            border: 1px solid @accentSoftBorder;
            border-radius: @cardRadius;
        }
        QPushButton#CleanupSectionButton {
            background: @cardBg;
            border: 1px solid @cardBorder;
            border-radius: @controlRadius;
            min-width: 78px;
            min-height: 42px;
            padding: 4px 10px;
            color: @textSecondary;
            font-weight: 700;
        }
        QPushButton#CleanupSectionButton:hover {
            border-color: @accentSoftBorder;
            background: @accentSoft;
        }
        QPushButton#CleanupSectionButton:checked {
            background: @accent;
            border: 1px solid @accentDeep;
            color: @accentContrastText;
        }

        /* 信息栏忙碌动画与加载浮层 */
        QLabel#BusyIndicator {
            color: @accent;
            font-weight: 700;
        }
        QFrame#LoadingOverlay {
            background: @overlayScrim;
            border: none;
        }
        QFrame#LoadingCard {
            background: @cardBg;
            border: 1px solid @cardBorder;
            border-radius: @cardRadius px;
        }
        QLabel#LoadingSpinner {
            color: @accent;
            font-size: @fsDisplay;
            font-weight: 700;
        }
        QLabel#LoadingTitle {
            color: @textPrimary;
            font-size: @fsTitle;
            font-weight: 700;
        }
        QLabel#LoadingDetail {
            color: @textSecondary;
            font-size: @fsCaption;
        }
        QProgressBar#LoadingProgress {
            background: @cardBorder;
            border: none;
            border-radius: @trackRadius px;
        }
        QProgressBar#LoadingProgress::chunk {
            background: @accent;
            border-radius: @trackRadius px;
        }
)"
        R"(
        /* 通用按钮 */
        QPushButton {
            min-height: @controlHeight;
            padding: 0 @spaceLg;
            border: 1px solid @inputBorder;
            border-radius: @controlRadius;
            background: @cardBg;
            color: @textPrimary;
            font-weight: 500;
        }
        QPushButton:hover {
            background: @accentSoft;
            border-color: @accentSoftBorder;
            color: @accent;
        }
        QPushButton:pressed {
            background: @accentSoftBorder;
            border-color: @accent;
            padding-top: 1px;
            padding-left: 15px;
        }
        QPushButton:focus {
            border-color: @accent;
        }
        QPushButton#PrimaryButton {
            background: @accent;
            border: 1px solid @accentDeep;
            color: @accentContrastText;
            font-weight: 600;
        }
        QPushButton#PrimaryButton:hover {
            background: @accentHover;
            border-color: @accentDeep;
        }
        QPushButton#PrimaryButton:pressed {
            background: @accentPressed;
            border-color: @accentDeep;
        }
        QPushButton#PrimaryButton:disabled {
            background: @accentSoft;
            border-color: @accentSoftBorder;
            color: @textMuted;
        }
        QPushButton#SecondaryButton {
            background: @cardBg;
            border: 1px solid @inputBorder;
            color: @textSecondary;
            font-weight: 500;
        }
        QPushButton#SecondaryButton:hover {
            background: @accentSoft;
            border-color: @accentSoftBorder;
            color: @accent;
        }
        QPushButton#SecondaryButton:pressed {
            background: @accentSoftBorder;
            border-color: @accent;
            padding-top: 1px;
            padding-left: 15px;
        }
        QPushButton#SecondaryButton:disabled {
            color: @textMuted;
            background: @cardBg;
            border-color: @cardBorder;
        }
        QPushButton:disabled {
            color: @textMuted;
            background: @cardBorder;
            border-color: @cardBorder;
        }

        /* 输入框与下拉框 */
        QLineEdit, QComboBox {
            min-height: @controlHeight;
            border: 1px solid @inputBorder;
            border-radius: @controlRadius;
            padding: 0 @spaceSm;
            background: @inputBg;
            color: @textPrimary;
            selection-background-color: @selectedRow;
            selection-color: @selectedText;
        }
        QLineEdit:focus, QComboBox:focus {
            border-color: @accent;
        }
        QLineEdit:hover {
            border-color: @accentSoftBorder;
        }
        QComboBox:hover {
            border-color: @accentSoftBorder;
        }
        QComboBox::drop-down {
            width: 28px;
            border: none;
            background: transparent;
            border-top-right-radius: @controlRadius;
            border-bottom-right-radius: @controlRadius;
        }
        QComboBox::down-arrow {
            width: 0;
            height: 0;
            border-left: 5px solid transparent;
            border-right: 5px solid transparent;
            border-top: 6px solid @textSecondary;
            margin-right: 10px;
        }
        QComboBox QAbstractItemView {
            background: @cardBg;
            border: 1px solid @cardBorder;
            selection-background-color: @accentSoft;
            selection-color: @accent;
            outline: 0;
            padding: @spaceXs;
        }
        QCheckBox {
            color: @textSecondary;
            spacing: 6px;
        }

        /* 滚动条 */
        QScrollBar:vertical {
            background: transparent;
            border: none;
            width: 12px;
            margin: 2px;
        }
        QScrollBar::handle:vertical {
            background: @inputBorder;
            min-height: 36px;
            border-radius: 4px;
        }
        QScrollBar::handle:vertical:hover {
            background: @textMuted;
        }
        QScrollBar::add-line:vertical,
        QScrollBar::sub-line:vertical,
        QScrollBar::add-page:vertical,
        QScrollBar::sub-page:vertical {
            background: transparent;
            border: none;
            height: 0;
        }
        QScrollBar:horizontal {
            background: transparent;
            border: none;
            height: 12px;
            margin: 2px;
        }
        QScrollBar::handle:horizontal {
            background: @inputBorder;
            min-width: 36px;
            border-radius: 4px;
        }
        QScrollBar::handle:horizontal:hover {
            background: @textMuted;
        }
        QScrollBar::add-line:horizontal,
        QScrollBar::sub-line:horizontal,
        QScrollBar::add-page:horizontal,
        QScrollBar::sub-page:horizontal {
            background: transparent;
            border: none;
            width: 0;
        }

        /* 树与表 */
        QTreeWidget, QTableWidget, QTableView {
            background: @cardBg;
            alternate-background-color: @altRow;
            border: 1px solid @cardBorder;
            border-radius: @cardRadius;
            selection-background-color: @selectedRow;
            selection-color: @selectedText;
            outline: 0;
            gridline-color: transparent;
            color: @textPrimary;
        }
        QTreeWidget::item, QTableWidget::item, QTableView::item {
            min-height: @rowHeight;
            padding: 3px 6px;
            border: none;
            outline: 0;
        }
        /* 目录树收紧左内距，避免首列文字离左边过远。 */
        QTreeWidget#DirectoryTree::item {
            padding: 3px 2px;
        }
        QTreeWidget::item:hover, QTableWidget::item:hover, QTableView::item:hover {
            background: @hoverRow;
        }
        QTreeWidget::item:selected, QTableWidget::item:selected, QTableView::item:selected {
            background: @selectedRow;
            color: @selectedText;
        }
        QTreeWidget#CleanupTree {
            background: @cardBg;
            alternate-background-color: @altRow;
            border: 1px solid @cardBorder;
            border-radius: @cardRadius;
        }
        QTreeWidget#CleanupTree::item {
            min-height: 30px;
            padding: @spaceXs @spaceSm;
        }
        QTreeWidget#CleanupTree::item:hover {
            background: @hoverRow;
        }
        QTreeWidget#CleanupTree::item:selected {
            background: @selectedRow;
            color: @selectedText;
        }
        QFrame#CleanupBottomBar {
            background: @accentSoft;
            border: 1px solid @accentSoftBorder;
            border-radius: @cardRadius;
        }
        QLabel#CleanupSelected {
            color: @danger;
            font-size: @fsTitle;
            font-weight: 700;
        }
        QTreeView::branch {
            background: transparent;
            border-image: none;
        }
        QTreeView::branch:has-siblings:!adjoins-item,
        QTreeView::branch:has-siblings:adjoins-item,
        QTreeView::branch:!has-children:!has-siblings:adjoins-item {
            border-image: none;
        }
        QHeaderView::section {
            background: @headerBg;
            border: none;
            border-right: 1px solid @headerLine;
            border-bottom: 1px solid @headerLine;
            padding: 8px 9px;
            font-weight: 600;
            color: @headerText;
        }
        QHeaderView::section:hover {
            background: @hoverRow;
        }
)"
        R"(
        /* 指标卡 */
        QLabel#MetricTitle {
            color: @textMuted;
            font-size: @fsCaption;
        }
        QLabel#MetricValue {
            color: @textPrimary;
            font-size: @fsTitle;
            font-weight: 700;
        }

        /* 空间图面板 */
        QLabel#TreemapHint {
            color: @textSecondary;
            font-weight: 600;
            line-height: 140%;
        }

        /* 空状态引导卡片：各页无数据时居中提示 */
        QFrame#EmptyState { background: transparent; border: none; }
        QLabel#EmptyStateIcon { background: transparent; }
        QLabel#EmptyStateTitle {
            color: @textSecondary;
            font-size: @fsTitle;
            font-weight: 600;
            background: transparent;
        }
        QLabel#EmptyStateHint {
            color: @textMuted;
            font-size: @fsBody;
            background: transparent;
        }

        /* 标签页：透明底 + 选中态下方强调线 */
        QTabWidget::pane {
            border: none;
            background: transparent;
        }
        /* 首选项标签页:纯 QWidget 子页默认不刷背景,显式刷 @windowBg 与对话框底色一致,
           消除深/蓝主题下可能的灰色拼接缝(等同对话框底色,无拼接缝时为空操作)。 */
        QWidget#PrefPage {
            background: @windowBg;
            border: none;
        }
        QTabBar {
            background: transparent;
        }
        QTabBar::tab {
            padding: @spaceSm @spaceXl;
            background: transparent;
            border: 1px solid transparent;
            border-bottom: 3px solid transparent;
            color: @textSecondary;
            font-weight: 500;
            margin-right: 2px;
        }
        QTabBar::tab:hover {
            color: @accent;
            background: @accentSoft;
            border-top-left-radius: @controlRadius;
            border-top-right-radius: @controlRadius;
        }
        QTabBar::tab:selected {
            background: @accentSoft;
            color: @accent;
            font-weight: 700;
            border-bottom: 3px solid @accent;
            border-top-left-radius: @controlRadius;
            border-top-right-radius: @controlRadius;
        }

        QLabel#AboutTitle {
            color: @textPrimary;
            font-size: @fsTitle;
            font-weight: 700;
        }
        QLabel#AboutVersion {
            color: @textMuted;
            font-size: @fsCaption;
        }
        QFrame#AboutSeparator {
            color: @cardBorder;
            background: @cardBorder;
            border: none;
            max-height: 1px;
            min-height: 1px;
        }

        /* 指标卡焦点强调：总占用用强调色突出层次 */
        QLabel#MetricValueAccent {
            color: @accent;
            font-size: @fsH1;
            font-weight: 800;
        }

        /* 磁盘增长告警横幅:度量卡观感 + 左侧 @warn 提示条,与三主题协调 */
        QFrame#GrowthAlert {
            background: @cardBg;
            border: 1px solid @cardBorder;
            border-left: 4px solid @warn;
            border-radius: @cardRadius;
        }
        QLabel#GrowthAlertTitle {
            color: @warn;
            font-weight: 700;
            font-size: @fsCaption;
        }
        QLabel#GrowthAlertBody {
            color: @textPrimary;
            font-size: @fsBody;
        }
        QPushButton#GrowthAlertClose {
            background: transparent;
            border: none;
            color: @textMuted;
            font-size: @fsTitle;
            padding: 0 8px;
            min-width: 24px;
        }
        QPushButton#GrowthAlertClose:hover {
            color: @danger;
            background: @accentSoft;
            border-radius: @controlRadius;
        }

        /* 左侧导航品牌页脚 */
        QLabel#ModuleNavFooter {
            color: @textMuted;
            font-size: @fsCaption;
            padding: 6px 4px 2px 4px;
        }

        /* 下拉菜单与右键菜单：跟随主题，替换原生样式 */
        QMenu {
            background: @cardBg;
            border: 1px solid @cardBorder;
            border-radius: @controlRadius;
            padding: 6px;
            color: @textPrimary;
        }
        QMenu::item {
            padding: 6px 28px 6px 18px;
            border-radius: @controlRadius;
            margin: 1px 3px;
            color: @textPrimary;
        }
        QMenu::item:selected {
            background: @accentSoft;
            color: @accent;
        }
        QMenu::item:disabled {
            color: @textMuted;
        }
        QMenu::separator {
            height: 1px;
            background: @cardBorder;
            margin: 4px 8px;
        }
        QMenu::right-arrow {
            width: 8px;
            height: 8px;
            margin-right: 10px;
        }

        /* 对话框与消息框：跟随主题 */
        QDialog, QMessageBox {
            background: @windowBg;
            color: @textPrimary;
        }
        QMessageBox {
            background: @windowBg;
        }
        QMessageBox QLabel, QDialog QLabel {
            color: @textPrimary;
            background: transparent;
        }
        QCheckBox QCheckBox {
            color: @textSecondary;
        }
        QProgressBar {
            background: @cardBorder;
            border: none;
            border-radius: @trackRadius px;
            color: @textPrimary;
        }
        QProgressBar::chunk {
            background: @accent;
            border-radius: @trackRadius px;
        }
        QGroupBox {
            color: @textSecondary;
            border: 1px solid @cardBorder;
            border-radius: @controlRadius;
            margin-top: 10px;
            padding-top: 10px;
            font-weight: 600;
        }
        QGroupBox::title {
            subcontrol-origin: margin;
            left: 10px;
            padding: 0 @spaceXs;
        }
    )" R"(
        QFrame#HealthCard {
            background: @cardBg;
            border: 1px solid @cardBorder;
            border-radius: @cardRadius;
        }
        QFrame#HealthCardMeter {
            background: @windowBg;
            border: 1px solid @cardBorder;
            border-radius: @controlRadius;
        }
        QLabel#HealthCardModel {
            color: @textPrimary;
            font-size: @fsTitle pt;
            font-weight: 600;
        }
        QLabel#HealthCardSub {
            color: @textSecondary;
            font-size: @fsBody pt;
        }
        QLabel#HealthCardKey {
            color: @textMuted;
            font-size: @fsCaption pt;
        }
        QLabel#HealthCardVal {
            color: @textPrimary;
            font-size: @fsLabel pt;
            font-weight: 600;
        }
        QLabel#HealthCardPct {
            color: @textPrimary;
            font-size: @fsH1 pt;
            font-weight: 700;
        }
        QLabel#HealthBadge {
            color: #ffffff;
            background: @textMuted;
            border: none;
            border-radius: @pillRadius;
            padding: 2px @spaceSm px;
            font-size: @fsCaption pt;
            font-weight: 600;
        }
        QLabel#HealthBadge[statusProp="good"] { background: @good; }
        QLabel#HealthBadge[statusProp="warn"] { background: @warn; }
        QLabel#HealthBadge[statusProp="danger"] { background: @danger; }
        QLabel#HealthBadge[statusProp="muted"] { background: @textMuted; }
    )");

    // 注意：必须先替换更长的占位符再替换其前缀（如 @accentSoftBorder 先于 @accentSoft 先于 @accent），
    // 否则前缀替换会破坏更长的占位符。
    style
        .replace(QStringLiteral("@accentContrastText"), t.accentContrastText)
        .replace(QStringLiteral("@accentSoftBorder"), t.accentSoftBorder)
        .replace(QStringLiteral("@accentPressed"), t.accentPressed)
        .replace(QStringLiteral("@accentHover"), t.accentHover)
        .replace(QStringLiteral("@accentDeep"), t.accentDeep)
        .replace(QStringLiteral("@accentSoft"), t.accentSoft)
        .replace(QStringLiteral("@accent"), t.accent)
        .replace(QStringLiteral("@windowBg"), t.windowBg)
        .replace(QStringLiteral("@navBg"), t.navBg)
        .replace(QStringLiteral("@navBorder"), t.navBorder)
        .replace(QStringLiteral("@cardBg"), t.cardBg)
        .replace(QStringLiteral("@cardBorder"), t.cardBorder)
        .replace(QStringLiteral("@cardTopHi"), t.cardTopHi)
        .replace(QStringLiteral("@inputBg"), t.inputBg)
        .replace(QStringLiteral("@inputBorder"), t.inputBorder)
        .replace(QStringLiteral("@textPrimary"), t.textPrimary)
        .replace(QStringLiteral("@textSecondary"), t.textSecondary)
        .replace(QStringLiteral("@textMuted"), t.textMuted)
        .replace(QStringLiteral("@headerBg"), t.headerBg)
        .replace(QStringLiteral("@headerText"), t.headerText)
        .replace(QStringLiteral("@headerLine"), t.headerLine)
        .replace(QStringLiteral("@sortArrow"), t.sortArrow)
        .replace(QStringLiteral("@altRow"), t.altRow)
        .replace(QStringLiteral("@hoverRow"), t.hoverRow)
        .replace(QStringLiteral("@selectedRow"), t.selectedRow)
        .replace(QStringLiteral("@selectedText"), t.selectedText)
        .replace(QStringLiteral("@good"), t.good)
        .replace(QStringLiteral("@warn"), t.warn)
        .replace(QStringLiteral("@danger"), t.danger)
        .replace(QStringLiteral("@overlayScrim"), t.overlayScrim)
        .replace(QStringLiteral("@controlRadius"), QString::number(t.controlRadius))
        .replace(QStringLiteral("@trackRadius"), QString::number(t.trackRadius))
        .replace(QStringLiteral("@cardRadius"), QString::number(t.cardRadius))
        .replace(QStringLiteral("@pillRadius"), QString::number(t.pillRadius))
        .replace(QStringLiteral("@rowHeight"), QString::number(t.rowHeight))
        // 度量/字号/尺寸令牌(按长度降序,避免前缀污染)。
        .replace(QStringLiteral("@primaryButtonMinW"), QString::number(t.primaryButtonMinW))
        .replace(QStringLiteral("@primaryButtonHeight"), QString::number(t.primaryButtonHeight))
        .replace(QStringLiteral("@navButtonHeight"), QString::number(t.navButtonHeight))
        .replace(QStringLiteral("@controlHeight"), QString::number(t.controlHeight))
        .replace(QStringLiteral("@space2xl"), QString::number(t.space2xl))
        .replace(QStringLiteral("@spaceDialog"), QString::number(t.spaceDialog))
        .replace(QStringLiteral("@spaceXl"), QString::number(t.spaceXl))
        .replace(QStringLiteral("@spaceMd"), QString::number(t.spaceMd))
        .replace(QStringLiteral("@spaceSm"), QString::number(t.spaceSm))
        .replace(QStringLiteral("@spaceLg"), QString::number(t.spaceLg))
        .replace(QStringLiteral("@spaceXs"), QString::number(t.spaceXs))
        .replace(QStringLiteral("@fsDisplay"), QString::number(t.fsDisplay) + QStringLiteral("pt"))
        .replace(QStringLiteral("@fsCaption"), QString::number(t.fsCaption) + QStringLiteral("pt"))
        .replace(QStringLiteral("@fsLabel"), QString::number(t.fsLabel) + QStringLiteral("pt"))
        .replace(QStringLiteral("@fsTitle"), QString::number(t.fsTitle) + QStringLiteral("pt"))
        .replace(QStringLiteral("@fsBody"), QString::number(t.fsBody) + QStringLiteral("pt"))
        .replace(QStringLiteral("@fsH1"), QString::number(t.fsH1) + QStringLiteral("pt"));

    setStyleSheet(style);

    ApplyActionIcons();

#ifdef Q_OS_WIN
    // Windows 原生标题栏跟随皮肤：暗色启用沉浸式深色标题栏（Win10 2004+/Win11），
    // 浅色/蓝色恢复系统浅色；切换时用 SWP_FRAMECHANGED 强制重绘非客户区，确保即时生效。
    {
        HWND hwnd = reinterpret_cast<HWND>(winId());
        if (hwnd != nullptr) {
            const BOOL darkTitleBar = (currentTheme_ == QStringLiteral("dark")) ? TRUE : FALSE;
            if (FAILED(DwmSetWindowAttribute(hwnd, 20, &darkTitleBar, sizeof(darkTitleBar)))) {
                DwmSetWindowAttribute(hwnd, 19, &darkTitleBar, sizeof(darkTitleBar));
            }
            // SWP_FRAMECHANGED 在部分 Win10 构建上不触发 DWM 重新合成标题栏；用一次真实几何变化
            // （±1px 尺寸往返）强制 DWM 重绘非客户区，实现切肤即时生效，无需最小化。
            // 最大化时不能改尺寸（会取消最大化），退化为 SWP_FRAMECHANGED。
            RECT windowRect{};
            if (IsZoomed(hwnd)) {
                SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                             SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
            } else if (GetWindowRect(hwnd, &windowRect)) {
                const int width = windowRect.right - windowRect.left;
                const int height = windowRect.bottom - windowRect.top;
                SetWindowPos(hwnd, nullptr, windowRect.left, windowRect.top, width, height + 1,
                             SWP_NOZORDER | SWP_NOACTIVATE);
                SetWindowPos(hwnd, nullptr, windowRect.left, windowRect.top, width, height,
                             SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
            }
            RedrawWindow(hwnd, nullptr, nullptr, RDW_FRAME | RDW_INVALIDATE | RDW_UPDATENOW);
        }
    }
#endif
}

void MainWindow::ApplyActionIcons() {
    // 动作类图标使用当前主题强调色，因此每次 ApplyStyle 都重新设置，使浅色 / 暗色 / 蓝色皮肤
    // 切换后图标颜色同步。内容类图标（文件夹 / 文件 / 磁盘）使用固定配色，在各处填充时设置。
    const QColor accent(g_activeTokens.accent);
    // 主按钮（PrimaryButton）背景即强调色，图标若也注入强调色会与背景同色近乎不可见；
    // 故主按钮图标改用按钮文字对比色（三套主题均为强调色反白），与文字同色保证可见。
    const QColor primaryIcon(g_activeTokens.accentContrastText);
    if (browseButton_ != nullptr) {
        browseButton_->setIcon(app_icons::folderOpen(16, accent));
    }
    if (scanButton_ != nullptr) {
        scanButton_->setIcon(app_icons::play(16, primaryIcon));
    }
    if (stopButton_ != nullptr) {
        stopButton_->setIcon(app_icons::stop(16, accent));
    }
    if (boostButton_ != nullptr) {
        boostButton_->setIcon(app_icons::arrowUp(16, accent));
    }
    if (searchIndexButton_ != nullptr) {
        searchIndexButton_->setIcon(app_icons::refresh(16, accent));
    }
    if (cleanupScanButton_ != nullptr) {
        cleanupScanButton_->setIcon(app_icons::refresh(16, primaryIcon));
    }
    if (cleanupDeleteButton_ != nullptr) {
        cleanupDeleteButton_->setIcon(app_icons::trash(16, primaryIcon));
    }

    // 侧边栏主功能导航图标：交给 UpdateModuleNavIcons 以多态像素图着色（未选灰、悬停/选中强调色）。
    UpdateModuleNavIcons();
}

void MainWindow::UpdateModuleNavIcons() {
    const QColor muted(g_activeTokens.textSecondary);
    const QColor accent(g_activeTokens.accent);
    const int px = 22;

    // 以 (mode, state) 多态像素图组装图标：Normal/Off 为次要文字色，Active 与 On 态为强调色，
    // 让未选/悬停/选中三态获得清晰的图标反馈（与文字着色同步）。
    const auto buildNavIcon = [px](const std::function<QIcon(const QColor&)>& glyph,
                                   const QColor& mutedColor, const QColor& accentColor) {
        const QSize size(px, px);
        QIcon icon;
        icon.addPixmap(glyph(mutedColor).pixmap(size), QIcon::Normal, QIcon::Off);
        const QPixmap accentPixmap = glyph(accentColor).pixmap(size);
        icon.addPixmap(accentPixmap, QIcon::Active, QIcon::Off);
        icon.addPixmap(accentPixmap, QIcon::Normal, QIcon::On);
        icon.addPixmap(accentPixmap, QIcon::Active, QIcon::On);
        return icon;
    };

    if (diskModuleButton_ != nullptr) {
        diskModuleButton_->setIcon(buildNavIcon(
            [px](const QColor& c) { return app_icons::drive(px, c); }, muted, accent));
    }
    if (searchModuleButton_ != nullptr) {
        searchModuleButton_->setIcon(buildNavIcon(
            [px](const QColor& c) { return app_icons::search(px, c); }, muted, accent));
    }
    if (cleanupModuleButton_ != nullptr) {
        cleanupModuleButton_->setIcon(buildNavIcon(
            [px](const QColor& c) { return app_icons::trash(px, c); }, muted, accent));
    }
    if (healthModuleButton_ != nullptr) {
        healthModuleButton_->setIcon(buildNavIcon(
            [px](const QColor& c) { return app_icons::refresh(px, c); }, muted, accent));
    }
}

void MainWindow::InitializeEmptyState() {
    // 清空各视图数据，触发空状态遮罩（空状态由各页遮罩统一展示，不再使用占位行）。
    if (directoryTree_ != nullptr) {
        directoryTree_->clear();
    }
    if (directoryModel_ != nullptr) {
        directoryModel_->Clear();
    }
    if (largeFilesModel_ != nullptr) {
        largeFilesModel_->Clear();
    }
    if (searchModel_ != nullptr) {
        searchModel_->Clear();
    }
    if (cleanupTree_ != nullptr) {
        cleanupTree_->clear();
    }
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
    const QEvent::Type type = event->type();
    // 视口尺寸或显隐变化时，让空状态遮罩始终铺满视口，避免缩放或切换标签页后错位。
    if (type == QEvent::Resize || type == QEvent::Show) {
        for (const EmptyOverlayEntry& entry : emptyOverlays_) {
            if (entry.overlay->isVisible() && entry.view->viewport() == watched) {
                entry.overlay->setGeometry(entry.view->viewport()->rect());
            }
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
    if (event == nullptr || event->mimeData() == nullptr || !event->mimeData()->hasUrls()) {
        event->ignore();
        return;
    }
    // 只接受带本地文件/目录 URL 的拖入(从资源管理器拖文件夹进来)。
    for (const QUrl& url : event->mimeData()->urls()) {
        if (url.isLocalFile()) {
            event->acceptProposedAction();
            return;
        }
    }
    event->ignore();
}

void MainWindow::dragMoveEvent(QDragMoveEvent* event) {
    if (event != nullptr && event->mimeData() != nullptr && event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    } else {
        event->ignore();
    }
}

void MainWindow::dropEvent(QDropEvent* event) {
    if (event == nullptr || event->mimeData() == nullptr || !event->mimeData()->hasUrls()) {
        event->ignore();
        return;
    }
    // 扫描进行中不接新拖放(StartScan 本身也拒绝并发扫描,这里提前给出提示更友好)。
    if (scanning_.load()) {
        SetInfoBar(QStringLiteral("扫描中"), 0, 0, QStringLiteral("当前扫描结束后再拖入文件夹"));
        event->ignore();
        return;
    }
    // 取首个本地路径:目录直接扫;文件则扫其所在目录(便于直接拖一个文件过来分析其所在文件夹)。
    QString droppedPath;
    for (const QUrl& url : event->mimeData()->urls()) {
        if (url.isLocalFile()) {
            droppedPath = url.toLocalFile();
            break;
        }
    }
    if (droppedPath.isEmpty()) {
        event->ignore();
        return;
    }
    const QFileInfo droppedInfo(droppedPath);
    QString targetPath = droppedPath;
    if (droppedInfo.isFile()) {
        targetPath = droppedInfo.absolutePath();
    }
    if (targetPath.isEmpty()) {
        event->ignore();
        return;
    }
    event->acceptProposedAction();
    // RescanPath 设定 driveCombo_ 文本并 StartScan;StartScan 会把新路径插入下拉顶部,
    // 下次 SaveUiSettings 自动归入 scan/recentPaths MRU,无需单独维护最近列表。
    RescanPath(targetPath);
}

void MainWindow::InstallEmptyStateOverlays() {
    // 空状态引导图标统一中性灰调，三套皮肤下柔和；磁盘分析页用品牌蓝文件夹做视觉焦点。
    const QColor muted(148, 163, 184);  // slate-400

    AttachEmptyOverlay(directoryTree_, CreateEmptyOverlay(directoryTree_,
        QStringLiteral("选择磁盘并开始扫描"),
        QStringLiteral("扫描完成后这里会展示完整的目录树结构"),
        app_icons::folder(48)));

    AttachEmptyOverlay(directoryView_, CreateEmptyOverlay(directoryView_,
        QStringLiteral("暂无内容"),
        QStringLiteral("在左侧目录树选择一项，即可查看其中的文件与子目录"),
        app_icons::folder(48)));

    AttachEmptyOverlay(largeFilesView_, CreateEmptyOverlay(largeFilesView_,
        QStringLiteral("暂无大文件数据"),
        QStringLiteral("完成扫描后切换到本页，会按需生成大文件列表"),
        app_icons::fileGlyph(48)));

    AttachEmptyOverlay(staleFilesView_, CreateEmptyOverlay(staleFilesView_,
        QStringLiteral("暂无长期未动文件"),
        QStringLiteral("完成扫描后切换到本页，会按修改时间列出长期未变的大文件"),
        app_icons::fileGlyph(48)));

    AttachEmptyOverlay(searchView_, CreateEmptyOverlay(searchView_,
        QStringLiteral("输入关键字开始搜索"),
        QStringLiteral("全系统搜索文件名、扩展名或路径片段"),
        app_icons::search(48, muted)));

    AttachEmptyOverlay(cleanupTree_, CreateEmptyOverlay(cleanupTree_,
        QStringLiteral("点击「扫描垃圾」开始分析"),
        QStringLiteral("扫描后按类别分组，可展开查看具体路径"),
        app_icons::trash(48, muted)));
}

QFrame* MainWindow::CreateEmptyOverlay(QAbstractItemView* view, const QString& title, const QString& hint, const QIcon& icon) {
    auto* overlay = new QFrame(view->viewport());
    overlay->setObjectName(QStringLiteral("EmptyState"));

    auto* outer = new QVBoxLayout(overlay);
    outer->setContentsMargins(32, 32, 32, 32);
    outer->setSpacing(0);

    auto* iconLabel = new QLabel(overlay);
    iconLabel->setObjectName(QStringLiteral("EmptyStateIcon"));
    iconLabel->setPixmap(icon.pixmap(QSize(48, 48)));
    iconLabel->setAlignment(Qt::AlignCenter);

    auto* titleLabel = new QLabel(title, overlay);
    titleLabel->setObjectName(QStringLiteral("EmptyStateTitle"));
    titleLabel->setAlignment(Qt::AlignCenter);

    auto* hintLabel = new QLabel(hint, overlay);
    hintLabel->setObjectName(QStringLiteral("EmptyStateHint"));
    hintLabel->setAlignment(Qt::AlignCenter);
    hintLabel->setWordWrap(true);
    hintLabel->setMaximumWidth(360);

    auto* center = new QVBoxLayout();
    center->setSpacing(8);
    center->setAlignment(Qt::AlignCenter);
    center->addWidget(iconLabel);
    center->addWidget(titleLabel);
    center->addWidget(hintLabel);

    outer->addStretch(1);
    outer->addLayout(center);
    outer->addStretch(1);

    view->viewport()->installEventFilter(this);
    return overlay;
}

void MainWindow::AttachEmptyOverlay(QAbstractItemView* view, QFrame* overlay) {
    emptyOverlays_.push_back({view, overlay});

    // 可见性跟随模型行数：无数据时显示遮罩，有数据时自动隐藏。
    // 绑定到模型的增删 / 重置信号，因此无需在每个填充点手动切换可见性。
    const auto sync = [this, view, overlay]() {
        const QAbstractItemModel* model = view->model();
        const bool empty = (model == nullptr) || (model->rowCount() == 0);
        overlay->setVisible(empty);
        if (empty) {
            overlay->setGeometry(view->viewport()->rect());
            overlay->raise();
        }
    };

    if (auto* model = view->model()) {
        connect(model, &QAbstractItemModel::rowsInserted, this, sync);
        connect(model, &QAbstractItemModel::rowsRemoved, this, sync);
        connect(model, &QAbstractItemModel::modelReset, this, sync);
    }

    sync();
}

void MainWindow::StartScan() {
    if (scanning_.exchange(true)) {
        return;
    }

    const QString rootPath = driveCombo_->currentText().trimmed();
    if (rootPath.isEmpty()) {
        SetInfoBar(QStringLiteral("请选择位置"), 0, 0, QStringLiteral("扫描位置不能为空"));
        scanning_.store(false);
        return;
    }
    if (driveCombo_->findText(rootPath) < 0) {
        driveCombo_->insertItem(0, rootPath);
        driveCombo_->setCurrentIndex(0);
    }

    // E2:扫描期间结构性保证 watcher 无路径(扫完由 HandleScanFinished→ReevaluateWatcher 重新 arm)。
    DisarmWatcher();

    scanStartedAt_ = std::chrono::steady_clock::now();
    lastUiProgressMilliseconds_.store(SteadyMilliseconds());
    const bool elevatedForScan = IsRunningAsAdministrator();
    scanButton_->setEnabled(false);
    stopButton_->setEnabled(true);
    SetBusyState(true, QStringLiteral("扫描中"));
    FlushImmediateFeedback();
    directoryTree_->clear();
    if (directoryModel_ != nullptr) {
        directoryModel_->Clear();
    }
    if (largeFilesModel_ != nullptr) {
        largeFilesModel_->Clear();
    }
    typeStatsTable_->setRowCount(0);
    if (typeStatsDonut_ != nullptr) {
        typeStatsDonut_->Clear(QStringLiteral("正在扫描…"));
    }
    if (ageHistogramWidget_ != nullptr) {
        ageHistogramWidget_->Clear(QStringLiteral("正在扫描…"));
    }
    if (growthAlertFrame_ != nullptr) {
        growthAlertFrame_->setVisible(false);  // 旧告警失效,待本次扫描完成由 EvaluateGrowthAlert 重新评估。
    }
    CancelDuplicateContentScan();
    if (duplicateTree_ != nullptr) {
        duplicateTree_->clear();
    }
    duplicateGroups_.clear();
    if (directoryModel_ != nullptr) {
        directoryModel_->SetRows(QVector<ResultRow>{
            ResultRow{
                QStringLiteral("正在扫描磁盘"),
                QStringLiteral("-"),
                QStringLiteral("加载中"),
                rootPath,
                QString(),
                QString(),
                0,
                0,
                false,
                false,
            }
        });
    }
    if (largeFilesModel_ != nullptr) {
        largeFilesModel_->SetRows(QVector<ResultRow>{
            ResultRow{
                QStringLiteral("等待扫描完成"),
                QStringLiteral("-"),
                QStringLiteral("提示"),
                QStringLiteral("扫描完成后生成大文件列表"),
                QString(),
                QString(),
                0,
                0,
                false,
                false,
            }
        });
    }
    ShowLoadingRow(typeStatsTable_, QStringLiteral("等待扫描完成"), QStringLiteral("扫描完成后生成类型统计"));
    if (duplicateStatusLabel_ != nullptr) {
        duplicateStatusLabel_->setText(QStringLiteral("等待扫描完成 · 扫描后将分析疑似重复文件"));
    }
    if (searchModel_ != nullptr) {
        searchModel_->SetRows(QVector<ResultRow>{
            ResultRow{
                QStringLiteral("空间扫描进行中"),
                QStringLiteral("-"),
                QStringLiteral("提示"),
                QStringLiteral("快速搜索使用全系统索引，不受当前扫描磁盘限制"),
                QString(),
                QString(),
                0,
                0,
                false,
                false,
            }
        });
    }
    if (totalMetricLabel_ != nullptr) {
        totalMetricLabel_->setText(QStringLiteral("--"));
        totalMetricLabel_->setToolTip(QString());
    }
    if (fileMetricLabel_ != nullptr) {
        fileMetricLabel_->setText(QStringLiteral("0"));
    }
    if (directoryMetricLabel_ != nullptr) {
        directoryMetricLabel_->setText(QStringLiteral("0"));
    }
    if (modeMetricLabel_ != nullptr) {
        modeMetricLabel_->setText(QStringLiteral("扫描中"));
    }
    if (treemapWidget_ != nullptr) {
        treemapWidget_->Clear(QStringLiteral("扫描中..."));
    }
    SetInfoBar(QStringLiteral("扫描中"), 0, 0, rootPath);

    std::thread([this, rootPath, elevatedForScan]() {
        auto result = std::make_unique<core::ScanResult>();
        const std::wstring wideRootPath = ToWideString(rootPath);
        bool usedNtfsMft = false;
        bool ntfsValidationFailed = false;
        bool ntfsAttempted = false;
        bool mftScanCancelled = false;
        QString modeText = QStringLiteral("兼容");
        QString modeDetail = QStringLiteral("未使用 NTFS MFT 极速通道");

        try {
            const QString fastScanBlocker = DiagnoseNtfsFastScanBlocker(wideRootPath, elevatedForScan);
            if (fastScanBlocker.isEmpty() && ntfsScanner_.CanScan(wideRootPath)) {
                ntfsAttempted = true;
                QMetaObject::invokeMethod(this, [this, rootPath]() {
                    SetInfoBar(QStringLiteral("NTFS 极速扫描"), 0, 0, rootPath);
                }, Qt::QueuedConnection);
                *result = ntfsScanner_.Scan(wideRootPath);
                mftScanCancelled = ntfsScanner_.IsCancelled();
                ntfsValidationFailed = IsNtfsResultSuspiciouslySmall(*result, wideRootPath);
                usedNtfsMft = !mftScanCancelled && !ntfsValidationFailed;
                if (usedNtfsMft) {
                    modeText = QStringLiteral("NTFS 极速");
                    modeDetail = QStringLiteral("已读取 NTFS MFT；与磁盘已用空间的差额会归入系统保留/无法访问项");
                } else if (mftScanCancelled) {
                    modeText = QStringLiteral("已取消");
                    modeDetail = QStringLiteral("NTFS 极速扫描已被取消");
                } else {
                    modeText = QStringLiteral("兼容（校验回退）");
                    modeDetail = QStringLiteral("MFT 结果明显无效，已回退兼容扫描");
                }
            } else {
                modeText = QStringLiteral("兼容（极速不可用）");
                modeDetail = fastScanBlocker.isEmpty()
                    ? QStringLiteral("当前卷未通过 NTFS MFT 极速扫描可用性检测，已回退兼容扫描")
                    : fastScanBlocker;
            }
        } catch (...) {
            usedNtfsMft = false;
            modeText = QStringLiteral("兼容（MFT失败）");
            modeDetail = QStringLiteral("已满足基本条件，但读取或解析 NTFS MFT 失败，可能是 BitLocker、磁盘驱动、安全软件或特殊卷布局导致；已回退兼容扫描");
        }

        if (!usedNtfsMft) {
            if (mftScanCancelled) {
                // 取消即停止:不再回退兼容扫描,否则会无视用户的取消、用兼容引擎重新开扫。
                QMetaObject::invokeMethod(this, [this, rootPath]() {
                    SetInfoBar(QStringLiteral("已取消"), 0, 0, rootPath);
                }, Qt::QueuedConnection);
            } else {
                if (!ntfsAttempted && modeText == QStringLiteral("兼容")) {
                    modeText = QStringLiteral("兼容");
                    modeDetail = QStringLiteral("未尝试 NTFS MFT，使用兼容扫描");
                }
                QMetaObject::invokeMethod(this, [this, rootPath, modeText, modeDetail]() {
                    SetInfoBar(modeText, 0, 0, modeDetail + QStringLiteral(" · ") + rootPath);
                }, Qt::QueuedConnection);
                *result = scanner_.Scan(wideRootPath, [this](const core::ScanProgress& progress) {
                    const std::int64_t now = SteadyMilliseconds();
                    std::int64_t previous = lastUiProgressMilliseconds_.load();
                    while (now - previous >= 300) {
                        if (lastUiProgressMilliseconds_.compare_exchange_weak(previous, now)) {
                            break;
                        }
                    }
                    if (now - previous < 300) {
                        return;
                    }
                    QMetaObject::invokeMethod(this, [this, progress]() {
                        SetInfoBar(
                            QStringLiteral("扫描中"),
                            progress.filesVisited,
                            progress.directoriesVisited,
                            ToQString(progress.currentPath));
                    }, Qt::QueuedConnection);
                });
            }
        }

        AddReservedSpacePlaceholder(*result, wideRootPath);
        auto* rawResult = result.release();
        QMetaObject::invokeMethod(this, [this, rawResult, usedNtfsMft, modeText, modeDetail]() {
            latestResult_.reset(rawResult);
            lastScanUsedNtfsMft_ = usedNtfsMft;
            lastScanModeText_ = modeText;
            lastScanModeDetail_ = modeDetail;
            scanning_.store(false);
            SetBusyState(false, QString());
            SetInfoBar(QStringLiteral("完成：%1").arg(modeText),
                       latestResult_ ? latestResult_->fileCount : 0,
                       latestResult_ ? latestResult_->directoryCount : 0,
                       modeDetail);
            HandleScanFinished();
            const std::shared_ptr<core::ScanResult> cacheSource = latestResult_;
            std::thread([cacheSource, usedNtfsMft]() {
                if (cacheSource != nullptr) {
                    SaveScanCache(*cacheSource, usedNtfsMft);
                }
            }).detach();
        }, Qt::QueuedConnection);
    }).detach();
}

void MainWindow::RestartAsAdministrator() {
    if (IsRunningAsAdministrator()) {
        SetInfoBar(QStringLiteral("已是极速权限"), 0, 0, QStringLiteral("当前进程已经以管理员权限运行"));
        return;
    }

    const std::wstring executablePath = QCoreApplication::applicationFilePath().toStdWString();
    HINSTANCE result = ShellExecuteW(nullptr, L"runas", executablePath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(result) <= 32) {
        SetInfoBar(QStringLiteral("提权取消"), 0, 0, QStringLiteral("管理员重启已取消或失败"));
        return;
    }

    close();
}

bool MainWindow::IsRunningAsAdministrator() const {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        return false;
    }

    TOKEN_ELEVATION elevation{};
    DWORD returnedBytes = 0;
    const BOOL ok = GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &returnedBytes);
    CloseHandle(token);
    return ok && elevation.TokenIsElevated != 0;
}

void MainWindow::StopScan() {
    scanner_.RequestCancel();
    ntfsScanner_.RequestCancel();
    stopButton_->setEnabled(false);
    SetInfoBar(QStringLiteral("正在停止"), 0, 0, QStringLiteral("等待扫描线程退出"));
}

void MainWindow::HandleScanFinished() {
    scanButton_->setEnabled(true);
    stopButton_->setEnabled(false);
    PopulateScanResult();

    if (latestResult_) {
        SetInfoBar(
            QStringLiteral("完成：%1").arg(lastScanModeText_),
            latestResult_->fileCount,
            latestResult_->directoryCount,
            lastScanModeDetail_.isEmpty() ? (latestResult_->root ? ToQString(latestResult_->root->path) : QString()) : lastScanModeDetail_);
        EvaluateGrowthAlert();
    }

    // E2:按本次扫描耗时刷新 watcher 重扫冷却窗口(防外部搅动导致的稳态重扫循环),再按新根重新 arm。
    {
        const qint64 scanMs = static_cast<qint64>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - scanStartedAt_).count());
        const qint64 clampedScanMs = qBound(qint64(0), scanMs, qint64(3600000));
        watcherCooldownUntilMsec_ = QDateTime::currentMSecsSinceEpoch() + qMax(qint64(30000), clampedScanMs * 2);
    }
    ReevaluateWatcher();
}

void MainWindow::EvaluateGrowthAlert() {
    if (growthAlertFrame_ == nullptr || latestResult_ == nullptr || latestResult_->root == nullptr) {
        return;
    }
    growthAlertFrame_->setVisible(false);  // 默认隐藏,满足阈值才显示。

    const QString rootPath = ToQString(latestResult_->root->path);
    const std::uint64_t currentBytes = latestResult_->root->totalBytes;
    const qint64 nowMsec = QDateTime::currentMSecsSinceEpoch();

    QSettings settings(QStringLiteral("SunnyFan"), QStringLiteral("DiskLens"));
    const QString key = QStringLiteral("growth/") + SanitizeSettingsKey(rootPath);
    const QStringList baseline = settings.value(key).toStringList();

    // 基线格式:["<totalBytes>", "<fileCount>", "<epochMsec>"]。至少需 totalBytes 才能比较。
    if (baseline.size() >= 1) {
        bool okBytes = false;
        const std::uint64_t baselineBytes = static_cast<std::uint64_t>(baseline[0].toULongLong(&okBytes));
        if (okBytes) {
            // 增长量(currentBytes 减基线;若反而缩小则不计入告警)。
            const std::uint64_t growth = (currentBytes > baselineBytes) ? (currentBytes - baselineBytes) : 0;
            // 阈值:绝对下限 100 MiB,且(基线为 0 时仅看绝对;否则需达到基线的 1%),避免在大盘上对微小变化误报。
            constexpr std::uint64_t kFloorBytes = 100ULL * 1024ULL * 1024ULL;
            bool notable = (growth >= kFloorBytes);
            if (notable && baselineBytes > 0) {
                notable = (growth * 100ULL >= baselineBytes);  // growth >= 1% baseline
            }
            if (notable) {
                const QString growthText = ToQString(core::FormatBytes(growth));
                QString detailText;
                if (baselineBytes > 0) {
                    const double pct = (static_cast<double>(growth) / static_cast<double>(baselineBytes)) * 100.0;
                    detailText = QStringLiteral(" · %1%").arg(pct, 0, 'f', 1);
                }
                QString sinceText = QStringLiteral("相比上次扫描");
                if (baseline.size() >= 3) {
                    bool okMsec = false;
                    const qint64 baseMsec = baseline[2].toLongLong(&okMsec);
                    if (okMsec && baseMsec > 0) {
                        sinceText = QStringLiteral("自 %1 以来")
                                        .arg(QDateTime::fromMSecsSinceEpoch(baseMsec)
                                                 .toString(QStringLiteral("yyyy-MM-dd HH:mm")));
                    }
                }
                growthAlertBodyLabel_->setText(
                    sinceText + QStringLiteral(",") + rootPath + QStringLiteral(" 增长 ")
                    + growthText + detailText);
                growthAlertFrame_->setVisible(true);
            }
        }
    }

    // 无论是否告警,都把当前结果写为新基线,使下次扫描比较的是"上一次"。
    settings.setValue(key, QStringList{
        QString::number(static_cast<qulonglong>(currentBytes)),
        QString::number(static_cast<qulonglong>(latestResult_->fileCount)),
        QString::number(nowMsec),
    });
}

void MainWindow::PopulateScanResult() {
    if (!latestResult_ || !latestResult_->root) {
        return;
    }

    RebuildParentIndex();
    ResetDeferredTableStates();
    directoryTree_->clear();
    auto* rootItem = new QTreeWidgetItem(directoryTree_);
    rootItem->setText(0, ToQString(latestResult_->root->name));
    rootItem->setText(1, ToQString(core::FormatBytes(latestResult_->root->totalBytes)));
    rootItem->setIcon(0, app_icons::drive(16));
    rootItem->setData(0, Qt::UserRole, QVariant::fromValue<qulonglong>(reinterpret_cast<qulonglong>(latestResult_->root.get())));
    rootItem->setData(0, Qt::UserRole + 1, true);
    directoryTree_->addTopLevelItem(rootItem);
    PopulateTreeItem(rootItem, *latestResult_->root);
    rootItem->setExpanded(true);

    SelectNodeDetails(*latestResult_->root);

    if (totalMetricLabel_ != nullptr) {
        const QString usedText = ToQString(core::FormatBytes(latestResult_->root->totalBytes));
        const VolumeSpaceInfo volumeSpace = QueryVolumeSpaceInfo(latestResult_->root->path);
        if (volumeSpace.valid && volumeSpace.totalBytes > 0) {
            const QString totalText = ToQString(core::FormatBytes(volumeSpace.totalBytes));
            const QString freeText = ToQString(core::FormatBytes(volumeSpace.freeBytes));
            totalMetricLabel_->setText(QStringLiteral(
                "<span style=\"font-size:15pt;font-weight:800;color:#101828;\">%1</span><br/>"
                "<span style=\"font-size:8pt;font-weight:600;color:#667085;\">总 %2 · 可用 %3</span>")
                                           .arg(usedText, totalText, freeText));
            totalMetricLabel_->setToolTip(QStringLiteral("已占用：%1\n总容量：%2\n可用空间：%3").arg(usedText, totalText, freeText));
        } else {
            totalMetricLabel_->setText(usedText);
            totalMetricLabel_->setToolTip(QStringLiteral("已占用：%1").arg(usedText));
        }
    }
    if (fileMetricLabel_ != nullptr) {
        fileMetricLabel_->setText(QString::number(static_cast<qulonglong>(latestResult_->fileCount)));
    }
    if (directoryMetricLabel_ != nullptr) {
        directoryMetricLabel_->setText(QString::number(static_cast<qulonglong>(latestResult_->directoryCount)));
    }
    if (modeMetricLabel_ != nullptr) {
        modeMetricLabel_->setText(lastScanModeText_);
        modeMetricLabel_->setToolTip(lastScanModeDetail_);
    }
    QTimer::singleShot(0, this, &MainWindow::PopulateCurrentDeferredTab);
}

void MainWindow::ResetDeferredTableStates() {
    largeFilesTableLoaded_ = false;
    typeStatsTableLoaded_ = false;
    duplicateTreeLoaded_ = false;
    staleFilesTableLoaded_ = false;
    ageHistogramLoaded_ = false;

    if (largeFilesModel_ != nullptr) {
        largeFilesModel_->SetRows(QVector<ResultRow>{
            ResultRow{
                QStringLiteral("切换到此页时加载"),
                QStringLiteral("-"),
                QStringLiteral("提示"),
                QStringLiteral("大文件列表会按需生成，避免扫描完成后卡住界面"),
                QString(),
                QString(),
                0,
                0,
                false,
                false,
            }
        });
    }
    if (staleFilesModel_ != nullptr) {
        staleFilesModel_->SetRows(QVector<ResultRow>{
            ResultRow{
                QStringLiteral("切换到此页时加载"),
                QStringLiteral("-"),
                QStringLiteral("提示"),
                QStringLiteral("长期未动文件列表会按需生成，列出最久未修改的大文件"),
                QString(),
                QString(),
                0,
                0,
                false,
                false,
            }
        });
    }
    ShowLoadingRow(typeStatsTable_, QStringLiteral("切换到此页时加载"), QStringLiteral("类型统计会按需生成"));
    if (duplicateStatusLabel_ != nullptr) {
        duplicateStatusLabel_->setText(QStringLiteral("切换到此页时加载 · 重复候选分析会按需生成"));
    }
}

void MainWindow::PopulateCurrentDeferredTab() {
    if (tabs_ == nullptr || latestResult_ == nullptr) {
        return;
    }

    QWidget* current = tabs_->currentWidget();
    if (current == largeFilesView_ && !largeFilesTableLoaded_) {
        largeFilesTableLoaded_ = true;
        PopulateLargeFilesTable();
        return;
    }
    if (current == typeStatsPage_ && !typeStatsTableLoaded_) {
        typeStatsTableLoaded_ = true;
        PopulateTypeStatsTable();
        return;
    }
    if (current == duplicatePage_ && !duplicateTreeLoaded_) {
        duplicateTreeLoaded_ = true;
        PopulateDuplicateTree();
        return;
    }
    if (current == staleFilesView_ && !staleFilesTableLoaded_) {
        staleFilesTableLoaded_ = true;
        PopulateStaleFilesTable();
        return;
    }
    if (current == ageHistogramWidget_ && !ageHistogramLoaded_) {
        ageHistogramLoaded_ = true;
        PopulateAgeHistogram();
    }
}

void MainWindow::PopulateTreeItem(QTreeWidgetItem* parent, const core::ScanNode& node) {
    if (parent == nullptr) {
        return;
    }

    int added = 0;
    for (const auto& child : node.children) {
        if (child->kind != core::NodeKind::Directory) {
            continue;
        }

        auto* item = new QTreeWidgetItem(parent);
        item->setText(0, ToQString(child->name));
        item->setText(1, ToQString(core::FormatBytes(child->totalBytes)));
        item->setIcon(0, app_icons::folder(16));
        item->setData(0, Qt::UserRole, QVariant::fromValue<qulonglong>(reinterpret_cast<qulonglong>(child.get())));
        item->setData(0, Qt::UserRole + 1, false);
        const bool hasDirectoryChildren = std::any_of(child->children.begin(), child->children.end(), [](const auto& grandChild) {
            return grandChild->kind == core::NodeKind::Directory;
        });
        if (hasDirectoryChildren) {
            auto* placeholder = new QTreeWidgetItem(item);
            placeholder->setText(0, QStringLiteral("加载中..."));
        }

        ++added;
        if (added >= 500) {
            break;
        }
    }
}

void MainWindow::PopulateDirectoryTable(const core::ScanNode& node) {
    currentDirectoryNode_ = &node;
    QVector<ResultRow> rows;
    rows.reserve(static_cast<int>(std::min<std::size_t>(node.children.size() + 1, 3001)));

    if (latestResult_ && latestResult_->root && &node != latestResult_->root.get()) {
        rows.push_back(ResultRow{
            QStringLiteral(".."),
            QString(),
            QStringLiteral("上级"),
            QStringLiteral("返回上级目录"),
            QString(),
            QString(),
            0,
            0,
            true,
            true,
        });
    }

    int visibleRows = 0;
    constexpr int maxDirectoryRows = 3000;
    for (const auto& child : node.children) {
        if (!MatchesFilter(*child)) {
            continue;
        }

        const bool isDirectory = child->kind == core::NodeKind::Directory;
        const QString name = ToQString(child->name);
        const QString path = ToQString(child->path);
        rows.push_back(ResultRow{
            name,
            ToQString(core::FormatBytes(child->totalBytes)),
            isDirectory ? QStringLiteral("目录") : QStringLiteral("文件"),
            ContainingDirectoryForDisplay(path, isDirectory),
            path,
            MakeSearchKey(name, path),
            child->totalBytes,
            reinterpret_cast<quint64>(child.get()),
            isDirectory,
            false,
            FormatModifiedDate(child->lastModifiedMsec),
            child->lastModifiedMsec,
        });
        ++visibleRows;
        if (visibleRows >= maxDirectoryRows) {
            rows.push_back(ResultRow{
                QStringLiteral("已显示前 %1 项").arg(maxDirectoryRows),
                QStringLiteral("-"),
                QStringLiteral("提示"),
                QStringLiteral("当前目录项目很多，为保证流畅已截断显示；可使用筛选或进入子目录继续查看"),
                QString(),
                QString(),
                0,
                0,
                false,
                false,
            });
            break;
        }
    }

    if (directoryModel_ != nullptr) {
        directoryModel_->SetRows(std::move(rows));
    }

    if (directoryView_ != nullptr) {
        directoryView_->setCurrentIndex(directoryModel_ != nullptr ? directoryModel_->index(0, 0) : QModelIndex());
    }
}

void MainWindow::AddDirectoryTableNodeRow(const core::ScanNode& node) {
    const bool isDirectory = node.kind == core::NodeKind::Directory;

    const int row = directoryTable_->rowCount();
    directoryTable_->insertRow(row);

    auto* nameItem = new QTableWidgetItem(ToQString(node.name));
    nameItem->setIcon(isDirectory ? app_icons::folder(16) : app_icons::fileGlyph(16));
    auto* sizeItem = new QTableWidgetItem(ToQString(core::FormatBytes(node.totalBytes)));
    auto* typeItem = new QTableWidgetItem(isDirectory ? QStringLiteral("目录") : QStringLiteral("文件"));
    const QString fullPath = ToQString(node.path);
    const QString displayPath = ContainingDirectoryForDisplay(fullPath, isDirectory);
    auto* pathItem = new QTableWidgetItem(displayPath);
    nameItem->setData(Qt::UserRole, QVariant::fromValue<qulonglong>(reinterpret_cast<qulonglong>(&node)));
    nameItem->setData(Qt::UserRole + 1, isDirectory);
    nameItem->setToolTip(ToQString(node.name));
    sizeItem->setToolTip(sizeItem->text());
    typeItem->setToolTip(typeItem->text());
    pathItem->setToolTip(fullPath);
    pathItem->setData(Qt::UserRole, fullPath);

    directoryTable_->setItem(row, 0, nameItem);
    directoryTable_->setItem(row, 1, sizeItem);
    directoryTable_->setItem(row, 2, typeItem);
    directoryTable_->setItem(row, 3, pathItem);
}

void MainWindow::PopulateLargeFilesTable() {
    if (!latestResult_ || !latestResult_->root) {
        return;
    }

    if (largeFilesModel_ != nullptr) {
        largeFilesModel_->SetRows(QVector<ResultRow>{
            ResultRow{
                QStringLiteral("正在生成大文件列表"),
                QStringLiteral("-"),
                QStringLiteral("加载中"),
                QStringLiteral("后台筛选最大文件，不阻塞界面操作"),
                QString(),
                QString(),
                0,
                0,
                false,
                false,
            }
        });
    }
    const std::shared_ptr<core::ScanResult> snapshot = latestResult_;
    const core::ScanNode* root = snapshot->root.get();
    const QString filterText = filterEdit_ != nullptr ? filterEdit_->text().trimmed() : QString();
    std::thread([this, snapshot, root, filterText]() {
        constexpr std::size_t maxLargeFiles = 5000;
        auto smallestFirst = [](const core::ScanNode* left, const core::ScanNode* right) {
            return left->totalBytes > right->totalBytes;
        };
        std::priority_queue<const core::ScanNode*, std::vector<const core::ScanNode*>, decltype(smallestFirst)> topFiles(smallestFirst);

        std::vector<const core::ScanNode*> stack;
        stack.reserve(4096);
        stack.push_back(root);
        while (!stack.empty()) {
            const core::ScanNode* node = stack.back();
            stack.pop_back();
            if (node == nullptr) {
                continue;
            }

            if (node->kind == core::NodeKind::File) {
                if (!filterText.isEmpty()) {
                    const QString name = ToQString(node->name);
                    const QString path = ToQString(node->path);
                    if (!name.contains(filterText, Qt::CaseInsensitive) && !path.contains(filterText, Qt::CaseInsensitive)) {
                        continue;
                    }
                }

                if (topFiles.size() < maxLargeFiles) {
                    topFiles.push(node);
                } else if (!topFiles.empty() && node->totalBytes > topFiles.top()->totalBytes) {
                    topFiles.pop();
                    topFiles.push(node);
                }
                continue;
            }

            for (const auto& child : node->children) {
                stack.push_back(child.get());
            }
        }

        std::vector<const core::ScanNode*> files;
        files.reserve(topFiles.size());
        while (!topFiles.empty()) {
            files.push_back(topFiles.top());
            topFiles.pop();
        }
        std::sort(files.begin(), files.end(), [](const core::ScanNode* left, const core::ScanNode* right) {
            return left->totalBytes > right->totalBytes;
        });

        auto rows = std::make_shared<QVector<ResultRow>>();
        rows->reserve(static_cast<int>(files.size()));
        for (const core::ScanNode* file : files) {
            const QString name = ToQString(file->name);
            const QString path = ToQString(file->path);
            rows->push_back(ResultRow{
                name,
                ToQString(core::FormatBytes(file->totalBytes)),
                QStringLiteral("文件"),
                ContainingDirectoryForDisplay(path, false),
                path,
                MakeSearchKey(name, path),
                file->totalBytes,
                reinterpret_cast<quint64>(file),
                false,
                false,
                FormatModifiedDate(file->lastModifiedMsec),
                file->lastModifiedMsec,
            });
        }

        QMetaObject::invokeMethod(this, [this, snapshot, root, rows]() {
            if (latestResult_ != snapshot || !latestResult_ || latestResult_->root.get() != root) {
                return;
            }
            if (largeFilesModel_ != nullptr) {
                largeFilesModel_->SetRows(std::move(*rows));
            }
        }, Qt::QueuedConnection);
    }).detach();
}

void MainWindow::PopulateStaleFilesTable() {
    if (!latestResult_ || !latestResult_->root) {
        return;
    }

    if (staleFilesModel_ != nullptr) {
        staleFilesModel_->SetRows(QVector<ResultRow>{
            ResultRow{
                QStringLiteral("正在生成长期未动文件列表"),
                QStringLiteral("-"),
                QStringLiteral("加载中"),
                QStringLiteral("后台筛选最久未修改的大文件，不阻塞界面操作"),
                QString(),
                QString(),
                0,
                0,
                false,
                false,
            }
        });
    }
    const std::shared_ptr<core::ScanResult> snapshot = latestResult_;
    const core::ScanNode* root = snapshot->root.get();
    const QString filterText = filterEdit_ != nullptr ? filterEdit_->text().trimmed() : QString();
    std::thread([this, snapshot, root, filterText]() {
        constexpr std::size_t maxStaleFiles = 2000;
        constexpr std::uint64_t minFileBytes = 1024ULL * 1024ULL;  // 仅统计 ≥1 MiB 的文件，避免系统小文件刷屏。
        // 最大堆：堆顶 mtime 最大；满后淘汰最大的，最终保留最老的 N 个。
        auto newestFirst = [](const core::ScanNode* left, const core::ScanNode* right) {
            return left->lastModifiedMsec < right->lastModifiedMsec;
        };
        std::priority_queue<const core::ScanNode*, std::vector<const core::ScanNode*>, decltype(newestFirst)> topFiles(newestFirst);

        std::vector<const core::ScanNode*> stack;
        stack.reserve(4096);
        stack.push_back(root);
        while (!stack.empty()) {
            const core::ScanNode* node = stack.back();
            stack.pop_back();
            if (node == nullptr) {
                continue;
            }

            if (node->kind == core::NodeKind::File) {
                // 仅纳入有有效修改时间、且达到最小大小的文件。
                if (node->lastModifiedMsec <= 0 || node->ownBytes < minFileBytes) {
                    continue;
                }
                if (!filterText.isEmpty()) {
                    const QString name = ToQString(node->name);
                    const QString path = ToQString(node->path);
                    if (!name.contains(filterText, Qt::CaseInsensitive) && !path.contains(filterText, Qt::CaseInsensitive)) {
                        continue;
                    }
                }

                if (topFiles.size() < maxStaleFiles) {
                    topFiles.push(node);
                } else if (!topFiles.empty() && node->lastModifiedMsec < topFiles.top()->lastModifiedMsec) {
                    topFiles.pop();
                    topFiles.push(node);
                }
                continue;
            }

            for (const auto& child : node->children) {
                stack.push_back(child.get());
            }
        }

        std::vector<const core::ScanNode*> files;
        files.reserve(topFiles.size());
        while (!topFiles.empty()) {
            files.push_back(topFiles.top());
            topFiles.pop();
        }
        // 升序：最久未修改（mtime 最小）的排在最前。
        std::sort(files.begin(), files.end(), [](const core::ScanNode* left, const core::ScanNode* right) {
            return left->lastModifiedMsec < right->lastModifiedMsec;
        });

        auto rows = std::make_shared<QVector<ResultRow>>();
        rows->reserve(static_cast<int>(files.size()));
        for (const core::ScanNode* file : files) {
            const QString name = ToQString(file->name);
            const QString path = ToQString(file->path);
            rows->push_back(ResultRow{
                name,
                ToQString(core::FormatBytes(file->totalBytes)),
                QStringLiteral("文件"),
                ContainingDirectoryForDisplay(path, false),
                path,
                MakeSearchKey(name, path),
                file->totalBytes,
                reinterpret_cast<quint64>(file),
                false,
                false,
                FormatModifiedDate(file->lastModifiedMsec),
                file->lastModifiedMsec,
            });
        }

        QMetaObject::invokeMethod(this, [this, snapshot, root, rows]() {
            if (latestResult_ != snapshot || !latestResult_ || latestResult_->root.get() != root) {
                return;
            }
            if (staleFilesModel_ != nullptr) {
                if (rows->isEmpty()) {
                    // 无结果时清空模型，由空状态遮罩统一提示（与大文件页一致）。
                    staleFilesModel_->Clear();
                } else {
                    staleFilesModel_->SetRows(std::move(*rows));
                }
            }
        }, Qt::QueuedConnection);
    }).detach();
}

void MainWindow::PopulateTypeStatsTable() {
    BeginTableUpdate(typeStatsTable_);
    typeStatsTable_->setRowCount(0);
    if (!latestResult_) {
        EndTableUpdate(typeStatsTable_);
        if (typeStatsDonut_ != nullptr) {
            typeStatsDonut_->Clear(QStringLiteral("暂无扫描结果"));
        }
        return;
    }

    std::vector<core::ExtensionSummary> summaries;
    for (const auto& entry : latestResult_->extensions) {
        summaries.push_back(entry.second);
    }

    std::sort(summaries.begin(), summaries.end(), [](const auto& left, const auto& right) {
        return left.totalBytes > right.totalBytes;
    });

    for (const auto& summary : summaries) {
        AddTableRow(
            typeStatsTable_,
            ToQString(summary.extension),
            ToQString(core::FormatBytes(summary.totalBytes)),
            QStringLiteral("%1 个文件").arg(static_cast<qulonglong>(summary.fileCount)),
            QStringLiteral("按扩展名汇总，不对应单个文件路径"));
    }
    EndTableUpdate(typeStatsTable_);

    // 同步右侧分类环形图:把逐扩展名统计卷成 8 类 + 其他。这是唯一的刷新点——
    // PopulateTypeStatsTable 已在“切换到类型统计页”与“扫描完成懒加载”两条路径上被调用。
    if (typeStatsDonut_ != nullptr) {
        typeStatsDonut_->SetCategories(core::ComputeExtensionCategories(*latestResult_));
    }
}

void MainWindow::PopulateAgeHistogram() {
    if (ageHistogramWidget_ == nullptr) {
        return;
    }
    if (!latestResult_) {
        ageHistogramWidget_->Clear(QStringLiteral("暂无扫描结果"));
        return;
    }
    // nowMsec 取扫描呈现时刻;用 ScanNode.lastModifiedMsec(同源 Unix epoch 毫秒)做差得到年龄天数。
    const std::int64_t nowMsec = static_cast<std::int64_t>(QDateTime::currentMSecsSinceEpoch());
    ageHistogramWidget_->SetBuckets(core::ComputeAgeBuckets(*latestResult_, nowMsec));
}

void MainWindow::PopulateDuplicateTree() {
    if (!latestResult_ || !latestResult_->root || duplicateTree_ == nullptr) {
        return;
    }

    if (duplicateStatusLabel_ != nullptr) {
        duplicateStatusLabel_->setText(QStringLiteral("正在按同名同大小分组……"));
    }
    duplicateTree_->clear();
    duplicateGroups_.clear();

    const std::shared_ptr<core::ScanResult> snapshot = latestResult_;
    const core::ScanNode* root = snapshot->root.get();
    std::thread([this, snapshot, root]() {
        std::vector<const core::ScanNode*> files;
        CollectFiles(*root, files);

        std::map<std::wstring, std::vector<const core::ScanNode*>> groups;
        for (const core::ScanNode* file : files) {
            if (file == nullptr || file->totalBytes == 0) {
                continue;
            }
            groups[file->name + L"|" + std::to_wstring(file->totalBytes)].push_back(file);
        }

        std::vector<DuplicateGroupUi> built;
        for (auto& [key, members] : groups) {
            (void)key;
            if (members.size() < 2) {
                continue;
            }
            DuplicateGroupUi group;
            group.bytes = members.front()->totalBytes;
            group.contentConfirmed = false;
            for (const core::ScanNode* node : members) {
                const QString path = ToQString(node->path);
                DuplicateMemberUi member;
                member.path = path;
                member.bytes = node->totalBytes;
                member.name = ToQString(node->name);
                const QFileInfo info(path);
                member.modifiedText = info.exists()
                    ? FormatModifiedDate(QDateTime(info.lastModified()).toMSecsSinceEpoch())
                    : QStringLiteral("—");
                group.members.push_back(std::move(member));
            }
            std::sort(group.members.begin(), group.members.end(),
                      [](const DuplicateMemberUi& left, const DuplicateMemberUi& right) { return left.path < right.path; });
            built.push_back(std::move(group));
        }
        std::sort(built.begin(), built.end(),
                  [](const DuplicateGroupUi& left, const DuplicateGroupUi& right) { return left.bytes > right.bytes; });

        QMetaObject::invokeMethod(this, [this, snapshot, built = std::move(built)]() mutable {
            if (latestResult_ != snapshot) {
                return;
            }
            duplicateGroups_ = std::move(built);
            RebuildDuplicateTreeFromModel();
            if (duplicateStatusLabel_ != nullptr) {
                duplicateStatusLabel_->setText(QStringLiteral("快速视图(同名同大小):%1 组候选。是否真正重复请点「内容深度校验」用 SHA-256 确认。")
                    .arg(static_cast<qulonglong>(duplicateGroups_.size())));
            }
        }, Qt::QueuedConnection);
    }).detach();
}

void MainWindow::StartDuplicateContentScan() {
    if (duplicateHashing_.load() || duplicateTree_ == nullptr || !latestResult_ || !latestResult_->root) {
        return;
    }

    duplicateHashCancel_.store(false);
    duplicateHashing_.store(true);
    if (duplicateDeepScanButton_ != nullptr) {
        duplicateDeepScanButton_->setEnabled(false);
    }
    if (duplicateQuickButton_ != nullptr) {
        duplicateQuickButton_->setEnabled(false);
    }
    if (duplicateCancelButton_ != nullptr) {
        duplicateCancelButton_->show();
    }
    if (duplicateStatusLabel_ != nullptr) {
        duplicateStatusLabel_->setText(QStringLiteral("正在校验文件内容(SHA-256),大文件需要更多时间……"));
    }

    const std::shared_ptr<core::ScanResult> snapshot = latestResult_;
    const core::ScanNode* root = snapshot->root.get();
    std::thread([this, snapshot, root]() {
        std::vector<const core::ScanNode*> files;
        CollectFiles(*root, files);
        std::vector<std::pair<std::wstring, std::uint64_t>> inputs;
        inputs.reserve(files.size());
        for (const core::ScanNode* node : files) {
            if (node != nullptr && node->totalBytes > 0) {
                inputs.emplace_back(node->path, node->totalBytes);
            }
        }

        const core::FileHasher hasher;
        std::vector<core::DuplicateGroup> groups = hasher.FindContentDuplicates(
            inputs,
            [this](const core::DuplicateHashProgress& progress) {
                QMetaObject::invokeMethod(this, [this, progress]() {
                    if (duplicateStatusLabel_ != nullptr && duplicateHashing_.load()) {
                        duplicateStatusLabel_->setText(
                            QStringLiteral("正在校验内容:已处理 %1 / %2 个文件 · 已读 %3 / %4")
                                .arg(static_cast<qulonglong>(progress.filesHashed))
                                .arg(static_cast<qulonglong>(progress.hashCandidates))
                                .arg(ToQString(core::FormatBytes(progress.bytesHashed)))
                                .arg(ToQString(core::FormatBytes(progress.bytesTotal))));
                    }
                }, Qt::QueuedConnection);
            },
            [this]() { return duplicateHashCancel_.load(); });

        QMetaObject::invokeMethod(this, [this, snapshot, groups = std::move(groups)]() mutable {
            if (latestResult_ != snapshot) {
                // 扫描已更新:丢弃结果,但仍复位校验态。
                duplicateHashing_.store(false);
                duplicateHashCancel_.store(false);
                if (duplicateDeepScanButton_ != nullptr) duplicateDeepScanButton_->setEnabled(true);
                if (duplicateQuickButton_ != nullptr) duplicateQuickButton_->setEnabled(true);
                if (duplicateCancelButton_ != nullptr) duplicateCancelButton_->hide();
                return;
            }
            PopulateDuplicateTreeFromContent(groups);
        }, Qt::QueuedConnection);
    }).detach();
}

void MainWindow::CancelDuplicateContentScan() {
    if (!duplicateHashing_.load()) {
        return;
    }
    duplicateHashCancel_.store(true);
    if (duplicateStatusLabel_ != nullptr) {
        duplicateStatusLabel_->setText(QStringLiteral("正在取消内容校验……"));
    }
}

void MainWindow::PopulateDuplicateTreeFromContent(const std::vector<core::DuplicateGroup>& groups) {
    const bool wasCancelled = duplicateHashCancel_.load();
    duplicateHashing_.store(false);
    duplicateHashCancel_.store(false);
    if (duplicateDeepScanButton_ != nullptr) {
        duplicateDeepScanButton_->setEnabled(true);
    }
    if (duplicateQuickButton_ != nullptr) {
        duplicateQuickButton_->setEnabled(true);
    }
    if (duplicateCancelButton_ != nullptr) {
        duplicateCancelButton_->hide();
    }

    duplicateGroups_.clear();
    duplicateGroups_.reserve(groups.size());
    std::uint64_t reclaimable = 0;
    for (const core::DuplicateGroup& group : groups) {
        DuplicateGroupUi ui;
        ui.bytes = group.bytes;
        ui.contentConfirmed = true;
        for (const core::DuplicateMember& member : group.members) {
            const QString path = ToQString(member.path);
            DuplicateMemberUi m;
            m.path = path;
            m.bytes = member.bytes;
            const QFileInfo info(path);
            m.name = info.fileName().isEmpty() ? path : info.fileName();
            m.modifiedText = info.exists()
                ? FormatModifiedDate(QDateTime(info.lastModified()).toMSecsSinceEpoch())
                : QStringLiteral("—");
            ui.members.push_back(std::move(m));
        }
        if (ui.members.size() >= 2) {
            reclaimable += ui.bytes * static_cast<std::uint64_t>(ui.members.size() - 1);
            duplicateGroups_.push_back(std::move(ui));
        }
    }

    RebuildDuplicateTreeFromModel();

    if (duplicateStatusLabel_ != nullptr) {
        if (wasCancelled) {
            duplicateStatusLabel_->setText(QStringLiteral("已取消内容校验:显示 %1 组已确认的重复(部分结果)。")
                .arg(static_cast<qulonglong>(duplicateGroups_.size())));
        } else {
            duplicateStatusLabel_->setText(QStringLiteral("内容深度校验完成:%1 组内容完全相同 · 可回收约 %2(已用 SHA-256 逐字节确认)。")
                .arg(static_cast<qulonglong>(duplicateGroups_.size()))
                .arg(ToQString(core::FormatBytes(reclaimable))));
        }
    }
}

void MainWindow::RebuildDuplicateTreeFromModel() {
    if (duplicateTree_ == nullptr) {
        return;
    }
    duplicateTree_->blockSignals(true);
    duplicateTree_->clear();

    for (const DuplicateGroupUi& group : duplicateGroups_) {
        if (group.members.size() < 2) {
            continue;
        }
        const std::uint64_t reclaimable = group.bytes * static_cast<std::uint64_t>(group.members.size() - 1);
        auto* groupItem = new QTreeWidgetItem(duplicateTree_);
        const QString headLabel = group.contentConfirmed
            ? QStringLiteral("内容相同 · %1 项 · 哈希确认").arg(static_cast<qulonglong>(group.members.size()))
            : QStringLiteral("同名同大小 · %1 项").arg(static_cast<qulonglong>(group.members.size()));
        groupItem->setText(0, headLabel);
        groupItem->setText(1, ToQString(core::FormatBytes(group.bytes)));
        groupItem->setText(2, group.contentConfirmed ? QStringLiteral("可回收 ") + ToQString(core::FormatBytes(reclaimable)) : QStringLiteral("待确认"));
        groupItem->setText(3, QString());
        groupItem->setCheckState(0, Qt::Unchecked);
        QFont groupFont = groupItem->font(0);
        groupFont.setBold(true);
        groupItem->setFont(0, groupFont);

        for (const DuplicateMemberUi& member : group.members) {
            auto* child = new QTreeWidgetItem(groupItem);
            child->setText(0, member.name);
            child->setText(1, ToQString(core::FormatBytes(member.bytes)));
            child->setText(2, member.modifiedText);
            child->setText(3, QDir::toNativeSeparators(member.path));
            child->setData(0, Qt::UserRole + 1, member.path);
            child->setData(0, Qt::UserRole + 2, static_cast<qulonglong>(member.bytes));
            child->setCheckState(0, Qt::Unchecked);
        }
    }

    duplicateTree_->blockSignals(false);
    UpdateDuplicateSelectedSummary();
}

void MainWindow::UpdateDuplicateSelectedSummary() {
    if (duplicateTree_ == nullptr || duplicateSelectedLabel_ == nullptr) {
        return;
    }
    std::uint64_t selectedBytes = 0;
    std::size_t selectedCount = 0;
    for (int i = 0; i < duplicateTree_->topLevelItemCount(); ++i) {
        QTreeWidgetItem* groupItem = duplicateTree_->topLevelItem(i);
        if (groupItem == nullptr) {
            continue;
        }
        for (int c = 0; c < groupItem->childCount(); ++c) {
            QTreeWidgetItem* child = groupItem->child(c);
            if (child != nullptr && child->checkState(0) == Qt::Checked) {
                selectedBytes += child->data(0, Qt::UserRole + 2).toULongLong();
                ++selectedCount;
            }
        }
    }
    duplicateSelectedLabel_->setText(QStringLiteral("已勾选 %1 项 · %2")
        .arg(static_cast<qulonglong>(selectedCount))
        .arg(ToQString(core::FormatBytes(selectedBytes))));
    if (duplicateDeleteButton_ != nullptr) {
        duplicateDeleteButton_->setEnabled(selectedCount > 0);
    }
}

void MainWindow::SetDuplicateCheckedMode(const QString& mode) {
    if (duplicateTree_ == nullptr) {
        return;
    }
    duplicateTree_->blockSignals(true);
    for (int i = 0; i < duplicateTree_->topLevelItemCount(); ++i) {
        QTreeWidgetItem* groupItem = duplicateTree_->topLevelItem(i);
        if (groupItem == nullptr) {
            continue;
        }
        if (mode == QStringLiteral("none")) {
            for (int c = 0; c < groupItem->childCount(); ++c) {
                if (groupItem->child(c) != nullptr) {
                    groupItem->child(c)->setCheckState(0, Qt::Unchecked);
                }
            }
            groupItem->setCheckState(0, Qt::Unchecked);
        } else {
            // all / keepFirst:勾选除首项外的全部子项(每组至少保留 1 个副本)。
            for (int c = 0; c < groupItem->childCount(); ++c) {
                if (groupItem->child(c) != nullptr) {
                    groupItem->child(c)->setCheckState(0, c == 0 ? Qt::Unchecked : Qt::Checked);
                }
            }
            groupItem->setCheckState(0, Qt::Checked);
        }
    }
    duplicateTree_->blockSignals(false);
    UpdateDuplicateSelectedSummary();
}

void MainWindow::ShowDuplicateContextMenu(const QPoint& position) {
    if (duplicateTree_ == nullptr) {
        return;
    }
    QTreeWidgetItem* item = duplicateTree_->itemAt(position);
    QMenu menu(this);
    if (item != nullptr && item->childCount() > 0) {
        menu.addAction(QStringLiteral("展开/折叠"), this, [item]() {
            if (item != nullptr) {
                item->setExpanded(!item->isExpanded());
            }
        });
        menu.addSeparator();
        menu.addAction(QStringLiteral("导出当前列表"), this, &MainWindow::ExportCurrentTable);
        menu.exec(duplicateTree_->viewport()->mapToGlobal(position));
        return;
    }
    if (item != nullptr) {
        duplicateTree_->setCurrentItem(item);
    }
    const QString path = item != nullptr ? item->data(0, Qt::UserRole + 1).toString() : QString();
    const std::uint64_t bytes = item != nullptr ? item->data(0, Qt::UserRole + 2).toULongLong() : 0;
    AddPathActions(menu, path, true, bytes, true);
    menu.addSeparator();
    QAction* toggleAction = menu.addAction(QStringLiteral("勾选/取消勾选"), this, [item]() {
        if (item != nullptr) {
            item->setCheckState(0, item->checkState(0) == Qt::Checked ? Qt::Unchecked : Qt::Checked);
        }
    });
    toggleAction->setEnabled(item != nullptr);
    menu.addSeparator();
    menu.addAction(QStringLiteral("导出当前列表"), this, &MainWindow::ExportCurrentTable);
    menu.exec(duplicateTree_->viewport()->mapToGlobal(position));
}

void MainWindow::DeleteSelectedDuplicateItems() {
    if (duplicateTree_ == nullptr) {
        return;
    }

    // 收集每个组里勾选的子项路径;每组至少保留 1 个副本(首项),避免删光所有副本。
    struct PendingDelete {
        QString path;
        std::uint64_t bytes = 0;
    };
    std::vector<PendingDelete> pending;
    QStringList protectedList;
    QStringList missingList;
    for (int i = 0; i < duplicateTree_->topLevelItemCount(); ++i) {
        QTreeWidgetItem* groupItem = duplicateTree_->topLevelItem(i);
        if (groupItem == nullptr) {
            continue;
        }
        std::vector<int> checkedChildren;
        for (int c = 0; c < groupItem->childCount(); ++c) {
            QTreeWidgetItem* child = groupItem->child(c);
            if (child != nullptr && child->checkState(0) == Qt::Checked) {
                checkedChildren.push_back(c);
            }
        }
        if (checkedChildren.empty()) {
            continue;
        }
        // 若整组都被勾选,保留首项。
        if (static_cast<int>(checkedChildren.size()) >= groupItem->childCount() && !checkedChildren.empty()) {
            checkedChildren.erase(checkedChildren.begin());
        }
        for (int c : checkedChildren) {
            QTreeWidgetItem* child = groupItem->child(c);
            if (child == nullptr) {
                continue;
            }
            const QString path = child->data(0, Qt::UserRole + 1).toString();
            const std::uint64_t bytes = child->data(0, Qt::UserRole + 2).toULongLong();
            if (path.isEmpty()) {
                continue;
            }
            if (IsProtectedManualDeletePath(path)) {
                protectedList << QFileInfo(path).fileName();
                continue;
            }
            if (!QFileInfo::exists(path)) {
                missingList << QFileInfo(path).fileName();
                continue;
            }
            pending.push_back(PendingDelete{path, bytes});
        }
    }

    if (pending.empty()) {
        ShowAppMessageBox(this, QMessageBox::Information, QStringLiteral("重复去重"),
            QStringLiteral("没有可删除的项目(可能已勾选受保护位置或文件已不存在)。"));
        return;
    }

    std::uint64_t pendingBytes = 0;
    for (const PendingDelete& item : pending) {
        pendingBytes += item.bytes;
    }
    const bool permanentDelete = duplicatePermanentCheckBox_ != nullptr && duplicatePermanentCheckBox_->isChecked();
    const QString modeText = permanentDelete
        ? QStringLiteral("永久删除:不会进入回收站,无法还原")
        : QStringLiteral("移入回收站:可从回收站手动还原");
    const QMessageBox::StandardButton choice = ShowAppMessageBox(
        this,
        QMessageBox::Question,
        permanentDelete ? QStringLiteral("确认永久删除") : QStringLiteral("确认移入回收站"),
        QStringLiteral("将%1 %2 个重复文件,共 %3。\n%4。\n\n每组会至少保留 1 个副本。是否继续?")
            .arg(permanentDelete ? QStringLiteral("永久删除") : QStringLiteral("移入回收站"))
            .arg(static_cast<qulonglong>(pending.size()))
            .arg(ToQString(core::FormatBytes(pendingBytes)))
            .arg(modeText),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);
    if (choice != QMessageBox::Yes) {
        return;
    }

    int cleaned = 0;
    SetBusyState(true, permanentDelete ? QStringLiteral("永久删除重复文件") : QStringLiteral("移入回收站"));
    for (const PendingDelete& item : pending) {
        if (permanentDelete ? PermanentlyDeletePath(item.path) : RecyclePath(item.path)) {
            ++cleaned;
        }
    }
    SetBusyState(false, QString());

    // 刷新:剔除已不存在的成员,丢掉成员不足 2 的组。
    for (auto it = duplicateGroups_.begin(); it != duplicateGroups_.end();) {
        auto& members = it->members;
        members.erase(std::remove_if(members.begin(), members.end(),
                          [](const DuplicateMemberUi& member) { return !QFileInfo::exists(member.path); }),
                      members.end());
        if (members.size() < 2) {
            it = duplicateGroups_.erase(it);
        } else {
            ++it;
        }
    }
    RebuildDuplicateTreeFromModel();

    const QString note = (!protectedList.isEmpty() ? QStringLiteral("  其中 %1 个受保护已跳过").arg(protectedList.size()) : QString());
    if (duplicateStatusLabel_ != nullptr) {
        duplicateStatusLabel_->setText(QStringLiteral("已处理 %1 / %2 个重复文件。%3")
            .arg(cleaned)
            .arg(static_cast<qulonglong>(pending.size()))
            .arg(note));
    }
}

QWidget* MainWindow::CreateHealthTab() {
    auto* page = new QWidget(this);
    healthPage_ = page;
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    auto* hero = new QFrame(page);
    hero->setObjectName(QStringLiteral("CleanupHero"));
    auto* heroLayout = new QVBoxLayout(hero);
    heroLayout->setContentsMargins(16, 12, 16, 12);
    heroLayout->setSpacing(8);

    auto* titleLabel = new QLabel(QStringLiteral("磁盘健康监测"), hero);
    titleLabel->setObjectName(QStringLiteral("CleanupTitle"));
    healthStatusLabel_ = new QLabel(
        QStringLiteral("读取每块物理盘的 SMART / NVMe 健康日志,查看温度、通电时长与寿命评估。读取物理盘需要管理员权限。"),
        hero);
    healthStatusLabel_->setObjectName(QStringLiteral("CleanupStatus"));
    healthStatusLabel_->setWordWrap(true);

    auto* titleBlock = new QVBoxLayout();
    titleBlock->setSpacing(2);
    titleBlock->addWidget(titleLabel);
    titleBlock->addWidget(healthStatusLabel_);
    heroLayout->addLayout(titleBlock);

    // 卡片滚动容器:每盘一卡,竖向排列;卡片超出视口时纵向滚动,横向不滚动。
    healthScroll_ = new QScrollArea(page);
    healthScroll_->setObjectName(QStringLiteral("HealthScroll"));
    healthScroll_->setWidgetResizable(true);
    healthScroll_->setFrameShape(QFrame::NoFrame);
    healthScroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    healthCardsHost_ = new QWidget();
    healthCardsHost_->setObjectName(QStringLiteral("HealthCardsHost"));
    healthCardsLayout_ = new QVBoxLayout(healthCardsHost_);
    healthCardsLayout_->setContentsMargins(g_activeTokens.spaceLg, g_activeTokens.spaceMd, g_activeTokens.spaceLg, g_activeTokens.spaceMd);
    healthCardsLayout_->setSpacing(g_activeTokens.spaceMd);
    healthEmptyHint_ = new QLabel(
        QStringLiteral("正在读取各物理盘的 SMART / NVMe 健康信息……\n读取物理盘需要管理员权限。"),
        healthCardsHost_);
    healthEmptyHint_->setObjectName(QStringLiteral("EmptyStateHint"));
    healthEmptyHint_->setWordWrap(true);
    healthEmptyHint_->setAlignment(Qt::AlignCenter);
    healthCardsLayout_->addWidget(healthEmptyHint_);
    healthCardsLayout_->addStretch(1);
    healthScroll_->setWidget(healthCardsHost_);

    layout->addWidget(hero);
    layout->addWidget(healthScroll_, 1);

    return page;
}

void MainWindow::ShowPreferencesDialog() {
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("首选项"));
    dialog.setWindowIcon(windowIcon());
    dialog.resize(540, 420);

    auto* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* tabs = new QTabWidget(&dialog);

    // —— 外观页:主题皮肤(复用 SetTheme,与「工具 · 主题皮肤」菜单同源) ——
    auto* appearancePage = new QWidget(&dialog);
    appearancePage->setObjectName(QStringLiteral("PrefPage"));
    appearancePage->setAttribute(Qt::WA_StyledBackground, true);
    auto* appearanceLayout = new QVBoxLayout(appearancePage);
    appearanceLayout->setContentsMargins(22, 20, 22, 18);
    appearanceLayout->setSpacing(10);
    auto* themeCaption = new QLabel(QStringLiteral("主题皮肤"), appearancePage);
    themeCaption->setObjectName(QStringLiteral("AboutTitle"));
    auto* themeCombo = new QComboBox(appearancePage);
    themeCombo->addItem(QStringLiteral("浅色专业"), QStringLiteral("light"));
    themeCombo->addItem(QStringLiteral("暗色大师"), QStringLiteral("dark"));
    themeCombo->addItem(QStringLiteral("蓝色清爽"), QStringLiteral("blue"));
    themeCombo->setCurrentIndex(qMax(0, themeCombo->findData(currentTheme_)));
    auto* themeHint = new QLabel(QStringLiteral("切换会立即生效。也可在「工具 · 主题皮肤」菜单快速切换。"), appearancePage);
    themeHint->setWordWrap(true);
    themeHint->setObjectName(QStringLiteral("EmptyStateHint"));
    appearanceLayout->addWidget(themeCaption);
    appearanceLayout->addWidget(themeCombo);
    appearanceLayout->addWidget(themeHint);
    appearanceLayout->addStretch(1);
    tabs->addTab(appearancePage, QStringLiteral("外观"));

    // —— 清理页:复用清理 tab 的三个默认开关(确定时写回成员复选框) ——
    auto* cleanupPage = new QWidget(&dialog);
    cleanupPage->setObjectName(QStringLiteral("PrefPage"));
    cleanupPage->setAttribute(Qt::WA_StyledBackground, true);
    auto* cleanupLayout = new QVBoxLayout(cleanupPage);
    cleanupLayout->setContentsMargins(22, 20, 22, 18);
    cleanupLayout->setSpacing(10);
    auto* cleanupCaption = new QLabel(QStringLiteral("垃圾清理默认选项(影响下次扫描清理与删除方式)"), cleanupPage);
    cleanupCaption->setWordWrap(true);
    cleanupCaption->setObjectName(QStringLiteral("AboutTitle"));
    auto* privacyBox = new QCheckBox(QStringLiteral("扫描隐私痕迹(浏览器 / 最近文档 / 剪贴板等)"), cleanupPage);
    privacyBox->setChecked(cleanupPrivacyCheckBox_ != nullptr ? cleanupPrivacyCheckBox_->isChecked() : true);
    auto* developerBox = new QCheckBox(QStringLiteral("扫描开发缓存(npm / pip / NuGet / Gradle / Maven 等)"), cleanupPage);
    developerBox->setChecked(cleanupDeveloperCheckBox_ != nullptr ? cleanupDeveloperCheckBox_->isChecked() : true);
    auto* deepCleanBox = new QCheckBox(QStringLiteral("深度清理:删除时不进回收站,直接永久删除"), cleanupPage);
    deepCleanBox->setChecked(cleanupDeepCleanCheckBox_ != nullptr ? cleanupDeepCleanCheckBox_->isChecked() : false);
    auto* deepCleanHint = new QLabel(QStringLiteral("提示:永久删除不可恢复,请确认信任所选清理项目。"), cleanupPage);
    deepCleanHint->setWordWrap(true);
    deepCleanHint->setObjectName(QStringLiteral("EmptyStateHint"));
    cleanupLayout->addWidget(cleanupCaption);
    cleanupLayout->addWidget(privacyBox);
    cleanupLayout->addWidget(developerBox);
    cleanupLayout->addWidget(deepCleanBox);
    cleanupLayout->addWidget(deepCleanHint);
    cleanupLayout->addStretch(1);
    tabs->addTab(cleanupPage, QStringLiteral("清理"));

    // —— 去重页:复用去重 tab 的永久删除开关 ——
    auto* dedupPage = new QWidget(&dialog);
    dedupPage->setObjectName(QStringLiteral("PrefPage"));
    dedupPage->setAttribute(Qt::WA_StyledBackground, true);
    auto* dedupLayout = new QVBoxLayout(dedupPage);
    dedupLayout->setContentsMargins(22, 20, 22, 18);
    dedupLayout->setSpacing(10);
    auto* dedupCaption = new QLabel(QStringLiteral("疑似重复文件处理方式"), dedupPage);
    dedupCaption->setWordWrap(true);
    dedupCaption->setObjectName(QStringLiteral("AboutTitle"));
    auto* permanentBox = new QCheckBox(QStringLiteral("一键去重时永久删除(不进回收站)"), dedupPage);
    permanentBox->setChecked(duplicatePermanentCheckBox_ != nullptr ? duplicatePermanentCheckBox_->isChecked() : false);
    auto* dedupHint = new QLabel(QStringLiteral("未勾选时,删除的重复文件移入回收站,可恢复。"), dedupPage);
    dedupHint->setWordWrap(true);
    dedupHint->setObjectName(QStringLiteral("EmptyStateHint"));
    dedupLayout->addWidget(dedupCaption);
    dedupLayout->addWidget(permanentBox);
    dedupLayout->addWidget(dedupHint);
    dedupLayout->addStretch(1);
    tabs->addTab(dedupPage, QStringLiteral("去重"));

    // —— 实时监控页:E2 文件夹实时监控开关(默认关闭,纯本地,无后台线程/网络) ——
    auto* autoPage = new QWidget(&dialog);
    autoPage->setObjectName(QStringLiteral("PrefPage"));
    autoPage->setAttribute(Qt::WA_StyledBackground, true);
    auto* autoLayout = new QVBoxLayout(autoPage);
    autoLayout->setContentsMargins(22, 20, 22, 18);
    autoLayout->setSpacing(10);
    auto* autoCaption = new QLabel(QStringLiteral("文件夹实时监控（扫描完成后监视该位置，变化即自动重扫）"), autoPage);
    autoCaption->setWordWrap(true);
    autoCaption->setObjectName(QStringLiteral("AboutTitle"));
    auto* liveWatchBox = new QCheckBox(QStringLiteral("启用文件夹实时监控（变化后自动重扫，默认关闭）"), autoPage);
    liveWatchBox->setChecked(liveWatchEnabled_);
    auto* liveWatchHint = new QLabel(QStringLiteral("仅监控扫描根目录及其一级子目录；裸盘根目录（如 C:\\）不启用，以避免频繁全盘重扫；网络路径 / 深路径为尽力而为（Qt 已知局限）。"), autoPage);
    liveWatchHint->setWordWrap(true);
    liveWatchHint->setObjectName(QStringLiteral("EmptyStateHint"));
    autoLayout->addWidget(autoCaption);
    autoLayout->addWidget(liveWatchBox);
    autoLayout->addWidget(liveWatchHint);
    autoLayout->addStretch(1);
    tabs->addTab(autoPage, QStringLiteral("实时监控"));

    layout->addWidget(tabs, 1);

    auto* buttons = new QDialogButtonBox(&dialog);
    QPushButton* okButton = buttons->addButton(QStringLiteral("确定"), QDialogButtonBox::AcceptRole);
    buttons->addButton(QStringLiteral("取消"), QDialogButtonBox::RejectRole);
    okButton->setDefault(true);
    okButton->setObjectName(QStringLiteral("PrimaryButton"));
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    // 按钮行单独内边距(20,0,20,16):标签页保持贴顶强调线观感,确定/取消右下留白,
    // 与 CopyPath/HealthDetail(20,18,20,16)等同级对话框尺度一致(本对话框为标签式,顶部由标签栏处理)。
    auto* buttonRow = new QHBoxLayout();
    buttonRow->setContentsMargins(20, 0, 20, 16);
    buttonRow->addStretch(1);
    buttonRow->addWidget(buttons);
    layout->addLayout(buttonRow);

    ApplyNativeWindowIcon(&dialog);
    QTimer::singleShot(0, &dialog, [&dialog]() { ApplyNativeWindowIcon(&dialog); });

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    // 应用:主题立即切换,四个开关写回成员复选框(既有扫描/删除消费路径自动生效)。
    const QString chosenTheme = themeCombo->currentData().toString();
    if (!chosenTheme.isEmpty() && chosenTheme != currentTheme_) {
        SetTheme(chosenTheme);
    }
    if (cleanupPrivacyCheckBox_ != nullptr) {
        cleanupPrivacyCheckBox_->setChecked(privacyBox->isChecked());
    }
    if (cleanupDeveloperCheckBox_ != nullptr) {
        cleanupDeveloperCheckBox_->setChecked(developerBox->isChecked());
    }
    if (cleanupDeepCleanCheckBox_ != nullptr) {
        cleanupDeepCleanCheckBox_->setChecked(deepCleanBox->isChecked());
    }
    if (duplicatePermanentCheckBox_ != nullptr) {
        duplicatePermanentCheckBox_->setChecked(permanentBox->isChecked());
    }

    // 实时监控开关写回成员(E2),立即生效。
    liveWatchEnabled_ = liveWatchBox->isChecked();

    // 立即落盘各开关(主题经 SetTheme 已更新 currentTheme_,会在关闭窗口时随 ui/theme 持久化)。
    {
        QSettings settings(QStringLiteral("SunnyFan"), QStringLiteral("DiskLens"));
        settings.setValue(QStringLiteral("cleanup/privacy"), cleanupPrivacyCheckBox_ != nullptr && cleanupPrivacyCheckBox_->isChecked());
        settings.setValue(QStringLiteral("cleanup/developer"), cleanupDeveloperCheckBox_ != nullptr && cleanupDeveloperCheckBox_->isChecked());
        settings.setValue(QStringLiteral("cleanup/deepClean"), cleanupDeepCleanCheckBox_ != nullptr && cleanupDeepCleanCheckBox_->isChecked());
        settings.setValue(QStringLiteral("dedup/permanentDelete"), duplicatePermanentCheckBox_ != nullptr && duplicatePermanentCheckBox_->isChecked());
        settings.setValue(QStringLiteral("watch/liveEnabled"), liveWatchEnabled_);
    }
    ReevaluateWatcher();
    SetInfoBar(QStringLiteral("已保存首选项"), 0, 0, QStringLiteral("设置将在下次扫描清理 / 去重时生效"));
}

void MainWindow::ShowHealthDetailDialog(int row) {
    if (healthCardsHost_ == nullptr || row < 0 || row >= static_cast<int>(healthInfos_.size())) {
        return;
    }
    const core::DiskHealthInfo& info = healthInfos_[row];

    auto statusColor = [](core::DiskHealthStatus status) -> QColor {
        switch (status) {
            case core::DiskHealthStatus::Good: return QColor(g_activeTokens.good);
            case core::DiskHealthStatus::Attention: return QColor(g_activeTokens.warn);
            case core::DiskHealthStatus::Warning: return QColor(g_activeTokens.danger);
            default: return QColor(g_activeTokens.textMuted);
        }
    };

    const QString dash = QStringLiteral("-");
    QString diskText = QStringLiteral("物理盘 %1").arg(info.physicalDriveNumber);
    if (!info.driveLetters.empty()) {
        diskText += QStringLiteral(" · ") + ToQString(info.driveLetters);
    }
    const QString modelText = info.model.empty() ? QStringLiteral("(未知型号)") : ToQString(info.model);

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("磁盘健康详情"));
    dialog.setWindowIcon(windowIcon());
    // ATA 盘带 SMART 全量属性表时加高对话框,避免上下两个表互相挤压。
    dialog.resize(600, info.smartAttributes.empty() ? 560 : 720);

    auto* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(20, 18, 20, 16);
    layout->setSpacing(12);

    auto* titleLabel = new QLabel(diskText + QStringLiteral("  ·  ") + modelText, &dialog);
    titleLabel->setObjectName(QStringLiteral("AboutTitle"));
    titleLabel->setWordWrap(true);

    auto* detailTable = new QTableWidget(&dialog);
    detailTable->setObjectName(QStringLiteral("CleanupTree"));
    detailTable->setColumnCount(2);
    detailTable->setHorizontalHeaderLabels({QStringLiteral("项目"), QStringLiteral("内容")});
    detailTable->verticalHeader()->setVisible(false);
    detailTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    detailTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    detailTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    detailTable->setSelectionMode(QAbstractItemView::SingleSelection);
    detailTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    detailTable->setAlternatingRowColors(true);
    detailTable->setShowGrid(false);
    detailTable->setTextElideMode(Qt::ElideRight);

    const auto addRow = [detailTable](const QString& key, const QString& value) {
        const int r = detailTable->rowCount();
        detailTable->insertRow(r);
        auto* keyItem = new QTableWidgetItem(key);
        auto* valueItem = new QTableWidgetItem(value);
        valueItem->setToolTip(value);
        detailTable->setItem(r, 0, keyItem);
        detailTable->setItem(r, 1, valueItem);
        return r;
    };

    addRow(QStringLiteral("磁盘"), diskText);
    addRow(QStringLiteral("型号"), modelText);
    addRow(QStringLiteral("序列号"), info.serial.empty() ? dash : ToQString(info.serial));
    addRow(QStringLiteral("固件版本"), info.firmwareRevision.empty() ? dash : ToQString(info.firmwareRevision));
    addRow(QStringLiteral("接口"), info.interfaceType.empty() ? dash : ToQString(info.interfaceType));
    addRow(QStringLiteral("容量"), info.totalBytes > 0 ? ToQString(core::FormatBytes(info.totalBytes)) : dash);
    addRow(QStringLiteral("健康度"), info.healthPercent >= 0 ? QStringLiteral("%1%").arg(info.healthPercent) : dash);
    addRow(QStringLiteral("温度"), info.temperatureCelsius >= 0 ? QStringLiteral("%1 °C").arg(info.temperatureCelsius) : dash);
    QString powerText = dash;
    if (info.powerOnHours >= 0) {
        const long long hours = info.powerOnHours;
        const long long days = hours / 24;
        const long long remHours = hours % 24;
        powerText = QStringLiteral("%1 小时").arg(static_cast<qlonglong>(hours));
        if (days > 0) {
            powerText += QStringLiteral(" · 约 %1 天 %2 时").arg(static_cast<qlonglong>(days)).arg(static_cast<qlonglong>(remHours));
        }
    }
    addRow(QStringLiteral("通电时长"), powerText);
    addRow(QStringLiteral("通电次数"), info.powerCycleCount >= 0 ? QString::number(static_cast<qlonglong>(info.powerCycleCount)) : dash);
    // 可用备用:NVMe 盘显示当前值 + 阈值(便于一眼判断备用块是否告急);非 NVMe 仅在有值时显示。
    if (info.availableSparePercent >= 0) {
        QString spareValue = QStringLiteral("%1%").arg(info.availableSparePercent);
        if (info.nvmeAvailableSpareThreshold >= 0) {
            spareValue += QStringLiteral("（阈值 %1%）").arg(info.nvmeAvailableSpareThreshold);
        }
        addRow(QStringLiteral("可用备用(NVMe)"), spareValue);
    } else {
        addRow(QStringLiteral("可用备用(NVMe)"), dash);
    }
    // NVMe 耐久度明细(非 NVMe 盘这些字段保持 -1,逐字段跳过)。数据单元换算:NVMe 规范
    // 1 数据单元 = 512000 字节(1000×512),不是 524288;累计写入即 TBW。
    constexpr std::uint64_t nvmeDataUnitBytes = 512000ULL;
    if (info.nvmePercentageUsed >= 0) {
        addRow(QStringLiteral("寿命已用(NVMe)"), QStringLiteral("%1%").arg(info.nvmePercentageUsed));
    }
    if (info.nvmeDataUnitsWritten >= 0) {
        const QString written = ToQString(core::FormatBytes(static_cast<std::uint64_t>(info.nvmeDataUnitsWritten) * nvmeDataUnitBytes));
        addRow(QStringLiteral("累计写入(NVMe)"),
               QStringLiteral("%1（约 %2 个数据单元）").arg(written, QString::number(static_cast<qlonglong>(info.nvmeDataUnitsWritten))));
    }
    if (info.nvmeDataUnitsRead >= 0) {
        const QString read = ToQString(core::FormatBytes(static_cast<std::uint64_t>(info.nvmeDataUnitsRead) * nvmeDataUnitBytes));
        addRow(QStringLiteral("累计读取(NVMe)"),
               QStringLiteral("%1（约 %2 个数据单元）").arg(read, QString::number(static_cast<qlonglong>(info.nvmeDataUnitsRead))));
    }
    if (info.nvmeUnsafeShutdowns >= 0) {
        addRow(QStringLiteral("非正常关机(NVMe)"), QString::number(static_cast<qlonglong>(info.nvmeUnsafeShutdowns)));
    }
    if (info.nvmeMediaErrors >= 0) {
        addRow(QStringLiteral("介质错误(NVMe)"), QString::number(static_cast<qlonglong>(info.nvmeMediaErrors)));
    }
    if (info.nvmeErrorLogEntries >= 0) {
        addRow(QStringLiteral("错误日志条目(NVMe)"), QString::number(static_cast<qlonglong>(info.nvmeErrorLogEntries)));
    }
    // NVMe 多传感器温度:复合温度已在上方“温度”行,这里列出各独立传感器(通常 2-8 个),便于发现局部热点。
    if (!info.nvmeTemperatureSensors.empty()) {
        QString sensorsText;
        for (std::size_t i = 0; i < info.nvmeTemperatureSensors.size(); ++i) {
            sensorsText += (i == 0 ? QString() : QStringLiteral("  ·  "))
                           + QStringLiteral("传感器%1: %2 °C").arg(static_cast<int>(i + 1)).arg(info.nvmeTemperatureSensors[i]);
        }
        addRow(QStringLiteral("温度传感器(NVMe)"), sensorsText);
    }
    addRow(QStringLiteral("重映射扇区"), info.reallocatedSectorCount >= 0 ? QString::number(static_cast<qlonglong>(info.reallocatedSectorCount)) : dash);
    addRow(QStringLiteral("当前待映射扇区"), info.currentPendingSectorCount >= 0 ? QString::number(static_cast<qlonglong>(info.currentPendingSectorCount)) : dash);
    addRow(QStringLiteral("离线无法校正扇区"), info.uncorrectableSectorCount >= 0 ? QString::number(static_cast<qlonglong>(info.uncorrectableSectorCount)) : dash);
    // 状态行单独着色,便于一眼定位异常。
    const QString statusText = info.statusText.empty() ? QStringLiteral("不可读取") : ToQString(info.statusText);
    {
        const int r = addRow(QStringLiteral("状态"), statusText);
        if (auto* v = detailTable->item(r, 1)) {
            v->setForeground(statusColor(info.status));
        }
    }
    detailTable->resizeRowsToContents();

    layout->addWidget(titleLabel);
    layout->addWidget(detailTable, 1);

    // ATA SMART 全量属性明细(仅 SATA/ATA 盘经 SMART READ DATA 解析得到;NVMe/USB 为空,整段跳过)。
    if (!info.smartAttributes.empty()) {
        auto* smartCaption = new QLabel(QStringLiteral("SMART 属性(ATA 直读,共 %1 项)").arg(info.smartAttributes.size()), &dialog);
        smartCaption->setObjectName(QStringLiteral("AboutTitle"));
        layout->addWidget(smartCaption);

        auto* smartTable = new QTableWidget(static_cast<int>(info.smartAttributes.size()), 5, &dialog);
        smartTable->setObjectName(QStringLiteral("CleanupTree"));
        smartTable->setHorizontalHeaderLabels({QStringLiteral("ID"), QStringLiteral("属性"),
                                               QStringLiteral("当前值"), QStringLiteral("最差值"), QStringLiteral("原始值")});
        smartTable->verticalHeader()->setVisible(false);
        smartTable->setSelectionBehavior(QAbstractItemView::SelectRows);
        smartTable->setSelectionMode(QAbstractItemView::SingleSelection);
        smartTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
        smartTable->setAlternatingRowColors(true);
        smartTable->setShowGrid(false);
        smartTable->setTextElideMode(Qt::ElideRight);
        smartTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);   // ID
        smartTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);            // 属性名
        smartTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);   // 当前值
        smartTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);   // 最差值
        smartTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);            // 原始值
        for (int r = 0; r < static_cast<int>(info.smartAttributes.size()); ++r) {
            const core::SmartAttribute& a = info.smartAttributes[static_cast<std::size_t>(r)];
            auto* idItem = new QTableWidgetItem(QString::number(a.id));
            auto* nameItem = new QTableWidgetItem(ToQString(a.name));
            nameItem->setToolTip(ToQString(a.name));
            auto* valueItem = new QTableWidgetItem(QString::number(a.value));
            valueItem->setToolTip(QStringLiteral("当前归一化值(厂商刻度,越大通常越好)"));
            auto* worstItem = new QTableWidgetItem(QString::number(a.worst));
            worstItem->setToolTip(QStringLiteral("历史最差归一化值"));
            auto* rawItem = new QTableWidgetItem(QString::number(static_cast<qlonglong>(a.raw)));
            rawItem->setToolTip(QString::number(static_cast<qlonglong>(a.raw)));
            smartTable->setItem(r, 0, idItem);
            smartTable->setItem(r, 1, nameItem);
            smartTable->setItem(r, 2, valueItem);
            smartTable->setItem(r, 3, worstItem);
            smartTable->setItem(r, 4, rawItem);
        }
        smartTable->resizeRowsToContents();
        layout->addWidget(smartTable, 1);
    }

    // 失败原因 / 数据来源备注单独成段:可换行、可选中复制,直接解决表格列省略、tooltip 不可靠的痛点。
    const QString noteText = info.note.empty() ? QString() : ToQString(info.note);
    if (!noteText.isEmpty()) {
        auto* noteCaption = new QLabel(QStringLiteral("备注(数据来源 / 失败原因)"), &dialog);
        auto* noteLabel = new QLabel(noteText, &dialog);
        noteLabel->setWordWrap(true);
        noteLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        noteLabel->setToolTip(noteText);
        // 不可读取时高亮诊断信息,正常时跟随主题默认色。
        if (info.status == core::DiskHealthStatus::Unreadable) {
            noteLabel->setStyleSheet(QStringLiteral("color: %1;").arg(statusColor(info.status).name()));
        }
        layout->addWidget(noteCaption);
        layout->addWidget(noteLabel);
    }

    auto* buttons = new QDialogButtonBox(&dialog);
    QPushButton* closeButton = buttons->addButton(QStringLiteral("关闭"), QDialogButtonBox::AcceptRole);
    connect(closeButton, &QPushButton::clicked, &dialog, &QDialog::accept);
    layout->addWidget(buttons);

    ApplyNativeWindowIcon(&dialog);
    QTimer::singleShot(0, &dialog, [&dialog]() { ApplyNativeWindowIcon(&dialog); });
    dialog.exec();
}

void MainWindow::RefreshDiskHealth() {
    if (healthQuerying_.load() || healthCardsHost_ == nullptr) {
        return;
    }

    healthQueryCancel_.store(false);
    healthQuerying_.store(true);
    if (healthStatusLabel_ != nullptr) {
        healthStatusLabel_->setText(QStringLiteral("正在读取物理盘健康信息(SMART / NVMe),请稍候……"));
    }
    if (healthInfoLabel_ != nullptr) {
        healthInfoLabel_->setText(QStringLiteral("磁盘健康 · 正在读取……"));
    }

    std::thread([this]() {
        const core::DiskHealth reader;
        std::vector<core::DiskHealthInfo> infos = reader.QueryAll();

        QMetaObject::invokeMethod(this, [this, infos = std::move(infos)]() mutable {
            const bool cancelled = healthQueryCancel_.load();
            healthQuerying_.store(false);
            healthQueryCancel_.store(false);
            if (cancelled) {
                if (healthStatusLabel_ != nullptr) {
                    healthStatusLabel_->setText(QStringLiteral("已取消读取。点「读取健康信息」重新开始。"));
                }
                return;
            }
            healthInfos_ = std::move(infos);
            PopulateHealthCards(healthInfos_);
        }, Qt::QueuedConnection);
    }).detach();
}

void MainWindow::CancelDiskHealth() {
    if (!healthQuerying_.load()) {
        return;
    }
    healthQueryCancel_.store(true);
    if (healthStatusLabel_ != nullptr) {
        healthStatusLabel_->setText(QStringLiteral("正在取消……"));
    }
}

void MainWindow::PopulateHealthCards(const std::vector<disk_lens::core::DiskHealthInfo>& infos) {
    if (healthCardsLayout_ == nullptr) {
        return;
    }
    healthInfos_ = infos;

    // 清空旧卡片:只删 objectName 为 HealthCard 的直接子 QFrame。
    // 关键:healthEmptyHint_ 是 QLabel(QLabel 继承自 QFrame),会被 findChildren<QFrame*> 一并捕获;
    // 若误删,成员指针悬垂,后续 setVisible/addWidget 触发 use-after-free 崩溃。必须按 objectName 过滤排除。
    const auto oldCards = healthCardsHost_->findChildren<QFrame*>(QStringLiteral("HealthCard"), Qt::FindDirectChildrenOnly);
    for (QFrame* card : oldCards) {
        delete card;
    }
    // 清空剩余布局条目(空状态占位的 QWidgetItem 与末尾弹簧),空状态控件本体保留,稍后重新追加。
    while (healthCardsLayout_->count() > 0) {
        QLayoutItem* item = healthCardsLayout_->takeAt(0);
        delete item;
    }

    int goodCount = 0;
    int attentionCount = 0;
    int warningCount = 0;
    int unreadableCount = 0;
    for (std::size_t i = 0; i < infos.size(); ++i) {
        const core::DiskHealthInfo& info = infos[i];
        switch (info.status) {
            case core::DiskHealthStatus::Good: ++goodCount; break;
            case core::DiskHealthStatus::Attention: ++attentionCount; break;
            case core::DiskHealthStatus::Warning: ++warningCount; break;
            default: ++unreadableCount; break;
        }
        healthCardsLayout_->addWidget(BuildHealthCard(info, static_cast<int>(i)));
    }
    if (healthEmptyHint_ != nullptr) {
        healthEmptyHint_->setVisible(infos.empty());
        healthCardsLayout_->addWidget(healthEmptyHint_);
    }
    healthCardsLayout_->addStretch(1);

    const QString summary = QStringLiteral("磁盘健康 · 共 %1 块物理盘 · 良好 %2 · 注意 %3 · 警告 %4 · 不可读取 %5")
        .arg(static_cast<int>(infos.size()))
        .arg(goodCount)
        .arg(attentionCount)
        .arg(warningCount)
        .arg(unreadableCount);
    if (healthInfoLabel_ != nullptr) {
        healthInfoLabel_->setText(summary);
    }
    if (healthStatusLabel_ != nullptr) {
        if (infos.empty()) {
            healthStatusLabel_->setText(QStringLiteral("未发现可读取的物理盘(可能未以管理员身份运行,或无固定盘)。"));
        } else if (warningCount > 0) {
            healthStatusLabel_->setText(QStringLiteral("存在警告级别磁盘,建议尽快备份数据并考虑更换。"));
        } else if (attentionCount > 0) {
            healthStatusLabel_->setText(QStringLiteral("部分磁盘出现注意级别指标,建议持续留意。"));
        } else {
            healthStatusLabel_->setText(QStringLiteral("所有可读取磁盘状态良好。"));
        }
    }
}

QFrame* MainWindow::BuildHealthCard(const disk_lens::core::DiskHealthInfo& info, int row) {
    auto* card = new QFrame(healthCardsHost_);
    card->setObjectName(QStringLiteral("HealthCard"));

    // 状态映射:statusProp 驱动徽章与卡片着色;statusColor 内联给进度条 chunk,避开 QSS 子控件属性选择器。
    QString statusProp;
    QColor statusColor;
    switch (info.status) {
        case core::DiskHealthStatus::Good:
            statusProp = QStringLiteral("good");
            statusColor = QColor(g_activeTokens.good);
            break;
        case core::DiskHealthStatus::Attention:
            statusProp = QStringLiteral("warn");
            statusColor = QColor(g_activeTokens.warn);
            break;
        case core::DiskHealthStatus::Warning:
            statusProp = QStringLiteral("danger");
            statusColor = QColor(g_activeTokens.danger);
            break;
        default:
            statusProp = QStringLiteral("muted");
            statusColor = QColor(g_activeTokens.textMuted);
            break;
    }
    card->setProperty("statusProp", statusProp);

    const QString dash = QStringLiteral("—");
    const QString modelText = info.model.empty() ? QStringLiteral("(未知型号)") : ToQString(info.model);
    const QString interfaceText = info.interfaceType.empty() ? QStringLiteral("-") : ToQString(info.interfaceType);
    const QString capacityText = info.totalBytes > 0 ? ToQString(core::FormatBytes(info.totalBytes)) : QStringLiteral("-");
    const QString statusText = info.statusText.empty() ? QStringLiteral("不可读取") : ToQString(info.statusText);
    const QString healthText = info.healthPercent >= 0 ? QStringLiteral("%1%").arg(info.healthPercent) : dash;
    const QString tempText = info.temperatureCelsius >= 0 ? QStringLiteral("%1 °C").arg(info.temperatureCelsius) : dash;
    const QString powerText = info.powerOnHours >= 0 ? QStringLiteral("%1 小时").arg(static_cast<qlonglong>(info.powerOnHours)) : dash;
    const QString cycleText = info.powerCycleCount >= 0 ? QString::number(static_cast<qlonglong>(info.powerCycleCount)) : dash;
    const QString spareText = info.availableSparePercent >= 0 ? QStringLiteral("%1%").arg(info.availableSparePercent) : dash;
    const QString reallocatedText = info.reallocatedSectorCount >= 0 ? QString::number(static_cast<qlonglong>(info.reallocatedSectorCount)) : dash;
    const QString pendingText = info.currentPendingSectorCount >= 0 ? QString::number(static_cast<qlonglong>(info.currentPendingSectorCount)) : dash;
    const QString uncorrectableText = info.uncorrectableSectorCount >= 0 ? QString::number(static_cast<qlonglong>(info.uncorrectableSectorCount)) : dash;
    const QString lettersText = info.driveLetters.empty() ? QString() : ToQString(info.driveLetters);
    const QString noteText = info.note.empty() ? QString() : ToQString(info.note);

    // 副标题:物理盘号 · 盘符 · 接口 · 容量(单行,超长靠 tooltip 与省略,不换行以规避滚动区 layout 循环)。
    QString subText = QStringLiteral("物理盘 %1").arg(info.physicalDriveNumber);
    if (!lettersText.isEmpty()) {
        subText += QStringLiteral("  ·  ") + lettersText;
    }
    subText += QStringLiteral("  ·  ") + interfaceText + QStringLiteral("  ·  ") + capacityText;

    auto* root = new QVBoxLayout(card);
    root->setContentsMargins(g_activeTokens.spaceLg, g_activeTokens.spaceLg, g_activeTokens.spaceLg, g_activeTokens.spaceLg);
    root->setSpacing(g_activeTokens.spaceMd);

    // 头部:磁盘图标 + 型号(大标题)+ 副标题 + 状态药丸。
    auto* iconLabel = new QLabel(card);
    iconLabel->setPixmap(app_icons::drive(40).pixmap(40, 40));
    auto* titleLabel = new QLabel(modelText, card);
    titleLabel->setObjectName(QStringLiteral("HealthCardModel"));
    titleLabel->setToolTip(noteText.isEmpty() ? modelText : modelText + QStringLiteral("\n") + noteText);
    auto* subLabel = new QLabel(subText, card);
    subLabel->setObjectName(QStringLiteral("HealthCardSub"));
    subLabel->setToolTip(subText);
    auto* badgeLabel = new QLabel(statusText, card);
    badgeLabel->setObjectName(QStringLiteral("HealthBadge"));
    badgeLabel->setProperty("statusProp", statusProp);
    badgeLabel->setAlignment(Qt::AlignCenter);
    badgeLabel->setToolTip(noteText.isEmpty() ? statusText : noteText);
    auto* titleBlock = new QVBoxLayout();
    titleBlock->setSpacing(g_activeTokens.spaceXs);
    titleBlock->addWidget(titleLabel);
    titleBlock->addWidget(subLabel);
    auto* head = new QHBoxLayout();
    head->setSpacing(g_activeTokens.spaceMd);
    head->addWidget(iconLabel, 0, Qt::AlignVCenter);
    head->addLayout(titleBlock, 1);
    head->addWidget(badgeLabel, 0, Qt::AlignTop);
    root->addLayout(head);

    // 健康度区块:独立浅底小块,标签 + 大百分比 + 进度条(chunk 内联着色,规避 QSS 子控件属性选择器)。
    auto* meterBox = new QFrame(card);
    meterBox->setObjectName(QStringLiteral("HealthCardMeter"));
    auto* meterLayout = new QVBoxLayout(meterBox);
    meterLayout->setContentsMargins(g_activeTokens.spaceMd, g_activeTokens.spaceSm, g_activeTokens.spaceMd, g_activeTokens.spaceSm);
    meterLayout->setSpacing(g_activeTokens.spaceXs);
    auto* meterHead = new QHBoxLayout();
    auto* meterCaption = new QLabel(QStringLiteral("健康度"), meterBox);
    meterCaption->setObjectName(QStringLiteral("HealthCardKey"));
    auto* pctLabel = new QLabel(healthText, meterBox);
    pctLabel->setObjectName(QStringLiteral("HealthCardPct"));
    pctLabel->setToolTip(noteText);
    meterHead->addWidget(meterCaption);
    meterHead->addStretch(1);
    meterHead->addWidget(pctLabel);
    auto* healthBar = new QProgressBar(meterBox);
    healthBar->setObjectName(QStringLiteral("HealthBar"));
    healthBar->setRange(0, 100);
    healthBar->setValue(info.healthPercent >= 0 ? info.healthPercent : 0);
    healthBar->setTextVisible(false);
    healthBar->setFixedHeight(8);
    healthBar->setStyleSheet(QStringLiteral(
        "QProgressBar{background:%1;border:none;border-radius:%2px;}"
        "QProgressBar::chunk{background:%3;border-radius:%2px;}")
        .arg(g_activeTokens.cardBorder)
        .arg(g_activeTokens.trackRadius)
        .arg(statusColor.name()));
    meterLayout->addLayout(meterHead);
    meterLayout->addWidget(healthBar);
    root->addWidget(meterBox);

    // 指标网格:键(灰小)在上、值(粗)在下,两列均匀分布。
    auto* grid = new QGridLayout();
    grid->setHorizontalSpacing(g_activeTokens.spaceXl);
    grid->setVerticalSpacing(g_activeTokens.spaceSm);
    grid->setColumnStretch(0, 1);
    grid->setColumnStretch(1, 1);
    auto addMetric = [card, grid](int r, int c, const QString& key, const QString& val) {
        auto* k = new QLabel(key, card);
        k->setObjectName(QStringLiteral("HealthCardKey"));
        auto* v = new QLabel(val, card);
        v->setObjectName(QStringLiteral("HealthCardVal"));
        auto* cell = new QVBoxLayout();
        cell->setSpacing(0);
        cell->addWidget(k);
        cell->addWidget(v);
        grid->addLayout(cell, r, c);
    };
    addMetric(0, 0, QStringLiteral("温度"), tempText);
    addMetric(0, 1, QStringLiteral("通电时长"), powerText);
    addMetric(1, 0, QStringLiteral("通电次数"), cycleText);
    addMetric(1, 1, QStringLiteral("可用备用"), spareText);
    addMetric(2, 0, QStringLiteral("重映射扇区"), reallocatedText);
    addMetric(2, 1, QStringLiteral("当前待映射"), pendingText);
    addMetric(3, 0, QStringLiteral("离线无法校正"), uncorrectableText);
    root->addLayout(grid);

    // 右键卡片弹菜单(详情 / 刷新),替代递归事件过滤器,降低运行时复杂度。
    card->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(card, &QWidget::customContextMenuRequested, this, [this, row, card](const QPoint& pos) {
        if (row >= 0 && row < static_cast<int>(healthInfos_.size())) {
            QMenu menu(this);
            menu.addAction(QStringLiteral("查看详情"), this, [this, row]() { ShowHealthDetailDialog(row); });
            menu.addSeparator();
            menu.addAction(QStringLiteral("刷新健康信息"), this, [this]() { RefreshDiskHealth(); });
            menu.exec(card->mapToGlobal(pos));
        }
    });

    return card;
}

QWidget* MainWindow::CreateDuplicateTab() {
    auto* page = new QWidget(this);
    duplicatePage_ = page;
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    auto* hero = new QFrame(page);
    hero->setObjectName(QStringLiteral("CleanupHero"));
    auto* heroLayout = new QVBoxLayout(hero);
    heroLayout->setContentsMargins(16, 12, 16, 12);
    heroLayout->setSpacing(8);

    auto* titleLabel = new QLabel(QStringLiteral("重复文件查找与去重"), hero);
    titleLabel->setObjectName(QStringLiteral("CleanupTitle"));
    duplicateStatusLabel_ = new QLabel(QStringLiteral("先扫描磁盘,默认按同名同大小列出重复候选;点「内容深度校验」用 SHA-256 逐字节确认真实重复。"), hero);
    duplicateStatusLabel_->setObjectName(QStringLiteral("CleanupStatus"));
    duplicateStatusLabel_->setWordWrap(true);

    duplicateDeepScanButton_ = new QPushButton(QStringLiteral("内容深度校验"), hero);
    duplicateDeepScanButton_->setObjectName(QStringLiteral("PrimaryButton"));
    duplicateDeepScanButton_->setMinimumHeight(g_activeTokens.primaryButtonHeight);
    duplicateQuickButton_ = new QPushButton(QStringLiteral("快速(同名同大小)"), hero);
    duplicateQuickButton_->setMinimumHeight(g_activeTokens.primaryButtonHeight);
    duplicateCancelButton_ = new QPushButton(QStringLiteral("取消校验"), hero);
    duplicateCancelButton_->setMinimumHeight(g_activeTokens.primaryButtonHeight);
    duplicateCancelButton_->hide();

    auto* titleBlock = new QVBoxLayout();
    titleBlock->setSpacing(2);
    titleBlock->addWidget(titleLabel);
    titleBlock->addWidget(duplicateStatusLabel_);
    auto* headerLayout = new QHBoxLayout();
    headerLayout->setSpacing(12);
    headerLayout->addLayout(titleBlock, 1);
    headerLayout->addWidget(duplicateQuickButton_);
    headerLayout->addWidget(duplicateDeepScanButton_);
    headerLayout->addWidget(duplicateCancelButton_);
    heroLayout->addLayout(headerLayout);

    duplicateTree_ = new ThemedTreeWidget(hero);
    duplicateTree_->setObjectName(QStringLiteral("CleanupTree"));
    duplicateTree_->setHeader(new ModernHeaderView(Qt::Horizontal, duplicateTree_));
    duplicateTree_->setColumnCount(4);
    duplicateTree_->setHeaderLabels({
        QStringLiteral("名称"),
        QStringLiteral("大小"),
        QStringLiteral("修改时间"),
        QStringLiteral("路径"),
    });
    duplicateTree_->setRootIsDecorated(true);
    duplicateTree_->setAnimated(true);
    duplicateTree_->setAlternatingRowColors(true);
    duplicateTree_->setUniformRowHeights(false);
    duplicateTree_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    duplicateTree_->setIndentation(22);
    duplicateTree_->setIconSize(QSize(16, 16));
    duplicateTree_->header()->setStretchLastSection(false);
    duplicateTree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    duplicateTree_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    duplicateTree_->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    duplicateTree_->header()->setSectionResizeMode(3, QHeaderView::Stretch);
    duplicateTree_->header()->resizeSection(0, 280);
    duplicateTree_->header()->resizeSection(1, 100);
    duplicateTree_->header()->resizeSection(2, 150);

    auto* bottomBar = new QFrame(page);
    bottomBar->setObjectName(QStringLiteral("CleanupBottomBar"));
    auto* bottomLayout = new QHBoxLayout(bottomBar);
    bottomLayout->setContentsMargins(16, 10, 16, 10);
    bottomLayout->setSpacing(12);

    auto* selectAllButton = new QPushButton(QStringLiteral("全选"), bottomBar);
    auto* selectNoneButton = new QPushButton(QStringLiteral("全不选"), bottomBar);
    duplicateKeepFirstButton_ = new QPushButton(QStringLiteral("勾选:每组保留首项"), bottomBar);
    duplicatePermanentCheckBox_ = new QCheckBox(QStringLiteral("永久删除(不进回收站)"), bottomBar);
    duplicateSelectedLabel_ = new QLabel(QStringLiteral("已勾选 0 项 · 0 B"), bottomBar);
    duplicateSelectedLabel_->setObjectName(QStringLiteral("CleanupSelected"));
    duplicateDeleteButton_ = new QPushButton(QStringLiteral("一键去重"), bottomBar);
    duplicateDeleteButton_->setObjectName(QStringLiteral("PrimaryButton"));
    duplicateDeleteButton_->setEnabled(false);

    bottomLayout->addWidget(selectAllButton);
    bottomLayout->addWidget(selectNoneButton);
    bottomLayout->addWidget(duplicateKeepFirstButton_);
    bottomLayout->addStretch(1);
    bottomLayout->addWidget(duplicatePermanentCheckBox_);
    bottomLayout->addWidget(duplicateSelectedLabel_);
    bottomLayout->addWidget(duplicateDeleteButton_);

    layout->addWidget(hero);
    layout->addWidget(duplicateTree_, 1);
    layout->addWidget(bottomBar);

    connect(duplicateDeepScanButton_, &QPushButton::clicked, this, [this]() { StartDuplicateContentScan(); });
    connect(duplicateQuickButton_, &QPushButton::clicked, this, [this]() {
        CancelDuplicateContentScan();
        duplicateTreeLoaded_ = true;
        PopulateDuplicateTree();
    });
    connect(duplicateCancelButton_, &QPushButton::clicked, this, [this]() { CancelDuplicateContentScan(); });
    connect(selectAllButton, &QPushButton::clicked, this, [this]() { SetDuplicateCheckedMode(QStringLiteral("all")); });
    connect(selectNoneButton, &QPushButton::clicked, this, [this]() { SetDuplicateCheckedMode(QStringLiteral("none")); });
    connect(duplicateKeepFirstButton_, &QPushButton::clicked, this, [this]() { SetDuplicateCheckedMode(QStringLiteral("keepFirst")); });
    connect(duplicateDeleteButton_, &QPushButton::clicked, this, [this]() { DeleteSelectedDuplicateItems(); });
    connect(duplicatePermanentCheckBox_, &QCheckBox::toggled, this, [this](bool checked) {
        if (duplicateDeleteButton_ != nullptr) {
            duplicateDeleteButton_->setText(checked ? QStringLiteral("一键去重(永久删除)") : QStringLiteral("一键去重"));
        }
    });
    connect(duplicateTree_, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* item, int column) {
        if (item == nullptr || column != 0) {
            return;
        }
        // 组勾选:批量切换除首项外的子项(保留每组首项)。
        if (item->childCount() > 0) {
            const Qt::CheckState state = item->checkState(0);
            duplicateTree_->blockSignals(true);
            for (int c = 0; c < item->childCount(); ++c) {
                QTreeWidgetItem* child = item->child(c);
                if (child != nullptr) {
                    child->setCheckState(0, c == 0 ? Qt::Unchecked : state);
                }
            }
            duplicateTree_->blockSignals(false);
        }
        UpdateDuplicateSelectedSummary();
    });

    return page;
}

void MainWindow::PopulateSearchTable() {
    if (searchModel_ == nullptr) {
        return;
    }

    const auto indexSnapshot = searchIndex_;
    if (searchEdit_ == nullptr || indexSnapshot == nullptr || indexSnapshot->empty()) {
        searchResultRows_.clear();
        searchVisibleResultCount_ = 0;
        searchTotalMatchCount_ = 0;
        if (searchLoadMoreButton_ != nullptr) {
            searchLoadMoreButton_->hide();
        }
        const bool loadingCache = searchCacheLoading_.load();
        const bool buildingIndex = searchIndexing_.load();
        searchModel_->SetRows(QVector<ResultRow>{
            ResultRow{
                loadingCache
                    ? QStringLiteral("正在加载索引缓存")
                    : (buildingIndex ? QStringLiteral("正在建立全系统索引") : QStringLiteral("尚未建立全系统索引")),
                QStringLiteral("-"),
                QStringLiteral("提示"),
                loadingCache
                    ? QStringLiteral("缓存加载完成后即可搜索；没有缓存时会自动建立全系统索引")
                    : QStringLiteral("快速搜索会索引所有固定磁盘，不依赖当前空间扫描"),
                QString(),
                QString(),
                0,
                0,
                false,
                false,
            }
        });
        return;
    }

    const QString keyword = searchEdit_->text().trimmed();
    if (keyword.isEmpty()) {
        searchResultRows_.clear();
        searchVisibleResultCount_ = 0;
        searchTotalMatchCount_ = 0;
        if (searchLoadMoreButton_ != nullptr) {
            searchLoadMoreButton_->hide();
        }
        searchModel_->SetRows(QVector<ResultRow>{
            ResultRow{
                QStringLiteral("输入关键字后开始搜索"),
                QStringLiteral("-"),
                QStringLiteral("提示"),
                QStringLiteral("支持名称和路径片段"),
                QString(),
                QString(),
                0,
                0,
                false,
                false,
            }
        });
        return;
    }
    if (keyword.size() < 2) {
        searchResultRows_.clear();
        searchVisibleResultCount_ = 0;
        searchTotalMatchCount_ = 0;
        if (searchLoadMoreButton_ != nullptr) {
            searchLoadMoreButton_->hide();
        }
        searchModel_->SetRows(QVector<ResultRow>{
            ResultRow{
                QStringLiteral("继续输入以开始搜索"),
                QStringLiteral("-"),
                QStringLiteral("提示"),
                QStringLiteral("全系统索引较大，输入至少 2 个字符后开始匹配"),
                QString(),
                QString(),
                0,
                0,
                false,
                false,
            }
        });
        return;
    }

    const std::uint64_t requestId = ++searchRequestId_;
    const QString keywordKey = keyword.toCaseFolded();

    // 在 UI 线程计算筛选条件，随后按值捕获进工作线程匹配循环（线程安全）。
    qint64 filterMinMsec = 0;
    qint64 filterMaxMsec = std::numeric_limits<qint64>::max();
    if (searchTimeFilterCombo_ != nullptr) {
        const int days = searchTimeFilterCombo_->currentData().toInt();
        if (days == 1) {
            filterMinMsec = QDateTime(QDate::currentDate(), QTime(0, 0, 0)).toMSecsSinceEpoch();
        } else if (days > 1) {
            filterMinMsec = QDateTime::currentMSecsSinceEpoch() - static_cast<qint64>(days) * 86400000LL;
        } else if (days == -1 && searchStartDateEdit_ != nullptr && searchEndDateEdit_ != nullptr) {
            filterMinMsec = QDateTime(searchStartDateEdit_->date(), QTime(0, 0, 0)).toMSecsSinceEpoch();
            filterMaxMsec = QDateTime(searchEndDateEdit_->date(), QTime(23, 59, 59, 999)).toMSecsSinceEpoch();
        }
    }
    quint64 filterMinBytes = 0;
    quint64 filterMaxBytes = std::numeric_limits<quint64>::max();
    if (searchSizeFilterCombo_ != nullptr) {
        switch (searchSizeFilterCombo_->currentIndex()) {
        case 1:
            filterMaxBytes = 10ULL * 1024 * 1024;
            break;
        case 2:
            filterMinBytes = 10ULL * 1024 * 1024;
            filterMaxBytes = 100ULL * 1024 * 1024;
            break;
        case 3:
            filterMinBytes = 100ULL * 1024 * 1024;
            filterMaxBytes = 1024ULL * 1024 * 1024;
            break;
        case 4:
            filterMinBytes = 1024ULL * 1024 * 1024;
            break;
        default:
            break;
        }
    }
    const int typeMode = searchTypeFilterCombo_ != nullptr ? searchTypeFilterCombo_->currentIndex() : 0;

    if (searchScopeLabel_ != nullptr) {
        searchScopeLabel_->setText(QStringLiteral("搜索中：%1").arg(keyword));
    }
    lastSearchKeyword_ = keyword;
    if (searchInfoLabel_ != nullptr) {
        searchInfoLabel_->setText(QStringLiteral("文件搜索 · 搜索中：%1").arg(keyword));
    }
    std::thread([this, indexSnapshot, keyword, keywordKey, requestId, filterMinMsec, filterMaxMsec, filterMinBytes, filterMaxBytes, typeMode]() {
        const auto searchStartedAt = std::chrono::steady_clock::now();
        std::vector<const SearchRecord*> candidates;
        candidates.reserve(4096);
        std::set<QString> matchedPaths;
        std::uint64_t totalMatches = 0;

        for (const SearchRecord& record : *indexSnapshot) {
            if (searchRequestId_.load() != requestId) {
                return;
            }
            if (!record.searchKey.contains(keywordKey)) {
                continue;
            }

            if (record.lastModifiedMsec < filterMinMsec || record.lastModifiedMsec > filterMaxMsec) {
                continue;
            }
            if (record.bytes < filterMinBytes || record.bytes >= filterMaxBytes) {
                continue;
            }
            if (typeMode == 1) {
                if (record.type == QStringLiteral("目录") || record.type == QStringLiteral("磁盘")) {
                    continue;
                }
            } else if (typeMode == 2) {
                if (record.type != QStringLiteral("目录") && record.type != QStringLiteral("磁盘")) {
                    continue;
                }
            }

            const QString normalizedPath = QDir::fromNativeSeparators(record.path).toCaseFolded();
            if (!matchedPaths.insert(normalizedPath).second) {
                continue;
            }

            ++totalMatches;
            candidates.push_back(&record);
        }

        const auto scoreRecord = [&keywordKey](const SearchRecord* record) {
            const QString nameKey = record->name.toCaseFolded();
            if (nameKey == keywordKey) {
                return 0;
            }
            if (nameKey.endsWith(keywordKey)) {
                return 1;
            }
            if (nameKey.startsWith(keywordKey)) {
                return 2;
            }
            if (nameKey.contains(keywordKey)) {
                return 3;
            }
            return 4;
        };
        std::stable_sort(candidates.begin(), candidates.end(), [&scoreRecord](const SearchRecord* left, const SearchRecord* right) {
            const int leftScore = scoreRecord(left);
            const int rightScore = scoreRecord(right);
            if (leftScore != rightScore) {
                return leftScore < rightScore;
            }
            return left->bytes > right->bytes;
        });

        auto rows = std::make_shared<QVector<ResultRow>>();
        rows->reserve(static_cast<int>(std::min<std::size_t>(candidates.size(), static_cast<std::size_t>(std::numeric_limits<int>::max()))));
        for (const SearchRecord* record : candidates) {
            rows->push_back(ResultRow{
                record->name,
                record->size,
                record->type,
                ContainingDirectoryForDisplay(record->path, record->type == QStringLiteral("目录") || record->type == QStringLiteral("磁盘")),
                record->path,
                record->searchKey,
                record->bytes,
                0,
                record->type == QStringLiteral("目录") || record->type == QStringLiteral("磁盘"),
                false,
                FormatModifiedDate(record->lastModifiedMsec),
                record->lastModifiedMsec,
            });
        }
        if (rows->isEmpty()) {
            rows->push_back(ResultRow{
                QStringLiteral("没有匹配结果"),
                QStringLiteral("-"),
                QStringLiteral("提示"),
                keyword,
                QString(),
                QString(),
                0,
                0,
                false,
                false,
            });
        }

        const double elapsedMilliseconds =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - searchStartedAt).count();

        QMetaObject::invokeMethod(this, [this, rows, requestId, keyword, elapsedMilliseconds, totalMatches]() {
            if (searchRequestId_.load() != requestId || searchModel_ == nullptr) {
                return;
            }
            searchResultRows_ = std::move(*rows);
            searchTotalMatchCount_ = totalMatches;
            searchVisibleResultCount_ = std::min(kSearchPageSize, static_cast<int>(searchResultRows_.size()));
            RenderVisibleSearchResults(keyword);
            if (searchScopeLabel_ != nullptr) {
                const bool hasMore = searchResultRows_.size() > searchVisibleResultCount_;
                searchScopeLabel_->setText(hasMore
                                               ? QStringLiteral("结果：显示 %1 / 匹配 %2 · %3 ms · 滚动到底部自动加载")
                                                     .arg(searchModel_->rowCount())
                                                     .arg(static_cast<qulonglong>(searchTotalMatchCount_))
                                                     .arg(elapsedMilliseconds, 0, 'f', 0)
                                               : QStringLiteral("结果：已全部显示 %1 / 匹配 %2 · %3 ms")
                                                     .arg(searchModel_->rowCount())
                                                     .arg(static_cast<qulonglong>(searchTotalMatchCount_))
                                                     .arg(elapsedMilliseconds, 0, 'f', 0));
                searchScopeLabel_->setToolTip(QStringLiteral("关键字：%1；结果很多时滚动到底部会继续加载").arg(keyword));
            }
            lastSearchKeyword_ = keyword;
            lastSearchElapsedMs_ = elapsedMilliseconds;
            UpdateSearchInfoBar();
        }, Qt::QueuedConnection);
    }).detach();
}

void MainWindow::RenderVisibleSearchResults(const QString& keyword) {
    if (searchModel_ == nullptr) {
        return;
    }

    const int totalRows = static_cast<int>(searchResultRows_.size());
    const int visibleCount = std::clamp(searchVisibleResultCount_, 0, totalRows);
    QVector<ResultRow> visibleRows;
    visibleRows.reserve(visibleCount);
    for (int index = 0; index < visibleCount; ++index) {
        visibleRows.push_back(searchResultRows_.at(index));
    }
    searchModel_->SetRows(std::move(visibleRows));

    const bool hasMore = searchResultRows_.size() > visibleCount;
    if (searchLoadMoreButton_ != nullptr) {
        searchLoadMoreButton_->setVisible(hasMore);
        searchLoadMoreButton_->setText(hasMore
                                           ? QStringLiteral("加载更多 %1").arg(std::min(kSearchPageSize, totalRows - visibleCount))
                                           : QStringLiteral("加载更多"));
    }
    if (searchScopeLabel_ != nullptr && !keyword.isEmpty()) {
        searchScopeLabel_->setText(hasMore
                                       ? QStringLiteral("结果：显示 %1 / 匹配 %2 · 滚动到底部自动加载")
                                             .arg(visibleCount)
                                             .arg(static_cast<qulonglong>(searchTotalMatchCount_))
                                       : QStringLiteral("结果：已全部显示 %1 / 匹配 %2")
                                             .arg(visibleCount)
                                             .arg(static_cast<qulonglong>(searchTotalMatchCount_)));
    }
}

void MainWindow::LoadMoreSearchResults() {
    if (searchResultRows_.isEmpty()) {
        return;
    }

    searchVisibleResultCount_ = std::min(searchVisibleResultCount_ + kSearchPageSize, static_cast<int>(searchResultRows_.size()));
    const QString keyword = searchEdit_ != nullptr ? searchEdit_->text().trimmed() : QString();
    RenderVisibleSearchResults(keyword);
}

void MainWindow::StartSystemSearchIndex() {
    if (searchIndexing_.exchange(true)) {
        return;
    }
    searchCacheLoadRequested_ = true;
    scanStartedAt_ = std::chrono::steady_clock::now();
    lastUiProgressMilliseconds_.store(SteadyMilliseconds());

    if (searchIndexButton_ != nullptr) {
        searchIndexButton_->setEnabled(false);
    }
    if (searchScopeLabel_ != nullptr) {
        searchScopeLabel_->setText(QStringLiteral("范围：正在索引全系统"));
    }
    if (searchModel_ != nullptr) {
        searchModel_->SetRows(QVector<ResultRow>{
            ResultRow{
                QStringLiteral("正在建立全系统索引"),
                QStringLiteral("-"),
                QStringLiteral("加载中"),
                QStringLiteral("优先读取 NTFS MFT，失败时回退兼容索引"),
                QString(),
                QString(),
                0,
                0,
                false,
                false,
            }
        });
    }

    const QStringList roots = EnumerateFixedDriveRoots();
    std::thread([this, roots]() {
        auto records = std::make_shared<std::vector<SearchRecord>>();
        auto volumeStates = std::make_shared<std::vector<SearchVolumeState>>();
        records->reserve(1000000);
        const auto cancelFlag = std::make_shared<std::atomic_bool>(false);
        bool usedFastIndex = false;
        std::uint64_t indexedFiles = 0;
        std::uint64_t indexedDirectories = 0;

        for (const QString& root : roots) {
            std::vector<SearchRecord> rootRecords;
            rootRecords.reserve(300000);
            SearchVolumeState rootVolumeState;
            const bool indexedByMft = CollectNtfsSearchIndex(root, rootRecords, rootVolumeState, *cancelFlag);

            if (!indexedByMft) {
                CollectSystemSearchIndex(root, rootRecords, *cancelFlag);
            } else {
                usedFastIndex = true;
                if (rootVolumeState.valid) {
                    volumeStates->push_back(rootVolumeState);
                }
            }

            for (SearchRecord& record : rootRecords) {
                if (record.type == QStringLiteral("目录") || record.type == QStringLiteral("磁盘")) {
                    ++indexedDirectories;
                } else {
                    ++indexedFiles;
                }
                records->push_back(std::move(record));
            }

            const std::uint64_t visibleItems = indexedFiles + indexedDirectories;
            QMetaObject::invokeMethod(this, [this, root, visibleItems]() {
                if (searchScopeLabel_ != nullptr) {
                    searchScopeLabel_->setText(QStringLiteral("范围：正在索引 %1 · 已收集 %2 项").arg(root).arg(static_cast<qulonglong>(visibleItems)));
                }
            }, Qt::QueuedConnection);
        }

        QMetaObject::invokeMethod(this, [this, records, volumeStates, usedFastIndex]() {
            searchIndex_ = records;
            searchVolumeStates_ = volumeStates;
            searchIndexing_.store(false);
            if (searchIndexButton_ != nullptr) {
                searchIndexButton_->setEnabled(true);
            }
            if (searchScopeLabel_ != nullptr) {
                searchScopeLabel_->setText(QStringLiteral("范围：%1 全量 %2 项").arg(usedFastIndex ? QStringLiteral("MFT 索引") : QStringLiteral("兼容索引")).arg(static_cast<qulonglong>(searchIndex_->size())));
            }
            PopulateSearchTable();
        }, Qt::QueuedConnection);

        SaveSystemSearchIndexCacheSnapshot(*records, *volumeStates);
    }).detach();
}

void MainWindow::LoadSystemSearchIndexCache() {
    if (searchIndexing_.load() || searchCacheLoading_.exchange(true)) {
        return;
    }

    if (searchIndexButton_ != nullptr) {
        searchIndexButton_->setEnabled(false);
    }
    if (searchScopeLabel_ != nullptr) {
        searchScopeLabel_->setText(QStringLiteral("范围：正在加载索引缓存"));
    }

    std::thread([this]() {
        auto records = std::make_shared<std::vector<SearchRecord>>();
        auto volumeStates = std::make_shared<std::vector<SearchVolumeState>>();
        QFile file(SearchIndexCacheFilePath());
        bool loaded = false;
        quint32 version = 0;
        if (file.open(QIODevice::ReadOnly)) {
            QDataStream stream(&file);
            stream.setVersion(QDataStream::Qt_6_0);

            quint32 magic = 0;
            quint64 count = 0;
            stream >> magic >> version >> count;
            if (magic == 0x4E444D53 && (version == 3 || version == 4 || version == 5) && stream.status() == QDataStream::Ok) {
                if (version >= 4) {
                    quint64 stateCount = 0;
                    stream >> stateCount;
                    volumeStates->reserve(static_cast<std::size_t>(stateCount));
                    for (quint64 stateIndex = 0; stateIndex < stateCount && stream.status() == QDataStream::Ok; ++stateIndex) {
                        SearchVolumeState state;
                        quint64 volumeSerialNumber = 0;
                        quint64 journalId = 0;
                        qint64 firstUsn = 0;
                        qint64 nextUsn = 0;
                        quint8 valid = 0;
                        stream >> state.rootPath
                               >> volumeSerialNumber
                               >> journalId
                               >> firstUsn
                               >> nextUsn
                               >> valid;
                        state.volumeSerialNumber = static_cast<std::uint64_t>(volumeSerialNumber);
                        state.journalId = static_cast<std::uint64_t>(journalId);
                        state.firstUsn = static_cast<std::int64_t>(firstUsn);
                        state.nextUsn = static_cast<std::int64_t>(nextUsn);
                        state.valid = valid != 0;
                        volumeStates->push_back(std::move(state));
                    }
                }

                records->reserve(static_cast<std::size_t>(count));
                for (quint64 index = 0; index < count && stream.status() == QDataStream::Ok; ++index) {
                    SearchRecord record;
                    quint64 bytes = 0;
                    stream >> record.name >> record.size >> record.type >> record.path;
                    stream >> record.searchKey;
                    stream >> bytes;
                    record.bytes = static_cast<std::uint64_t>(bytes);
                    if (version >= 4) {
                        quint64 fileReference = 0;
                        quint64 parentReference = 0;
                        stream >> record.volumeRoot >> fileReference >> parentReference;
                        record.fileReference = static_cast<std::uint64_t>(fileReference);
                        record.parentReference = static_cast<std::uint64_t>(parentReference);
                    }
                    if (version >= 5) {
                        qint64 lastModifiedMsec = 0;
                        stream >> lastModifiedMsec;
                        record.lastModifiedMsec = lastModifiedMsec;
                    }
                    if (record.searchKey.isEmpty()) {
                        record.searchKey = MakeSearchKey(record.name, record.path);
                    }
                    records->push_back(std::move(record));
                }
                loaded = stream.status() == QDataStream::Ok;
            }
        }

        QMetaObject::invokeMethod(this, [this, records, volumeStates, loaded, version]() {
            searchCacheLoading_.store(false);
            if (searchIndexButton_ != nullptr) {
                searchIndexButton_->setEnabled(true);
            }

            if (loaded && !records->empty()) {
                searchIndex_ = records;
                searchVolumeStates_ = volumeStates;
                // v3/v4 缓存没有修改时间字段,必须重建一次以补全 mtime(写入 v5)。
                const bool needsUpgrade = version < 5;
                if (searchScopeLabel_ != nullptr) {
                    searchScopeLabel_->setText((needsUpgrade || volumeStates->empty())
                        ? QStringLiteral("范围：%1缓存 %2 项 · 正在升级以补充修改时间")
                            .arg(volumeStates->empty() ? QStringLiteral("旧版") : QStringLiteral("全系统"))
                            .arg(static_cast<qulonglong>(searchIndex_->size()))
                        : QStringLiteral("范围：全系统缓存 %1 项 · 正在增量校验")
                            .arg(static_cast<qulonglong>(searchIndex_->size())));
                }
                PopulateSearchTable();
                if (volumeStates->empty() || needsUpgrade) {
                    QTimer::singleShot(0, this, &MainWindow::StartSystemSearchIndex);
                }
                return;
            }

            if (searchScopeLabel_ != nullptr) {
                searchScopeLabel_->setText(QStringLiteral("范围：未找到缓存，正在建立全系统索引"));
            }
            if (searchModel_ != nullptr) {
                searchModel_->SetRows(QVector<ResultRow>{
                    ResultRow{
                        QStringLiteral("正在建立全系统索引"),
                        QStringLiteral("-"),
                        QStringLiteral("加载中"),
                        QStringLiteral("首次使用需要建立缓存，之后会优先走 USN 增量更新"),
                        QString(),
                        QString(),
                        0,
                        0,
                        false,
                        false,
                    }
                });
            }
            QTimer::singleShot(0, this, &MainWindow::StartSystemSearchIndex);
        }, Qt::QueuedConnection);

        // v3/v4 缓存缺修改时间,交给上面的 StartSystemSearchIndex 全量重建,这里不做增量。
        if (loaded && !records->empty() && !volumeStates->empty() && version >= 5) {
            auto refreshedRecords = std::make_shared<std::vector<SearchRecord>>(*records);
            auto refreshedVolumeStates = std::make_shared<std::vector<SearchVolumeState>>(*volumeStates);
            const bool incrementallyRefreshed = RefreshSearchIndexFromJournal(*refreshedRecords, *refreshedVolumeStates);
            if (incrementallyRefreshed) {
                SaveSystemSearchIndexCacheSnapshot(*refreshedRecords, *refreshedVolumeStates);
                QMetaObject::invokeMethod(this, [this, refreshedRecords, refreshedVolumeStates]() {
                    searchIndex_ = refreshedRecords;
                    searchVolumeStates_ = refreshedVolumeStates;
                    if (searchScopeLabel_ != nullptr) {
                        searchScopeLabel_->setText(QStringLiteral("范围：增量缓存 %1 项").arg(static_cast<qulonglong>(searchIndex_->size())));
                    }
                    PopulateSearchTable();
                }, Qt::QueuedConnection);
            } else {
                QMetaObject::invokeMethod(this, [this]() {
                    if (searchScopeLabel_ != nullptr) {
                        searchScopeLabel_->setText(QStringLiteral("范围：缓存可用，正在后台重建增量索引"));
                    }
                    StartSystemSearchIndex();
                }, Qt::QueuedConnection);
            }
        }
    }).detach();
}

void MainWindow::SaveSystemSearchIndexCache() const {
    const auto indexSnapshot = searchIndex_;
    const auto stateSnapshot = searchVolumeStates_;
    if (indexSnapshot == nullptr) {
        SaveSystemSearchIndexCacheSnapshot(std::vector<SearchRecord>{}, std::vector<SearchVolumeState>{});
        return;
    }

    SaveSystemSearchIndexCacheSnapshot(*indexSnapshot, stateSnapshot == nullptr ? std::vector<SearchVolumeState>{} : *stateSnapshot);
}

void MainWindow::SaveSystemSearchIndexCacheSnapshot(const std::vector<SearchRecord>& records, const std::vector<SearchVolumeState>& volumeStates) {
    QFile file(SearchIndexCacheFilePath());
    const QString temporaryPath = SearchIndexCacheFilePath() + QStringLiteral(".tmp");
    QFile temporaryFile(temporaryPath);
    if (temporaryFile.exists()) {
        temporaryFile.remove();
    }

    file.setFileName(temporaryPath);
    if (!file.open(QIODevice::WriteOnly)) {
        return;
    }

    QDataStream stream(&file);
    stream.setVersion(QDataStream::Qt_6_0);
    stream << static_cast<quint32>(0x4E444D53);
    stream << static_cast<quint32>(5);
    stream << static_cast<quint64>(records.size());
    stream << static_cast<quint64>(volumeStates.size());
    for (const SearchVolumeState& state : volumeStates) {
        stream << state.rootPath;
        stream << static_cast<quint64>(state.volumeSerialNumber);
        stream << static_cast<quint64>(state.journalId);
        stream << static_cast<qint64>(state.firstUsn);
        stream << static_cast<qint64>(state.nextUsn);
        stream << static_cast<quint8>(state.valid ? 1 : 0);
    }

    for (const SearchRecord& record : records) {
        stream << record.name;
        stream << record.size;
        stream << record.type;
        stream << record.path;
        stream << record.searchKey;
        stream << static_cast<quint64>(record.bytes);
        stream << record.volumeRoot;
        stream << static_cast<quint64>(record.fileReference);
        stream << static_cast<quint64>(record.parentReference);
        stream << static_cast<qint64>(record.lastModifiedMsec);
    }
    file.close();

    if (stream.status() != QDataStream::Ok) {
        QFile::remove(temporaryPath);
        return;
    }

    QFile::remove(SearchIndexCacheFilePath());
    QFile::rename(temporaryPath, SearchIndexCacheFilePath());
}

void MainWindow::ScanCleanupCandidates() {
    if (cleanupScanning_.exchange(true)) {
        return;
    }

    cleanupTree_->clear();
    cleanupScanButton_->setEnabled(false);
    cleanupDeleteButton_->setEnabled(false);
    if (cleanupSelectedLabel_ != nullptr) {
        cleanupSelectedLabel_->setText(QStringLiteral("已选中 0 B"));
    }
    for (QLabel* label : cleanupSectionValueLabels_) {
        if (label != nullptr) {
            label->setText(QStringLiteral("扫描中"));
        }
    }
    for (QPushButton* button : cleanupSectionButtons_) {
        if (button != nullptr) {
            const QString title = button->text().section(QLatin1Char('\n'), 0, 0);
            button->setText(title + QStringLiteral("\n扫描中"));
        }
    }
    cleanupSummaryLabel_->setText(QStringLiteral("正在扫描可安全清理的临时文件和缓存..."));
    if (cleanupTotalLabel_ != nullptr) {
        cleanupTotalLabel_->setText(QStringLiteral("扫描中"));
    }
    if (cleanupSafeCountLabel_ != nullptr) {
        cleanupSafeCountLabel_->setText(QStringLiteral("--"));
    }
    if (cleanupAttentionCountLabel_ != nullptr) {
        cleanupAttentionCountLabel_->setText(QStringLiteral("--"));
    }
    if (cleanupStatusLabel_ != nullptr) {
        cleanupStatusLabel_->setText(QStringLiteral("正在体检临时文件、浏览器缓存、系统日志和开发工具缓存"));
    }
    SetBusyState(true, QStringLiteral("扫描垃圾"));
    auto* loadingItem = new QTreeWidgetItem(cleanupTree_);
    loadingItem->setText(0, QStringLiteral("正在分析可清理项目"));
    loadingItem->setText(1, QStringLiteral("-"));
    loadingItem->setText(2, QStringLiteral("加载中"));
    loadingItem->setText(3, QStringLiteral("正在统计临时文件、缓存、日志和开发工具缓存"));
    const bool scanPrivacy = cleanupPrivacyCheckBox_ != nullptr && cleanupPrivacyCheckBox_->isChecked();
    const bool scanDeveloper = cleanupDeveloperCheckBox_ != nullptr && cleanupDeveloperCheckBox_->isChecked();

    std::thread([this, scanPrivacy, scanDeveloper]() {
        auto rows = std::make_shared<std::vector<CleanupRow>>();
        rows->reserve(1800);
        constexpr std::size_t maxRows = 1800;

        const QString localAppData = qEnvironmentVariable("LOCALAPPDATA");
        const QString appData = qEnvironmentVariable("APPDATA");
        const QString programData = qEnvironmentVariable("ProgramData");
        const QString userProfile = qEnvironmentVariable("USERPROFILE");
        CollectCleanupRows(QDir::tempPath(), QStringLiteral("用户临时文件"), *rows, maxRows);
        CollectCleanupRows(QStringLiteral("C:/Windows/Temp"), QStringLiteral("系统临时文件"), *rows, maxRows);
        CollectCleanupRows(localAppData + QStringLiteral("/Temp"), QStringLiteral("应用临时文件"), *rows, maxRows);
        CollectCleanupRows(localAppData + QStringLiteral("/Microsoft/Windows/INetCache"), QStringLiteral("网络缓存"), *rows, maxRows);
        CollectCleanupRows(localAppData + QStringLiteral("/Microsoft/Edge/User Data/Default/Cache"), QStringLiteral("Edge 缓存"), *rows, maxRows);
        CollectCleanupRows(localAppData + QStringLiteral("/Microsoft/Edge/User Data/Default/Code Cache"), QStringLiteral("Edge 脚本缓存"), *rows, maxRows);
        CollectCleanupDirectory(localAppData + QStringLiteral("/Microsoft/Edge/User Data/Default/GPUCache"), QStringLiteral("Edge GPU 缓存"), *rows, maxRows);
        CollectCleanupDirectory(localAppData + QStringLiteral("/Microsoft/Edge/User Data/Default/Service Worker/CacheStorage"), QStringLiteral("Edge 离线缓存"), *rows, maxRows);
        CollectCleanupRows(localAppData + QStringLiteral("/Google/Chrome/User Data/Default/Cache"), QStringLiteral("Chrome 缓存"), *rows, maxRows);
        CollectCleanupRows(localAppData + QStringLiteral("/Google/Chrome/User Data/Default/Code Cache"), QStringLiteral("Chrome 脚本缓存"), *rows, maxRows);
        CollectCleanupDirectory(localAppData + QStringLiteral("/Google/Chrome/User Data/Default/GPUCache"), QStringLiteral("Chrome GPU 缓存"), *rows, maxRows);
        CollectCleanupDirectory(localAppData + QStringLiteral("/Google/Chrome/User Data/Default/Service Worker/CacheStorage"), QStringLiteral("Chrome 离线缓存"), *rows, maxRows);
        CollectProfileCacheDirectories(localAppData + QStringLiteral("/Mozilla/Firefox/Profiles"), QStringLiteral("cache2"), QStringLiteral("Firefox 缓存"), *rows, maxRows);
        CollectProfileCacheDirectories(localAppData + QStringLiteral("/Mozilla/Firefox/Profiles"), QStringLiteral("startupCache"), QStringLiteral("Firefox 启动缓存"), *rows, maxRows);
        CollectCleanupRows(localAppData + QStringLiteral("/CrashDumps"), QStringLiteral("崩溃转储"), *rows, maxRows);
        CollectCleanupRows(localAppData + QStringLiteral("/Microsoft/Teams/Cache"), QStringLiteral("Teams 缓存"), *rows, maxRows);
        CollectCleanupRows(appData + QStringLiteral("/Code/Cache"), QStringLiteral("VS Code 缓存"), *rows, maxRows);
        CollectCleanupRows(appData + QStringLiteral("/Code/CachedData"), QStringLiteral("VS Code 脚本缓存"), *rows, maxRows);
        CollectCleanupRows(localAppData + QStringLiteral("/JetBrains/Transient"), QStringLiteral("JetBrains 临时缓存"), *rows, maxRows);
        CollectCleanupRows(localAppData + QStringLiteral("/JetBrains/IntelliJIdea2026.1/log"), QStringLiteral("JetBrains 日志"), *rows, maxRows);
        CollectCleanupRows(localAppData + QStringLiteral("/JetBrains/PyCharm2026.1/log"), QStringLiteral("JetBrains 日志"), *rows, maxRows);
        CollectCleanupRows(localAppData + QStringLiteral("/JetBrains/PhpStorm2026.1/log"), QStringLiteral("JetBrains 日志"), *rows, maxRows);
        CollectCleanupRows(localAppData + QStringLiteral("/Microsoft/Office/16.0/OfficeFileCache"), QStringLiteral("Office 文件缓存"), *rows, maxRows);
        CollectCleanupDirectory(QStringLiteral("C:/$Recycle.Bin"), QStringLiteral("回收站"), *rows, maxRows);
        CollectCleanupDirectory(QStringLiteral("C:/Windows/SoftwareDistribution/Download"), QStringLiteral("Windows 更新下载缓存"), *rows, maxRows);
        CollectCleanupDirectory(QStringLiteral("C:/Windows/SoftwareDistribution/DeliveryOptimization"), QStringLiteral("Windows 传递优化缓存"), *rows, maxRows);
        CollectCleanupDirectory(QStringLiteral("C:/Windows/Prefetch"), QStringLiteral("预读取缓存"), *rows, maxRows);
        CollectCleanupDirectory(QStringLiteral("C:/Windows/Logs/CBS"), QStringLiteral("Windows 组件日志"), *rows, maxRows);
        CollectCleanupDirectory(QStringLiteral("C:/Windows/Logs/DISM"), QStringLiteral("DISM 维护日志"), *rows, maxRows);
        CollectCleanupDirectory(programData + QStringLiteral("/Microsoft/Windows/WER/ReportArchive"), QStringLiteral("Windows 错误报告"), *rows, maxRows);
        CollectCleanupDirectory(programData + QStringLiteral("/Microsoft/Windows/WER/ReportQueue"), QStringLiteral("Windows 错误报告"), *rows, maxRows);
        CollectCleanupDirectory(localAppData + QStringLiteral("/Microsoft/Windows/WER"), QStringLiteral("用户错误报告"), *rows, maxRows);
        CollectPrefixedFiles(localAppData + QStringLiteral("/Microsoft/Windows/Explorer"), QStringLiteral("thumbcache_"), QStringLiteral("缩略图缓存"), *rows, maxRows);
        CollectPrefixedFiles(localAppData + QStringLiteral("/Microsoft/Windows/Explorer"), QStringLiteral("iconcache_"), QStringLiteral("图标缓存"), *rows, maxRows);
        CollectCleanupDirectory(localAppData + QStringLiteral("/D3DSCache"), QStringLiteral("DirectX Shader Cache"), *rows, maxRows);
        CollectCleanupDirectory(localAppData + QStringLiteral("/NVIDIA/DXCache"), QStringLiteral("显卡 Shader Cache"), *rows, maxRows);
        CollectCleanupDirectory(localAppData + QStringLiteral("/NVIDIA/GLCache"), QStringLiteral("显卡 Shader Cache"), *rows, maxRows);
        CollectCleanupDirectory(localAppData + QStringLiteral("/AMD/DxCache"), QStringLiteral("显卡 Shader Cache"), *rows, maxRows);
        if (scanPrivacy) {
            CollectCleanupRows(appData + QStringLiteral("/Microsoft/Windows/Recent"), QStringLiteral("最近访问记录"), *rows, maxRows);
            CollectCleanupRows(appData + QStringLiteral("/Microsoft/Windows/Recent/AutomaticDestinations"), QStringLiteral("跳转列表记录"), *rows, maxRows);
            CollectCleanupRows(appData + QStringLiteral("/Microsoft/Windows/Recent/CustomDestinations"), QStringLiteral("跳转列表记录"), *rows, maxRows);
        }
        if (scanDeveloper) {
            CollectCleanupDirectory(appData + QStringLiteral("/npm-cache"), QStringLiteral("npm 缓存"), *rows, maxRows);
            CollectCleanupDirectory(localAppData + QStringLiteral("/pip/Cache"), QStringLiteral("pip 缓存"), *rows, maxRows);
            CollectCleanupDirectory(localAppData + QStringLiteral("/NuGet/Cache"), QStringLiteral("NuGet 缓存"), *rows, maxRows);
            CollectCleanupDirectory(userProfile + QStringLiteral("/.gradle/caches"), QStringLiteral("Gradle 缓存"), *rows, maxRows);
            CollectCleanupDirectory(userProfile + QStringLiteral("/.m2/repository"), QStringLiteral("Maven 本地缓存"), *rows, maxRows);
            CollectCleanupDirectory(userProfile + QStringLiteral("/.cargo/registry/cache"), QStringLiteral("Cargo 缓存"), *rows, maxRows);
            CollectCleanupDirectory(userProfile + QStringLiteral("/.pnpm-store"), QStringLiteral("pnpm 缓存"), *rows, maxRows);
            CollectCleanupDirectory(userProfile + QStringLiteral("/.cache/yarn"), QStringLiteral("Yarn 缓存"), *rows, maxRows);
            CollectCleanupDirectory(localAppData + QStringLiteral("/Yarn/Cache"), QStringLiteral("Yarn 缓存"), *rows, maxRows);
            CollectCleanupDirectory(localAppData + QStringLiteral("/pnpm/store"), QStringLiteral("pnpm 缓存"), *rows, maxRows);
            CollectCleanupDirectory(localAppData + QStringLiteral("/Docker/log"), QStringLiteral("Docker 日志"), *rows, maxRows);
            CollectCleanupDirectory(localAppData + QStringLiteral("/Docker/cache"), QStringLiteral("Docker 缓存"), *rows, maxRows);
        }

        QMetaObject::invokeMethod(this, [this, rows]() {
            cleanupGroups_.clear();
            cleanupTreeUpdating_ = true;
            cleanupTree_->clear();

            std::map<QString, CleanupGroup> groups;
            for (const CleanupRow& row : *rows) {
                auto& group = groups[row.type];
                group.name = row.type;
                group.section = row.section;
                group.bytes += row.bytes;
                group.paths.push_back(row.path);
                group.pathBytes.push_back(row.bytes);
            }

            for (auto& pair : groups) {
                CleanupGroup group = std::move(pair.second);
                if (group.name.contains(QStringLiteral("更新")) ||
                    group.name.contains(QStringLiteral("日志")) ||
                    group.name.contains(QStringLiteral("错误报告")) ||
                    group.name.contains(QStringLiteral("崩溃转储"))) {
                    group.risk = QStringLiteral("谨慎");
                    group.description = QStringLiteral("系统诊断或维护数据，通常可清理，但可能影响问题排查或更新回滚。");
                } else if (group.name.contains(QStringLiteral("npm")) ||
                           group.name.contains(QStringLiteral("pip")) ||
                           group.name.contains(QStringLiteral("NuGet")) ||
                           group.name.contains(QStringLiteral("Gradle")) ||
                           group.name.contains(QStringLiteral("Maven"))) {
                    group.risk = QStringLiteral("高级");
                    group.description = QStringLiteral("开发工具缓存，清理后可释放空间，但后续构建或安装依赖会重新下载。");
                } else if (group.name.contains(QStringLiteral("回收站"))) {
                    group.risk = QStringLiteral("谨慎");
                    group.description = QStringLiteral("会清空已删除文件的缓存副本，清理后仍会先移入回收站处理队列。");
                } else if (group.name.contains(QStringLiteral("预读取"))) {
                    group.risk = QStringLiteral("谨慎");
                    group.description = QStringLiteral("Windows 启动和应用预读取缓存，清理后系统会自动重建，短期可能影响启动速度。");
                } else if (group.name.contains(QStringLiteral("Shader"))) {
                    group.risk = QStringLiteral("安全");
                    group.description = QStringLiteral("图形驱动或 DirectX 着色器缓存，清理后游戏或图形应用会在下次启动时重建。");
                } else if (group.name.contains(QStringLiteral("浏览器")) ||
                           group.name.contains(QStringLiteral("缓存")) ||
                           group.name.contains(QStringLiteral("网络缓存")) ||
                           group.name.contains(QStringLiteral("缩略图")) ||
                           group.name.contains(QStringLiteral("图标"))) {
                    group.risk = QStringLiteral("安全");
                    group.description = QStringLiteral("缓存文件可安全清理，系统或应用会在需要时自动重建。");
                } else if (group.name.contains(QStringLiteral("临时"))) {
                    group.risk = QStringLiteral("安全");
                    group.description = QStringLiteral("临时文件通常可安全清理，正在使用的文件会自动跳过。");
                } else {
                    group.risk = QStringLiteral("谨慎");
                    group.description = QStringLiteral("建议确认用途后清理，避免影响诊断或重新下载缓存。");
                }
                group.checkedByDefault = CleanupRiskCheckedByDefault(group.risk);
                if (group.risk == QStringLiteral("安全")) {
                    group.recommendation = QStringLiteral("推荐清理");
                } else if (group.risk == QStringLiteral("谨慎")) {
                    group.recommendation = QStringLiteral("确认后清理");
                } else {
                    group.recommendation = QStringLiteral("高级用户确认");
                }
                cleanupGroups_.push_back(std::move(group));
            }

            std::sort(cleanupGroups_.begin(), cleanupGroups_.end(), [](const CleanupGroup& left, const CleanupGroup& right) {
                const int leftOrder = CleanupSectionOrder(left.section);
                const int rightOrder = CleanupSectionOrder(right.section);
                if (leftOrder != rightOrder) {
                    return leftOrder < rightOrder;
                }
                if (left.section != right.section) {
                    return left.section < right.section;
                }
                return left.bytes > right.bytes;
            });

            std::uint64_t totalBytes = 0;
            int safeCount = 0;
            int attentionCount = 0;
            std::map<QString, std::uint64_t> sectionBytes;
            std::map<QString, int> sectionCounts;
            for (const CleanupGroup& group : cleanupGroups_) {
                sectionBytes[group.section] += group.bytes;
                sectionCounts[group.section] += 1;
            }
            const std::uint64_t softwareBytes = sectionBytes[QStringLiteral("浏览器缓存")] + sectionBytes[QStringLiteral("应用缓存")];
            const std::uint64_t allBytes = softwareBytes +
                sectionBytes[QStringLiteral("系统清理")] +
                sectionBytes[QStringLiteral("隐私痕迹")] +
                sectionBytes[QStringLiteral("图形缓存")] +
                sectionBytes[QStringLiteral("开发工具")];
            const QStringList sectionOrder = {
                QStringLiteral("全部"),
                QStringLiteral("软件缓存"),
                QStringLiteral("系统清理"),
                QStringLiteral("隐私痕迹"),
                QStringLiteral("图形缓存"),
                QStringLiteral("开发工具")
            };
            for (int labelIndex = 0; labelIndex < cleanupSectionValueLabels_.size(); ++labelIndex) {
                QLabel* label = cleanupSectionValueLabels_[static_cast<std::size_t>(labelIndex)];
                QPushButton* button = labelIndex < cleanupSectionButtons_.size()
                    ? cleanupSectionButtons_[static_cast<std::size_t>(labelIndex)]
                    : nullptr;
                if (label == nullptr || button == nullptr) {
                    continue;
                }
                const QString section = labelIndex < sectionOrder.size() ? sectionOrder.at(labelIndex) : QString();
                const std::uint64_t bytes = labelIndex == 0
                    ? allBytes
                    : (section == QStringLiteral("软件缓存")
                        ? softwareBytes
                        : sectionBytes[section]);
                const QString bytesText = ToQString(core::FormatBytes(bytes));
                label->setText(bytesText);
                const QString title = button->text().section(QLatin1Char('\n'), 0, 0);
                button->setText(title + QStringLiteral("\n") + bytesText);
                button->setToolTip(section == QStringLiteral("全部")
                    ? QStringLiteral("显示全部清理类别，共 %1").arg(bytesText)
                    : QStringLiteral("只显示%1，共 %2").arg(title, bytesText));
            }

            for (std::size_t index = 0; index < cleanupGroups_.size(); ++index) {
                const CleanupGroup& group = cleanupGroups_[index];
                totalBytes += group.bytes;
                if (group.risk == QStringLiteral("安全")) {
                    ++safeCount;
                } else {
                    ++attentionCount;
                }

                auto* groupItem = new QTreeWidgetItem(cleanupTree_);
                groupItem->setIcon(0, app_icons::folder(16));
                groupItem->setText(0, group.name);
                groupItem->setText(1, ToQString(core::FormatBytes(group.bytes)));
                groupItem->setText(2, group.risk == QStringLiteral("安全") ? QStringLiteral("建议清理") : group.recommendation);
                groupItem->setText(3, QStringLiteral("%1 · 共 %2 项").arg(group.description).arg(static_cast<qulonglong>(group.paths.size())));
                groupItem->setCheckState(0, group.checkedByDefault ? Qt::Checked : Qt::Unchecked);
                groupItem->setData(0, Qt::UserRole, static_cast<qulonglong>(index));
                groupItem->setToolTip(0, group.description);
                groupItem->setToolTip(1, groupItem->text(1));
                groupItem->setToolTip(2, group.recommendation);
                groupItem->setToolTip(3, groupItem->text(3));
                QFont groupFont = groupItem->font(0);
                groupFont.setBold(true);
                groupItem->setFont(0, groupFont);
                groupItem->setForeground(2, group.risk == QStringLiteral("安全") ? QColor(g_activeTokens.good) : QColor(g_activeTokens.danger));

                const std::size_t maxChildRows = std::min<std::size_t>(group.paths.size(), 80);
                for (std::size_t pathIndex = 0; pathIndex < maxChildRows; ++pathIndex) {
                    const QString path = QDir::toNativeSeparators(group.paths[pathIndex]);
                    const std::uint64_t pathBytes = pathIndex < group.pathBytes.size() ? group.pathBytes[pathIndex] : 0;
                    QFileInfo fileInfo(path);
                    auto* pathItem = new QTreeWidgetItem(groupItem);
                    pathItem->setIcon(0, fileInfo.isDir() ? app_icons::folder(16) : app_icons::fileGlyph(16));
                    pathItem->setText(0, fileInfo.fileName().isEmpty() ? path : fileInfo.fileName());
                    pathItem->setText(1, ToQString(core::FormatBytes(pathBytes)));
                    pathItem->setText(2, group.recommendation);
                    pathItem->setText(3, path);
                    pathItem->setCheckState(0, group.checkedByDefault ? Qt::Checked : Qt::Unchecked);
                    pathItem->setData(0, Qt::UserRole, static_cast<qulonglong>(index));
                    pathItem->setData(0, Qt::UserRole + 1, path);
                    pathItem->setData(0, Qt::UserRole + 2, static_cast<qulonglong>(pathBytes));
                    pathItem->setToolTip(0, path);
                    pathItem->setToolTip(1, pathItem->text(1));
                    pathItem->setToolTip(2, group.recommendation);
                    pathItem->setToolTip(3, path);
                    pathItem->setForeground(0, QColor(g_activeTokens.textPrimary));
                    pathItem->setForeground(3, QColor(g_activeTokens.textSecondary));
                }
                if (group.paths.size() > maxChildRows) {
                    auto* moreItem = new QTreeWidgetItem(groupItem);
                    moreItem->setIcon(0, app_icons::info(16));
                    moreItem->setText(0, QStringLiteral("另有 %1 项未在列表中展开显示").arg(static_cast<qulonglong>(group.paths.size() - maxChildRows)));
                    moreItem->setText(1, QStringLiteral("-"));
                    moreItem->setText(2, QStringLiteral("提示"));
                    moreItem->setText(3, QStringLiteral("清理时仍会包含这些项目"));
                    moreItem->setFlags(Qt::ItemIsEnabled);
                    moreItem->setForeground(0, QColor(g_activeTokens.textMuted));
                }
            }
            if (cleanupGroups_.empty()) {
                auto* emptyItem = new QTreeWidgetItem(cleanupTree_);
                emptyItem->setText(0, QStringLiteral("暂未发现可建议清理的项目"));
                emptyItem->setText(1, QStringLiteral("-"));
                emptyItem->setText(2, QStringLiteral("完成"));
                emptyItem->setText(3, QStringLiteral("可以稍后重新扫描，或切换到大文件视图手动分析空间占用"));
            }
            cleanupTree_->collapseAll();
            cleanupTreeUpdating_ = false;
            ApplyCleanupSectionFilter(cleanupSectionFilter_);
            UpdateCleanupSelectionSummary();
            if (cleanupTotalLabel_ != nullptr) {
                cleanupTotalLabel_->setText(ToQString(core::FormatBytes(totalBytes)));
            }
            if (cleanupSafeCountLabel_ != nullptr) {
                cleanupSafeCountLabel_->setText(QStringLiteral("%1 项").arg(safeCount));
            }
            if (cleanupAttentionCountLabel_ != nullptr) {
                cleanupAttentionCountLabel_->setText(QStringLiteral("%1 项").arg(attentionCount));
            }
            if (cleanupStatusLabel_ != nullptr) {
                cleanupStatusLabel_->setText(totalBytes > 0
                                                ? QStringLiteral("共扫描出 %1 系统垃圾，安全项已默认勾选，谨慎和高级项建议展开确认")
                                                      .arg(ToQString(core::FormatBytes(totalBytes)))
                                                : QStringLiteral("体检完成：当前未发现明显可建议清理项"));
            }
            cleanupSummaryLabel_->setText(QStringLiteral("发现 %1 个清理类别 · 推荐 %2 项 · 需确认 %3 项")
                                              .arg(static_cast<qulonglong>(cleanupGroups_.size()))
                                              .arg(safeCount)
                                              .arg(attentionCount));
            cleanupScanButton_->setEnabled(true);
            UpdateCleanupSelectionSummary();
            cleanupScanning_.store(false);
            SetBusyState(false, QString());
        }, Qt::QueuedConnection);
    }).detach();
}

void MainWindow::DeleteSelectedCleanupItems() {
    if (cleanupTree_ == nullptr) {
        return;
    }

    std::set<QString> paths;
    QStringList selectedGroups;
    QStringList cautiousGroups;
    std::uint64_t selectedBytes = 0;
    for (int row = 0; row < cleanupTree_->topLevelItemCount(); ++row) {
        QTreeWidgetItem* groupItem = cleanupTree_->topLevelItem(row);
        const std::size_t groupIndex = CleanupGroupIndexFromItem(groupItem);
        if (groupItem == nullptr || groupIndex >= cleanupGroups_.size()) {
            continue;
        }

        const CleanupGroup& group = cleanupGroups_[groupIndex];
        if (groupItem->checkState(0) == Qt::Unchecked) {
            continue;
        }

        bool groupSelected = false;
        if (groupItem->checkState(0) == Qt::Checked) {
            groupSelected = true;
            selectedBytes += group.bytes;
            for (const QString& path : group.paths) {
                paths.insert(path);
            }
        } else {
            for (int childIndex = 0; childIndex < groupItem->childCount(); ++childIndex) {
                QTreeWidgetItem* child = groupItem->child(childIndex);
                if (child == nullptr || child->checkState(0) != Qt::Checked) {
                    continue;
                }
                const QString childPath = child->data(0, Qt::UserRole + 1).toString();
                if (childPath.isEmpty()) {
                    continue;
                }
                groupSelected = true;
                selectedBytes += child->data(0, Qt::UserRole + 2).toULongLong();
                paths.insert(childPath);
            }
        }

        if (!groupSelected) {
            continue;
        }
        selectedGroups << (groupItem->checkState(0) == Qt::Checked ? group.name : group.name + QStringLiteral("（部分）"));
        if (group.risk != QStringLiteral("安全")) {
            cautiousGroups << group.name + QStringLiteral("（") + group.risk + QStringLiteral("）");
        }
    }

    if (paths.empty()) {
        ShowAppMessageBox(this, QMessageBox::Information, QStringLiteral("垃圾清理"), QStringLiteral("请先勾选要清理的类别。"));
        return;
    }

    const bool permanentDelete = cleanupDeepCleanCheckBox_ != nullptr && cleanupDeepCleanCheckBox_->isChecked();
    const QString cleanupModeText = permanentDelete ? QStringLiteral("深度清理会直接删除，无法从回收站还原") : QStringLiteral("普通清理会移入回收站，可手动还原");
    const QMessageBox::StandardButton choice = ShowAppMessageBox(
        this,
        QMessageBox::Question,
        QStringLiteral("确认清理"),
        QStringLiteral("将清理以下类别：\n%1\n\n预计释放：%2\n%3%4。\n共 %5 个候选项，是否继续？")
            .arg(selectedGroups.join(QStringLiteral("、")))
            .arg(ToQString(core::FormatBytes(selectedBytes)))
            .arg(cautiousGroups.isEmpty() ? QString() : QStringLiteral("包含需注意项目：%1\n\n").arg(cautiousGroups.join(QStringLiteral("、"))))
            .arg(cleanupModeText)
            .arg(static_cast<qulonglong>(paths.size())),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);
    if (choice != QMessageBox::Yes) {
        return;
    }

    int cleaned = 0;
    SetBusyState(true, permanentDelete ? QStringLiteral("深度清理") : QStringLiteral("正在清理"));
    for (const QString& path : paths) {
        if (permanentDelete ? PermanentlyDeletePath(path) : RecyclePath(path)) {
            ++cleaned;
        }
    }
    SetBusyState(false, QString());

    cleanupSummaryLabel_->setText(permanentDelete
                                      ? QStringLiteral("已深度清理 %1 个项目").arg(cleaned)
                                      : QStringLiteral("已清理 %1 个项目到回收站").arg(cleaned));
    if (cleanupStatusLabel_ != nullptr) {
        cleanupStatusLabel_->setText(permanentDelete
                                         ? QStringLiteral("深度清理完成：已直接释放空间，正在重新体检剩余可清理项")
                                         : QStringLiteral("清理完成：已处理的项目已移入回收站，正在重新体检剩余可清理项"));
    }
    ScanCleanupCandidates();
}

void MainWindow::ScheduleSearch() {
    EnsureSearchIndexCacheLoading();
    if (searchDebounceTimer_ != nullptr) {
        searchDebounceTimer_->start();
    }
}

void MainWindow::EnsureSearchIndexCacheLoading() {
    if (searchCacheLoadRequested_ || searchIndexing_.load() || (searchIndex_ != nullptr && !searchIndex_->empty())) {
        return;
    }

    searchCacheLoadRequested_ = true;
    LoadSystemSearchIndexCache();
}

void MainWindow::RebuildSearchIndex() {
    auto rebuiltIndex = std::make_shared<std::vector<SearchRecord>>();
    if (!latestResult_ || !latestResult_->root) {
        searchIndex_ = rebuiltIndex;
        return;
    }

    rebuiltIndex->reserve(static_cast<std::size_t>(latestResult_->fileCount + latestResult_->directoryCount));
    const std::atomic_bool cancelFlag(false);
    CollectSearchIndexFromNode(*latestResult_->root, *rebuiltIndex, cancelFlag);
    searchIndex_ = rebuiltIndex;
}

void MainWindow::CollectFiles(const core::ScanNode& node, std::vector<const core::ScanNode*>& output) const {
    if (node.kind == core::NodeKind::File) {
        output.push_back(&node);
        return;
    }

    for (const auto& child : node.children) {
        CollectFiles(*child, output);
    }
}

void MainWindow::CollectSearchMatches(const core::ScanNode& node, const QString& keyword, std::vector<const core::ScanNode*>& output) const {
    const QString name = ToQString(node.name);
    const QString path = ToQString(node.path);
    if (name.contains(keyword, Qt::CaseInsensitive) || path.contains(keyword, Qt::CaseInsensitive)) {
        output.push_back(&node);
    }

    for (const auto& child : node.children) {
        if (child) {
            CollectSearchMatches(*child, keyword, output);
        }
    }
}

void MainWindow::CollectSearchIndex(const core::ScanNode& node) {
    if (searchIndex_ == nullptr) {
        searchIndex_ = std::make_shared<std::vector<SearchRecord>>();
    }
    const std::atomic_bool cancelFlag(false);
    CollectSearchIndexFromNode(node, *searchIndex_, cancelFlag);
}

void MainWindow::CollectSearchIndexFromNode(const core::ScanNode& node, std::vector<SearchRecord>& output, const std::atomic_bool& cancelFlag) const {
    if (cancelFlag.load()) {
        return;
    }

    const QString name = ToQString(node.name);
    const QString path = ToQString(node.path);
    output.push_back(SearchRecord{
        name,
        ToQString(core::FormatBytes(node.totalBytes)),
        node.kind == core::NodeKind::Directory ? QStringLiteral("目录") : QStringLiteral("文件"),
        path,
        MakeSearchKey(name, path),
        node.totalBytes,
        QString(),
        0,
        0,
        node.lastModifiedMsec,
    });

    for (const auto& child : node.children) {
        if (child) {
            CollectSearchIndexFromNode(*child, output, cancelFlag);
            if (cancelFlag.load()) {
                return;
            }
        }
    }
}

bool MainWindow::CollectNtfsSearchIndex(const QString& rootPath, std::vector<SearchRecord>& output, SearchVolumeState& volumeState, const std::atomic_bool& cancelFlag) const {
    if (rootPath.isEmpty() || cancelFlag.load()) {
        return false;
    }

    try {
        core::NtfsMftScanner fastScanner;
        const std::wstring wideRoot = rootPath.toStdWString();
        if (!fastScanner.CanScan(wideRoot)) {
            return false;
        }

        core::FileIndexResult indexResult = fastScanner.BuildFileIndex(wideRoot, cancelFlag);
        if (indexResult.records.empty() || cancelFlag.load()) {
            return false;
        }

        volumeState.rootPath = ToQString(indexResult.journalState.rootPath);
        volumeState.volumeSerialNumber = indexResult.journalState.volumeSerialNumber;
        volumeState.journalId = indexResult.journalState.journalId;
        volumeState.firstUsn = indexResult.journalState.firstUsn;
        volumeState.nextUsn = indexResult.journalState.nextUsn;
        volumeState.valid = indexResult.journalState.valid;

        output.reserve(output.size() + indexResult.records.size());
        for (const core::FileIndexRecord& record : indexResult.records) {
            if (cancelFlag.load()) {
                return false;
            }

            const QString name = ToQString(record.name);
            const QString path = ToQString(record.path);
            const bool isDirectory = record.kind == core::NodeKind::Directory;
            output.push_back(SearchRecord{
                name,
                isDirectory ? QStringLiteral("-") : ToQString(core::FormatBytes(record.bytes)),
                isDirectory ? QStringLiteral("目录") : QStringLiteral("文件"),
                path,
                MakeSearchKey(name, path),
                record.bytes,
                rootPath,
                record.fileReference,
                record.parentReference,
                record.lastModifiedMsec,
            });
        }
        return true;
    } catch (...) {
        return false;
    }
}

bool MainWindow::RefreshSearchIndexFromJournal(std::vector<SearchRecord>& records, std::vector<SearchVolumeState>& volumeStates) const {
    if (records.empty() || volumeStates.empty()) {
        return false;
    }

    const std::atomic_bool cancelFlag(false);
    core::NtfsMftScanner fastScanner;
    bool changed = false;

    for (SearchVolumeState& state : volumeStates) {
        if (!state.valid || state.rootPath.isEmpty()) {
            return false;
        }

        core::UsnJournalState previousState;
        previousState.rootPath = state.rootPath.toStdWString();
        previousState.volumeSerialNumber = state.volumeSerialNumber;
        previousState.journalId = state.journalId;
        previousState.firstUsn = state.firstUsn;
        previousState.nextUsn = state.nextUsn;
        previousState.valid = state.valid;

        core::FileIndexChangeResult changeResult;
        try {
            changeResult = fastScanner.ReadFileIndexChanges(previousState, cancelFlag);
        } catch (...) {
            return false;
        }

        if (changeResult.requiresFullRebuild || !changeResult.journalState.valid) {
            return false;
        }

        state.volumeSerialNumber = changeResult.journalState.volumeSerialNumber;
        state.journalId = changeResult.journalState.journalId;
        state.firstUsn = changeResult.journalState.firstUsn;
        state.nextUsn = changeResult.journalState.nextUsn;
        state.valid = changeResult.journalState.valid;

        if (changeResult.changes.empty()) {
            continue;
        }

        changed = true;
        std::unordered_map<std::uint64_t, std::size_t> indexByReference;
        indexByReference.reserve(records.size() / 2);
        for (std::size_t index = 0; index < records.size(); ++index) {
            SearchRecord& record = records[index];
            if (record.volumeRoot == state.rootPath && record.fileReference != 0 && !record.path.isEmpty()) {
                indexByReference[record.fileReference] = index;
            }
        }

        const auto makeChildPath = [](const QString& parentPath, const QString& name) {
            if (parentPath.endsWith(QLatin1Char('\\')) || parentPath.endsWith(QLatin1Char('/'))) {
                return parentPath + name;
            }
            return parentPath + QLatin1Char('\\') + name;
        };

        const auto updateDirectoryChildren = [&records, &state](const QString& oldPath, const QString& newPath) {
            if (oldPath.isEmpty() || newPath.isEmpty() || oldPath == newPath) {
                return;
            }

            const QString oldPrefix = oldPath.endsWith(QLatin1Char('\\')) ? oldPath : oldPath + QLatin1Char('\\');
            const QString newPrefix = newPath.endsWith(QLatin1Char('\\')) ? newPath : newPath + QLatin1Char('\\');
            for (SearchRecord& record : records) {
                if (record.volumeRoot != state.rootPath || !record.path.startsWith(oldPrefix, Qt::CaseInsensitive)) {
                    continue;
                }

                record.path = newPrefix + record.path.mid(oldPrefix.size());
                record.searchKey = MakeSearchKey(record.name, record.path);
            }
        };

        for (const core::FileIndexChange& change : changeResult.changes) {
            const bool fileDeleted = (change.reason & USN_REASON_FILE_DELETE) != 0;
            const bool oldRenameName = (change.reason & USN_REASON_RENAME_OLD_NAME) != 0;
            const auto existing = indexByReference.find(change.fileReference);
            if (fileDeleted) {
                if (existing == indexByReference.end()) {
                    continue;
                }

                SearchRecord& record = records[existing->second];
                const QString oldPath = record.path;
                if (record.type == QStringLiteral("目录")) {
                    const QString oldPrefix = oldPath.endsWith(QLatin1Char('\\')) ? oldPath : oldPath + QLatin1Char('\\');
                    for (SearchRecord& child : records) {
                        if (child.volumeRoot == state.rootPath && child.path.startsWith(oldPrefix, Qt::CaseInsensitive)) {
                            child.path.clear();
                            child.searchKey.clear();
                        }
                    }
                }
                record.path.clear();
                record.searchKey.clear();
                continue;
            }

            if (oldRenameName) {
                continue;
            }

            QString parentPath;
            if (change.parentReference == 5) {
                parentPath = state.rootPath;
            } else {
                const auto parent = indexByReference.find(change.parentReference);
                if (parent == indexByReference.end()) {
                    continue;
                }
                parentPath = records[parent->second].path;
            }

            if (parentPath.isEmpty()) {
                continue;
            }

            const QString name = ToQString(change.name);
            const QString path = makeChildPath(parentPath, name);
            const bool isDirectory = change.kind == core::NodeKind::Directory;
            std::uint64_t bytes = 0;
            QString sizeText = QStringLiteral("-");
            qint64 modifiedMsec = 0;
            {
                QFileInfo fileInfo(path);
                if (fileInfo.exists()) {
                    modifiedMsec = fileInfo.lastModified().toMSecsSinceEpoch();
                    if (!isDirectory && fileInfo.isFile()) {
                        bytes = static_cast<std::uint64_t>(std::max<qint64>(0, fileInfo.size()));
                        sizeText = ToQString(core::FormatBytes(bytes));
                    }
                } else if (existing != indexByReference.end()) {
                    bytes = records[existing->second].bytes;
                    sizeText = records[existing->second].size;
                    modifiedMsec = records[existing->second].lastModifiedMsec;
                }
            }

            if (existing != indexByReference.end()) {
                SearchRecord& record = records[existing->second];
                const QString oldPath = record.path;
                record.name = name;
                record.size = sizeText;
                record.type = isDirectory ? QStringLiteral("目录") : QStringLiteral("文件");
                record.path = path;
                record.searchKey = MakeSearchKey(name, path);
                record.bytes = bytes;
                record.lastModifiedMsec = modifiedMsec;
                record.volumeRoot = state.rootPath;
                record.parentReference = change.parentReference;
                if (isDirectory) {
                    updateDirectoryChildren(oldPath, path);
                }
                continue;
            }

            SearchRecord record;
            record.name = name;
            record.size = sizeText;
            record.type = isDirectory ? QStringLiteral("目录") : QStringLiteral("文件");
            record.path = path;
            record.searchKey = MakeSearchKey(name, path);
            record.bytes = bytes;
            record.lastModifiedMsec = modifiedMsec;
            record.volumeRoot = state.rootPath;
            record.fileReference = change.fileReference;
            record.parentReference = change.parentReference;
            indexByReference[record.fileReference] = records.size();
            records.push_back(std::move(record));
        }
    }

    if (changed) {
        records.erase(
            std::remove_if(records.begin(), records.end(), [](const SearchRecord& record) {
                return record.path.isEmpty();
            }),
            records.end());
    }

    return true;
}

void MainWindow::CollectSystemSearchIndex(const QString& rootPath, std::vector<SearchRecord>& output, const std::atomic_bool& cancelFlag) const {
    if (rootPath.isEmpty() || cancelFlag.load()) {
        return;
    }

    std::error_code error;
    const std::filesystem::path root = rootPath.toStdWString();
    if (!std::filesystem::exists(root, error)) {
        return;
    }

    const QString rootName = rootPath;
    output.push_back(SearchRecord{
        rootName,
        QStringLiteral("-"),
        QStringLiteral("磁盘"),
        rootPath,
        MakeSearchKey(rootName, rootPath),
        0,
    });

    std::filesystem::recursive_directory_iterator iterator(
        root,
        std::filesystem::directory_options::skip_permission_denied,
        error);
    const std::filesystem::recursive_directory_iterator end;
    while (!error && iterator != end && !cancelFlag.load()) {
        const std::filesystem::path itemPath = iterator->path();
        std::error_code itemError;
        const bool isDirectory = std::filesystem::is_directory(itemPath, itemError);
        std::uint64_t bytes = 0;
        if (!isDirectory && std::filesystem::is_regular_file(itemPath, itemError)) {
            bytes = std::filesystem::file_size(itemPath, itemError);
        }

        const QString name = QString::fromStdWString(itemPath.filename().wstring());
        const QString path = QString::fromStdWString(itemPath.wstring());
        qint64 modifiedMsec = 0;
        {
            QFileInfo pathInfo(path);
            if (pathInfo.exists()) {
                modifiedMsec = pathInfo.lastModified().toMSecsSinceEpoch();
            }
        }
        output.push_back(SearchRecord{
            name,
            isDirectory ? QStringLiteral("-") : ToQString(core::FormatBytes(bytes)),
            isDirectory ? QStringLiteral("目录") : QStringLiteral("文件"),
            path,
            MakeSearchKey(name, path),
            bytes,
            QString(),
            0,
            0,
            modifiedMsec,
        });
        iterator.increment(error);
        if (error) {
            error.clear();
        }
    }
}

void MainWindow::AddTableRow(QTableWidget* table, const QString& name, const QString& size, const QString& type, const QString& path) {
    if (table == nullptr) {
        return;
    }

    const int row = table->rowCount();
    table->insertRow(row);
    auto* nameItem = new QTableWidgetItem(name);
    if (type == QStringLiteral("目录") || type == QStringLiteral("磁盘")) {
        nameItem->setIcon(type == QStringLiteral("磁盘") ? app_icons::drive(16) : app_icons::folder(16));
    } else if (type == QStringLiteral("文件")) {
        nameItem->setIcon(app_icons::fileGlyph(16));
    }
    auto* sizeItem = new QTableWidgetItem(size);
    auto* typeItem = new QTableWidgetItem(type);
    const bool isDirectory = type == QStringLiteral("目录") || type == QStringLiteral("磁盘");
    const QString displayPath = LooksLikeWindowsPath(path) ? ContainingDirectoryForDisplay(path, isDirectory) : path;
    auto* pathItem = new QTableWidgetItem(displayPath);
    nameItem->setToolTip(name);
    sizeItem->setToolTip(size);
    typeItem->setToolTip(type);
    pathItem->setToolTip(path);
    pathItem->setData(Qt::UserRole, path);
    table->setItem(row, 0, nameItem);
    table->setItem(row, 1, sizeItem);
    table->setItem(row, 2, typeItem);
    table->setItem(row, 3, pathItem);
}

void MainWindow::AddCleanupRow(const QString& name, const QString& size, const QString& type, const QString& path) {
    if (cleanupTree_ == nullptr) {
        return;
    }

    auto* item = new QTreeWidgetItem(cleanupTree_);
    item->setText(0, name);
    item->setText(1, size);
    item->setText(2, type);
    item->setText(3, path);
    item->setToolTip(0, name);
    item->setToolTip(1, size);
    item->setToolTip(2, type);
    item->setToolTip(3, path);
}

void MainWindow::BeginTableUpdate(QTableWidget* table) const {
    if (table == nullptr) {
        return;
    }

    table->setUpdatesEnabled(false);
    table->setSortingEnabled(false);
}

void MainWindow::EndTableUpdate(QTableWidget* table) const {
    if (table == nullptr) {
        return;
    }

    table->setUpdatesEnabled(true);
    table->viewport()->update();
}

bool MainWindow::MatchesFilter(const core::ScanNode& node) const {
    Q_UNUSED(node);
    if (filterEdit_ == nullptr) {
        return true;
    }

    const QString filter = filterEdit_->text().trimmed();
    if (filter.isEmpty()) {
        return true;
    }

    QRegularExpression expression(
        QRegularExpression::wildcardToRegularExpression(filter.contains('*') || filter.contains('?') ? filter : QStringLiteral("*%1*").arg(filter)),
        QRegularExpression::CaseInsensitiveOption);

    return expression.match(ToQString(node.name)).hasMatch() || expression.match(ToQString(node.path)).hasMatch();
}

void MainWindow::OpenSelectedPath() {
    if (tabs_ != nullptr && tabs_->currentWidget() == largeFilesView_) {
        const ResultRow* row = largeFilesModel_ != nullptr ? largeFilesModel_->RowAt(largeFilesView_->currentIndex().row()) : nullptr;
        if (row == nullptr || row->fullPath.isEmpty()) {
            SetInfoBar(QStringLiteral("请选择项目"), 0, 0, QStringLiteral("没有选中路径"));
            return;
        }
        RevealPathInExplorer(row->fullPath);
        return;
    }
    if (tabs_ != nullptr && tabs_->currentWidget() == staleFilesView_) {
        const ResultRow* row = staleFilesModel_ != nullptr ? staleFilesModel_->RowAt(staleFilesView_->currentIndex().row()) : nullptr;
        if (row == nullptr || row->fullPath.isEmpty()) {
            SetInfoBar(QStringLiteral("请选择项目"), 0, 0, QStringLiteral("没有选中路径"));
            return;
        }
        RevealPathInExplorer(row->fullPath);
        return;
    }
    if (tabs_ != nullptr && tabs_->currentWidget() != nullptr && tabs_->currentWidget()->isAncestorOf(searchView_)) {
        const ResultRow* row = searchModel_ != nullptr ? searchModel_->RowAt(searchView_->currentIndex().row()) : nullptr;
        if (row == nullptr || row->fullPath.isEmpty()) {
            SetInfoBar(QStringLiteral("请选择项目"), 0, 0, QStringLiteral("没有选中路径"));
            return;
        }
        RevealPathInExplorer(row->fullPath);
        return;
    }

    QTableWidget* table = CurrentTable();
    if (table == nullptr || table->currentRow() < 0) {
        SetInfoBar(QStringLiteral("请选择项目"), 0, 0, QStringLiteral("没有选中路径"));
        return;
    }

    const QString path = SelectedTablePath(table);
    if (path.isEmpty()) {
        return;
    }

    RevealPathInExplorer(path);
}

void MainWindow::ExportCurrentTable() {
    QTableWidget* table = CurrentTable();
    const bool exportingLargeFiles = tabs_ != nullptr && tabs_->currentWidget() == largeFilesView_ && largeFilesModel_ != nullptr;
    const bool exportingStaleFiles = tabs_ != nullptr && tabs_->currentWidget() == staleFilesView_ && staleFilesModel_ != nullptr;
    const bool exportingSearch = tabs_ != nullptr && tabs_->currentWidget() != nullptr && tabs_->currentWidget()->isAncestorOf(searchView_) && searchModel_ != nullptr;
    const bool exportingCleanup = tabs_ != nullptr && tabs_->currentWidget() != nullptr && cleanupTree_ != nullptr && tabs_->currentWidget()->isAncestorOf(cleanupTree_);
    const bool exportingDuplicate = tabs_ != nullptr && tabs_->currentWidget() != nullptr && duplicateTree_ != nullptr && tabs_->currentWidget()->isAncestorOf(duplicateTree_);
    if (table == nullptr && !exportingLargeFiles && !exportingStaleFiles && !exportingSearch && !exportingCleanup && !exportingDuplicate) {
        return;
    }

    QString selectedFilter;
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("导出"),
        QStringLiteral("磁盘洞察分析结果.csv"),
        QStringLiteral("CSV 文件 (*.csv);;HTML 报表 (*.html)"),
        &selectedFilter);
    if (path.isEmpty()) {
        return;
    }
    const bool asHtml = selectedFilter.startsWith(QStringLiteral("HTML"))
        || path.endsWith(QStringLiteral(".html"), Qt::CaseInsensitive);

    const QStringList resultModelHeaders{
        QStringLiteral("名称"),
        QStringLiteral("大小"),
        QStringLiteral("类型"),
        QStringLiteral("路径"),
        QStringLiteral("修改时间"),
    };

    QStringList headers;
    QVector<QStringList> rows;

    auto collectResultModelRows = [&rows](ResultTableModel* model) {
        for (int row = 0; row < model->rowCount(); ++row) {
            const ResultRow* item = model->RowAt(row);
            if (item == nullptr) {
                continue;
            }
            rows << (QStringList()
                << item->name
                << item->size
                << item->type
                << (item->fullPath.isEmpty() ? item->displayPath : item->fullPath)
                << (item->modifiedText.isEmpty() ? QStringLiteral("—") : item->modifiedText));
        }
    };

    if (exportingLargeFiles) {
        headers = resultModelHeaders;
        collectResultModelRows(largeFilesModel_);
    } else if (exportingStaleFiles) {
        headers = resultModelHeaders;
        collectResultModelRows(staleFilesModel_);
    } else if (exportingSearch) {
        headers = resultModelHeaders;
        collectResultModelRows(searchModel_);
    } else if (exportingCleanup) {
        headers = QStringList{QStringLiteral("名称"), QStringLiteral("大小"), QStringLiteral("类型"), QStringLiteral("路径")};
        for (const CleanupGroup& group : cleanupGroups_) {
            rows << (QStringList()
                << group.name
                << ToQString(core::FormatBytes(group.bytes))
                << group.risk
                << group.description);
            for (std::size_t index = 0; index < group.paths.size(); ++index) {
                const QString nativePath = QDir::toNativeSeparators(group.paths[index]);
                const QString fileName = QFileInfo(nativePath).fileName();
                const std::uint64_t bytes = index < group.pathBytes.size() ? group.pathBytes[index] : 0;
                rows << (QStringList()
                    << (fileName.isEmpty() ? nativePath : fileName)
                    << ToQString(core::FormatBytes(bytes))
                    << group.name
                    << nativePath);
            }
        }
    } else if (exportingDuplicate) {
        headers = QStringList{QStringLiteral("名称"), QStringLiteral("大小"), QStringLiteral("修改时间"), QStringLiteral("路径")};
        for (const DuplicateGroupUi& group : duplicateGroups_) {
            const QString headLabel = group.contentConfirmed
                ? QStringLiteral("【内容相同·哈希确认】%1 项").arg(static_cast<qulonglong>(group.members.size()))
                : QStringLiteral("【同名同大小】%1 项").arg(static_cast<qulonglong>(group.members.size()));
            rows << (QStringList() << headLabel << ToQString(core::FormatBytes(group.bytes)) << QString() << QString());
            for (const DuplicateMemberUi& member : group.members) {
                rows << (QStringList()
                    << member.name
                    << ToQString(core::FormatBytes(member.bytes))
                    << (member.modifiedText.isEmpty() ? QStringLiteral("—") : member.modifiedText)
                    << QDir::toNativeSeparators(member.path));
            }
        }
    } else {
        for (int column = 0; column < table->columnCount(); ++column) {
            const QTableWidgetItem* headerItem = table->horizontalHeaderItem(column);
            headers << (headerItem != nullptr ? headerItem->text() : QStringLiteral("列 %1").arg(column + 1));
        }
        for (int row = 0; row < table->rowCount(); ++row) {
            QStringList cells;
            for (int column = 0; column < table->columnCount(); ++column) {
                const QTableWidgetItem* item = table->item(row, column);
                QString value = item != nullptr ? item->text() : QString();
                if (item != nullptr && column == 3) {
                    const QString fullPath = item->data(Qt::UserRole).toString();
                    if (!fullPath.isEmpty()) {
                        value = fullPath;
                    }
                }
                cells << value;
            }
            rows << cells;
        }
    }

    std::wofstream file(path.toStdWString(), std::ios::binary);
    file.imbue(std::locale(".UTF-8"));
    file << L"\xfeff";
    const QString title = QStringLiteral("磁盘洞察 分析结果（%1 项）").arg(rows.size());
    file << (asHtml ? RenderHtmlTable(title, headers, rows) : RenderCsvRows(headers, rows)).toStdWString();

    if (exportingSearch) {
        if (searchScopeLabel_ != nullptr) {
            searchScopeLabel_->setText(QStringLiteral("已导出 %1 项 → %2").arg(rows.size()).arg(QDir::toNativeSeparators(path)));
        }
        UpdateSearchInfoBar();
    } else {
        SetInfoBar(QStringLiteral("已导出"), 0, 0, path);
    }
}

QTableWidget* MainWindow::CurrentTable() const {
    QWidget* current = tabs_->currentWidget();
    if (auto* table = qobject_cast<QTableWidget*>(current)) {
        return table;
    }

    return nullptr;
}

void MainWindow::SetInfoBar(const QString& state, std::uint64_t files, std::uint64_t directories, const QString& path) {
    const auto now = std::chrono::steady_clock::now();
    double elapsed = 0.0;
    if (scanStartedAt_.time_since_epoch().count() != 0) {
        elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - scanStartedAt_).count() / 1000.0;
    }

    const double speed = elapsed > 0.0 ? (files + directories) / elapsed : 0.0;

    stateLabel_->setText(state);
    filesLabel_->setText(QStringLiteral("文件 %1").arg(files));
    directoriesLabel_->setText(QStringLiteral("目录 %1").arg(directories));
    speedLabel_->setText(QStringLiteral("速度 %1/秒").arg(static_cast<std::uint64_t>(speed)));
    elapsedLabel_->setText(QStringLiteral("耗时 %1 秒").arg(elapsed, 0, 'f', 1));
    pathLabel_->setToolTip(path);
    const int pathWidth = std::max(120, pathLabel_->width() - 8);
    pathLabel_->setText(QFontMetrics(pathLabel_->font()).elidedText(path, Qt::ElideMiddle, pathWidth));
    if (loadingOverlay_ != nullptr && loadingOverlay_->isVisible() && loadingDetailLabel_ != nullptr && !path.isEmpty()) {
        loadingDetailLabel_->setText(QFontMetrics(loadingDetailLabel_->font()).elidedText(path, Qt::ElideMiddle, 360));
        loadingDetailLabel_->setToolTip(path);
    }
}

void MainWindow::UpdateSearchInfoBar() {
    if (searchInfoLabel_ == nullptr) {
        return;
    }
    if (lastSearchKeyword_.isEmpty()) {
        searchInfoLabel_->setText(QStringLiteral("文件搜索 · 输入关键字开始搜索"));
        return;
    }
    searchInfoLabel_->setText(QStringLiteral("文件搜索 · 命中 %1 条 · 关键词「%2」 · %3 ms")
                                  .arg(static_cast<qulonglong>(searchTotalMatchCount_))
                                  .arg(lastSearchKeyword_)
                                  .arg(lastSearchElapsedMs_, 0, 'f', 0));
}

void MainWindow::SetBusyState(bool busy, const QString& text) {
    if (busy) {
        ++busyTaskCount_;
        if (!text.isEmpty() && stateLabel_ != nullptr) {
            stateLabel_->setText(text);
        }
        if (busyIndicatorLabel_ != nullptr) {
            busyIndicatorLabel_->setVisible(true);
        }
        if (loadingTitleLabel_ != nullptr && !text.isEmpty()) {
            loadingTitleLabel_->setText(text);
            loadingTitleLabel_->setProperty("baseText", text);
        }
        if (loadingDetailLabel_ != nullptr) {
            loadingDetailLabel_->setText(QStringLiteral("正在准备任务，请稍候"));
        }
        if (loadingOverlay_ != nullptr) {
            UpdateLoadingOverlayGeometry();
            loadingOverlay_->setVisible(true);
            loadingOverlay_->raise();
        }
        if (busyAnimationTimer_ != nullptr && !busyAnimationTimer_->isActive()) {
            busyAnimationTimer_->start();
        }
        return;
    }

    if (busyTaskCount_ > 0) {
        --busyTaskCount_;
    }
    if (busyTaskCount_ > 0) {
        return;
    }

    if (busyAnimationTimer_ != nullptr) {
        busyAnimationTimer_->stop();
    }
    if (busyIndicatorLabel_ != nullptr) {
        busyIndicatorLabel_->setVisible(false);
    }
    if (loadingOverlay_ != nullptr) {
        loadingOverlay_->setVisible(false);
    }
}

void MainWindow::PrimeLoadingFeedback(const QString& title, const QString& detail) {
    if (!title.isEmpty() && stateLabel_ != nullptr) {
        stateLabel_->setText(title);
    }
    if (busyIndicatorLabel_ != nullptr) {
        busyIndicatorLabel_->setVisible(true);
    }
    if (loadingTitleLabel_ != nullptr && !title.isEmpty()) {
        loadingTitleLabel_->setText(title);
        loadingTitleLabel_->setProperty("baseText", title);
    }
    if (loadingDetailLabel_ != nullptr) {
        const QString loadingDetail = detail.isEmpty() ? QStringLiteral("正在准备任务，请稍候") : detail;
        loadingDetailLabel_->setText(QFontMetrics(loadingDetailLabel_->font()).elidedText(loadingDetail, Qt::ElideMiddle, 360));
        loadingDetailLabel_->setToolTip(loadingDetail);
    }
    if (loadingOverlay_ != nullptr) {
        UpdateLoadingOverlayGeometry();
        loadingOverlay_->setVisible(true);
        loadingOverlay_->raise();
    }
    if (busyAnimationTimer_ != nullptr && !busyAnimationTimer_->isActive()) {
        busyAnimationTimer_->start();
    }
}

void MainWindow::FlushImmediateFeedback() {
    if (rootWidget_ != nullptr) {
        rootWidget_->repaint();
    }
    if (loadingOverlay_ != nullptr && loadingOverlay_->isVisible()) {
        UpdateLoadingOverlayGeometry();
        loadingOverlay_->raise();
        loadingOverlay_->repaint();
    }
    repaint();
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents | QEventLoop::ExcludeSocketNotifiers, 24);
}

void MainWindow::AdvanceBusyAnimation() {
    static const QStringList frames = {
        QStringLiteral("⠋"),
        QStringLiteral("⠙"),
        QStringLiteral("⠹"),
        QStringLiteral("⠸"),
        QStringLiteral("⠼"),
        QStringLiteral("⠴"),
        QStringLiteral("⠦"),
        QStringLiteral("⠧")
    };
    const QString frame = frames.at(busyAnimationFrame_ % frames.size());
    const QString suffix = QString(busyAnimationFrame_ % 4, QLatin1Char('.'));
    if (busyIndicatorLabel_ != nullptr) {
        busyIndicatorLabel_->setText(frame);
    }
    if (loadingSpinnerLabel_ != nullptr) {
        loadingSpinnerLabel_->setText(frame);
    }
    if (loadingTitleLabel_ != nullptr) {
        const QString baseText = loadingTitleLabel_->property("baseText").toString();
        if (!baseText.isEmpty()) {
            loadingTitleLabel_->setText(baseText + suffix);
        }
    }
    ++busyAnimationFrame_;
}

void MainWindow::UpdateLoadingOverlayGeometry() {
    if (rootWidget_ == nullptr || loadingOverlay_ == nullptr) {
        return;
    }

    loadingOverlay_->setGeometry(rootWidget_->rect());
    if (loadingOverlay_->isVisible()) {
        loadingOverlay_->raise();
    }
}

void MainWindow::ShowLoadingRow(QTableWidget* table, const QString& title, const QString& detail) {
    if (table == nullptr) {
        return;
    }

    BeginTableUpdate(table);
    table->setRowCount(0);
    AddTableRow(table, title, QStringLiteral("-"), QStringLiteral("加载中"), detail);
    EndTableUpdate(table);
}

}  // namespace disk_lens::qt_ui
