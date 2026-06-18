#pragma once

#include <QAbstractTableModel>
#include <QIcon>
#include <QString>
#include <QVector>

#include <cstdint>

namespace disk_lens::qt_ui {

/**
 * @brief 结果表格中的单行数据。
 */
struct ResultRow {
    /**
     * @brief 名称列文本。
     */
    QString name;

    /**
     * @brief 大小列文本。
     */
    QString size;

    /**
     * @brief 类型列文本。
     */
    QString type;

    /**
     * @brief 展示用路径文本。
     */
    QString displayPath;

    /**
     * @brief 完整路径。
     */
    QString fullPath;

    /**
     * @brief 搜索键。
     */
    QString searchKey;

    /**
     * @brief 原始字节大小。
     */
    std::uint64_t bytes = 0;

    /**
     * @brief 关联节点地址，目录表进入下级时使用。
     */
    quint64 nodeAddress = 0;

    /**
     * @brief 是否是目录。
     */
    bool isDirectory = false;

    /**
     * @brief 是否是返回上级行。
     */
    bool isParentRow = false;
};

/**
 * @brief 面向大数据量的虚拟结果表模型。
 */
class ResultTableModel : public QAbstractTableModel {
    Q_OBJECT

public:
    /**
     * @brief 构造虚拟结果表模型。
     * @param parent 父对象。
     */
    explicit ResultTableModel(QObject* parent = nullptr);

    /**
     * @brief 返回行数。
     * @param parent 父索引。
     * @return 行数。
     */
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;

    /**
     * @brief 返回列数。
     * @param parent 父索引。
     * @return 列数。
     */
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;

    /**
     * @brief 返回单元格数据。
     * @param index 单元格索引。
     * @param role 数据角色。
     * @return 单元格数据。
     */
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;

    /**
     * @brief 返回表头数据。
     * @param section 表头序号。
     * @param orientation 表头方向。
     * @param role 数据角色。
     * @return 表头数据。
     */
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    /**
     * @brief 按指定列排序当前模型数据。
     * @param column 排序列号。
     * @param order 排序方向。
     */
    void sort(int column, Qt::SortOrder order = Qt::AscendingOrder) override;

    /**
     * @brief 替换全部行。
     * @param rows 新行数据。
     */
    void SetRows(QVector<ResultRow> rows);

    /**
     * @brief 清空模型。
     */
    void Clear();

    /**
     * @brief 获取指定行。
     * @param row 行号。
     * @return 行数据指针，不存在时返回 nullptr。
     */
    const ResultRow* RowAt(int row) const;

private:
    /**
     * @brief 行数据。
     */
    QVector<ResultRow> rows_;
};

}  // namespace disk_lens::qt_ui
