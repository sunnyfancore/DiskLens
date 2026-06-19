#include "core/DiskHealth.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <windows.h>
#include <winioctl.h>
#include <ntddscsi.h>
#include <ntddstor.h>

namespace disk_lens::core {
namespace {

/**
 * @brief 把 ASCII(偏移处的空终止串)转为 wstring;偏移非法时返回空。
 */
std::wstring AnsiOffsetToString(const unsigned char* buffer, std::size_t bufferSize, std::uint32_t offset) {
    if (offset == 0 || offset >= bufferSize) {
        return std::wstring();
    }
    const char* text = reinterpret_cast<const char*>(buffer + offset);
    // 多数厂商串带尾随空格,裁掉。
    std::string raw(text);
    while (!raw.empty() && (raw.back() == ' ' || raw.back() == '\0')) {
        raw.pop_back();
    }
    return std::wstring(raw.begin(), raw.end());
}

/**
 * @brief 把单字节格式化为两位大写十六进制 wstring(用于寄存器/错误码诊断)。
 */
std::wstring ByteToHex(unsigned char value) {
    const wchar_t* digits = L"0123456789ABCDEF";
    return std::wstring(1, digits[value >> 4]) + std::wstring(1, digits[value & 0x0F]);
}

/**
 * @brief 打开物理盘句柄;先用读写权限(ATA 直读需要),失败再回退只读。
 */
HANDLE OpenPhysicalDrive(int diskNumber) {
    const std::wstring path = L"\\\\.\\PhysicalDrive" + std::to_wstring(diskNumber);
    HANDLE handle = CreateFileW(
        path.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_EXISTING,
        0,
        nullptr);
    if (handle != INVALID_HANDLE_VALUE) {
        return handle;
    }
    handle = CreateFileW(
        path.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_EXISTING,
        0,
        nullptr);
    return handle;
}

/**
 * @brief 取物理盘总字节数;失败返回 0。
 */
std::uint64_t QueryDiskTotalBytes(HANDLE handle) {
    GET_LENGTH_INFORMATION info{};
    DWORD returned = 0;
    if (!DeviceIoControl(handle, IOCTL_DISK_GET_LENGTH_INFO, nullptr, 0, &info, sizeof(info), &returned, nullptr)) {
        return 0;
    }
    return static_cast<std::uint64_t>(info.Length.QuadPart);
}

/**
 * @brief 取型号/序列号/总线类型;失败时各字段留空。
 */
void QueryStorageDevice(HANDLE handle, std::wstring& model, std::wstring& serial, STORAGE_BUS_TYPE& busType) {
    busType = BusTypeUnknown;
    STORAGE_PROPERTY_QUERY query{};
    query.PropertyId = StorageDeviceProperty;
    query.QueryType = PropertyStandardQuery;

    std::vector<unsigned char> buffer(4096, 0);
    DWORD returned = 0;
    if (!DeviceIoControl(handle, IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query), buffer.data(),
                         static_cast<DWORD>(buffer.size()), &returned, nullptr)) {
        return;
    }
    if (returned < sizeof(STORAGE_DEVICE_DESCRIPTOR)) {
        return;
    }
    const auto* desc = reinterpret_cast<const STORAGE_DEVICE_DESCRIPTOR*>(buffer.data());
    busType = desc->BusType;
    std::wstring vendor = AnsiOffsetToString(buffer.data(), buffer.size(), desc->VendorIdOffset);
    std::wstring product = AnsiOffsetToString(buffer.data(), buffer.size(), desc->ProductIdOffset);
    serial = AnsiOffsetToString(buffer.data(), buffer.size(), desc->SerialNumberOffset);

