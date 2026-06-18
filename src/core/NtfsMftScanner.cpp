#include "core/NtfsMftScanner.h"

#include <Windows.h>
#include <WinIoCtl.h>

#include <algorithm>
#include <cstdint>
#include <cwchar>
#include <cwctype>
#include <cstring>
#include <map>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace disk_lens::core {

namespace {

/**
 * @brief NTFS 引导扇区内需要的核心参数。
 */
struct NtfsBootInfo {
    /**
     * @brief 每扇区字节数。
     */
    std::uint16_t bytesPerSector = 0;

    /**
     * @brief 每簇扇区数。
     */
    std::uint8_t sectorsPerCluster = 0;

    /**
     * @brief 每簇字节数。
     */
    std::uint32_t bytesPerCluster = 0;

    /**
     * @brief MFT 起始逻辑簇号。
     */
    std::int64_t mftLogicalCluster = 0;

    /**
     * @brief 单条 MFT 记录字节数。
     */
    std::uint32_t fileRecordBytes = 0;
};

/**
 * @brief MFT 数据运行。
 */
struct DataRun {
    /**
     * @brief 起始逻辑簇号。
     */
    std::int64_t logicalCluster = 0;

    /**
     * @brief 运行包含的簇数量。
     */
    std::uint64_t clusterCount = 0;
};

/**
 * @brief 临时 MFT 文件记录。
 */
struct MftEntry {
    /**
     * @brief 文件引用号低 48 位。
     */
    std::uint64_t id = 0;

    /**
     * @brief 父目录文件引用号低 48 位。
     */
    std::uint64_t parentId = 0;

    /**
     * @brief 文件名。
     */
    std::wstring name;

    /**
     * @brief 文件大小，单位为字节。
     */
    std::uint64_t bytes = 0;

    /**
     * @brief 是否为目录。
     */
    bool isDirectory = false;

    /**
     * @brief 最后修改时间，Unix epoch 毫秒；0 表示未采集。
     */
    std::int64_t modificationTime = 0;
};

/**
 * @brief 保存一轮 MFT 读取后的临时条目表。
 */
struct MftEntryTable {
    /**
     * @brief 按文件引用号下标存储的 MFT 条目。
     */
    std::vector<MftEntry> entries;

    /**
     * @brief 条目下标是否存在有效记录。
     */
    std::vector<std::uint8_t> present;

    /**
     * @brief 有效文件数量。
     */
    std::uint64_t fileCount = 0;

