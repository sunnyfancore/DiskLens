#pragma once

#include "core/ScanModels.h"

#include <cstdint>
#include <string>
#include <vector>

namespace disk_lens::core {

/**
 * @brief 文件类型分类(8 类 + 其他),用于类型占比环形图。
 *
 * 每个扩展名归入 8 类之一(图片/视频/音频/文档/压缩/安装包/游戏/开发),不匹配的归入“其他”。
 * 枚举值稳定绑定类别身份(不随排序变化),供 UI 侧按 id 取固定颜色。本头文件刻意保持纯 STL
 * (无 Qt),与 ScanModels.h / DiskHealth.h / Format.h 同属无 Qt 依赖的 core 层。
 */
enum class FileCategory : int {
    Image = 0,      ///< 图片
    Video = 1,      ///< 视频
    Audio = 2,      ///< 音频
    Document = 3,   ///< 文档
    Archive = 4,    ///< 压缩
    Installer = 5,  ///< 安装包
    Game = 6,       ///< 游戏
    Code = 7,       ///< 开发
    Other = 8,      ///< 其他
};

/**
 * @brief 单个分类的聚合统计(由 ComputeExtensionCategories 产出)。
 */
struct CategorySlice {
    /**
     * @brief 分类显示名(图片/视频/.../其他)。
     */
    std::wstring name;

    /**
     * @brief 该分类下文件总大小(字节)。
     */
    std::uint64_t totalBytes = 0;

    /**
     * @brief 该分类下文件数量。
     */
    std::uint64_t fileCount = 0;

    /**
     * @brief 分类 id,UI 侧据此取固定颜色(见 FileCategory)。
     */
    FileCategory category = FileCategory::Other;
};

/**
 * @brief 把扫描结果的逐扩展名统计卷成 8 类 + 其他。
 * @param result 扫描结果(只读消费 result.extensions,不修改任何结构)。
 * @return 按总大小降序、已剔除 0 字节分类的切片列表(0..9 条)。
 *
 * 两个扫描引擎(DirectoryScanner 递归 + NtfsMftScanner MFT)产出的 extensions 形态一致:
 * 键为已小写、含前导点的扩展名(如 ".txt"),无扩展名文件键为 L"(无扩展名)"。本函数据此归类。
 */
std::vector<CategorySlice> ComputeExtensionCategories(const ScanResult& result);

}  // namespace disk_lens::core
