#pragma once

#include "core/ScanModels.h"

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <chrono>
#include <vector>

namespace disk_lens::core {

/**
 * @brief 使用标准 Win32 文件枚举 API 扫描目录。
 */
class DirectoryScanner {
public:
    /**
     * @brief 进度回调函数类型。
     * @param progress 当前扫描进度快照。
     */
    using ProgressCallback = std::function<void(const ScanProgress& progress)>;

    /**
     * @brief 构造目录扫描器。
     */
    DirectoryScanner();

    /**
     * @brief 请求取消当前扫描。
     */
    void RequestCancel();

    /**
     * @brief 扫描指定路径并返回聚合结果。
     * @param rootPath 要扫描的根路径。
     * @param callback 扫描过程中的进度回调，可为空。
     * @return 扫描结果。
     */
    ScanResult Scan(const std::wstring& rootPath, const ProgressCallback& callback);

private:
    /**
     * @brief 保存单个扫描线程的局部统计。
     */
    struct WorkerStats {
        /**
         * @brief 当前线程扫描到的文件总数。
         */
        std::uint64_t fileCount = 0;

        /**
         * @brief 当前线程扫描到的目录总数。
         */
        std::uint64_t directoryCount = 0;

        /**
         * @brief 当前线程遇到的错误数。
         */
        std::uint64_t errorCount = 0;

        /**
         * @brief 当前线程的扩展名统计。
         */
        std::map<std::wstring, ExtensionSummary> extensions;
    };

    /**
     * @brief NTFS 文件唯一标识，用于避免硬链接重复计入空间。
     */
    struct FileIdentity {
        /**
         * @brief 卷序列号。
         */
        std::uint32_t volumeSerial = 0;

        /**
         * @brief 文件索引高 32 位。
         */
        std::uint32_t indexHigh = 0;

        /**
         * @brief 文件索引低 32 位。
         */
        std::uint32_t indexLow = 0;

        /**
         * @brief 比较两个文件标识。
         * @param other 另一个文件标识。
         * @return 当前标识更小时返回 true。
         */
        bool operator<(const FileIdentity& other) const;
    };

    /**
     * @brief 扫描单个目录节点并把子目录加入共享任务队列。
     * @param directory 要扫描的目录节点。
     * @param stats 当前线程的局部统计。
     * @param callback 扫描过程中的进度回调。
     */
    void ScanDirectory(
        ScanNode& directory,
        WorkerStats& stats,
        const ProgressCallback& callback);

    /**
     * @brief 扫描工作线程入口。
     * @param stats 当前线程的局部统计。
     * @param callback 扫描过程中的进度回调。
     */
    void WorkerLoop(
        WorkerStats& stats,
        const ProgressCallback& callback);

    /**
     * @brief 累计文件扩展名统计。
     * @param extensions 扩展名统计表。
     * @param fileName 文件名。
     * @param fileBytes 文件大小，单位为字节。
     */
    void AddExtension(std::map<std::wstring, ExtensionSummary>& extensions, const std::wstring& fileName, std::uint64_t fileBytes) const;

    /**
     * @brief 判断文件是否应计入磁盘占用，重复文件标识只计一次。
     * @param path 文件完整路径。
     * @return 应计入时返回 true。
     */
    bool ShouldCountFile(const std::wstring& path);

    /**
     * @brief 获取文件实际分配大小，失败时回退到逻辑大小。
     * @param path 文件完整路径。
     * @param fallbackBytes 回退逻辑大小。
     * @return 文件实际分配大小或回退大小。
     */
    std::uint64_t GetAllocatedFileBytes(const std::wstring& path, std::uint64_t fallbackBytes) const;

    /**
     * @brief 递归聚合目录大小并按大小排序。
     * @param node 要聚合的节点。
     * @return 节点总大小，单位为字节。
     */
    std::uint64_t FinalizeNode(ScanNode& node) const;

    /**
     * @brief 发布节流后的扫描进度。
     * @param path 当前正在扫描的路径。
     * @param callback 扫描过程中的进度回调。
     */
    void ReportProgress(const std::wstring& path, const ProgressCallback& callback);

    /**
     * @brief 判断当前扫描是否已被取消。
     * @return 已取消时返回 true。
     */
    bool IsCancelled() const;

    /**
     * @brief 取消标记。
     */
    std::atomic_bool cancelled_;

    /**
     * @brief 待扫描目录任务队列。
     */
    std::vector<ScanNode*> pendingDirectories_;

    /**
     * @brief 任务队列互斥锁。
     */
    std::mutex queueMutex_;

    /**
     * @brief 进度回调互斥锁。
     */
    std::mutex progressMutex_;

    /**
     * @brief 硬链接去重集合互斥锁。
     */
    std::mutex hardlinkMutex_;

    /**
     * @brief 已计入的文件标识集合。
     */
    std::set<FileIdentity> countedHardlinks_;

    /**
     * @brief 已访问文件数。
     */
    std::atomic_uint64_t filesVisited_;

    /**
     * @brief 已访问目录数。
     */
    std::atomic_uint64_t directoriesVisited_;

    /**
     * @brief 正在扫描的目录任务数。
     */
    std::atomic_uint32_t activeWorkers_;

    /**
     * @brief 任务队列是否已经完成。
     */
    std::atomic_bool queueFinished_;

    /**
     * @brief 上次发布进度时的访问计数。
     */
    std::atomic_uint64_t lastProgressCount_;

    /**
     * @brief 上次发布进度的时间点。
     */
    std::chrono::steady_clock::time_point lastProgressAt_{};
};

}  // namespace disk_lens::core