    /**
     * @brief 有效目录数量。
     */
    std::uint64_t directoryCount = 0;
};

/**
 * @brief 判断是否为不应出现在普通根目录视图中的 NTFS 元文件。
 * @param name 文件名。
 * @return 是系统元文件时返回 true。
 */
bool IsNtfsMetadataFile(const std::wstring& name) {
    static constexpr const wchar_t* metadataNames[] = {
        L"$MFT",
        L"$MFTMirr",
        L"$LogFile",
        L"$Volume",
        L"$AttrDef",
        L"$Bitmap",
        L"$Boot",
        L"$BadClus",
        L"$Secure",
        L"$UpCase",
        L"$Extend"
    };

    for (const wchar_t* metadataName : metadataNames) {
        if (_wcsicmp(name.c_str(), metadataName) == 0) {
            return true;
        }
    }

    return false;
}

/**
 * @brief 读取小端整数。
 * @param data 数据指针。
 * @return 整数值。
 */
template <typename T>
T ReadLe(const std::uint8_t* data) {
    T value{};
    std::memcpy(&value, data, sizeof(T));
    return value;
}

/**
 * @brief 获取文件引用号低 48 位。
 * @param value 原始 64 位文件引用号。
 * @return 低 48 位文件引用号。
 */
std::uint64_t FileReferenceId(std::uint64_t value) {
    return value & 0x0000FFFFFFFFFFFFULL;
}

/**
 * @brief 将根路径转换为卷设备路径。
 * @param rootPath 根路径，例如 C:\。
 * @return 卷设备路径，例如 \\.\C:。
 */
std::wstring ToVolumePath(const std::wstring& rootPath) {
    if (rootPath.size() < 2 || rootPath[1] != L':') {
        throw std::runtime_error("root path is not a drive root");
    }

    std::wstring volume = L"\\\\.\\";
    volume.push_back(rootPath[0]);
    volume.push_back(L':');
    return volume;
}

/**
 * @brief 打开卷设备读取句柄。
 * @param rootPath 卷根路径，例如 C:\。
 * @return Windows 卷设备句柄，失败时返回 INVALID_HANDLE_VALUE。
 */
HANDLE OpenVolumeForRead(const std::wstring& rootPath) {
    const std::wstring volumePath = ToVolumePath(rootPath);
    return CreateFileW(
        volumePath.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
}

/**
 * @brief 查询 Windows 卷序列号。
 * @param rootPath 卷根路径。
 * @return 卷序列号，失败时返回 0。
 */
std::uint64_t QueryVolumeSerialNumber(const std::wstring& rootPath) {
    DWORD serialNumber = 0;
    if (!GetVolumeInformationW(
            rootPath.c_str(),
            nullptr,
            0,
            &serialNumber,
            nullptr,
            nullptr,
            nullptr,
            0)) {
        return 0;
    }

    return static_cast<std::uint64_t>(serialNumber);
}

/**
 * @brief 查询已打开卷的 USN Journal 状态。
 * @param volume 卷设备句柄。
 * @param rootPath 卷根路径。
 * @return Journal 状态，无法查询时 valid 为 false。
 */
UsnJournalState QueryJournalStateForHandle(HANDLE volume, const std::wstring& rootPath) {
    UsnJournalState state;
    state.rootPath = rootPath;
    state.volumeSerialNumber = QueryVolumeSerialNumber(rootPath);

    USN_JOURNAL_DATA_V0 data{};
    DWORD bytesReturned = 0;
    if (!DeviceIoControl(
            volume,
            FSCTL_QUERY_USN_JOURNAL,
            nullptr,
            0,
            &data,
            sizeof(data),
            &bytesReturned,
            nullptr)) {
        return state;
    }

    state.journalId = static_cast<std::uint64_t>(data.UsnJournalID);
    state.firstUsn = static_cast<std::int64_t>(data.FirstUsn);
    state.nextUsn = static_cast<std::int64_t>(data.NextUsn);
    state.valid = state.volumeSerialNumber != 0 && state.journalId != 0;
    return state;
}

/**
 * @brief 读取卷的指定偏移。
 * @param volume 卷句柄。
 * @param offset 字节偏移。
 * @param buffer 输出缓冲区。
 * @param bytes 要读取的字节数。
 */
void ReadVolume(HANDLE volume, std::uint64_t offset, void* buffer, DWORD bytes) {
    LARGE_INTEGER position{};
    position.QuadPart = static_cast<LONGLONG>(offset);
    if (!SetFilePointerEx(volume, position, nullptr, FILE_BEGIN)) {
        throw std::runtime_error("failed to seek volume");
    }

    DWORD readBytes = 0;
    if (!ReadFile(volume, buffer, bytes, &readBytes, nullptr) || readBytes != bytes) {
        throw std::runtime_error("failed to read volume");
    }
}

/**
 * @brief 读取并解析 NTFS 引导扇区。
 * @param volume 卷句柄。
 * @return NTFS 引导参数。
 */
NtfsBootInfo ReadBootInfo(HANDLE volume) {
    std::uint8_t boot[512]{};
    ReadVolume(volume, 0, boot, sizeof(boot));

    if (std::memcmp(boot + 3, "NTFS", 4) != 0) {
        throw std::runtime_error("not an NTFS volume");
    }

    NtfsBootInfo info;
    info.bytesPerSector = ReadLe<std::uint16_t>(boot + 11);
    info.sectorsPerCluster = boot[13];
    info.bytesPerCluster = static_cast<std::uint32_t>(info.bytesPerSector) * info.sectorsPerCluster;
    info.mftLogicalCluster = ReadLe<std::int64_t>(boot + 48);

    const std::int8_t clustersPerRecord = static_cast<std::int8_t>(boot[64]);
    if (clustersPerRecord < 0) {
        info.fileRecordBytes = 1U << static_cast<unsigned int>(-clustersPerRecord);
    } else {
        info.fileRecordBytes = static_cast<std::uint32_t>(clustersPerRecord) * info.bytesPerCluster;
    }

    if (info.bytesPerCluster == 0 || info.fileRecordBytes == 0) {
        throw std::runtime_error("invalid NTFS boot information");
    }

    return info;
}

/**
 * @brief 应用 MFT 记录更新序列修复。
 * @param record MFT 记录缓冲区。
 * @param recordBytes MFT 记录字节数。
 * @param bytesPerSector 每扇区字节数。
 * @return 修复成功时返回 true。
 */
bool ApplyFixup(std::vector<std::uint8_t>& record, std::uint32_t recordBytes, std::uint16_t bytesPerSector) {
    if (recordBytes < 42 || bytesPerSector == 0 || std::memcmp(record.data(), "FILE", 4) != 0) {
        return false;
    }

    const std::uint16_t usaOffset = ReadLe<std::uint16_t>(record.data() + 4);
    const std::uint16_t usaCount = ReadLe<std::uint16_t>(record.data() + 6);
    if (usaOffset + usaCount * sizeof(std::uint16_t) > recordBytes || usaCount == 0) {
        return false;
    }

    const std::uint16_t sequence = ReadLe<std::uint16_t>(record.data() + usaOffset);
    for (std::uint16_t index = 1; index < usaCount; ++index) {
        const std::uint32_t sectorEnd = static_cast<std::uint32_t>(index) * bytesPerSector - sizeof(std::uint16_t);
        if (sectorEnd + sizeof(std::uint16_t) > recordBytes) {
            return false;
        }

        if (ReadLe<std::uint16_t>(record.data() + sectorEnd) != sequence) {
            return false;
        }

        const std::uint16_t replacement = ReadLe<std::uint16_t>(record.data() + usaOffset + index * sizeof(std::uint16_t));
        std::memcpy(record.data() + sectorEnd, &replacement, sizeof(replacement));
    }

    return true;
}

/**
 * @brief 解析 NTFS 非驻留属性的数据运行。
 * @param runData 数据运行起始指针。
 * @param maxBytes 可读取最大字节数。
 * @return 数据运行列表。
 */
std::vector<DataRun> ParseDataRuns(const std::uint8_t* runData, std::size_t maxBytes) {
    std::vector<DataRun> runs;
    std::size_t offset = 0;
    std::int64_t currentLcn = 0;

    while (offset < maxBytes && runData[offset] != 0) {
        const std::uint8_t header = runData[offset++];
        const std::uint8_t lengthBytes = header & 0x0F;
        const std::uint8_t offsetBytes = (header >> 4) & 0x0F;
        if (lengthBytes == 0 || offset + lengthBytes + offsetBytes > maxBytes) {
            break;
        }

        std::uint64_t clusterCount = 0;
        for (std::uint8_t index = 0; index < lengthBytes; ++index) {
            clusterCount |= static_cast<std::uint64_t>(runData[offset++]) << (index * 8U);
        }

        std::int64_t lcnDelta = 0;
        if (offsetBytes > 0) {
            const bool negative = (runData[offset + offsetBytes - 1] & 0x80) != 0;
            for (std::uint8_t index = 0; index < offsetBytes; ++index) {
                lcnDelta |= static_cast<std::int64_t>(runData[offset++]) << (index * 8U);
            }
            if (negative) {
                lcnDelta |= -1LL << (offsetBytes * 8U);
            }
        }

        currentLcn += lcnDelta;
        if (clusterCount > 0 && currentLcn > 0) {
            runs.push_back(DataRun{currentLcn, clusterCount});
        }
    }

    return runs;
}

/**
 * @brief 从数据运行中读取指定虚拟偏移。
 * @param volume 卷句柄。
 * @param boot NTFS 引导参数。
 * @param runs 数据运行列表。
 * @param virtualOffset 文件内偏移。
 * @param buffer 输出缓冲区。
 * @param bytes 要读取的字节数。
 * @return 读取成功时返回 true。
 */
bool ReadFromRuns(HANDLE volume, const NtfsBootInfo& boot, const std::vector<DataRun>& runs, std::uint64_t virtualOffset, void* buffer, DWORD bytes) {
    std::uint64_t runStart = 0;
    for (const DataRun& run : runs) {
        const std::uint64_t runBytes = run.clusterCount * boot.bytesPerCluster;
        if (virtualOffset >= runStart && virtualOffset + bytes <= runStart + runBytes) {
            const std::uint64_t insideRun = virtualOffset - runStart;
            const std::uint64_t diskOffset = static_cast<std::uint64_t>(run.logicalCluster) * boot.bytesPerCluster + insideRun;
            ReadVolume(volume, diskOffset, buffer, bytes);
            return true;
        }
        runStart += runBytes;
    }

    return false;
}

/**
 * @brief 解析 MFT 记录 0 中 $MFT 文件的数据运行。
 * @param record MFT 记录 0。
 * @return $MFT 数据运行列表。
 */
std::vector<DataRun> ParseMftDataRuns(const std::vector<std::uint8_t>& record) {
    const std::uint16_t attrOffset = ReadLe<std::uint16_t>(record.data() + 20);
    std::uint32_t offset = attrOffset;

    while (offset + 16 < record.size()) {
        const std::uint32_t type = ReadLe<std::uint32_t>(record.data() + offset);
        if (type == 0xFFFFFFFF) {
            break;
        }

        const std::uint32_t length = ReadLe<std::uint32_t>(record.data() + offset + 4);
        if (length == 0 || offset + length > record.size()) {
            break;
        }

        const std::uint8_t nonResident = record[offset + 8];
        const std::uint16_t nameLength = record[offset + 9];
        if (type == 0x80 && nonResident != 0 && nameLength == 0) {
            const std::uint16_t runOffset = ReadLe<std::uint16_t>(record.data() + offset + 32);
            if (runOffset < length) {
                return ParseDataRuns(record.data() + offset + runOffset, length - runOffset);
            }
        }

        offset += length;
    }

    throw std::runtime_error("failed to parse MFT data runs");
}

/**
 * @brief 把 Windows FILETIME 转换为 Unix epoch 毫秒（对齐 QDateTime::toMSecsSinceEpoch）。
 * @param fileTime Windows FILETIME（1601-01-01 起，100ns 单位）。
 * @return Unix epoch 毫秒；非法或早于 1970 的值返回 0（表示未知）。
 */
std::int64_t FileTimeToEpochMsec(const FILETIME& fileTime) {
    ULARGE_INTEGER large;
    large.LowPart = fileTime.dwLowDateTime;
    large.HighPart = fileTime.dwHighDateTime;
    // 1601-01-01 到 1970-01-01 的 100ns 计数。
    constexpr std::uint64_t epochOffset100ns = 116444736000000000ULL;
    if (large.QuadPart < epochOffset100ns) {
        return 0;
    }
    return static_cast<std::int64_t>((large.QuadPart - epochOffset100ns) / 10000ULL);
}

/**
 * @brief 解析单条 MFT 文件记录。
 * @param record MFT 记录缓冲区。
 * @param id 文件引用号低 48 位。
 * @param entry 输出记录。
 * @return 成功解析时返回 true。
 */
bool ParseFileRecord(const std::vector<std::uint8_t>& record, std::uint64_t id, MftEntry& entry) {
    if (record.size() < 64 || std::memcmp(record.data(), "FILE", 4) != 0) {
        return false;
    }

    const std::uint16_t flags = ReadLe<std::uint16_t>(record.data() + 22);
    if ((flags & 0x0001) == 0) {
        return false;
    }

    const std::uint64_t baseRecord = FileReferenceId(ReadLe<std::uint64_t>(record.data() + 32));
    if (baseRecord != 0) {
        return false;
    }

    entry.id = id;
    entry.isDirectory = (flags & 0x0002) != 0;

    bool hasFileName = false;
    bool hasSize = entry.isDirectory;
    std::uint64_t fileNameBytes = 0;
    const std::uint16_t attrOffset = ReadLe<std::uint16_t>(record.data() + 20);
    std::uint32_t offset = attrOffset;

    while (offset + 16 < record.size()) {
        const std::uint32_t type = ReadLe<std::uint32_t>(record.data() + offset);
        if (type == 0xFFFFFFFF) {
            break;
        }

        const std::uint32_t length = ReadLe<std::uint32_t>(record.data() + offset + 4);
        if (length == 0 || offset + length > record.size()) {
            break;
        }

        const std::uint8_t nonResident = record[offset + 8];
        const std::uint16_t nameLength = record[offset + 9];

        if (type == 0x10 && nonResident == 0) {
            // $STANDARD_INFORMATION：常驻属性体偏移 0x08 为最后修改时间（8 字节 FILETIME）。
            const std::uint32_t valueLength = ReadLe<std::uint32_t>(record.data() + offset + 16);
            const std::uint16_t valueOffset = ReadLe<std::uint16_t>(record.data() + offset + 20);
            if (valueOffset + valueLength <= length && valueLength >= 0x10) {
                const std::uint8_t* value = record.data() + offset + valueOffset;
                FILETIME fileTime;
                std::memcpy(&fileTime, value + 0x08, sizeof(FILETIME));
                entry.modificationTime = FileTimeToEpochMsec(fileTime);
            }
        } else if (type == 0x30 && nonResident == 0) {
            const std::uint32_t valueLength = ReadLe<std::uint32_t>(record.data() + offset + 16);
            const std::uint16_t valueOffset = ReadLe<std::uint16_t>(record.data() + offset + 20);
            if (valueOffset + valueLength <= length && valueLength >= 66) {
                const std::uint8_t* value = record.data() + offset + valueOffset;
                const std::uint8_t fileNameLength = value[64];
                const std::uint8_t nameNamespace = value[65];
                if (fileNameLength > 0 && 66 + fileNameLength * sizeof(wchar_t) <= valueLength && nameNamespace != 2) {
                    entry.parentId = FileReferenceId(ReadLe<std::uint64_t>(value));
                    entry.name.assign(reinterpret_cast<const wchar_t*>(value + 66), fileNameLength);
                    if (!entry.isDirectory) {
                        fileNameBytes = ReadLe<std::uint64_t>(value + 48);
                    }
                    hasFileName = true;
                }
            }
        } else if (type == 0x80 && nameLength == 0 && !entry.isDirectory) {
            if (nonResident == 0) {
                entry.bytes = ReadLe<std::uint32_t>(record.data() + offset + 16);
                hasSize = true;
            } else if (length >= 56) {
                entry.bytes = ReadLe<std::uint64_t>(record.data() + offset + 48);
                hasSize = true;
            }
        }

        offset += length;
    }

    if (!hasSize && hasFileName && !entry.isDirectory) {
        entry.bytes = fileNameBytes;
        hasSize = true;
    }

    return hasFileName && hasSize && !entry.name.empty();
}

/**
 * @brief 累计文件扩展名统计。
 * @param result 扫描结果。
 * @param fileName 文件名。
 * @param fileBytes 文件大小。
 */
void AddExtension(ScanResult& result, const std::wstring& fileName, std::uint64_t fileBytes) {
    const std::size_t dot = fileName.find_last_of(L'.');
    std::wstring extension = dot == std::wstring::npos ? L"(无扩展名)" : fileName.substr(dot);
    std::transform(extension.begin(), extension.end(), extension.begin(), [](wchar_t value) {
        return static_cast<wchar_t>(towlower(value));
    });

    auto& summary = result.extensions[extension];
    summary.extension = extension;
    summary.totalBytes += fileBytes;
    summary.fileCount += 1;
}

/**
 * @brief 递归聚合节点大小和路径。
 * @param node 当前节点。
 * @param path 当前完整路径。
 * @return 节点总大小。
 */
std::uint64_t FinalizeTree(ScanNode& node, const std::wstring& path) {
    node.path = path;
    if (node.kind == NodeKind::File) {
        return node.totalBytes;
    }

    std::uint64_t total = 0;
    for (auto& child : node.children) {
        const std::wstring childPath = path.back() == L'\\' ? path + child->name : path + L"\\" + child->name;
        total += FinalizeTree(*child, childPath);
    }

    node.totalBytes = total;
    std::sort(node.children.begin(), node.children.end(), [](const auto& left, const auto& right) {
        return left->totalBytes > right->totalBytes;
    });
    return total;
}

/**
 * @brief 判断取消标志是否已经请求终止。
 * @param cancelFlag 可选取消标志。
 * @return 需要取消时返回 true。
 */
bool IsCancellationRequested(const std::atomic_bool* cancelFlag) {
    return cancelFlag != nullptr && cancelFlag->load();
}

/**
 * @brief 连接 Windows 路径片段。
 * @param parent 父路径。
 * @param name 子名称。
 * @return 拼接后的完整路径。
 */
std::wstring JoinNtfsPath(const std::wstring& parent, const std::wstring& name) {
    if (parent.empty()) {
        return name;
    }
    if (name.empty()) {
        return parent;
    }
    if (parent.back() == L'\\' || parent.back() == L'/') {
        return parent + name;
    }
    return parent + L"\\" + name;
}

/**
 * @brief 读取指定 NTFS 卷的 MFT 有效记录表。
 * @param volume 卷设备句柄。
 * @param boot NTFS 引导参数。
 * @param cancelFlag 可选取消标志。
 * @return MFT 条目表。
 */
MftEntryTable ReadMftEntryTable(HANDLE volume, const NtfsBootInfo& boot, const std::atomic_bool* cancelFlag) {
    std::vector<std::uint8_t> mftRecord0(boot.fileRecordBytes);
    ReadVolume(volume, static_cast<std::uint64_t>(boot.mftLogicalCluster) * boot.bytesPerCluster, mftRecord0.data(), boot.fileRecordBytes);
    if (!ApplyFixup(mftRecord0, boot.fileRecordBytes, boot.bytesPerSector)) {
        throw std::runtime_error("failed to fix MFT record 0");
    }

    const std::vector<DataRun> mftRuns = ParseMftDataRuns(mftRecord0);
    std::uint64_t mftBytes = 0;
    for (const DataRun& run : mftRuns) {
        mftBytes += run.clusterCount * boot.bytesPerCluster;
    }

    const std::uint64_t recordCapacity = mftBytes / boot.fileRecordBytes;
    MftEntryTable table;
    table.entries.resize(static_cast<std::size_t>(recordCapacity));
    table.present.resize(static_cast<std::size_t>(recordCapacity), 0);

    std::vector<std::uint8_t> record(boot.fileRecordBytes);
    std::uint64_t id = 0;
    constexpr std::uint64_t chunkBytesTarget = 128ULL * 1024ULL * 1024ULL;
    const std::uint64_t recordsPerChunk = std::max<std::uint64_t>(1, chunkBytesTarget / boot.fileRecordBytes);
    std::vector<std::uint8_t> chunk(static_cast<std::size_t>(recordsPerChunk * boot.fileRecordBytes));

    for (const DataRun& run : mftRuns) {
        std::uint64_t remainingRunBytes = run.clusterCount * boot.bytesPerCluster;
        std::uint64_t diskOffset = static_cast<std::uint64_t>(run.logicalCluster) * boot.bytesPerCluster;

        while (remainingRunBytes >= boot.fileRecordBytes && !IsCancellationRequested(cancelFlag)) {
            const std::uint64_t bytesToRead = std::min<std::uint64_t>(chunk.size(), remainingRunBytes - (remainingRunBytes % boot.fileRecordBytes));
            if (bytesToRead == 0) {
                break;
            }

            ReadVolume(volume, diskOffset, chunk.data(), static_cast<DWORD>(bytesToRead));
            const std::uint64_t recordsInChunk = bytesToRead / boot.fileRecordBytes;

            for (std::uint64_t recordIndex = 0; recordIndex < recordsInChunk; ++recordIndex) {
                if ((recordIndex & 0x3FFFULL) == 0 && IsCancellationRequested(cancelFlag)) {
                    break;
                }

                const std::uint8_t* source = chunk.data() + recordIndex * boot.fileRecordBytes;
                std::memcpy(record.data(), source, boot.fileRecordBytes);
                if (!ApplyFixup(record, boot.fileRecordBytes, boot.bytesPerSector)) {
                    ++id;
                    continue;
                }

                MftEntry entry;
                if (ParseFileRecord(record, id, entry) && id < table.entries.size()) {
                    if (entry.parentId == 5 && IsNtfsMetadataFile(entry.name)) {
                        ++id;
                        continue;
                    }

                    table.present[static_cast<std::size_t>(id)] = 1;
                    if (entry.isDirectory) {
                        ++table.directoryCount;
                    } else {
                        ++table.fileCount;
                    }
                    table.entries[static_cast<std::size_t>(id)] = std::move(entry);
                }
                ++id;
            }

            diskOffset += bytesToRead;
            remainingRunBytes -= bytesToRead;
        }
    }

    return table;
}

/**
 * @brief 构建目录记录的完整路径。
 * @param id 目录文件引用号低 48 位。
 * @param rootPath 卷根路径。
 * @param entries MFT 条目表。
 * @param present 条目是否存在的标记表。
 * @param directoryPaths 目录路径缓存。
 * @param ready 目录路径是否已经缓存。
 * @param visiting 递归访问状态，用于打断异常父级环。
 * @return 目录完整路径。
 */
std::wstring BuildDirectoryIndexPath(
    std::uint64_t id,
    const std::wstring& rootPath,
    const std::vector<MftEntry>& entries,
    const std::vector<std::uint8_t>& present,
    std::vector<std::wstring>& directoryPaths,
    std::vector<std::uint8_t>& ready,
    std::vector<std::uint8_t>& visiting) {
    if (id == 5) {
        return rootPath;
    }

    if (id >= entries.size() || present[static_cast<std::size_t>(id)] == 0 || !entries[static_cast<std::size_t>(id)].isDirectory) {
        return JoinNtfsPath(rootPath, L"(孤立项目)");
    }

    const std::size_t index = static_cast<std::size_t>(id);
    if (ready[index] != 0) {
        return directoryPaths[index];
    }
    if (visiting[index] != 0) {
        return JoinNtfsPath(rootPath, entries[index].name);
    }

    visiting[index] = 1;
    const std::wstring parentPath = entries[index].parentId == 5
        ? rootPath
        : BuildDirectoryIndexPath(entries[index].parentId, rootPath, entries, present, directoryPaths, ready, visiting);
    directoryPaths[index] = JoinNtfsPath(parentPath, entries[index].name);
    ready[index] = 1;
    visiting[index] = 0;
    return directoryPaths[index];
}

/**
 * @brief 判断 USN 原因是否表示当前文件名已经失效。
 * @param reason USN 原因掩码。
 * @return 删除或旧名称失效时返回 true。
 */
bool IsDeletedUsnReason(std::uint32_t reason) {
    return (reason & (USN_REASON_FILE_DELETE | USN_REASON_RENAME_OLD_NAME)) != 0;
}

/**
 * @brief 将 USN_RECORD_V2 转换为快速搜索索引变更。
 * @param record Windows USN 记录。
 * @return 内部文件索引变更记录。
 */
FileIndexChange ToFileIndexChange(const USN_RECORD_V2& record) {
    FileIndexChange change;
    change.fileReference = FileReferenceId(record.FileReferenceNumber);
    change.parentReference = FileReferenceId(record.ParentFileReferenceNumber);
    change.kind = (record.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 ? NodeKind::Directory : NodeKind::File;
    change.reason = record.Reason;
    change.deleted = IsDeletedUsnReason(record.Reason);
    if (record.FileNameLength > 0) {
        change.name.assign(record.FileName, record.FileNameLength / sizeof(wchar_t));
    }
    return change;
}

}  // namespace

bool NtfsMftScanner::CanScan(const std::wstring& rootPath) const {
    wchar_t fileSystemName[MAX_PATH]{};
    if (!GetVolumeInformationW(
            rootPath.c_str(),
            nullptr,
            0,
            nullptr,
            nullptr,
            nullptr,
            fileSystemName,
            MAX_PATH)) {
        return false;
    }

    return _wcsicmp(fileSystemName, L"NTFS") == 0;
}

ScanResult NtfsMftScanner::Scan(const std::wstring& rootPath) {
    ScanResult result;
    const std::wstring volumePath = ToVolumePath(rootPath);
    HANDLE volume = CreateFileW(
        volumePath.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);

    if (volume == INVALID_HANDLE_VALUE) {
        throw std::runtime_error("failed to open volume");
    }

    try {
        const NtfsBootInfo boot = ReadBootInfo(volume);
        std::vector<std::uint8_t> mftRecord0(boot.fileRecordBytes);
        ReadVolume(volume, static_cast<std::uint64_t>(boot.mftLogicalCluster) * boot.bytesPerCluster, mftRecord0.data(), boot.fileRecordBytes);
        if (!ApplyFixup(mftRecord0, boot.fileRecordBytes, boot.bytesPerSector)) {
            throw std::runtime_error("failed to fix MFT record 0");
        }

        const std::vector<DataRun> mftRuns = ParseMftDataRuns(mftRecord0);
        std::uint64_t mftBytes = 0;
        for (const DataRun& run : mftRuns) {
            mftBytes += run.clusterCount * boot.bytesPerCluster;
        }

        std::unordered_map<std::uint64_t, MftEntry> entries;
        entries.reserve(static_cast<std::size_t>(std::min<std::uint64_t>(mftBytes / boot.fileRecordBytes, 4000000ULL)));
        std::vector<std::uint8_t> record(boot.fileRecordBytes);

        std::uint64_t id = 0;
        constexpr std::uint64_t chunkBytesTarget = 64ULL * 1024ULL * 1024ULL;
        const std::uint64_t recordsPerChunk = std::max<std::uint64_t>(1, chunkBytesTarget / boot.fileRecordBytes);
        std::vector<std::uint8_t> chunk(static_cast<std::size_t>(recordsPerChunk * boot.fileRecordBytes));

        for (const DataRun& run : mftRuns) {
            std::uint64_t remainingRunBytes = run.clusterCount * boot.bytesPerCluster;
            std::uint64_t diskOffset = static_cast<std::uint64_t>(run.logicalCluster) * boot.bytesPerCluster;

            while (remainingRunBytes >= boot.fileRecordBytes) {
                const std::uint64_t bytesToRead = std::min<std::uint64_t>(chunk.size(), remainingRunBytes - (remainingRunBytes % boot.fileRecordBytes));
                if (bytesToRead == 0) {
                    break;
                }

                ReadVolume(volume, diskOffset, chunk.data(), static_cast<DWORD>(bytesToRead));
                const std::uint64_t recordsInChunk = bytesToRead / boot.fileRecordBytes;

                for (std::uint64_t recordIndex = 0; recordIndex < recordsInChunk; ++recordIndex) {
                    const std::uint8_t* source = chunk.data() + recordIndex * boot.fileRecordBytes;
                    std::memcpy(record.data(), source, boot.fileRecordBytes);
                    if (!ApplyFixup(record, boot.fileRecordBytes, boot.bytesPerSector)) {
                        ++id;
                        continue;
                    }

                    MftEntry entry;
                    if (ParseFileRecord(record, id, entry)) {
                        entries[id] = std::move(entry);
                    }
                    ++id;
                }

                diskOffset += bytesToRead;
                remainingRunBytes -= bytesToRead;
            }
        }

        result.root = std::make_unique<ScanNode>();
        result.root->name = rootPath;
        result.root->path = rootPath;
        result.root->kind = NodeKind::Directory;

        std::unordered_map<std::uint64_t, std::unique_ptr<ScanNode>> ownedNodes;
        std::unordered_map<std::uint64_t, ScanNode*> nodeById;
        ownedNodes.reserve(entries.size());
        nodeById.reserve(entries.size() + 1);
        nodeById[5] = result.root.get();

        for (const auto& pair : entries) {
            const MftEntry& entry = pair.second;
            if (entry.id == 5) {
                continue;
            }

            if (entry.parentId == 5 && IsNtfsMetadataFile(entry.name)) {
                continue;
            }

            auto node = std::make_unique<ScanNode>();
            node->name = entry.name;
            node->kind = entry.isDirectory ? NodeKind::Directory : NodeKind::File;
            node->ownBytes = entry.isDirectory ? 0 : entry.bytes;
            node->totalBytes = entry.isDirectory ? 0 : entry.bytes;
            node->lastModifiedMsec = entry.modificationTime;

            ScanNode* raw = node.get();
            nodeById[entry.id] = raw;
            ownedNodes[entry.id] = std::move(node);

            if (entry.isDirectory) {
                ++result.directoryCount;
            } else {
                ++result.fileCount;
                AddExtension(result, entry.name, entry.bytes);
            }
        }

        std::vector<std::uint64_t> orphanIds;
        for (const auto& pair : entries) {
            const MftEntry& entry = pair.second;
            if (entry.id == 5) {
                continue;
            }

            auto nodeIterator = ownedNodes.find(entry.id);
            if (nodeIterator == ownedNodes.end() || !nodeIterator->second) {
                continue;
            }

            auto parentIterator = nodeById.find(entry.parentId);
            if (parentIterator == nodeById.end() || parentIterator->second == nodeIterator->second.get()) {
                orphanIds.push_back(entry.id);
                continue;
            }

            parentIterator->second->children.push_back(std::move(nodeIterator->second));
        }

        if (!orphanIds.empty()) {
            auto orphanRoot = std::make_unique<ScanNode>();
            orphanRoot->name = L"(孤立项目)";
            orphanRoot->kind = NodeKind::Directory;

            for (std::uint64_t orphanId : orphanIds) {
                auto nodeIterator = ownedNodes.find(orphanId);
                if (nodeIterator != ownedNodes.end() && nodeIterator->second) {
                    orphanRoot->children.push_back(std::move(nodeIterator->second));
                }
            }

            if (!orphanRoot->children.empty()) {
                result.root->children.push_back(std::move(orphanRoot));
            }
        }

        result.directoryCount += 1;
        FinalizeTree(*result.root, rootPath);
        CloseHandle(volume);
        return result;
    } catch (...) {
        CloseHandle(volume);
        throw;
    }
}

FileIndexResult NtfsMftScanner::BuildFileIndex(const std::wstring& rootPath, const std::atomic_bool& cancelFlag) {
    FileIndexResult result;
    const std::wstring volumePath = ToVolumePath(rootPath);
    HANDLE volume = CreateFileW(
        volumePath.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);

    if (volume == INVALID_HANDLE_VALUE) {
        throw std::runtime_error("failed to open volume");
    }

    try {
        const NtfsBootInfo boot = ReadBootInfo(volume);
        MftEntryTable table = ReadMftEntryTable(volume, boot, &cancelFlag);
        result.journalState = QueryJournalStateForHandle(volume, rootPath);
        CloseHandle(volume);
        volume = INVALID_HANDLE_VALUE;

        if (cancelFlag.load()) {
            return result;
        }

        result.records.reserve(static_cast<std::size_t>(table.fileCount + table.directoryCount + 1));
        result.records.push_back(FileIndexRecord{
            rootPath,
            rootPath,
            NodeKind::Directory,
            0,
            5,
            5,
            0,
        });
        result.directoryCount = 1;

        std::vector<std::wstring> directoryPaths(table.entries.size());
        std::vector<std::uint8_t> directoryPathReady(table.entries.size(), 0);
        std::vector<std::uint8_t> directoryVisiting(table.entries.size(), 0);

        for (std::size_t index = 0; index < table.entries.size() && !cancelFlag.load(); ++index) {
            if (table.present[index] == 0 || index == 5) {
                continue;
            }

            const MftEntry& entry = table.entries[index];
            if (entry.isDirectory) {
                const std::wstring path = BuildDirectoryIndexPath(
                    static_cast<std::uint64_t>(index),
                    rootPath,
                    table.entries,
                    table.present,
                    directoryPaths,
                    directoryPathReady,
                    directoryVisiting);
                result.records.push_back(FileIndexRecord{
                    entry.name,
                    path,
                    NodeKind::Directory,
                    0,
                    entry.id,
                    entry.parentId,
                    entry.modificationTime,
                });
                ++result.directoryCount;
                continue;
            }

            const std::wstring parentPath = BuildDirectoryIndexPath(
                entry.parentId,
                rootPath,
                table.entries,
                table.present,
                directoryPaths,
                directoryPathReady,
                directoryVisiting);
            result.records.push_back(FileIndexRecord{
                entry.name,
                JoinNtfsPath(parentPath, entry.name),
                NodeKind::File,
                entry.bytes,
                entry.id,
                entry.parentId,
                entry.modificationTime,
            });
            ++result.fileCount;
        }

        return result;
    } catch (...) {
        if (volume != INVALID_HANDLE_VALUE) {
            CloseHandle(volume);
        }
        throw;
    }
}

UsnJournalState NtfsMftScanner::QueryJournalState(const std::wstring& rootPath) const {
    HANDLE volume = OpenVolumeForRead(rootPath);
    if (volume == INVALID_HANDLE_VALUE) {
        UsnJournalState state;
        state.rootPath = rootPath;
        state.volumeSerialNumber = QueryVolumeSerialNumber(rootPath);
        return state;
    }

    UsnJournalState state = QueryJournalStateForHandle(volume, rootPath);
    CloseHandle(volume);
    return state;
}

FileIndexChangeResult NtfsMftScanner::ReadFileIndexChanges(const UsnJournalState& previousState, const std::atomic_bool& cancelFlag) const {
    FileIndexChangeResult result;
    result.journalState = QueryJournalState(previousState.rootPath);
    if (!previousState.valid || !result.journalState.valid) {
        result.requiresFullRebuild = true;
        return result;
    }

    if (previousState.volumeSerialNumber != result.journalState.volumeSerialNumber ||
        previousState.journalId != result.journalState.journalId ||
        previousState.nextUsn < result.journalState.firstUsn ||
        previousState.nextUsn > result.journalState.nextUsn) {
        result.requiresFullRebuild = true;
        return result;
    }

    if (previousState.nextUsn == result.journalState.nextUsn || cancelFlag.load()) {
        return result;
    }

    HANDLE volume = OpenVolumeForRead(previousState.rootPath);
    if (volume == INVALID_HANDLE_VALUE) {
        result.requiresFullRebuild = true;
        return result;
    }

    READ_USN_JOURNAL_DATA_V0 readData{};
    readData.StartUsn = static_cast<USN>(previousState.nextUsn);
    readData.ReasonMask = 0xFFFFFFFF;
    readData.ReturnOnlyOnClose = FALSE;
    readData.Timeout = 0;
    readData.BytesToWaitFor = 0;
    readData.UsnJournalID = static_cast<DWORDLONG>(previousState.journalId);

    std::vector<std::uint8_t> buffer(1024U * 1024U);
    while (readData.StartUsn < static_cast<USN>(result.journalState.nextUsn) && !cancelFlag.load()) {
        DWORD bytesReturned = 0;
        if (!DeviceIoControl(
                volume,
                FSCTL_READ_USN_JOURNAL,
                &readData,
                sizeof(readData),
                buffer.data(),
                static_cast<DWORD>(buffer.size()),
                &bytesReturned,
                nullptr)) {
            const DWORD error = GetLastError();
            if (error == ERROR_JOURNAL_ENTRY_DELETED || error == ERROR_JOURNAL_DELETE_IN_PROGRESS || error == ERROR_INVALID_PARAMETER) {
                result.requiresFullRebuild = true;
            }
            break;
        }

        if (bytesReturned <= sizeof(USN)) {
            break;
        }

        USN nextUsn = 0;
        std::memcpy(&nextUsn, buffer.data(), sizeof(nextUsn));
        std::size_t offset = sizeof(USN);
        while (offset + sizeof(DWORD) <= bytesReturned && !cancelFlag.load()) {
            const auto* cursor = buffer.data() + offset;
            const DWORD recordLength = ReadLe<DWORD>(cursor);
            if (recordLength == 0 || offset + recordLength > bytesReturned) {
                break;
            }

            const WORD majorVersion = ReadLe<WORD>(cursor + sizeof(DWORD));
            if (majorVersion == 2 && recordLength >= sizeof(USN_RECORD_V2)) {
                const auto* record = reinterpret_cast<const USN_RECORD_V2*>(cursor);
                FileIndexChange change = ToFileIndexChange(*record);
                if (!change.name.empty() && change.fileReference != 0) {
                    result.changes.push_back(std::move(change));
                }
            }

            offset += recordLength;
        }

        if (nextUsn <= readData.StartUsn) {
            break;
        }
        readData.StartUsn = nextUsn;
    }

    CloseHandle(volume);
    return result;
}

}  // namespace disk_lens::core
