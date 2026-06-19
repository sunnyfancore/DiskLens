#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace disk_lens::core {

/**
 * @brief 内容去重组里的单个成员(一个物理文件)。
 */
struct DuplicateMember {
    /**
     * @brief 显示路径(不带 \\?\ 前缀),用于界面展示与删除流程。
     */
    std::wstring path;

    /**
     * @brief 该文件字节数。
     */
    std::uint64_t bytes = 0;
};

/**
 * @brief 一组内容完全相同的文件。
 *
 * 同组内每个成员的字节数相等,且经过全文 SHA-256 确认内容一致;成员均为**不同的物理文件**
 * (硬链接已被折叠为代表项,删除硬链接不释放空间,故不计入可回收去重)。
 */
struct DuplicateGroup {
    /**
     * @brief 每个成员的字节数(组内一致)。
     */
    std::uint64_t bytes = 0;

    /**
     * @brief 内容相同的成员列表(至少 2 个)。
     */
    std::vector<DuplicateMember> members;
};

/**
 * @brief 哈希校验进度快照。
 */
struct DuplicateHashProgress {
    /**
     * @brief 已完成头部(前 16KB)哈希的候选文件数。
     */
    std::size_t filesHashed = 0;

    /**
     * @brief 需要参与哈希的候选文件总数(大小分组后剩余)。
     */
    std::size_t hashCandidates = 0;

    /**
     * @brief 已读取并哈希的字节数。
     */
    std::uint64_t bytesHashed = 0;

    /**
     * @brief 候选文件总字节数。
     */
    std::uint64_t bytesTotal = 0;
};

/**
 * @brief 文件内容哈希去重器。
 *
 * 在给定的一批文件里找出**内容完全相同**的跨名副本。算法为四段式,性能可控、可随时取消:
 *   1. 精确大小分组:字节数不同的文件内容必然不同,直接丢弃只有 1 个的大小组。
 *   2. 硬链接短路:同一物理文件(硬链接)内容必然相同,按卷序列号 + 文件索引折叠为代表项,
 *      既免重复哈希,也避免把"删一个不释放空间"的硬链接当成可回收去重。
 *   3. 前 16KB SHA-256 分桶:同大小组内取每文件前 16KB 头哈希分桶,丢弃只有 1 个的桶。
 *   4. 全文 SHA-256 确认:头哈希桶内对剩余候选算全文哈希,完全一致才产出最终去重组。
 *
 * 仅使用 Win32 文件 API + 自带 SHA-256 实现,不依赖 Qt,与 DirectoryScanner 同属 core 层。
 * 显示路径出入,内部读取时通过 core::MakeLongPath 临时加 \\?\ 前缀,绝不持久化前缀。
 */
class FileHasher {
public:
    /**
     * @brief 进度回调类型(在执行哈希的后台线程上被周期性调用)。
     */
    using ProgressCallback = std::function<void(const DuplicateHashProgress&)>;

    /**
     * @brief 取消探针类型;返回 true 表示请求取消。
     */
    using CancelChecker = std::function<bool()>;

    /**
     * @brief 找出内容完全相同的去重组。
     * @param files 参与去重的文件(display path + 字节数),通常来自扫描树里 ownBytes>0 的文件。
     * @param progress 进度回调,可为空。
     * @param cancel 取消探针,可为空;返回 true 时尽快停止并返回已确认的去重组。
     * @return 内容去重组列表(每组至少 2 个不同物理文件)。
     */
    std::vector<DuplicateGroup> FindContentDuplicates(
        const std::vector<std::pair<std::wstring, std::uint64_t>>& files,
        const ProgressCallback& progress,
        const CancelChecker& cancel) const;
};

}  // namespace disk_lens::core
