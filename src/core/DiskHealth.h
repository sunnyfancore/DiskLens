#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace disk_lens::core {

/**
 * @brief 单个物理盘的健康评估等级。
 */
enum class DiskHealthStatus {
    /**
     * @brief 良好:关键 SMART 指标正常。
     */
    Good,

    /**
     * @brief 注意:存在轻微异常(如可用备用接近阈值、寿命已用较高)。
     */
    Attention,

    /**
     * @brief 警告:出现重映射/待映射扇区等需要关注的指标。
     */
    Warning,

    /**
     * @brief 不可读取:无法读取该盘的 SMART 数据。
     */
    Unreadable,
};

/**
 * @brief 单条 ATA SMART 属性(SMART READ DATA 中 12 字节属性项的可读投影)。
 *
 * 与 DiskHealthInfo 上的专用标量字段(重映射/待映射扇区等)互补:专用字段驱动状态判定,
 * 本结构给用户呈现完整可读明细。原始值取属性项 base+5..10 的 6 字节小端;当前值/最差值
 * 取 base+3/base+4(厂商自定义归一化刻度,与 CrystalDiskInfo 等工具一致)。
 */
struct SmartAttribute {
    /**
     * @brief 属性 ID(如 5、194、241)。
     */
    int id = 0;

    /**
     * @brief 中文属性名;未识别 ID 形如 "属性 123"。
     */
    std::wstring name;

    /**
     * @brief 当前归一化值(0-253 厂商刻度;越大通常越好)。
     */
    int value = -1;

    /**
     * @brief 历史最差归一化值。
     */
    int worst = -1;

    /**
     * @brief 原始值(6 字节小端),如温度、扇区计数、小时数。
     */
    long long raw = 0;
};

/**
 * @brief 单个物理盘的健康信息快照。
 *
 * 任何字段读不到时保持默认的 -1 / 空,**绝不抛异常**;调用方据此显示"不可读取"而不会崩溃。
 */
struct DiskHealthInfo {
    /**
     * @brief PhysicalDriveN 的 N(物理盘序号)。
     */
    int physicalDriveNumber = -1;

    /**
     * @brief 归属盘符,如 "C: D:"(可能为空)。
     */
    std::wstring driveLetters;

    /**
     * @brief 型号。
     */
    std::wstring model;

    /**
     * @brief 序列号。
     */
    std::wstring serial;

    /**
     * @brief 接口类型:NVMe / SATA / USB / 未知。
     */
    std::wstring interfaceType;

    /**
     * @brief 总字节数。
     */
    std::uint64_t totalBytes = 0;

    /**
     * @brief 温度(摄氏度),-1 表示未读到。
     */
    int temperatureCelsius = -1;

    /**
     * @brief 通电小时数,-1 表示未读到。
     */
    long long powerOnHours = -1;

    /**
     * @brief 通电次数,-1 表示未读到。
     */
    long long powerCycleCount = -1;

    /**
     * @brief 健康评估百分比(0-100),-1 表示无法评估。
     */
    int healthPercent = -1;

    /**
     * @brief NVMe 可用备用百分比,-1 表示不适用/未读到。
     */
    int availableSparePercent = -1;

    /**
     * @brief NVMe 可用备用阈值(%),-1 表示不适用/未读到。与 availableSparePercent 配对,判断备用块是否告急。
     */
    int nvmeAvailableSpareThreshold = -1;

    /**
     * @brief NVMe 寿命已用(%),-1 表示不适用/未读到。
     *
     * 即 SMART/Health 日志 byte5 Percentage Used,厂商按额定耐久度归一化,是 TBW 进度的权威指标
     * (健康度 = 100 - 此值)。非 NVMe 盘保持 -1。
     */
    int nvmePercentageUsed = -1;

    /**
     * @brief NVMe 累计数据读取量(以 NVMe 数据单元计,-1 表示不适用/未读到)。1 数据单元 = 512000 字节(1000×512)。
     */
    long long nvmeDataUnitsRead = -1;

    /**
     * @brief NVMe 累计数据写入量(以 NVMe 数据单元计,-1 表示不适用/未读到)。1 数据单元 = 512000 字节;此即 TBW 来源。
     */
    long long nvmeDataUnitsWritten = -1;

    /**
     * @brief NVMe 非正常(掉电)关机次数,-1 表示不适用/未读到。
     */
    long long nvmeUnsafeShutdowns = -1;

    /**
     * @brief NVMe 介质错误数,-1 表示不适用/未读到。
     */
    long long nvmeMediaErrors = -1;

    /**
     * @brief NVMe 错误信息日志条目数,-1 表示不适用/未读到。
     */
    long long nvmeErrorLogEntries = -1;

    /**
     * @brief ATA 重映射扇区数(属性 5),-1 表示未读到。
     */
    long long reallocatedSectorCount = -1;

    /**
     * @brief ATA 当前待映射扇区数(属性 197),-1 表示未读到。
     */
    long long currentPendingSectorCount = -1;

    /**
     * @brief ATA 离线无法校正扇区数(属性 198),-1 表示未读到。
     */
    long long uncorrectableSectorCount = -1;

    /**
     * @brief ATA SMART READ DATA 解析出的全部属性(按磁盘返回顺序;NVMe/USB 盘为空)。
     *
     * 与上面的专用字段互补:专用字段(重映射/待映射扇区等)驱动状态判定,本表给用户完整可读明细。
     * 仅在 QueryAtaSmart 成功解析时填充,最多 30 项(READ DATA 属性目录容量)。
     */
    std::vector<SmartAttribute> smartAttributes;

    /**
     * @brief 健康等级。
     */
    DiskHealthStatus status = DiskHealthStatus::Unreadable;

    /**
     * @brief 等级文案:良好 / 注意 / 警告 / 不可读取。
     */
    std::wstring statusText;

    /**
     * @brief 数据来源或失败原因备注。
     */
    std::wstring note;
};

/**
 * @brief 物理盘健康读取器(SMART / NVMe 健康日志)。
 *
 * 仅使用 Win32 设备 IOCTL,与 DirectoryScanner 同属 core 层,不依赖 Qt。
 * 流程:
 *   1. 枚举逻辑固定盘 → IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS 映射到物理盘号,得到盘符归属。
 *   2. 每个物理盘:\\\\.\\PhysicalDriveN 打开后
 *      - IOCTL_STORAGE_QUERY_PROPERTY(StorageDeviceProperty)取型号/序列号/总线类型;
 *      - IOCTL_DISK_GET_LENGTH_INFO 取总容量;
 *      - NVMe 总线:ProtocolSpecific 查询取 SMART/Health 日志(温度/可用备用/寿命/通电);
 *      - SATA/ATA 总线:IOCTL_ATA_PASS_THROUGH 发 SMART READ DATA 解析关键属性。
 *   3. 任一步失败:该字段留空(-1),状态降级,绝不崩溃。
 *
 * 需要管理员权限才能打开物理盘做 SMART 直读;非管理员或 USB 盘会优雅降级为"不可读取"。
 * 在后台线程调用(打开物理盘 + IOCTL 为阻塞 I/O)。
 */
class DiskHealth {
public:
    /**
     * @brief 枚举所有物理盘并尽力读取健康信息(每盘独立优雅降级)。
     * @return 每个物理盘一条 DiskHealthInfo,按物理盘号升序。
     */
    std::vector<DiskHealthInfo> QueryAll() const;
};

}  // namespace disk_lens::core
