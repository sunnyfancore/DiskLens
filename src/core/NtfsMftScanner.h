#pragma once

#include "core/ScanModels.h"

#include <atomic>
#include <string>

namespace disk_lens::core {

/**
 * @brief 为 NTFS MFT 快速扫描预留的扫描器。
 */
class NtfsMftScanner {
public:
    /**
     * @brief 判断指定根路径是否适合使用 NTFS 快速扫描。
     * @param rootPath 要检测的根路径，例如 C:\。
     * @return 可以使用 NTFS 快速扫描时返回 true。
     */
    bool CanScan(const std::wstring& rootPath) const;

    /**
     * @brief 执行 NTFS MFT 快速扫描。
     * @param rootPath 要扫描的卷根路径。
     * @return 扫描结果(扫描被 RequestCancel() 取消时返回已读到的部分结果)。
     *
     * 取消语义与 DirectoryScanner 对等:外部调 RequestCancel() 后,MFT 读取循环
     * 在下个记录边界提前退出,已读条目照常构建为部分树返回(不再回退兼容扫描)。
     */
    ScanResult Scan(const std::wstring& rootPath);

    /**
     * @brief 构建用于全系统快速搜索的 NTFS 扁平索引。
     * @param rootPath 要索引的卷根路径。
     * @param cancelFlag 取消标志，外部置 true 时提前返回。
     * @return 扁平文件系统索引。
     */
    FileIndexResult BuildFileIndex(const std::wstring& rootPath, const std::atomic_bool& cancelFlag);

    /**
     * @brief 查询指定 NTFS 卷的 USN Journal 状态。
     * @param rootPath 要查询的卷根路径。
     * @return Journal 状态，无法查询时 valid 为 false。
     */
    UsnJournalState QueryJournalState(const std::wstring& rootPath) const;

    /**
     * @brief 从上次 Journal 状态开始读取文件索引增量变更。
     * @param previousState 上次缓存的 Journal 状态。
     * @param cancelFlag 取消标志，外部置 true 时提前返回。
     * @return 增量变更结果；Journal 不连续时 requiresFullRebuild 为 true。
     */
    FileIndexChangeResult ReadFileIndexChanges(const UsnJournalState& previousState, const std::atomic_bool& cancelFlag) const;

    /**
     * @brief 请求取消正在进行的 Scan()(与 DirectoryScanner::RequestCancel 对等)。
     *
     * 仅作用于 Scan();BuildFileIndex/ReadFileIndexChanges 用各自的 cancelFlag 参数。
     */
    void RequestCancel();

    /**
     * @brief Scan() 是否被取消(供调用方判断是否跳过兼容回退)。
     */
    bool IsCancelled() const;

private:
    std::atomic_bool cancelled_{false};
};

}  // namespace disk_lens::core
