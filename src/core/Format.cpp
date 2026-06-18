#include "core/Format.h"

#include <cwchar>

namespace disk_lens::core {

std::wstring FormatBytes(std::uint64_t bytes) {
    constexpr const wchar_t* units[] = {L"B", L"KB", L"MB", L"GB", L"TB", L"PB"};
    double value = static_cast<double>(bytes);
    int unitIndex = 0;

    while (value >= 1024.0 && unitIndex < 5) {
        value /= 1024.0;
        ++unitIndex;
    }

    wchar_t buffer[64]{};
    if (unitIndex == 0) {
        swprintf_s(buffer, L"%llu %s", static_cast<unsigned long long>(bytes), units[unitIndex]);
    } else {
        swprintf_s(buffer, L"%.2f %s", value, units[unitIndex]);
    }

    return buffer;
}

}  // namespace disk_lens::core
