#include "core/FileHasher.h"

#include "core/LongPath.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <map>
#include <optional>
#include <windows.h>

namespace disk_lens::core {
namespace {

/**
 * @brief 自带 SHA-256 实现(FIPS 180-4),不依赖 Qt/外部库。
 *
 * core 层刻意保持 Qt 无关,因此这里内置标准算法;仅用于内容去重比对, correctness 与 OpenSSL 等价。
 */
class Sha256 {
public:
    Sha256() {
        Reset();
    }

    void Reset() {
        state_[0] = 0x6a09e667u;
        state_[1] = 0xbb67ae85u;
        state_[2] = 0x3c6ef372u;
        state_[3] = 0xa54ff53au;
        state_[4] = 0x510e527fu;
        state_[5] = 0x9b05688cu;
        state_[6] = 0x1f83d9abu;
        state_[7] = 0x5be0cd19u;
        bitLength_ = 0;
        bufferLength_ = 0;
    }

    void Update(const std::uint8_t* data, std::size_t length) {
        bitLength_ += static_cast<std::uint64_t>(length) * 8u;
        while (length > 0) {
            const std::size_t copy = (std::min)(static_cast<std::size_t>(64) - bufferLength_, length);
            std::memcpy(buffer_.data() + bufferLength_, data, copy);
            bufferLength_ += copy;
            data += copy;
            length -= copy;
            if (bufferLength_ == 64) {
                ProcessBlock(buffer_.data());
                bufferLength_ = 0;
            }
        }
    }

    std::array<std::uint8_t, 32> Finalize() {
        // 填充:0x80,再补 0 直到长度 ≡ 56 (mod 64),最后 8 字节为大端位长度。
        buffer_[bufferLength_++] = 0x80u;
        if (bufferLength_ > 56) {
            while (bufferLength_ < 64) {
                buffer_[bufferLength_++] = 0x00u;
            }
            ProcessBlock(buffer_.data());
            bufferLength_ = 0;
        }
        while (bufferLength_ < 56) {
            buffer_[bufferLength_++] = 0x00u;
        }
        for (int shift = 56; shift >= 0; shift -= 8) {
            buffer_[bufferLength_++] = static_cast<std::uint8_t>((bitLength_ >> shift) & 0xffu);
        }
        ProcessBlock(buffer_.data());

        std::array<std::uint8_t, 32> digest{};
        for (int i = 0; i < 8; ++i) {
            digest[i * 4 + 0] = static_cast<std::uint8_t>((state_[i] >> 24) & 0xffu);
            digest[i * 4 + 1] = static_cast<std::uint8_t>((state_[i] >> 16) & 0xffu);
            digest[i * 4 + 2] = static_cast<std::uint8_t>((state_[i] >> 8) & 0xffu);
            digest[i * 4 + 3] = static_cast<std::uint8_t>(state_[i] & 0xffu);
        }
        return digest;
    }

private:
    static std::uint32_t RotateRight(std::uint32_t value, std::uint32_t count) {
        return (value >> count) | (value << (32u - count));
    }

    void ProcessBlock(const std::uint8_t* block) {
        static const std::uint32_t kRoundConstants[64] = {
            0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a5u, 0xab1c5ed5u,
            0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
            0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
            0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
            0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
            0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
            0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
            0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
        };

        std::uint32_t schedule[64];
        for (int i = 0; i < 16; ++i) {
            schedule[i] = (static_cast<std::uint32_t>(block[i * 4 + 0]) << 24)
                | (static_cast<std::uint32_t>(block[i * 4 + 1]) << 16)
                | (static_cast<std::uint32_t>(block[i * 4 + 2]) << 8)
                | static_cast<std::uint32_t>(block[i * 4 + 3]);
        }
        for (int i = 16; i < 64; ++i) {
            const std::uint32_t sigma0 = RotateRight(schedule[i - 15], 7) ^ RotateRight(schedule[i - 15], 18) ^ (schedule[i - 15] >> 3);
            const std::uint32_t sigma1 = RotateRight(schedule[i - 2], 17) ^ RotateRight(schedule[i - 2], 19) ^ (schedule[i - 2] >> 10);
            schedule[i] = schedule[i - 16] + sigma0 + schedule[i - 7] + sigma1;
        }

        std::uint32_t a = state_[0];
        std::uint32_t b = state_[1];
        std::uint32_t c = state_[2];
        std::uint32_t d = state_[3];
        std::uint32_t e = state_[4];
        std::uint32_t f = state_[5];
        std::uint32_t g = state_[6];
        std::uint32_t h = state_[7];

        for (int i = 0; i < 64; ++i) {
            const std::uint32_t bigSigma1 = RotateRight(e, 6) ^ RotateRight(e, 11) ^ RotateRight(e, 25);
            const std::uint32_t choice = (e & f) ^ ((~e) & g);
            const std::uint32_t temp1 = h + bigSigma1 + choice + kRoundConstants[i] + schedule[i];
            const std::uint32_t bigSigma0 = RotateRight(a, 2) ^ RotateRight(a, 13) ^ RotateRight(a, 22);
            const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
            const std::uint32_t temp2 = bigSigma0 + majority;
            h = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
        }

        state_[0] += a;
        state_[1] += b;
        state_[2] += c;
        state_[3] += d;
        state_[4] += e;
        state_[5] += f;
        state_[6] += g;
        state_[7] += h;
    }

