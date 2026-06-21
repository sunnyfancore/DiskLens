#include "core/DiskHealth.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
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
 * @brief ATA SMART 属性 ID → 中文名。覆盖常见 60 余项;未命中返回 "属性 N"。
 *
 * 名称对照 smartmontools / CrystalDiskInfo 的通行译法,SSD 专属属性(170-242)一并收录。
 * 仅用于详情对话框的可读展示,不参与状态判定。
 */
const std::unordered_map<int, std::wstring>& SmartAttributeNames() {
    static const std::unordered_map<int, std::wstring> names = []() {
        std::unordered_map<int, std::wstring> m;
        const auto add = [&m](int id, const wchar_t* name) { m[id] = name; };
        add(1, L"读取错误率");
        add(2, L"吞吐性能");
        add(3, L"主轴起转时间");
        add(4, L"主轴起停次数");
        add(5, L"重映射扇区数");
        add(6, L"读取通道裕量");
        add(7, L"寻道错误率");
        add(8, L"寻道时间性能");
        add(9, L"通电小时数");
        add(10, L"主轴重试次数");
        add(11, L"校准重试次数");
        add(12, L"通电次数");
        add(13, L"软读取错误率");
        add(22, L"氦气等级");
        add(170, L"可用预留空间");
        add(171, L"编程失败计数");
        add(172, L"擦除失败计数");
        add(173, L"平均擦除次数");
        add(174, L"意外掉电次数");
        add(175, L"掉电保护失败");
        add(176, L"擦除失败计数(块)");
        add(177, L"磨损范围差值");
        add(178, L"已用预留块(校验)");
        add(179, L"已用预留块(总)");
        add(180, L"未用预留块");
        add(181, L"编程失败计数(总)");
        add(182, L"擦除失败计数(总)");
        add(183, L"SATA 降速次数");
        add(184, L"端到端错误");
        add(187, L"已报告不可校正错误");
        add(188, L"命令超时");
        add(189, L"高空写入");
        add(190, L"气流温度");
        add(191, L"冲击传感器");
        add(192, L"断电缩回计数");
        add(193, L"磁头加载周期");
        add(194, L"温度");
        add(195, L"硬件 ECC 已恢复");
        add(196, L"重映射事件计数");
        add(197, L"当前待映射扇区");
        add(198, L"离线无法校正");
        add(199, L"Ultra DMA CRC 错误");
        add(200, L"写入错误率");
        add(201, L"软读取错误率(2)");
        add(202, L"数据地址标记错误");
        add(203, L"软 ECC 错误");
        add(204, L"软 ECC 校正");
        add(205, L"热非对称率");
        add(206, L"飞行高度");
        add(207, L"主轴高速过流");
        add(208, L"主轴蜂鸣");
        add(209, L"离线寻道性能");
        add(211, L"写入时振动");
        add(212, L"写入时冲击");
        add(220, L"磁盘位移");
        add(221, L"冲击错误率");
        add(222, L"加载已用小时");
        add(223, L"加载/卸载重试");
        add(224, L"加载摩擦");
        add(225, L"加载/卸载周期");
        add(226, L"加载时间");
        add(227, L"转矩放大计数");
        add(228, L"断电缩回周期");
        add(230, L"磁头幅度");
        add(231, L"SSD 剩余寿命");
        add(232, L"耐久剩余");
        add(233, L"介质磨损指示");
        add(234, L"平均擦除次数(2)");
        add(241, L"写入 LBA 总数");
        add(242, L"读取 LBA 总数");
        add(249, L"NAND 写入量");
        add(250, L"读取错误重试率");
        return m;
    }();
    return names;
}

std::wstring SmartAttributeName(int id) {
    const auto& names = SmartAttributeNames();
    const auto found = names.find(id);
    if (found != names.end()) {
        return found->second;
    }
    return L"属性 " + std::to_wstring(id);
}

