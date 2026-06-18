#pragma once

#include <cstdint>
#include <string>

namespace disk_lens::core {

/**
 * @brief 将字节数格式化为便于阅读的容量字符串。
 * @param bytes 字节数。
 * @return 格式化后的容量字符串。
 */
std::wstring FormatBytes(std::uint64_t bytes);

}  // namespace disk_lens::core
