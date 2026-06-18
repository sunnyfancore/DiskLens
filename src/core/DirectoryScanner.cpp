#include "core/DirectoryScanner.h"

#include "core/LongPath.h"

#include <Windows.h>

#include <algorithm>
#include <chrono>
#include <cwchar>
#include <cwctype>
#include <thread>

namespace disk_lens::core {

namespace {

/**
 * @brief 拼接父路径和子名称。
 * @param parent 父路径。
 * @param child 子名称。
 * @return 拼接后的完整路径。
 */
std::wstring JoinPath(const std::wstring& parent, const std::wstring& child) {
    if (parent.empty()) {
        return child;
    }

    const wchar_t last = parent.back();
    if (last == L'\\' || last == L'/') {
        return parent + child;
    }

    return parent + L"\\" + child;
}

/**
 * @brief 将 Win32 高低位文件大小转换为 64 位字节数。
 * @param high 高 32 位。
 * @param low 低 32 位。
 * @return 64 位文件大小。
 */
std::uint64_t MakeFileSize(DWORD high, DWORD low) {
    return (static_cast<std::uint64_t>(high) << 32U) | static_cast<std::uint64_t>(low);
}

/**
 * @brief 判断文件名是否为当前目录或父目录标记。
 * @param name 文件名。
 * @return 是特殊目录名时返回 true。
 */
bool IsDotDirectory(const wchar_t* name) {
    return wcscmp(name, L".") == 0 || wcscmp(name, L"..") == 0;
}

}  // namespace

DirectoryScanner::DirectoryScanner() : cancelled_(false) {}

/**
 * @brief 比较两个文件唯一标识。
 * @param other 另一个文件唯一标识。
 * @return 当前标识更小时返回 true。
 */
bool DirectoryScanner::FileIdentity::operator<(const FileIdentity& other) const {
    if (volumeSerial != other.volumeSerial) {
        return volumeSerial < other.volumeSerial;
    }
    if (indexHigh != other.indexHigh) {
        return indexHigh < other.indexHigh;
    }
    return indexLow < other.indexLow;
}

void DirectoryScanner::RequestCancel() {
    cancelled_.store(true);
}

ScanResult DirectoryScanner::Scan(const std::wstring& rootPath, const ProgressCallback& callback) {
    cancelled_.store(false);
    filesVisited_.store(0);
    directoriesVisited_.store(0);
    activeWorkers_.store(0);
    queueFinished_.store(false);
    lastProgressCount_.store(0);
    lastProgressAt_ = std::chrono::steady_clock::now();
    {
        std::lock_guard<std::mutex> lock(hardlinkMutex_);
        countedHardlinks_.clear();
    }

    ScanResult result;
    result.root = std::make_unique<ScanNode>();
    result.root->name = rootPath;
    result.root->path = rootPath;
    result.root->kind = NodeKind::Directory;

    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        pendingDirectories_.clear();
        pendingDirectories_.push_back(result.root.get());
    }

    const unsigned int hardwareThreads = std::max(2U, std::thread::hardware_concurrency());
    const unsigned int workerCount = std::max(2U, std::min(8U, hardwareThreads));
    std::vector<WorkerStats> workerStats(workerCount);
    std::vector<std::thread> workers;
    workers.reserve(workerCount);

    for (unsigned int index = 0; index < workerCount; ++index) {
        workers.emplace_back(&DirectoryScanner::WorkerLoop, this, std::ref(workerStats[index]), std::cref(callback));
    }

    for (auto& worker : workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }

    result.fileCount = 0;
    result.directoryCount = 0;
    result.errorCount = 0;
    for (const WorkerStats& stats : workerStats) {
        result.fileCount += stats.fileCount;
        result.directoryCount += stats.directoryCount;
        result.errorCount += stats.errorCount;

        for (const auto& entry : stats.extensions) {
            auto& summary = result.extensions[entry.first];
            summary.extension = entry.second.extension;
            summary.fileCount += entry.second.fileCount;
            summary.totalBytes += entry.second.totalBytes;
        }
    }

    if (result.root) {
        result.root->totalBytes = FinalizeNode(*result.root);
    }

    return result;
}

void DirectoryScanner::WorkerLoop(
    WorkerStats& stats,
    const ProgressCallback& callback) {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);

    while (!IsCancelled()) {
        ScanNode* directory = nullptr;
        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            if (!pendingDirectories_.empty()) {
                directory = pendingDirectories_.back();
                pendingDirectories_.pop_back();
                activeWorkers_.fetch_add(1);
            } else if (activeWorkers_.load() == 0) {
                queueFinished_.store(true);
                break;
            }
        }

        if (directory == nullptr) {
            if (queueFinished_.load()) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }

        ScanDirectory(*directory, stats, callback);
        activeWorkers_.fetch_sub(1);
    }
}

