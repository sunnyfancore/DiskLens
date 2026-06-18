#include <windows.h>
#include <shlobj.h>
#include <compressapi.h>

#include <array>
#include <cstdint>
#include <string>

namespace {

/**
 * @brief 单文件包内的资源项。
 */
struct PayloadItem {
    /**
     * @brief 资源编号。
     */
    int resourceId;

    /**
     * @brief 解压后的相对路径。
     */
    const wchar_t* relativePath;
};

/**
 * @brief 单文件包内嵌的运行文件列表。
 */
constexpr std::array<PayloadItem, 12> kPayloadItems{{
    {101, L"DiskLens.exe"},
    {102, L"Qt6Core.dll"},
    {103, L"Qt6Gui.dll"},
    {104, L"Qt6Widgets.dll"},
    {105, L"msvcp140.dll"},
    {106, L"vcruntime140.dll"},
    {107, L"vcruntime140_1.dll"},
    {108, L"platforms\\qwindows.dll"},
    {109, L"msvcp140_1.dll"},
    {110, L"msvcp140_2.dll"},
    {111, L"concrt140.dll"},
    {112, L"styles\\qmodernwindowsstyle.dll"},
}};

/**
 * @brief 拼接 Windows 路径。
 * @param left 左侧路径。
 * @param right 右侧路径。
 * @return 拼接后的路径。
 */
std::wstring JoinPath(const std::wstring& left, const std::wstring& right) {
    if (left.empty()) {
        return right;
    }
    if (left.back() == L'\\' || left.back() == L'/') {
        return left + right;
    }
    return left + L"\\" + right;
}

/**
 * @brief 确保指定文件的父目录存在。
 * @param filePath 文件路径。
 */
void EnsureParentDirectory(const std::wstring& filePath) {
    const std::size_t slash = filePath.find_last_of(L"\\/");
    if (slash == std::wstring::npos) {
        return;
    }

    const std::wstring directory = filePath.substr(0, slash);
    if (!directory.empty()) {
        CreateDirectoryW(directory.c_str(), nullptr);
    }
}

/**
 * @brief 解压内嵌资源并写入文件。
 * @param resourceId 资源编号。
 * @param filePath 目标文件路径。
 * @return 成功时返回 true。
 */
bool ExtractResourceToFile(int resourceId, const std::wstring& filePath) {
    HRSRC resource = FindResourceW(nullptr, MAKEINTRESOURCEW(resourceId), RT_RCDATA);
    if (resource == nullptr) {
        return false;
    }

    HGLOBAL loadedResource = LoadResource(nullptr, resource);
    if (loadedResource == nullptr) {
        return false;
    }

    const DWORD compressedSize = SizeofResource(nullptr, resource);
    const auto* data = static_cast<const unsigned char*>(LockResource(loadedResource));
    if (data == nullptr || compressedSize <= sizeof(std::uint64_t)) {
        return false;
    }

    std::uint64_t outputSize = 0;
    for (std::size_t index = 0; index < sizeof(outputSize); ++index) {
        outputSize |= static_cast<std::uint64_t>(data[index]) << (index * 8);
    }
    if (outputSize == 0 || outputSize > 256ULL * 1024ULL * 1024ULL) {
        return false;
    }

    DECOMPRESSOR_HANDLE decompressor = nullptr;
    if (!CreateDecompressor(COMPRESS_ALGORITHM_XPRESS_HUFF, nullptr, &decompressor)) {
        return false;
    }

    std::string output(static_cast<std::size_t>(outputSize), '\0');
    SIZE_T decompressedSize = 0;
    const BOOL decompressed = Decompress(
        decompressor,
        data + sizeof(outputSize),
        compressedSize - static_cast<DWORD>(sizeof(outputSize)),
        &output[0],
        output.size(),
        &decompressedSize);
    CloseDecompressor(decompressor);
    if (!decompressed || decompressedSize != output.size()) {
        return false;
    }

    EnsureParentDirectory(filePath);
    HANDLE file = CreateFileW(filePath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }

    DWORD written = 0;
    const BOOL ok = WriteFile(file, &output[0], static_cast<DWORD>(output.size()), &written, nullptr);
    CloseHandle(file);
    return ok && written == output.size();
}

/**
 * @brief 确保目录存在。
 * @param directory 目录路径。
 * @return 成功或已经存在时返回 true。
 */
bool EnsureDirectory(const std::wstring& directory) {
    if (directory.empty()) {
        return false;
    }
    if (CreateDirectoryW(directory.c_str(), nullptr)) {
        return true;
    }
    return GetLastError() == ERROR_ALREADY_EXISTS;
}

/**
 * @brief 获取当前启动器版本戳。
 * @return 由文件大小、修改时间和文件哈希组成的版本戳。
 */
std::wstring LauncherVersionStamp();

/**
 * @brief 获取单文件包持久缓存目录。
 * @return 缓存目录路径。
 */
std::wstring PackageCacheDirectory() {
    wchar_t localAppData[MAX_PATH]{};
    if (SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, localAppData) != S_OK) {
        GetTempPathW(MAX_PATH, localAppData);
    }
    const std::wstring root = JoinPath(localAppData, L"DiskLens");
    const std::wstring cacheRoot = JoinPath(root, L"OneFileCache");
    const std::wstring cache = JoinPath(cacheRoot, LauncherVersionStamp());
    EnsureDirectory(root);
    EnsureDirectory(cacheRoot);
    EnsureDirectory(cache);
    EnsureDirectory(JoinPath(cache, L"platforms"));
    return cache;
}

