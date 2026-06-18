#include <windows.h>
#include <compressapi.h>

#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

/**
 * @brief 读取完整二进制文件。
 * @param path 输入文件路径。
 * @return 文件字节内容，失败时返回空数组。
 */
std::vector<char> ReadBinaryFile(const std::wstring& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return {};
    }

    input.seekg(0, std::ios::end);
    const std::streamoff size = input.tellg();
    input.seekg(0, std::ios::beg);
    if (size <= 0) {
        return {};
    }

    std::vector<char> data(static_cast<std::size_t>(size));
    input.read(data.data(), size);
    return input ? data : std::vector<char>{};
}

/**
 * @brief 写入带原始大小头的压缩二进制文件。
 * @param path 输出文件路径。
 * @param originalSize 原始文件大小。
 * @param compressedData 压缩后的文件内容。
 * @return 写入成功时返回 true。
 */
bool WriteCompressedFile(const std::wstring& path, std::uint64_t originalSize, const std::vector<char>& compressedData) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        return false;
    }

    for (int index = 0; index < 8; ++index) {
        const char byte = static_cast<char>((originalSize >> (index * 8)) & 0xFF);
        output.write(&byte, 1);
    }
    output.write(compressedData.data(), static_cast<std::streamsize>(compressedData.size()));
    return static_cast<bool>(output);
}

/**
 * @brief 压缩单个文件。
 * @param inputPath 输入文件路径。
 * @param outputPath 输出文件路径。
 * @return 成功时返回 true。
 */
bool CompressFilePayload(const std::wstring& inputPath, const std::wstring& outputPath) {
    const std::vector<char> inputData = ReadBinaryFile(inputPath);
    if (inputData.empty()) {
        return false;
    }

    COMPRESSOR_HANDLE compressor = nullptr;
    if (!CreateCompressor(COMPRESS_ALGORITHM_XPRESS_HUFF, nullptr, &compressor)) {
        return false;
    }

    SIZE_T compressedSize = 0;
    Compress(compressor, inputData.data(), inputData.size(), nullptr, 0, &compressedSize);
    if (compressedSize == 0) {
        CloseCompressor(compressor);
        return false;
    }

    std::vector<char> compressedData(compressedSize);
    const BOOL ok = Compress(
        compressor,
        inputData.data(),
        inputData.size(),
        compressedData.data(),
        compressedData.size(),
        &compressedSize);
    CloseCompressor(compressor);
    if (!ok) {
        return false;
    }

    compressedData.resize(compressedSize);
    return WriteCompressedFile(outputPath, static_cast<std::uint64_t>(inputData.size()), compressedData);
}

/**
 * @brief 压缩工具入口。
 * @param argc 参数数量。
 * @param argv 参数数组。
 * @return 进程退出码。
 */
int wmain(int argc, wchar_t** argv) {
    if (argc != 3) {
        std::wcerr << L"Usage: payload_compressor <input> <output>\n";
        return 2;
    }

    if (!CompressFilePayload(argv[1], argv[2])) {
        std::wcerr << L"Failed to compress payload: " << argv[1] << L"\n";
        return 1;
    }

    return 0;
}