    model = product;
    if (!vendor.empty()) {
        model = vendor + L" " + product;
    }
    // 裁掉序列号尾随空格(已在 AnsiOffsetToString 裁过,这里双保险)。
}

/**
 * @brief 读取 NVMe SMART/Health 日志并填充温度/可用备用/寿命/通电。
 *
 * 输入须为编译器布局的 STORAGE_PROPERTY_QUERY + STORAGE_PROTOCOL_SPECIFIC_DATA 组合体:后者需 8 字节对齐,
 * 故不能手动按 sizeof(STORAGE_PROPERTY_QUERY)=12 偏移拼接(会落到偏移 12 而非编译器补齐的 16,驱动读错位直接失败)。
 * 这里用组合结构体让编译器算正确偏移,缓冲尾部再留 512 字节承载日志(MS stordiag 标准范式)。
 */
void QueryNvmeHealth(HANDLE handle, DiskHealthInfo& info) {
    struct NvmeProtocolQuery {
        STORAGE_PROPERTY_QUERY propertyQuery;
        STORAGE_PROTOCOL_SPECIFIC_DATA protocolData;
    };

    constexpr std::size_t logBytes = 512;
    std::vector<unsigned char> buffer(sizeof(NvmeProtocolQuery) + logBytes, 0);
    auto* query = reinterpret_cast<NvmeProtocolQuery*>(buffer.data());
    query->propertyQuery.PropertyId = StorageAdapterProtocolSpecificProperty;
    query->propertyQuery.QueryType = PropertyStandardQuery;
    query->protocolData.ProtocolType = ProtocolTypeNvme;
    query->protocolData.DataType = NVMeDataTypeLogPage;
    query->protocolData.ProtocolDataRequestValue = 0x02;        // SMART/Health 日志页。
    query->protocolData.ProtocolDataRequestSubValue = 0;        // NSID=0(控制器级日志)。
    query->protocolData.ProtocolDataOffset = sizeof(STORAGE_PROTOCOL_SPECIFIC_DATA);
    query->protocolData.ProtocolDataLength = static_cast<ULONG>(logBytes);

    DWORD returned = 0;
    if (!DeviceIoControl(handle, IOCTL_STORAGE_QUERY_PROPERTY, buffer.data(), static_cast<DWORD>(buffer.size()),
                         buffer.data(), static_cast<DWORD>(buffer.size()), &returned, nullptr)) {
        const DWORD err = GetLastError();
        info.note = L"NVMe 健康日志读取失败(错误码 " + std::to_wstring(err) + L")";
        return;
    }

    // 输出为 STORAGE_PROTOCOL_DATA_DESCRIPTOR:其后是回显的 STORAGE_PROTOCOL_SPECIFIC_DATA,再其后是日志数据。
    if (returned < sizeof(STORAGE_PROTOCOL_DATA_DESCRIPTOR)) {
        info.note = L"NVMe 健康日志返回数据过短(returned=" + std::to_wstring(returned) + L")";
        return;
    }
    const auto* descriptor = reinterpret_cast<const STORAGE_PROTOCOL_DATA_DESCRIPTOR*>(buffer.data());
    const STORAGE_PROTOCOL_SPECIFIC_DATA& echo = descriptor->ProtocolSpecificData;
    const ULONG dataOffset = echo.ProtocolDataOffset;
    const unsigned char* protocolSpecificStart = reinterpret_cast<const unsigned char*>(&echo);
    const std::size_t base = static_cast<std::size_t>(protocolSpecificStart - buffer.data()) + dataOffset;
    if (dataOffset == 0 || base + logBytes > buffer.size()) {
        info.note = L"NVMe 健康日志偏移非法(offset=" + std::to_wstring(dataOffset) + L")";
        return;
    }
    const unsigned char* log = buffer.data() + base;

    const std::uint8_t criticalWarning = log[0];
    const std::uint16_t temperatureKelvin = static_cast<std::uint16_t>(log[1]) | (static_cast<std::uint16_t>(log[2]) << 8);
    info.temperatureCelsius = static_cast<int>(temperatureKelvin) - 273;
    info.availableSparePercent = static_cast<int>(log[3]);
    const std::uint8_t availableSpareThreshold = log[4];
    const std::uint8_t percentageUsed = log[5];
    info.healthPercent = 100 - static_cast<int>(percentageUsed);
    if (info.healthPercent < 0) {
        info.healthPercent = 0;
    }

    // uint128 小端计数:只取低 8 字节足够。
    auto readU128 = [&log](std::size_t offset) -> long long {
        unsigned long long value = 0;
        for (int i = 7; i >= 0; --i) {
            value = (value << 8) | log[offset + i];
        }
        return static_cast<long long>(value);
    };
    info.powerCycleCount = readU128(72);
    info.powerOnHours = readU128(80);

    // 状态:位 0 = 可用备用已低于阈值;位 2 = 严重可靠性降级。
    if ((criticalWarning & 0x04) != 0) {
        info.status = DiskHealthStatus::Warning;
    } else if ((criticalWarning & 0x01) != 0 || info.availableSparePercent < static_cast<int>(availableSpareThreshold)) {
        info.status = DiskHealthStatus::Attention;
    } else if (percentageUsed >= 90) {
        info.status = DiskHealthStatus::Attention;
    } else {
        info.status = DiskHealthStatus::Good;
    }
    info.note = L"NVMe 健康日志";
}

/**
 * @brief 解析 512 字节 SMART READ DATA 的 30 个属性,取出关键原始值。
 */
void ParseAtaSmartAttributes(const unsigned char* data, DiskHealthInfo& info) {
    for (int index = 0; index < 30; ++index) {
        const std::size_t base = 2 + static_cast<std::size_t>(index) * 12;
        const unsigned char id = data[base];
        if (id == 0) {
            continue;
        }
        // 原始值在 base+5,6 字节小端。
        unsigned long long raw = 0;
        for (int b = 5; b <= 10; ++b) {
            raw |= static_cast<unsigned long long>(data[base + b]) << (8 * (b - 5));
        }
        const long long rawValue = static_cast<long long>(raw);
        switch (id) {
            case 5:
                info.reallocatedSectorCount = rawValue;
                break;
            case 9:
                info.powerOnHours = rawValue;
                break;
            case 12:
                info.powerCycleCount = rawValue;
                break;
            case 194:
                info.temperatureCelsius = static_cast<int>(raw & 0xFF);
                break;
            case 197:
                info.currentPendingSectorCount = rawValue;
                break;
            case 198:
                info.uncorrectableSectorCount = rawValue;
                break;
            default:
                break;
        }
    }
}

/**
 * @brief 通过 IOCTL_ATA_PASS_THROUGH 发 SMART READ DATA(0xB0/D0),解析关键属性。
 */
void QueryAtaSmart(HANDLE handle, DiskHealthInfo& info) {
    struct AtaPassThroughRequest {
        ATA_PASS_THROUGH_EX apt;
        unsigned char data[512];
    };

    AtaPassThroughRequest request{};
    request.apt.Length = sizeof(ATA_PASS_THROUGH_EX);
    // 仅 DATA_IN:不要求 DRDY。部分消费级 SATA 控制器在 ATA_FLAGS_DRDY_REQUIRED 下会判盘未就绪而直接失败,
    // 而多数 SMART 工具正是省略此位以扩大兼容面。
    request.apt.AtaFlags = ATA_FLAGS_DATA_IN;
    request.apt.DataTransferLength = 512;
    request.apt.TimeOutValue = 3;
    request.apt.DataBufferOffset = offsetof(AtaPassThroughRequest, data);
    request.apt.CurrentTaskFile[0] = 0xD0;   // Features = SMART READ DATA 子命令。
    request.apt.CurrentTaskFile[1] = 0x01;   // Sector Count。
    request.apt.CurrentTaskFile[2] = 0x00;   // Sector Number。
    request.apt.CurrentTaskFile[3] = 0x4F;   // Cylinder Low(SMART 签名必须 0x4F)。
    request.apt.CurrentTaskFile[4] = 0xC2;   // Cylinder High(SMART 签名必须 0xC2)。
    request.apt.CurrentTaskFile[5] = 0xA0;   // Device/Head。
    request.apt.CurrentTaskFile[6] = 0xB0;   // Command = SMART。

    DWORD returned = 0;
    if (!DeviceIoControl(handle, IOCTL_ATA_PASS_THROUGH, &request, sizeof(request), &request, sizeof(request), &returned, nullptr)) {
        const DWORD err = GetLastError();
        info.note = L"ATA SMART 直读失败(错误码 " + std::to_wstring(err) + L")";
        return;
    }
    // DeviceIoControl 成功后,寄存器被驱动回填:CurrentTaskFile[6] 为状态寄存器,位 0(0x01)= ERR。
    const unsigned char statusReg = request.apt.CurrentTaskFile[6];
    if ((statusReg & 0x01) != 0) {
        // ERR 置位:错误寄存器(CurrentTaskFile[0],常见 0x04=ABRT 命令被拒/SMART 未启用)说明原因。
        const unsigned char errorReg = request.apt.CurrentTaskFile[0];
        info.note = L"ATA SMART 命令返回错误(状态 0x" + ByteToHex(statusReg) +
                    L" · 错误 0x" + ByteToHex(errorReg) + L")";
        return;
    }
    // 不再用 returned 严格校验:部分存储驱动对 ATA 直通少报返回字节数(returned 可能 < 结构+512),
    // 但数据已实际写入缓冲;改为判缓冲是否非全零,避免误把有效数据当"不完整"丢弃。
    bool anyNonZero = false;
    for (std::size_t i = 0; i < sizeof(request.data); ++i) {
        if (request.data[i] != 0) {
            anyNonZero = true;
            break;
        }
    }
    if (!anyNonZero) {
        info.note = L"ATA SMART 返回空数据(returned=" + std::to_wstring(returned) + L")";
        return;
    }

    ParseAtaSmartAttributes(request.data, info);

    // 状态:重映射/待映射/离线无法校正任一 > 0 视为警告,否则良好。
    const long long reallocated = info.reallocatedSectorCount > 0 ? info.reallocatedSectorCount : 0;
    const long long pending = info.currentPendingSectorCount > 0 ? info.currentPendingSectorCount : 0;
    const long long uncorrectable = info.uncorrectableSectorCount > 0 ? info.uncorrectableSectorCount : 0;
    if (reallocated > 0 || pending > 0 || uncorrectable > 0) {
        info.status = (reallocated > 0 || uncorrectable > 0) ? DiskHealthStatus::Warning : DiskHealthStatus::Attention;
        info.healthPercent = 40;
        if (reallocated > 0) {
            info.healthPercent -= static_cast<int>(std::min<long long>(30, reallocated));
        }
        if (pending > 0) {
            info.healthPercent -= static_cast<int>(std::min<long long>(20, pending));
        }
        if (info.healthPercent < 0) {
            info.healthPercent = 0;
        }
    } else {
        info.status = DiskHealthStatus::Good;
        info.healthPercent = 100;
    }
    info.note = L"ATA SMART 属性";
}

/**
 * @brief 把总线类型映射到接口文案;是否走 NVMe 路径。
 */
bool BusTypeToInterface(STORAGE_BUS_TYPE busType, std::wstring& interfaceText) {
    switch (busType) {
        case BusTypeNvme:
            interfaceText = L"NVMe";
            return true;
        case BusTypeSata:
        case BusTypeAta:
        case BusTypeAtapi:
            interfaceText = L"SATA";
            return false;
        case BusTypeUsb:
            interfaceText = L"USB";
            return false;
        default:
            interfaceText = L"未知";
            return false;
    }
}

/**
 * @brief 枚举逻辑固定盘 → 物理盘号,并收集每个物理盘归属的盘符。
 */
std::map<int, std::wstring> EnumeratePhysicalDriveLetters() {
    std::map<int, std::set<wchar_t>> lettersByDisk;
    wchar_t buffer[512]{};
    const DWORD bufferLength = static_cast<DWORD>(sizeof(buffer) / sizeof(buffer[0]));
    const DWORD length = GetLogicalDriveStringsW(bufferLength, buffer);
    if (length == 0 || length > bufferLength) {
        return {};
    }
    const wchar_t* current = buffer;
    while (*current != L'\0') {
        const std::wstring root(current);
        current += root.size() + 1;
        if (GetDriveTypeW(root.c_str()) != DRIVE_FIXED) {
            continue;
        }
        const std::wstring volumePath = L"\\\\.\\" + root.substr(0, 2);  // \\.\C:
        HANDLE handle = CreateFileW(volumePath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                    nullptr, OPEN_EXISTING, 0, nullptr);
        if (handle == INVALID_HANDLE_VALUE) {
            continue;
        }
        VOLUME_DISK_EXTENTS extents[8]{};
        DWORD returned = 0;
        if (DeviceIoControl(handle, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, nullptr, 0, extents,
                            sizeof(extents), &returned, nullptr)) {
            const DWORD extentCount = returned >= sizeof(DWORD) ? extents[0].NumberOfDiskExtents : 0;
            for (DWORD i = 0; i < extentCount && i < 8; ++i) {
                const int diskNumber = static_cast<int>(extents[0].Extents[i].DiskNumber);
                if (!root.empty()) {
                    lettersByDisk[diskNumber].insert(root[0]);
                }
            }
        }
        CloseHandle(handle);
    }

    std::map<int, std::wstring> result;
    for (const auto& [diskNumber, letters] : lettersByDisk) {
        std::wstring joined;
        for (wchar_t letter : letters) {
            if (!joined.empty()) {
                joined += L" ";
            }
            joined += std::wstring(1, letter) + L":";
        }
        result[diskNumber] = joined;
    }
    return result;
}

std::wstring StatusToText(DiskHealthStatus status) {
    switch (status) {
        case DiskHealthStatus::Good: return L"良好";
        case DiskHealthStatus::Attention: return L"注意";
        case DiskHealthStatus::Warning: return L"警告";
        default: return L"不可读取";
    }
}

}  // namespace