/**
 * @brief 计算当前启动器文件的 FNV-1a 哈希。
 * @param modulePath 当前启动器路径。
 * @return 64 位哈希文本。
 */
std::wstring ComputeModuleHash(const wchar_t* modulePath) {
    HANDLE file = CreateFileW(modulePath, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return L"nohash";
    }

    std::uint64_t hash = 1469598103934665603ULL;
    std::array<unsigned char, 64 * 1024> buffer{};
    DWORD readBytes = 0;
    while (ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &readBytes, nullptr) && readBytes > 0) {
        for (DWORD index = 0; index < readBytes; ++index) {
            hash ^= buffer[index];
            hash *= 1099511628211ULL;
        }
    }
    CloseHandle(file);

    wchar_t text[32]{};
    wsprintfW(text, L"%016I64X", static_cast<unsigned long long>(hash));
    return text;
}

/**
 * @brief 获取当前启动器版本戳。
 * @return 由文件大小和修改时间组成的版本戳。
 */
std::wstring LauncherVersionStamp() {
    wchar_t modulePath[MAX_PATH]{};
    GetModuleFileNameW(nullptr, modulePath, MAX_PATH);

    WIN32_FILE_ATTRIBUTE_DATA attributes{};
    if (!GetFileAttributesExW(modulePath, GetFileExInfoStandard, &attributes)) {
        return L"unknown";
    }

    return std::to_wstring(attributes.nFileSizeHigh) + L"." +
        std::to_wstring(attributes.nFileSizeLow) + L"." +
        std::to_wstring(attributes.ftLastWriteTime.dwHighDateTime) + L"." +
        std::to_wstring(attributes.ftLastWriteTime.dwLowDateTime) + L"." +
        ComputeModuleHash(modulePath);
}

/**
 * @brief 读取文本文件内容。
 * @param path 文本文件路径。
 * @return 文件内容，读取失败时返回空字符串。
 */
std::wstring ReadTextFile(const std::wstring& path) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return std::wstring();
    }

    DWORD size = GetFileSize(file, nullptr);
    std::string buffer(size, '\0');
    DWORD readBytes = 0;
    ReadFile(file, &buffer[0], size, &readBytes, nullptr);
    CloseHandle(file);
    buffer.resize(readBytes);
    return std::wstring(buffer.begin(), buffer.end());
}

/**
 * @brief 写入文本文件内容。
 * @param path 文本文件路径。
 * @param value 要写入的文本。
 * @return 写入成功时返回 true。
 */
bool WriteTextFile(const std::wstring& path, const std::wstring& value) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }

    std::string buffer;
    buffer.reserve(value.size());
    for (const wchar_t character : value) {
        buffer.push_back(static_cast<char>(character));
    }
    DWORD written = 0;
    const BOOL ok = WriteFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &written, nullptr);
    CloseHandle(file);
    return ok && written == buffer.size();
}

/**
 * @brief 检查缓存包是否可直接复用。
 * @param directory 缓存目录。
 * @param stamp 当前版本戳。
 * @return 可复用时返回 true。
 */
bool IsPackageCacheValid(const std::wstring& directory, const std::wstring& stamp) {
    if (ReadTextFile(JoinPath(directory, L"package.version")) != stamp) {
        return false;
    }

    for (const PayloadItem& item : kPayloadItems) {
        const std::wstring path = JoinPath(directory, item.relativePath);
        if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
            return false;
        }
    }
    return true;
}

/**
 * @brief 确保缓存包已释放到磁盘。
 * @param directory 缓存目录。
 * @return 成功时返回 true。
 */
bool EnsurePackageCache(const std::wstring& directory) {
    const std::wstring stamp = LauncherVersionStamp();
    if (IsPackageCacheValid(directory, stamp)) {
        return true;
    }

    for (const PayloadItem& item : kPayloadItems) {
        if (!ExtractResourceToFile(item.resourceId, JoinPath(directory, item.relativePath))) {
            return false;
        }
    }

    return WriteTextFile(JoinPath(directory, L"package.version"), stamp);
}

/**
 * @brief 启动释放后的主程序。
 * @param directory 临时目录。
 * @return 主程序退出码，启动失败时返回 1。
 */
DWORD RunExtractedApplication(const std::wstring& directory) {
    const std::wstring applicationPath = JoinPath(directory, L"DiskLens.exe");
    std::wstring commandLine = L"\"" + applicationPath + L"\"";

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(applicationPath.c_str(), &commandLine[0], nullptr, nullptr, FALSE, 0, nullptr, directory.c_str(), &startup, &process)) {
        MessageBoxW(nullptr, L"无法启动磁盘洞察。", L"启动失败", MB_ICONERROR | MB_OK);
        return 1;
    }

    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return 0;
}

}  // namespace

/**
 * @brief 单文件启动器入口。
 * @return 进程退出码。
 */
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    const std::wstring directory = PackageCacheDirectory();
    if (!EnsurePackageCache(directory)) {
        MessageBoxW(nullptr, L"无法释放应用文件。", L"启动失败", MB_ICONERROR | MB_OK);
        return 1;
    }

    return static_cast<int>(RunExtractedApplication(directory));
}
