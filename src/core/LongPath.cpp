#include "core/LongPath.h"

namespace disk_lens::core {

std::wstring MakeLongPath(const std::wstring& displayPath) {
    // 已带前缀,幂等返回,避免重复叠加。
    if (displayPath.rfind(L"\\\\?\\", 0) == 0) {
        return displayPath;
    }

    // 盘符根:形如 C:\ 或 C:/。盘符为 ASCII 字母,不依赖本地化,直接判断。
    const bool hasDriveLetter = displayPath.size() >= 3
        && ((displayPath[0] >= L'a' && displayPath[0] <= L'z') || (displayPath[0] >= L'A' && displayPath[0] <= L'Z'))
        && displayPath[1] == L':'
        && (displayPath[2] == L'\\' || displayPath[2] == L'/');
    if (hasDriveLetter) {
        return L"\\\\?\\" + displayPath;
    }

    // UNC:\\server\share\... 去掉前导两个反斜杠,接在 \\?\UNC\ 之后。
    if (displayPath.size() >= 2 && displayPath[0] == L'\\' && displayPath[1] == L'\\') {
        return L"\\\\?\\UNC\\" + displayPath.substr(2);
    }

    // 相对 / 空等非全限定路径,不加前缀以免破坏其语义,原样返回。
    return displayPath;
}

}  // namespace disk_lens::core