/**
 * @brief 打开物理盘句柄。
 * @param writable 为真时请求读写权限(IOCTL_ATA_PASS_THROUGH 的 SATA SMART 直读需要);默认只读——
 *        NVMe(STORAGE_QUERY_PROPERTY)、磁盘容量/型号等只读查询本就无需写权限。默认只读可避免对每块
 *        物理盘都创建写句柄:杀软启发式把"未签名 + requireAdministrator + 裸盘 CreateFileW 写权限 +
 *        海量枚举"叠加视为勒索/擦盘特征,读写句柄现仅在 SATA 健康直读被拒时按需重开(见 QueryAll)。
 */
HANDLE OpenPhysicalDrive(int diskNumber, bool writable = false) {
    const std::wstring path = L"\\\\.\\PhysicalDrive" + std::to_wstring(diskNumber);
    const DWORD access = writable ? (GENERIC_READ | GENERIC_WRITE) : GENERIC_READ;
    HANDLE handle = CreateFileW(
        path.c_str(),
        access,
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
 * @brief 取型号/序列号/固件版本/总线类型;失败时各字段留空。
 */
void QueryStorageDevice(HANDLE handle, std::wstring& model, std::wstring& serial, std::wstring& firmware, STORAGE_BUS_TYPE& busType) {
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
    firmware = AnsiOffsetToString(buffer.data(), buffer.size(), desc->ProductRevisionOffset);

    model = product;
    if (!vendor.empty()) {
        model = vendor + L" " + product;
    }
    // 裁掉序列号尾随空格(已在 AnsiOffsetToString 裁过,这里双保险)。
}

/**
 * @brief 读取 NVMe SMART/Health 日志并填充温度/可用备用/寿命/通电。
 *
 * 输入缓冲须为微软标准布局:STORAGE_PROTOCOL_SPECIFIC_DATA 紧接 STORAGE_PROPERTY_QUERY 的
 * AdditionalParameters 字段,即从偏移 FIELD_OFFSET(STORAGE_PROPERTY_QUERY, AdditionalParameters)=8 起。
 * 绝不能用 sizeof(STORAGE_PROPERTY_QUERY)=12 偏移(STORAGE_PROPERTY_QUERY 仅 4 字节对齐,sizeof 恰为 12,
 * 组合结构体给出的也是 12)——否则 ProtocolData 整体错位 4 字节,驱动按偏移 8 读到错位字段,
 * 判参数无效返回错误码 87。缓冲尾部再留 512 字节承载 SMART/Health 日志。
 */
void QueryNvmeHealth(HANDLE handle, DiskHealthInfo& info) {
    constexpr std::size_t logBytes = 512;
    const std::size_t protocolOffset = FIELD_OFFSET(STORAGE_PROPERTY_QUERY, AdditionalParameters);
    const std::size_t bufferSize = protocolOffset + sizeof(STORAGE_PROTOCOL_SPECIFIC_DATA) + logBytes;
    std::vector<unsigned char> buffer(bufferSize, 0);

    auto* query = reinterpret_cast<STORAGE_PROPERTY_QUERY*>(buffer.data());
    query->PropertyId = StorageDeviceProtocolSpecificProperty;   // 设备级:SMART/Health 日志属设备数据。
    query->QueryType = PropertyStandardQuery;

    auto* protocolData = reinterpret_cast<STORAGE_PROTOCOL_SPECIFIC_DATA*>(buffer.data() + protocolOffset);
    protocolData->ProtocolType = ProtocolTypeNvme;
    protocolData->DataType = NVMeDataTypeLogPage;
    protocolData->ProtocolDataRequestValue = 0x02;        // SMART/Health 日志页 LID。
    protocolData->ProtocolDataRequestSubValue = 0;        // 日志页内 DWORDDIndex,从头读。
    protocolData->ProtocolDataOffset = sizeof(STORAGE_PROTOCOL_SPECIFIC_DATA);  // 日志数据相对本结构起始的偏移。
    protocolData->ProtocolDataLength = static_cast<ULONG>(logBytes);

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
    // NVMe SMART/Health Log Page(Log ID 02h)布局——逐字段对照 Windows SDK nvme.h 的
    // NVME_HEALTH_INFO_LOG 结构体累加(所有计数器为 16 字节小端 uint128,读低 8 字节足够):
    //   byte 0       Critical Warning
    //   byte 1-2     Composite Temperature(K 小端)
    //   byte 3       Available Spare          byte 4   Available Spare Threshold
    //   byte 5       Percentage Used          byte 6-31 Reserved0[26]  ← 26 字节保留间隙
    //   byte 32-47   Data Units Read          byte 48-63  Data Units Written
    //   byte 64-79   Host Read Commands       byte 80-95  Host Write Commands
    //   byte 96-111  Controller Busy Time(分钟)
    //   byte 112-127 Power Cycles             ← 通电次数
    //   byte 128-143 Power On Hours           ← 通电时长(小时)
    //   byte 144-159 Unsafe Shutdowns         byte 160-175 Media Errors
    //   byte 176-191 Number of Error Info Log Entries
    // 偏移曾两次写错:先 72/80(落 Host Read/Write Commands)、后 88/104(落 Host Write
    // Commands / Controller Busy Time)——idle 探测下都读到 ~0,与"温度/寿命读对、通电读 0"
    // 的现象吻合。根因是 byte6-31 的 26 字节保留间隙被当成 2 字节,整段计数器错位 24 字节。
    // 正确为 Power Cycles=112、Power On Hours=128(已逐字段核对 nvme.h)。
    info.powerCycleCount = readU128(112);
    info.powerOnHours = readU128(128);

    // NVMe 耐久度明细:全部来自已校验的 log[] 缓冲(同上 readU128)。此处只读数暴露给详情对话框,
    // 不参与下方状态判定(availableSpareThreshold/percentageUsed 在状态分支里本就各自已读)。
    //   byte 32-47  Data Units Read        byte 48-63   Data Units Written(= TBW)
    //   byte 144-159 Unsafe Shutdowns      byte 160-175 Media Errors
    //   byte 176-191 Number of Error Info Log Entries
    info.nvmeAvailableSpareThreshold = static_cast<int>(availableSpareThreshold);
    info.nvmePercentageUsed = static_cast<int>(percentageUsed);
    info.nvmeDataUnitsRead = readU128(32);
    info.nvmeDataUnitsWritten = readU128(48);
    info.nvmeUnsafeShutdowns = readU128(144);
    info.nvmeMediaErrors = readU128(160);
    info.nvmeErrorLogEntries = readU128(176);

    // NVMe 多传感器温度:SMART/Health 日志 bytes 200-215 共 8 个 uint16(Kelvin 小端),逐个减 273 转摄氏。
    // 值为 0(Kelvin)的传感器按 NVMe 规范视为未实现而跳过;复合温度(byte 1-2)已在上方 temperatureCelsius。
    // 注意偏移是 200-215,绝非 bytes 78-87(后者落在 Host Read/Write Commands 计数器区,会读到垃圾)。
    for (int sensor = 0; sensor < 8; ++sensor) {
        const std::size_t off = 200 + static_cast<std::size_t>(sensor) * 2;
        const std::uint16_t kelvin = static_cast<std::uint16_t>(log[off]) | (static_cast<std::uint16_t>(log[off + 1]) << 8);
        if (kelvin == 0) {
            continue;
        }
        info.nvmeTemperatureSensors.push_back(static_cast<int>(kelvin) - 273);
    }

    // 状态:NVMe SMART/Health Log(Log ID 02h)byte0 Critical Warning 共 5 个有效位,
    // 对照 NVMe 1.4/2.0 规范与 Microsoft NVME_HEALTH_INFO_LOG 文档:
    //   bit0(0x01) 可用备用低于阈值   bit1(0x02) 温度超阈值(过温)
    //   bit2(0x04) 子系统可靠性降级   bit3(0x08) 介质进入只读模式(已锁死,拒绝写入)
    //   bit4(0x10) 易失性备份设备失效(掉电保护电容损坏)
    // 按严重程度优先级取最高级状态:
    //   只读/可靠性降级 > 备份失效/过温 > 可用备用低 > 老化 > 良好。
    // 旧实现只判 bit0/bit2,导致进入只读保护模式或过温/掉电保护失效的盘
    // 仍可能显示 healthPercent≈100 + Good,严重误导用户。
    if ((criticalWarning & 0x08) != 0 || (criticalWarning & 0x04) != 0) {
        // bit3 只读 = 盘已无法写入,bit2 可靠性降级 —— 均按最严重(Warning)处理。
        info.status = DiskHealthStatus::Warning;
    } else if ((criticalWarning & 0x10) != 0 || (criticalWarning & 0x02) != 0) {
        // bit4 掉电保护失效、bit1 过温 —— 可靠性事件,不应判为 Good。
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
        // 当前归一化值(base+3)、最差值(base+4):ATA SMART 属性 12 字节项的标准布局,
        // 与 smartmontools / CrystalDiskInfo 一致。
        const int value = static_cast<int>(data[base + 3]);
        const int worst = static_cast<int>(data[base + 4]);

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

        // 全量明细:无论是否命中上面的专用字段,都把这条属性登记进 smartAttributes 供详情对话框展示。
        info.smartAttributes.push_back({static_cast<int>(id), SmartAttributeName(static_cast<int>(id)),
                                        value, worst, rawValue});
    }
}

/**
 * @brief 通过 IOCTL_ATA_PASS_THROUGH 发 SMART READ DATA(0xB0/D0),解析关键属性。
 * @return DeviceIoControl 的错误码(成功为 0)。注意:ERR 位置位或返回空数据属"盘级"问题
 *         (IOCTL 本身成功),仍返回 0;唯有 IOCTL 调用失败(如只读句柄被拒 ERROR_ACCESS_DENIED)
 *         才返回非零,供调用方据此换读写句柄重试。
 */
DWORD QueryAtaSmart(HANDLE handle, DiskHealthInfo& info) {
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
        return err;  // 调用方据此在只读句柄被拒(ERROR_ACCESS_DENIED)时换读写句柄重试。
    }
    // DeviceIoControl 成功后,寄存器被驱动回填:CurrentTaskFile[6] 为状态寄存器,位 0(0x01)= ERR。
    const unsigned char statusReg = request.apt.CurrentTaskFile[6];
    if ((statusReg & 0x01) != 0) {
        // ERR 置位:错误寄存器(CurrentTaskFile[0],常见 0x04=ABRT 命令被拒/SMART 未启用)说明原因。
        const unsigned char errorReg = request.apt.CurrentTaskFile[0];
        info.note = L"ATA SMART 命令返回错误(状态 0x" + ByteToHex(statusReg) +
                    L" · 错误 0x" + ByteToHex(errorReg) + L")";
        return 0;  // IOCTL 本身成功,是盘级错误,换读写句柄重试无意义。
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
        return 0;  // IOCTL 成功但数据空,换读写句柄重试无意义。
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
    return 0;
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
        std::wstring firmware;
        STORAGE_BUS_TYPE busType = BusTypeUnknown;
        QueryStorageDevice(handle, model, serial, firmware, busType);
        info.model = model;
        info.serial = serial;
        info.firmwareRevision = firmware;
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
            const DWORD ataErr = QueryAtaSmart(handle, info);
            if (ataErr == ERROR_ACCESS_DENIED) {
                // MS 文档要求 IOCTL_ATA_PASS_THROUGH 的句柄带 GENERIC_READ|GENERIC_WRITE。上方默认只开
                // 只读句柄(NVMe/USB 等只读查询无需写,也避免无谓的"写物理盘"特征);仅当该 SATA 盘只读直读
                // 被拒时,才为其单独开一个读写句柄重试一次,用完即关——SATA 行为与改动前完全一致,而 NVMe
                // 等其它盘不再创建任何写句柄,从而降低杀软对"写裸盘"的启发式权重。
                info.note.clear();  // 先清掉只读被拒的诊断:下面无论重试成功(设"ATA SMART 属性")、盘级失败(设新诊断)、还是无法提权(下行),note 都会重写。
                HANDLE rwHandle = OpenPhysicalDrive(diskNumber, true);
                if (rwHandle != INVALID_HANDLE_VALUE) {
                    QueryAtaSmart(rwHandle, info);
                    CloseHandle(rwHandle);
                } else {
                    info.note = L"ATA SMART 直读需要读写权限,但提权后仍无法打开";
                }
            }
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
