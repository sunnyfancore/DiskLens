#include "qt/ResultTableModel.h"

#include "qt/AppIcons.h"

#include <QDateTime>

#include <algorithm>

namespace disk_lens::qt_ui {

namespace {

/**
 * @brief 把 Unix epoch 毫秒格式化为相对年龄（多久之前）。
 * @param msec Unix epoch 毫秒（>0）。
 * @return "刚刚 / N 分钟前 / N 小时前 / N 天前 / N 个月前 / N 年前"。
 */
QString FormatRelativeAge(qint64 msec) {
    const QDateTime modified = QDateTime::fromMSecsSinceEpoch(msec);
    const qint64 secs = modified.secsTo(QDateTime::currentDateTime());
    if (secs < 60) {
        return QStringLiteral("刚刚");
    }
    if (secs < 3600) {
        return QStringLiteral("%1 分钟前").arg(secs / 60);
    }
    if (secs < 86400) {
        return QStringLiteral("%1 小时前").arg(secs / 3600);
    }
    if (secs < 2592000) {
        return QStringLiteral("%1 天前").arg(secs / 86400);
    }
    if (secs < 31536000) {
        return QStringLiteral("%1 个月前").arg(secs / 2592000);
    }
    return QStringLiteral("%1 年前").arg(secs / 31536000);
}

}  // namespace

ResultTableModel::ResultTableModel(QObject* parent) : QAbstractTableModel(parent) {
}

int ResultTableModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : rows_.size();
}

int ResultTableModel::columnCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : 5;
}

QVariant ResultTableModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= rows_.size()) {
        return QVariant();
    }

    const ResultRow& row = rows_.at(index.row());
    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case 0:
            return row.name;
        case 1:
            return row.size;
        case 2:
            return row.type;
        case 3:
            return row.displayPath.isEmpty() ? row.fullPath : row.displayPath;
        case 4:
            return row.modifiedText.isEmpty() ? QStringLiteral("—") : row.modifiedText;
        default:
            return QVariant();
        }
    }

    if (role == Qt::ToolTipRole) {
        if (index.column() == 4 && row.modifiedMsec > 0) {
            return QStringLiteral("%1（%2）").arg(row.modifiedText, FormatRelativeAge(row.modifiedMsec));
        }
        return index.column() == 3 && !row.fullPath.isEmpty() ? row.fullPath : data(index, Qt::DisplayRole);
    }

    if (role == Qt::UserRole && index.column() == 3) {
        return row.fullPath.isEmpty() ? row.displayPath : row.fullPath;
    }

    if (role == Qt::TextAlignmentRole) {
        if (index.column() == 1) {
            return static_cast<int>(Qt::AlignRight | Qt::AlignVCenter);
        }
        if (index.column() == 2 || index.column() == 4) {
            return static_cast<int>(Qt::AlignCenter);
        }
        return static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter);
    }

    if (role == Qt::DecorationRole && index.column() == 0) {
        if (row.isParentRow) {
            return app_icons::arrowBack(16);
        }
        if (row.type == QStringLiteral("磁盘")) {
            return app_icons::drive(16);
        }
        if (row.isDirectory || row.type == QStringLiteral("目录")) {
            return app_icons::folder(16);
        }
        if (row.type == QStringLiteral("文件")) {
            return app_icons::fileGlyph(16);
        }
    }

    return QVariant();
}

QVariant ResultTableModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
        return QVariant();
    }

    switch (section) {
    case 0:
        return QStringLiteral("名称");
    case 1:
        return QStringLiteral("大小");
    case 2:
        return QStringLiteral("类型");
    case 3:
        return QStringLiteral("路径");
    case 4:
        return QStringLiteral("修改时间");
    default:
        return QVariant();
    }
}

void ResultTableModel::sort(int column, Qt::SortOrder order) {
    if (rows_.size() <= 1) {
        return;
    }

    beginResetModel();
    std::stable_sort(rows_.begin(), rows_.end(), [column, order](const ResultRow& left, const ResultRow& right) {
        if (left.isParentRow != right.isParentRow) {
            return left.isParentRow;
        }

        int comparison = 0;
        switch (column) {
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
    endResetModel();
}

void ResultTableModel::SetRows(QVector<ResultRow> rows) {
    beginResetModel();
    rows_ = std::move(rows);
    endResetModel();
}

void ResultTableModel::Clear() {
    beginResetModel();
    rows_.clear();
    endResetModel();
}

const ResultRow* ResultTableModel::RowAt(int row) const {
    if (row < 0 || row >= rows_.size()) {
        return nullptr;
    }
    return &rows_.at(row);
}

}  // namespace disk_lens::qt_ui
