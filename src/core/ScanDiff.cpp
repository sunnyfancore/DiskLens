#include "core/ScanDiff.h"

#include <algorithm>

namespace disk_lens::core {

namespace {

/**
 * @brief 把显示路径归一为对比键:去 \\?\ / \\?\UNC\ 长路径前缀、/→\、ASCII 小写。
 *
 * 扫描节点路径本不含 \\?\ 前缀(LongPath.h 契约:绝不写入 ScanNode::path),此处对前缀的剥离是
 * 防御性的。ASCII 小写(非全 Unicode towlower)保证跨两次扫描确定性且无 locale 依赖——覆盖盘符、
 * 常见路径的 dominant 情形;非 ASCII 字符原样保留(键仍一致,因两次扫描对同一文件给出同一字符)。
 */
std::wstring NormalizeDiffPath(const std::wstring& input) {
    std::wstring path = input;
    // \\?\UNC\server\share → \\server\share
    if (path.rfind(L"\\\\?\\UNC\\", 0) == 0) {
        path = L"\\\\" + path.substr(8);
    } else if (path.rfind(L"\\\\?\\", 0) == 0) {
        // \\?\C:\... → C:\...
        path = path.substr(4);
    }
    for (wchar_t& c : path) {
        if (c == L'/') {
            c = L'\\';
        } else if (c >= L'A' && c <= L'Z') {
            c = static_cast<wchar_t>(c - L'A' + L'a');
        }
    }
    return path;
}

}  // namespace

FlatSnapshot BuildFlatSnapshot(const ScanResult& result) {
    FlatSnapshot snapshot;
    if (result.root == nullptr) {
        return snapshot;
    }
    snapshot.rootPath = result.root->path;

    // 迭代式 DFS(显式栈)避免极深目录树的递归栈风险。
    std::vector<const ScanNode*> stack;
    stack.push_back(result.root.get());
    while (!stack.empty()) {
        const ScanNode* node = stack.back();
        stack.pop_back();
        if (node == nullptr) {
            continue;
        }
        if (node->kind == NodeKind::File && node->ownBytes >= kMinDiffFileBytes) {
            FlatFile file;
            file.displayPath = node->path;
            file.bytes = static_cast<std::int64_t>(node->ownBytes);
            // 同一归一化键若重复(理论上树中无重复路径),后者覆盖;不计入重复计数。
            snapshot.files[NormalizeDiffPath(node->path)] = file;
            ++snapshot.fileCount;
        }
        // 预留容量提示,降低深树下 vector 频繁扩张。
        for (const auto& child : node->children) {
            if (child != nullptr) {
                stack.push_back(child.get());
            }
        }
    }
    return snapshot;
}

bool SameScanRoot(const FlatSnapshot& a, const FlatSnapshot& b) {
    if (a.rootPath.empty() || b.rootPath.empty()) {
        return false;
    }
    return NormalizeDiffPath(a.rootPath) == NormalizeDiffPath(b.rootPath);
}

ScanDiffResult ComputeScanDiff(const FlatSnapshot& prev, const FlatSnapshot& curr, std::size_t maxEntries) {
    ScanDiffResult result;

    // 本次快照中每个键:在上次快照里?
    for (const auto& [key, currFile] : curr.files) {
        const auto it = prev.files.find(key);
        if (it == prev.files.end()) {
            result.entries.push_back({ currFile.displayPath, 0, currFile.bytes, currFile.bytes,
                                       ScanDiffCategory::New });
            ++result.newCount;
            result.netDelta += currFile.bytes;
            continue;
        }
        const std::int64_t prevBytes = it->second.bytes;
        if (currFile.bytes > prevBytes) {
            const std::int64_t delta = currFile.bytes - prevBytes;
            result.entries.push_back({ currFile.displayPath, prevBytes, currFile.bytes, delta,
                                       ScanDiffCategory::Growth });
            ++result.growthCount;
            result.netDelta += delta;
        } else if (currFile.bytes < prevBytes) {
            const std::int64_t delta = currFile.bytes - prevBytes;  // 负
            result.entries.push_back({ currFile.displayPath, prevBytes, currFile.bytes, delta,
                                       ScanDiffCategory::Shrink });
            ++result.shrinkCount;
            result.netDelta += delta;
        }
        // 相等:不变,跳过(不产生条目)。
    }

    // 上次有、本次无 → Gone。
    for (const auto& [key, prevFile] : prev.files) {
        if (curr.files.find(key) == curr.files.end()) {
            result.entries.push_back({ prevFile.displayPath, prevFile.bytes, 0, -prevFile.bytes,
                                       ScanDiffCategory::Gone });
            ++result.goneCount;
            result.netDelta -= prevFile.bytes;
        }
    }

    // 按 |delta| 降序(变化最大的在前)。
    std::sort(result.entries.begin(), result.entries.end(),
              [](const ScanDiffEntry& a, const ScanDiffEntry& b) {
                  const std::int64_t ma = a.delta < 0 ? -a.delta : a.delta;
                  const std::int64_t mb = b.delta < 0 ? -b.delta : b.delta;
                  if (ma != mb) {
                      return ma > mb;
                  }
                  // |delta| 相同时按路径稳定排序,避免两次对比顺序跳动。
                  return a.displayPath < b.displayPath;
              });

    if (maxEntries > 0 && result.entries.size() > maxEntries) {
        result.entries.resize(maxEntries);
        result.truncated = true;
    }

    return result;
}

}  // namespace disk_lens::core