std::vector<DiskHealthInfo> DiskHealth::QueryAll() const {
    const std::map<int, std::wstring> driveLetters = EnumeratePhysicalDriveLetters();

    std::vector<DiskHealthInfo> results;
    // 从 0 起逐个打开物理盘,直到连续失败若干次(无更多盘)。
    int consecutiveFailures = 0;
    for (int diskNumber = 0; diskNumber < 64 && consecutiveFailures < 4; ++diskNumber) {
        HANDLE handle = OpenPhysicalDrive(diskNumber);
        if (handle == INVALID_HANDLE_VALUE) {
            ++consecutiveFailures;
            continue;
        }
        consecutiveFailures = 0;

        DiskHealthInfo info;
        info.physicalDriveNumber = diskNumber;
        info.driveLetters = driveLetters.count(diskNumber) ? driveLetters.at(diskNumber) : std::wstring();

        std::wstring model;
        std::wstring serial;
        STORAGE_BUS_TYPE busType = BusTypeUnknown;
        QueryStorageDevice(handle, model, serial, busType);
        info.model = model;
        info.serial = serial;
        info.totalBytes = QueryDiskTotalBytes(handle);

        std::wstring interfaceText;
        const bool isNvme = BusTypeToInterface(busType, interfaceText);
        info.interfaceType = interfaceText;

        if (isNvme) {
            QueryNvmeHealth(handle, info);
        } else if (busType == BusTypeUsb) {
            info.note = L"USB 盘不支持 SMART 直读";
            info.status = DiskHealthStatus::Unreadable;
        } else {
            // SATA/ATA/ATAPI,以及 SAS/RAID/虚拟盘等非常规总线,统一尝试 ATA SMART 直读;
            // 不支持直通的盘会在此逐盘降级为不可读取,不影响其它盘。
            QueryAtaSmart(handle, info);
            if (info.status == DiskHealthStatus::Unreadable) {
                info.note += L" · 总线类型 " + std::to_wstring(static_cast<int>(busType));
            }
        }

        if (info.temperatureCelsius < -100) {
            info.temperatureCelsius = -1;
        }
        info.statusText = StatusToText(info.status);
        results.push_back(std::move(info));
        CloseHandle(handle);
    }

    return results;
}

}  // namespace disk_lens::core
