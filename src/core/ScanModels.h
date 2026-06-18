#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace disk_lens::core {

/**
 * @brief 表示单个文件或目录节点的类型。
 */
enum class NodeKind {
    File,
    Directory
};

/**
 * @brief 保存文件扩展名聚合统计。
 */
struct ExtensionSummary {
    /**
     * @brief 文件扩展名，空字符串表示无扩展名文件。
     */
    std::wstring extension;

    /**
     * @brief 此扩展名下文件总大小，单位为字节。
     */
    std::uint64_t totalBytes = 0;

    /**
     * @brief 此扩展名下文件数量。
     */
    std::uint64_t fileCount = 0;
};

/**
 * @brief 表示扫描结果中的一个文件系统节点。
 */
struct ScanNode {
    /**
     * @brief 节点显示名称。
     */
    std::wstring name;

    /**
     * @brief 节点完整路径。
     */
    std::wstring path;

    /**
     * @brief 节点类型。
     */
    NodeKind kind = NodeKind::Directory;

    /**
     * @brief 节点自身大小，目录节点通常为 0。
     */
    std::uint64_t ownBytes = 0;

    /**
     * @brief 节点及其所有子节点总大小，单位为字节。
     */
    std::uint64_t totalBytes = 0;

    /**
     * @brief 子节点列表。
     */
    std::vector<std::unique_ptr<ScanNode>> children;
};

/**
 * @brief 表示一次扫描的聚合结果。
 */
struct ScanResult {
    /**
     * @brief 扫描根节点。
     */
    std::unique_ptr<ScanNode> root;

    /**
     * @brief 扫描到的文件总数。
     */
    std::uint64_t fileCount = 0;

    /**
     * @brief 扫描到的目录总数。
     */
    std::uint64_t directoryCount = 0;

    /**
     * @brief 扫描过程中被跳过或读取失败的路径数。
     */
    std::uint64_t errorCount = 0;

    /**
     * @brief 扩展名统计表。
     */
    std::map<std::wstring, ExtensionSummary> extensions;
};

/**
 * @brief 表示用于快速搜索的一条扁平文件系统索引记录。
 */
struct FileIndexRecord {
    /**
     * @brief 条目名称。
     */
    std::wstring name;

    /**
     * @brief 条目完整路径。
     */
    std::wstring path;

    /**
     * @brief 条目类型。
     */
    NodeKind kind = NodeKind::Directory;

    /**
     * @brief 文件大小，目录通常为 0。
     */
    std::uint64_t bytes = 0;

    /**
     * @brief NTFS 文件引用号低 48 位，用于增量索引定位记录。
     */
    std::uint64_t fileReference = 0;

    /**
     * @brief NTFS 父目录文件引用号低 48 位，用于增量索引拼接路径。
     */
    std::uint64_t parentReference = 0;
};

/**
 * @brief 表示某个卷的 USN Journal 状态。
 */
struct UsnJournalState {
    /**
     * @brief 卷根路径，例如 C:\。
     */
    std::wstring rootPath;

    /**
     * @brief Windows 卷序列号。
     */
    std::uint64_t volumeSerialNumber = 0;

    /**
     * @brief USN Journal 唯一标识。
     */
    std::uint64_t journalId = 0;

    /**
     * @brief 当前 Journal 中仍可读取的最早 USN。
     */
    std::int64_t firstUsn = 0;

    /**
     * @brief 下次增量读取应从此 USN 开始。
     */
    std::int64_t nextUsn = 0;

    /**
     * @brief 状态是否可用于增量索引。
     */
    bool valid = false;
};

/**
 * @brief 表示 USN Journal 中的一条文件索引变更。
 */
struct FileIndexChange {
    /**
     * @brief 发生变化的文件引用号低 48 位。
     */
    std::uint64_t fileReference = 0;

    /**
     * @brief 变化记录中的父目录文件引用号低 48 位。
     */
    std::uint64_t parentReference = 0;

    /**
     * @brief 变化后的条目名称。
     */
    std::wstring name;

    /**
     * @brief 条目类型。
     */
    NodeKind kind = NodeKind::File;

    /**
     * @brief USN 原始原因掩码。
     */
    std::uint32_t reason = 0;

    /**
     * @brief 此变更是否表示删除或旧名称失效。
     */
    bool deleted = false;
};

/**
 * @brief 表示 USN Journal 增量读取结果。
 */
struct FileIndexChangeResult {
    /**
     * @brief 变更记录列表。
     */
    std::vector<FileIndexChange> changes;

    /**
     * @brief 读取后最新的 Journal 状态。
     */
    UsnJournalState journalState;

    /**
     * @brief 是否需要重新执行全量索引。
     */
    bool requiresFullRebuild = false;
};

/**
 * @brief 表示一次快速搜索扁平索引构建结果。
 */
struct FileIndexResult {
    /**
     * @brief 扁平索引记录列表。
     */
    std::vector<FileIndexRecord> records;

    /**
     * @brief 文件数量。
     */
    std::uint64_t fileCount = 0;

    /**
     * @brief 目录数量。
     */
    std::uint64_t directoryCount = 0;

    /**
     * @brief 本次索引对应的 USN Journal 状态。
     */
    UsnJournalState journalState;
};

/**
 * @brief 保存扫描进度快照。
 */
struct ScanProgress {
    /**
     * @brief 最近正在处理的路径。
     */
    std::wstring currentPath;

    /**
     * @brief 已访问文件数量。
     */
    std::uint64_t filesVisited = 0;

    /**
     * @brief 已访问目录数量。
     */
    std::uint64_t directoriesVisited = 0;
};

}  // namespace disk_lens::core
