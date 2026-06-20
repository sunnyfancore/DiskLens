#include "core/AgeStats.h"

#include <array>

namespace disk_lens::core {

namespace {

// 一天的毫秒数(对齐 lastModifiedMsec / nowMsec 的单位)。
constexpr std::int64_t kMillisPerDay = 24LL * 60LL * 60LL * 1000LL;

// 7 个固定分带:显示名 + 上界(该带包含的最大年龄天数,含)。逐段判断:年龄天数 <= 上界即归入此带。
// 末带(">3 年")的 maxDaysInclusive 不参与判断(循环里对其特判,任何更老的文件都兜底落此),
// 故这里取 0 占位即可,避免引入 numeric_limits 与宏的纠葛。
struct BandDef {
    const wchar_t* label;
    long long maxDaysInclusive;
};

const std::array<BandDef, 7> kBands = {{
    {L"7 天内", 7LL},
    {L"7-30 天", 30LL},
    {L"30-90 天", 90LL},
    {L"90-180 天", 180LL},
    {L"180 天-1 年", 365LL},
    {L"1-3 年", 365LL * 3},
    {L">3 年", 0LL},
}};

/**
 * @brief 递归累加:把每个有效 mtime 的文件按年龄天数归入对应分带。
 */
void Accumulate(const ScanNode& node, std::array<std::uint64_t, 7>& bytes,
                std::array<std::uint64_t, 7>& counts, std::int64_t nowMsec) {
    if (node.kind == NodeKind::File) {
        const std::int64_t mtime = node.lastModifiedMsec;
        // mtime==0 表示未采集;now<mtime 是异常的未来时间,二者都跳过,避免负年龄与误计入"最新带"。
        if (mtime > 0 && nowMsec >= mtime) {
            const long long ageDays = (nowMsec - mtime) / kMillisPerDay;
            for (std::size_t i = 0; i < kBands.size(); ++i) {
                const bool isLast = (i + 1 == kBands.size());
                if (isLast || ageDays <= kBands[i].maxDaysInclusive) {
                    bytes[i] += node.ownBytes;
                    counts[i] += 1;
                    break;
                }
            }
        }
    }
    for (const auto& child : node.children) {
        if (child != nullptr) {
            Accumulate(*child, bytes, counts, nowMsec);
        }
    }
}

}  // namespace

std::vector<AgeBucket> ComputeAgeBuckets(const ScanResult& result, std::int64_t nowMsec) {
    std::array<std::uint64_t, 7> bytes{};
    std::array<std::uint64_t, 7> counts{};
    if (result.root != nullptr) {
        Accumulate(*result.root, bytes, counts, nowMsec);
    }

    std::vector<AgeBucket> buckets;
    buckets.reserve(kBands.size());
    for (std::size_t i = 0; i < kBands.size(); ++i) {
        buckets.push_back(AgeBucket{kBands[i].label, bytes[i], counts[i]});
    }
    return buckets;
}

}  // namespace disk_lens::core
