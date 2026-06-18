#pragma once

#include <string>

namespace disk_lens::core {

/**
 * @brief 把显示路径转换为可越过 MAX_PATH(260)限制的 \\?\ 前缀路径。
 *
 * 仅用于把路径传给 Win32 文件 API(FindFirstFileExW / CreateFileW / GetCompressedFileSizeW)或
 * std::filesystem。**绝不**写入 ScanNode::path —— 那是显示路径,会进入磁盘缓存、界面展示、
 * 删除流程,带 \\?\ 前缀会污染显示并让 SHFileOperationW 删除失败。
 *
 * 映射规则(幂等,重复调用安全):
 * - 已是 \\?\ 或 \\?\UNC\ 前缀 → 原样返回。
 * - 盘符根(如 C:\...) → \\?\C:\...
 * - UNC(\\server\share\...) → \\?\UNC\server\share\...
 * - 相对 / 空等非全限定路径 → 原样返回(防御;实际路径均由盘符选择器保证全限定)。
 * @param displayPath 显示路径,如扫描节点保存的路径。
 * @return 加好 \\?\ 前缀、可越过 MAX_PATH 的路径。
 */
std::wstring MakeLongPath(const std::wstring& displayPath);

}  // namespace disk_lens::core
