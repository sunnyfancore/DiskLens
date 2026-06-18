#include "core/LongPath.h"

namespace disk_lens::core {

std::wstring MakeLongPath(const std::wstring& displayPath) {
    // \\?\ 前缀要求路径已规范化(反斜杠分隔)——带前缀后 Win32 不再做正斜杠→反斜杠的归一化,
    // 正斜杠路径会直接判不存在。这里先统一为反斜杠,保证所有调用方(含用 '/' 拼接的清理路径)都能解析。
    std::wstring normalized;
    normalized.reserve(displayPath.size());
    for (wchar_t value : displayPath) {
        normalized.push_back(value == L'/' ? L'\\' : value);
    }

    // 已带前缀,幂等返回,避免重复叠加。
    if (normalized.rfind(L"\\\\?\\", 0) == 0) {
        return normalized;
    }

    // 盘符根:形如 C:\ 或 C:/。盘符为 ASCII 字母,不依赖本地化,直接判断。
    const bool hasDriveLetter = normalized.size() >= 3
        && ((normalized[0] >= L'a' && normalized[0] <= L'z') || (normalized[0] >= L'A' && normalized[0] <= L'Z'))
        && normalized[1] == L':'
        && (normalized[2] == L'\\' || normalized[2] == L'/');
    if (hasDriveLetter) {
        return L"\\\\?\\" + normalized;
    }

    // UNC:\\server\share\... 去掉前导两个反斜杠,接在 \\?\UNC\ 之后。
    if (normalized.size() >= 2 && normalized[0] == L'\\' && normalized[1] == L'\\') {
        return L"\\\\?\\UNC\\" + normalized.substr(2);
    }

    // 相对 / 空等非全限定路径,不加前缀以免破坏其语义,原样返回。
    return normalized;
}

}  // namespace disk_lens::core
