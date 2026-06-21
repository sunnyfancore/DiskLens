#pragma once

#include "core/ScanModels.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace disk_lens::core {

/**
 * @brief 仅把 ≥ 此阈值的文件纳入扫描对比快照。
 *
 * 既把 diff 聚焦在「有意义的字节变化」(小文件日志/缓存 churn 是噪声、不可操作),又对百万级文件
 * 的全盘扫描给出有界的快照内存占用。两次扫描均按同一阈值筛,故阈值以下的文件永不进入对比。
 */
constexpr std::uint64_t kMinDiffFileBytes = 1ULL * 1024ULL * 1024ULL;  // 1 MiB

/**
 * @brief 扫描结果中单个文件的扁平记录(原始大小写显示路径 + 字节数)。
 */
struct FlatFile {
    /**
     * @brief 原始大小写的显示路径(不做大小写归一,保留给界面展示;对比键在 FlatSnapshot 内单独归一)。
     */
    std::wstring displayPath;

    /**
     * @brief 文件自身大小(字节)。
     */
    std::int64_t bytes = 0;
};

/**
 * @brief 扫描结果的扁平快照:把树中所有 File 节点(≥ kMinDiffFileBytes)按归一化路径键 → FlatFile。
 *
 * 路径键归一(见 ScanDiff.cpp):去 \\?\ / \\?\UNC\ 长路径前缀、/→\、ASCII 小写(NTFS 大小写不敏感)。
 * 保证两次扫描同一文件键一致——长路径(\\?\)、正反斜杠混用、盘符大小写均不影响匹配。
 * displayPath 保留原始大小写供界面展示。
 */
struct FlatSnapshot {
    /**
     * @brief 归一化路径键 → 扁平文件记录。
     */
    std::unordered_map<std::wstring, FlatFile> files;

    /**
     * @brief 扫描根的原始大小写显示路径。
     */
    std::wstring rootPath;

    /**
     * @brief 快照内文件数(仅 ≥ kMinDiffFileBytes)。
     */
    std::uint64_t fileCount = 0;
};

/**
 * @brief 单条对比差异。
 */
enum class ScanDiffCategory {
    New,      ///< 上次无、本次有(新增)。
    Gone,     ///< 上次有、本次无(消失)。
    Growth,   ///< 两都有、本次更大(增长)。
    Shrink    ///< 两都有、本次更小(缩减)。
};

/**
 * @brief 一条路径从上次扫描到本次的变化。
 */
struct ScanDiffEntry {
    /**
     * @brief 原始大小写显示路径。
     */
    std::wstring displayPath;

    /**
     * @brief 上次大小(字节);Gone 为旧值,New 为 0。
     */
    std::int64_t previousBytes = 0;

    /**
     * @brief 本次大小(字节);Gone 为 0,New 为新值。
     */
    std::int64_t currentBytes = 0;

    /**
     * @brief currentBytes - previousBytes(New/Growth 为正,Gone/Shrink 为负)。
     */
    std::int64_t delta = 0;

    /**
     * @brief 变化类别。
     */
    ScanDiffCategory category = ScanDiffCategory::New;
};

/**
 * @brief 两次同根扫描的对比结果。
 */
struct ScanDiffResult {
    /**
     * @brief 差异条目(已按 |delta| 降序;若超 maxEntries 则截断且 truncated=true)。
     */
    std::vector<ScanDiffEntry> entries;

    /**
     * @brief 新增条数。
     */
    std::uint64_t newCount = 0;

    /**
     * @brief 消失条数。
     */
    std::uint64_t goneCount = 0;

    /**
     * @brief 增长条数。
     */
    std::uint64_t growthCount = 0;

    /**
     * @brief 缩减条数。
     */
    std::uint64_t shrinkCount = 0;

    /**
     * @brief 净变化(Σ delta,字节;正=整体增长,负=整体缩减)。
     */
    std::int64_t netDelta = 0;

    /**
     * @brief 是否因超出 maxEntries 截断了条目(界面据此提示「仅显示前 N 项」)。
     */
    bool truncated = false;
};

/**
 * @brief 把扫描结果树拍平为快照(只读消费 result.root,不修改任何结构)。
 * @param result 扫描结果。
 * @return 扁平快照;result.root 为空时返回空快照。
 *
 * 在后台扫描线程上调用(树遍历可能耗时),把结果经 shared_ptr 投递到 UI 线程做 diff。
 */
FlatSnapshot BuildFlatSnapshot(const ScanResult& result);

/**
 * @brief 判断两个快照是否同根(归一化根路径相等),用于决定是否值得对比。
 * @param a 快照 A。
 * @param b 快照 B。
 * @return 归一化根路径相等且非空时为 true。
 */
bool SameScanRoot(const FlatSnapshot& a, const FlatSnapshot& b);

/**
 * @brief 计算 prev → curr 的路径键 diff。
 * @param prev 上次快照(基线)。
 * @param curr 本次快照。
 * @param maxEntries 返回条目上限(按 |delta| 降序截断),默认 5000。
 * @return 对比结果(含计数、净变化、截断标志)。
 *
 * 调用方应先用 SameScanRoot 门控;否则不同根的全部路径会互判为 New/Gone,无意义。
 * 在 UI 线程调用(map diff,耗时与快照规模成正比但远低于树遍历)。
 */
ScanDiffResult ComputeScanDiff(const FlatSnapshot& prev, const FlatSnapshot& curr,
                               std::size_t maxEntries = 5000);

}  // namespace disk_lens::core
