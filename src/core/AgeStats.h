#pragma once

#include "core/ScanModels.h"

#include <cstdint>
#include <string>
#include <vector>

namespace disk_lens::core {

/**
 * @brief 单个年龄分带的聚合统计(由 ComputeAgeBuckets 产出)。
 *
 * 与"长期未动"的平铺尾部列表不同:这里把全部文件按最后修改时间卷进固定 7 个分带,
 * 并以字节加权(ownBytes 累加),直观呈现"磁盘上哪些年代的数据最占空间"。
 */
struct AgeBucket {
    /**
     * @brief 分带显示名(如 "7 天内"、"1-3 年")。
     */
    std::wstring label;

    /**
     * @brief 该分带下文件总大小(字节,按文件体积加权)。
     */
    std::uint64_t totalBytes = 0;

    /**
     * @brief 该分带下文件数量。
     */
    std::uint64_t fileCount = 0;
};

/**
 * @brief 按字节加权计算文件年龄分带分布。
 * @param result 扫描结果(只读遍历 result.root,不修改任何结构)。
 * @param nowMsec 当前时间(Unix epoch 毫秒,对齐 QDateTime::currentMSecsSinceEpoch)。
 * @return 固定 7 个分带(从新到旧),各带累加落在该区间的文件 ownBytes 与数量。
 *
 * 分带(按文件最后修改时间距今的天数):<7 天 / 7-30 天 / 30-90 天 / 90-180 天 /
 * 180 天-1 年 / 1-3 年 / >3 年。lastModifiedMsec 为 0(未采集)或晚于 now(未来时间,异常)的文件
 * 跳过,不计入任何分带。返回向量始终为 7 项且顺序固定,便于 UI 绘制一致的横轴。
 *
 * 两个扫描引擎(DirectoryScanner 递归 + NtfsMftScanner MFT)产出的 ScanNode.lastModifiedMsec
 * 语义一致(目录取内容最后变更、文件取自身修改时间,0 表示未知),故本函数对两引擎结果通用。
 */
std::vector<AgeBucket> ComputeAgeBuckets(const ScanResult& result, std::int64_t nowMsec);

}  // namespace disk_lens::core
