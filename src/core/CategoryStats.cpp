#include "core/CategoryStats.h"

#include <algorithm>
#include <array>
#include <initializer_list>
#include <unordered_map>

namespace disk_lens::core {

namespace {

// 9 个分类的显示名,下标对齐 FileCategory 枚举值。
const std::array<std::wstring, 9> kCategoryNames = {
    L"图片",   // Image
    L"视频",   // Video
    L"音频",   // Audio
    L"文档",   // Document
    L"压缩",   // Archive
    L"安装包", // Installer
    L"游戏",   // Game
    L"开发",   // Code
    L"其他",   // Other
};

// 扩展名(已小写、含前导点)→ 分类。首调用时构建一次,后续复用(线程安全的 magic static)。
const std::unordered_map<std::wstring, FileCategory>& ExtensionMap() {
    static const std::unordered_map<std::wstring, FileCategory> map = []() {
        std::unordered_map<std::wstring, FileCategory> m;
        const auto add = [&m](FileCategory cat, std::initializer_list<const wchar_t*> exts) {
            for (const wchar_t* ext : exts) {
                m[std::wstring(ext)] = cat;
            }
        };
        add(FileCategory::Image, {L".jpg", L".jpeg", L".png", L".gif", L".bmp", L".webp", L".tif", L".tiff",
                                  L".heic", L".heif", L".ico", L".svg", L".raw", L".cr2", L".nef", L".arw",
                                  L".psd", L".tga"});
        add(FileCategory::Video, {L".mp4", L".mkv", L".avi", L".mov", L".wmv", L".flv", L".webm", L".m4v",
                                  L".mpg", L".mpeg", L".ts", L".rmvb", L".rm", L".3gp", L".vob", L".m2ts"});
        add(FileCategory::Audio, {L".mp3", L".wav", L".flac", L".aac", L".ogg", L".wma", L".m4a", L".ape",
                                  L".alac", L".aiff", L".opus", L".mid", L".midi"});
        add(FileCategory::Document, {L".pdf", L".doc", L".docx", L".xls", L".xlsx", L".ppt", L".pptx", L".txt",
                                     L".md", L".rtf", L".odt", L".ods", L".odp", L".csv", L".wps", L".et",
                                     L".dps", L".pages", L".numbers", L".key", L".epub", L".mobi", L".djvu"});
        add(FileCategory::Archive, {L".zip", L".rar", L".7z", L".tar", L".gz", L".bz2", L".xz", L".tgz",
                                    L".tbz2", L".iso", L".cab", L".lz", L".zst"});
        add(FileCategory::Installer, {L".exe", L".msi", L".appx", L".msix", L".deb", L".rpm", L".apk",
                                      L".dmg", L".pkg", L".xapk"});
        add(FileCategory::Game, {L".pak", L".uasset", L".bsa", L".vpk", L".cas", L".forge", L".wad",
                                 L".map", L".sav", L".vtf", L".bsp"});
        add(FileCategory::Code, {L".c", L".cpp", L".cc", L".cxx", L".h", L".hpp", L".hxx", L".cs", L".java",
                                 L".kt", L".swift", L".py", L".rb", L".go", L".rs", L".js", L".ts", L".jsx",
                                 L".tsx", L".php", L".pl", L".sh", L".bat", L".ps1", L".json", L".xml",
                                 L".yaml", L".yml", L".toml", L".ini", L".cfg", L".conf", L".csproj",
                                 L".vcxproj", L".sln", L".cmake", L".gradle", L".lua", L".r", L".scala",
                                 L".clj", L".ex", L".exs", L".dart", L".class", L".jar", L".war", L".dll",
                                 L".so", L".dylib", L".lib", L".a", L".o", L".obj"});
        return m;
    }();
    return map;
}

}  // namespace

std::vector<CategorySlice> ComputeExtensionCategories(const ScanResult& result) {
    std::array<std::uint64_t, 9> bytes{};
    std::array<std::uint64_t, 9> counts{};

    const auto& extMap = ExtensionMap();
    for (const auto& entry : result.extensions) {
        FileCategory cat = FileCategory::Other;  // 默认其他(含无扩展名哨兵 L"(无扩展名)" 与未命中键)。
        const auto found = extMap.find(entry.first);
        if (found != extMap.end()) {
            cat = found->second;
        }
        const std::size_t idx = static_cast<std::size_t>(cat);
        bytes[idx] += entry.second.totalBytes;
        counts[idx] += entry.second.fileCount;
    }

    std::vector<CategorySlice> slices;
    slices.reserve(9);
    for (std::size_t idx = 0; idx < 9; ++idx) {
        if (bytes[idx] == 0) {
            continue;  // 剔除 0 字节分类,避免无意义扇区与除零。
        }
        slices.push_back(CategorySlice{
            kCategoryNames[idx],
            bytes[idx],
            counts[idx],
            static_cast<FileCategory>(idx),
        });
    }

    std::sort(slices.begin(), slices.end(), [](const CategorySlice& left, const CategorySlice& right) {
        return left.totalBytes > right.totalBytes;
    });
    return slices;
}

}  // namespace disk_lens::core