void DirectoryScanner::ScanDirectory(
    ScanNode& directory,
    WorkerStats& stats,
    const ProgressCallback& callback) {
    ++stats.directoryCount;
    directoriesVisited_.fetch_add(1);
    ReportProgress(directory.path, callback);

    // 用 \\?\ 前缀越过 MAX_PATH(260),否则超深子树(常见于 node_modules/.git)会被枚举失败而整体漏掉。
    // directory.path 本身保持显示路径(写入缓存、界面、删除),仅在此处临时加前缀。
    const std::wstring pattern = JoinPath(MakeLongPath(directory.path), L"*");
    WIN32_FIND_DATAW findData{};
    HANDLE findHandle = FindFirstFileExW(
        pattern.c_str(),
        FindExInfoBasic,
        &findData,
        FindExSearchNameMatch,
        nullptr,
        FIND_FIRST_EX_LARGE_FETCH);

    if (findHandle == INVALID_HANDLE_VALUE) {
        ++stats.errorCount;
        return;
    }

    do {
        if (IsCancelled()) {
            break;
        }

        if (IsDotDirectory(findData.cFileName)) {
            continue;
        }

        const std::wstring childName = findData.cFileName;
        const std::wstring childPath = JoinPath(directory.path, childName);
        const bool isDirectory = (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        const bool isReparsePoint = (findData.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;

        if (isDirectory && !isReparsePoint) {
            auto child = std::make_unique<ScanNode>();
            child->name = childName;
            child->path = childPath;
            child->kind = NodeKind::Directory;

            ScanNode* childRaw = child.get();
            directory.children.push_back(std::move(child));

            std::lock_guard<std::mutex> lock(queueMutex_);
            pendingDirectories_.push_back(childRaw);
        } else if (!isDirectory) {
            const std::uint64_t logicalFileBytes = MakeFileSize(findData.nFileSizeHigh, findData.nFileSizeLow);
            if (!ShouldCountFile(childPath)) {
                ReportProgress(childPath, callback);
                continue;
            }
            const std::uint64_t fileBytes = GetAllocatedFileBytes(childPath, logicalFileBytes);

            auto file = std::make_unique<ScanNode>();
            file->name = childName;
            file->path = childPath;
            file->kind = NodeKind::File;
            file->ownBytes = fileBytes;
            file->totalBytes = fileBytes;

            directory.children.push_back(std::move(file));
            AddExtension(stats.extensions, childName, fileBytes);

            ++stats.fileCount;
            filesVisited_.fetch_add(1);
        }

        ReportProgress(childPath, callback);
    } while (FindNextFileW(findHandle, &findData) != 0);

    FindClose(findHandle);
}

void DirectoryScanner::AddExtension(std::map<std::wstring, ExtensionSummary>& extensions, const std::wstring& fileName, std::uint64_t fileBytes) const {
    const std::size_t dot = fileName.find_last_of(L'.');
    std::wstring extension = dot == std::wstring::npos ? L"(无扩展名)" : fileName.substr(dot);
    std::transform(extension.begin(), extension.end(), extension.begin(), [](wchar_t value) {
        return static_cast<wchar_t>(towlower(value));
    });

    auto& summary = extensions[extension];
    summary.extension = extension;
    summary.totalBytes += fileBytes;
    summary.fileCount += 1;
}

/**
 * @brief 判断文件是否应计入磁盘占用。
 * @param path 文件完整路径。
 * @return 应计入时返回 true，重复文件标识返回 false。
 */
bool DirectoryScanner::ShouldCountFile(const std::wstring& path) {
    // 超长路径同样需要 \\?\ 前缀才能打开;path 仍为显示路径,仅传给 API 时临时加前缀。
    const std::wstring longPath = MakeLongPath(path);
    HANDLE file = CreateFileW(
        longPath.c_str(),
        FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);

    if (file == INVALID_HANDLE_VALUE) {
        return true;
    }

    BY_HANDLE_FILE_INFORMATION information{};
    const BOOL ok = GetFileInformationByHandle(file, &information);
    CloseHandle(file);
    if (!ok) {
        return true;
    }

    FileIdentity identity;
    identity.volumeSerial = information.dwVolumeSerialNumber;
    identity.indexHigh = information.nFileIndexHigh;
    identity.indexLow = information.nFileIndexLow;

    std::lock_guard<std::mutex> lock(hardlinkMutex_);
    return countedHardlinks_.insert(identity).second;
}

std::uint64_t DirectoryScanner::GetAllocatedFileBytes(const std::wstring& path, std::uint64_t fallbackBytes) const {
    // 超长路径同样需要 \\?\ 前缀,否则取不到压缩/分配大小而回退逻辑大小。
    const std::wstring longPath = MakeLongPath(path);
    DWORD high = 0;
    const DWORD low = GetCompressedFileSizeW(longPath.c_str(), &high);
    if (low == INVALID_FILE_SIZE) {
        const DWORD error = GetLastError();
        if (error != NO_ERROR) {
            return fallbackBytes;
        }
    }

    return MakeFileSize(high, low);
}

std::uint64_t DirectoryScanner::FinalizeNode(ScanNode& node) const {
    if (node.kind == NodeKind::File) {
        return node.totalBytes;
    }

    std::uint64_t totalBytes = node.ownBytes;
    for (auto& child : node.children) {
        totalBytes += FinalizeNode(*child);
    }

    node.totalBytes = totalBytes;
    std::sort(node.children.begin(), node.children.end(), [](const auto& left, const auto& right) {
        return left->totalBytes > right->totalBytes;
    });

    return node.totalBytes;
}

void DirectoryScanner::ReportProgress(const std::wstring& path, const ProgressCallback& callback) {
    if (!callback) {
        return;
    }

    const std::uint64_t currentCount = filesVisited_.load() + directoriesVisited_.load();
    const std::uint64_t previousCount = lastProgressCount_.load();
    const auto now = std::chrono::steady_clock::now();
    if (currentCount < previousCount + 8192 && now - lastProgressAt_ < std::chrono::milliseconds(250)) {
        return;
    }

    std::lock_guard<std::mutex> lock(progressMutex_);
    if (currentCount < lastProgressCount_.load() + 8192 && now - lastProgressAt_ < std::chrono::milliseconds(250)) {
        return;
    }

    lastProgressCount_.store(currentCount);
    lastProgressAt_ = now;
    ScanProgress progress;
    progress.currentPath = path;
    progress.filesVisited = filesVisited_.load();
    progress.directoriesVisited = directoriesVisited_.load();
    callback(progress);
}

bool DirectoryScanner::IsCancelled() const {
    return cancelled_.load();
}

}  // namespace disk_lens::core