    std::uint32_t state_[8]{};
    std::uint64_t bitLength_ = 0;
    std::size_t bufferLength_ = 0;
    std::array<std::uint8_t, 64> buffer_{};
};

/**
 * @brief 文件物理标识(卷序列号 + 文件索引高/低),用于识别硬链接。
 *
 * 与 DirectoryScanner 内部一致:同一物理文件的多个硬链接共享此标识。该结构留在匿名命名空间,
 * 因为 DirectoryScanner::FileIdentity 是其私有嵌套类型,无法直接复用。
 */
struct FileIdentity {
    std::uint32_t volumeSerial = 0;
    std::uint32_t indexHigh = 0;
    std::uint32_t indexLow = 0;

    bool operator<(const FileIdentity& other) const {
        if (volumeSerial != other.volumeSerial) {
            return volumeSerial < other.volumeSerial;
        }
        if (indexHigh != other.indexHigh) {
            return indexHigh < other.indexHigh;
        }
        return indexLow < other.indexLow;
    }
};

constexpr std::size_t kHeadBytes = 16 * 1024;  // 头部哈希取前 16KB。

/**
 * @brief 以只读共享方式打开文件用于哈希读取。失败返回 INVALID_HANDLE_VALUE。
 *
 * FILE_SHARE_READ|WRITE|DELETE 允许文件被占用时仍能读取(如被编辑器、索引服务持有)。
 * 路径临时加 \\?\ 前缀以越过 MAX_PATH,但不持久化前缀(见 core::MakeLongPath 契约)。
 */
HANDLE OpenForRead(const std::wstring& displayPath, std::uint32_t access) {
    const std::wstring longPath = MakeLongPath(displayPath);
    return CreateFileW(
        longPath.c_str(),
        access,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
}

/**
 * @brief 解析文件物理标识;打开或取信息失败返回空。
 */
std::optional<FileIdentity> ResolveIdentity(const std::wstring& displayPath) {
    HANDLE handle = OpenForRead(displayPath, FILE_READ_ATTRIBUTES);
    if (handle == INVALID_HANDLE_VALUE) {
        return std::nullopt;
    }
    BY_HANDLE_FILE_INFORMATION info{};
    const BOOL ok = GetFileInformationByHandle(handle, &info);
    CloseHandle(handle);
    if (!ok) {
        return std::nullopt;
    }
    FileIdentity identity;
    identity.volumeSerial = info.dwVolumeSerialNumber;
    identity.indexHigh = info.nFileIndexHigh;
    identity.indexLow = info.nFileIndexLow;
    return identity;
}

/**
 * @brief 把打开的句柄从 fromBytes 起的字节流喂给哈希器,最多读 maxBytes。
 *
 * @param handle 已以 GENERIC_READ 打开的句柄。
 * @param hasher 目标哈希器。
 * @param buffer 复用的读缓冲(至少 64KB)。
 * @param maxBytes 最多读取字节数;std::numeric_limits<std::uint64_t>::max() 表示读到结尾。
 * @param cancel 取消探针;返回 true 立即中断。
 * @param bytesReadOut 累加本次实际读到的字节数。
 * @return 正常读完返回 true;读取失败或取消返回 false。
 */
bool FeedIntoHash(HANDLE handle, Sha256& hasher, std::vector<std::uint8_t>& buffer,
                  std::uint64_t maxBytes, const FileHasher::CancelChecker& cancel,
                  std::uint64_t& bytesReadOut) {
    std::uint64_t remaining = maxBytes;
    while (remaining > 0) {
        if (cancel && cancel()) {
            return false;
        }
        const DWORD chunk = static_cast<DWORD>(std::min<std::uint64_t>(static_cast<std::uint64_t>(buffer.size()), remaining));
        DWORD read = 0;
        const BOOL ok = ReadFile(handle, buffer.data(), chunk, &read, nullptr);
        if (!ok) {
            return false;  // 读取错误。
        }
        if (read == 0) {
            return true;  // 到达文件结尾。
        }
        hasher.Update(buffer.data(), static_cast<std::size_t>(read));
        bytesReadOut += read;
        remaining -= read;
        if (read < chunk) {
            return true;  // 短读,视为到达结尾。
        }
    }
    return true;  // 已读满 maxBytes。
}

}  // namespace

std::vector<DuplicateGroup> FileHasher::FindContentDuplicates(
    const std::vector<std::pair<std::wstring, std::uint64_t>>& files,
    const ProgressCallback& progress,
    const CancelChecker& cancel) const {

    auto isCancelled = [&cancel]() { return cancel && cancel(); };
    auto reportProgress = [&progress](const DuplicateHashProgress& snapshot) {
        if (progress) {
            progress(snapshot);
        }
    };

    // 1) 精确大小分组:字节数不同内容必然不同,丢弃只有 1 个的大小组。
    std::map<std::uint64_t, std::vector<std::pair<std::wstring, std::uint64_t>>> bySize;
    for (const auto& entry : files) {
        if (entry.second == 0) {
            continue;
        }
        bySize[entry.second].push_back(entry);
    }

    // 收集所有候选(大小>=2 的组),先统计进度总量,供 UI 展示。
    struct Candidate {
        std::wstring path;
        std::uint64_t bytes;
    };
    std::vector<std::vector<Candidate>> sizeGroups;
    DuplicateHashProgress progressSnapshot{};
    for (auto& [bytes, entries] : bySize) {
        if (entries.size() < 2) {
            continue;
        }
        std::vector<Candidate> group;
        group.reserve(entries.size());
        for (auto& entry : entries) {
            group.push_back(Candidate{entry.first, entry.second});
            progressSnapshot.bytesTotal += entry.second;
            ++progressSnapshot.hashCandidates;
        }
        sizeGroups.push_back(std::move(group));
    }

    std::vector<DuplicateGroup> result;
    if (sizeGroups.empty()) {
        reportProgress(progressSnapshot);
        return result;
    }

    std::vector<std::uint8_t> readBuffer(64 * 1024);

    for (std::vector<Candidate>& group : sizeGroups) {
        if (isCancelled()) {
            break;
        }

        // 2) 硬链接短路:按物理标识折叠,同物理文件只保留一个代表项(取不到标识的文件跳过)。
        std::map<FileIdentity, Candidate> representatives;
        for (Candidate& candidate : group) {
            std::optional<FileIdentity> identity = ResolveIdentity(candidate.path);
            if (!identity.has_value()) {
                continue;
            }
            // 同一物理文件(硬链接)只保留首次遇到的代表路径。
            representatives.try_emplace(*identity, candidate);
        }
        if (representatives.size() < 2) {
            // 折叠后只剩 0/1 个不同物理文件:无可回收重复。
            continue;
        }

        // 3) 前 16KB SHA-256 分桶。
        std::map<std::array<std::uint8_t, 32>, std::vector<Candidate>> headBuckets;
        for (auto& [identity, candidate] : representatives) {
            (void)identity;
            if (isCancelled()) {
                break;
            }
            HANDLE handle = OpenForRead(candidate.path, GENERIC_READ);
            if (handle == INVALID_HANDLE_VALUE) {
                continue;
            }
            Sha256 hasher;
            std::uint64_t bytesRead = 0;
            const std::uint64_t headLimit = std::min<std::uint64_t>(kHeadBytes, candidate.bytes);
            const bool ok = FeedIntoHash(handle, hasher, readBuffer, headLimit, cancel, bytesRead);
            CloseHandle(handle);
            if (!ok) {
                continue;
            }
            progressSnapshot.bytesHashed += bytesRead;
            ++progressSnapshot.filesHashed;
            reportProgress(progressSnapshot);
            headBuckets[hasher.Finalize()].push_back(candidate);
        }

        // 4) 头哈希桶内做全文 SHA-256 确认,完全一致才产出最终去重组。
        for (auto& [headDigest, bucket] : headBuckets) {
            (void)headDigest;
            if (bucket.size() < 2) {
                continue;
            }
            std::map<std::array<std::uint8_t, 32>, std::vector<Candidate>> fullBuckets;
            for (Candidate& candidate : bucket) {
                if (isCancelled()) {
                    break;
                }
                HANDLE handle = OpenForRead(candidate.path, GENERIC_READ);
                if (handle == INVALID_HANDLE_VALUE) {
                    continue;
                }
                Sha256 hasher;
                std::uint64_t bytesRead = 0;
                const bool ok = FeedIntoHash(handle, hasher, readBuffer, candidate.bytes, cancel, bytesRead);
                CloseHandle(handle);
                if (!ok) {
                    continue;
                }
                progressSnapshot.bytesHashed += bytesRead;
                reportProgress(progressSnapshot);
                fullBuckets[hasher.Finalize()].push_back(candidate);
            }
            for (auto& [fullDigest, confirmed] : fullBuckets) {
                (void)fullDigest;
                if (confirmed.size() < 2) {
                    continue;
                }
                DuplicateGroup out;
                out.bytes = confirmed.front().bytes;
                out.members.reserve(confirmed.size());
                for (Candidate& member : confirmed) {
                    out.members.push_back(DuplicateMember{std::move(member.path), member.bytes});
                }
                std::sort(out.members.begin(), out.members.end(),
                          [](const DuplicateMember& left, const DuplicateMember& right) {
                              return left.path < right.path;
                          });
                result.push_back(std::move(out));
            }
        }
    }

    // 大到小、再按首个路径,便于界面优先展示最值得回收的组。
    std::sort(result.begin(), result.end(),
              [](const DuplicateGroup& left, const DuplicateGroup& right) {
                  if (left.bytes != right.bytes) {
                      return left.bytes > right.bytes;
                  }
                  if (left.members.empty() || right.members.empty()) {
                      return false;
                  }
                  return left.members.front().path < right.members.front().path;
              });

    reportProgress(progressSnapshot);
    return result;
}

}  // namespace disk_lens::core
