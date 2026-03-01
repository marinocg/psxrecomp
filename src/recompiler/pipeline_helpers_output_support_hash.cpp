#include "pipeline_helpers_output_support_internal.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>

namespace psxrecomp
{
namespace recompiler
{
namespace detail
{

namespace
{

constexpr size_t kSniffPrefixBytes = 4096;

u32 rotateLeft(u32 value, u32 amount)
{
    return (value << amount) | (value >> (32 - amount));
}

std::string computeSha1Hex(const u8* data, size_t size)
{
    Sha1Hasher hasher;
    hasher.update(data, size);
    return sha1DigestToHex(hasher.finalize());
}

bool validateSha1Implementation(std::string& outError)
{
    struct TestVector
    {
        std::string input;
        std::string expectedSha1;
    };

    const std::array<TestVector, 4> testVectors = {{
        {"", "da39a3ee5e6b4b0d3255bfef95601890afd80709"},
        {"abc", "a9993e364706816aba3e25717850c26c9cd0d89d"},
        {"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
         "84983e441c3bd26ebaae4aa1f95129e5e54670f1"},
        {std::string(1000000, 'a'), "34aa973cd4c4daa4f61eeb2bdbad27316534016f"},
    }};

    for (const auto& vector : testVectors)
    {
        const auto* bytes = reinterpret_cast<const u8*>(vector.input.data());
        const std::string digest = computeSha1Hex(bytes, vector.input.size());
        if (digest != vector.expectedSha1)
        {
            outError = "Expected " + vector.expectedSha1 + " but got " + digest + ".";
            return false;
        }
    }

    outError.clear();
    return true;
}

} // namespace

Sha1Hasher::Sha1Hasher()
{
    m_state = {0x67452301U, 0xEFCDAB89U, 0x98BADCFEU, 0x10325476U, 0xC3D2E1F0U};
    m_totalBytes = 0;
    m_bufferSize = 0;
}

void Sha1Hasher::update(const u8* data, size_t size)
{
    if (size == 0)
    {
        return;
    }

    m_totalBytes += static_cast<u64>(size);
    size_t offset = 0;
    while (offset < size)
    {
        const size_t toCopy = std::min(kBlockSize - m_bufferSize, size - offset);
        std::memcpy(m_buffer.data() + m_bufferSize, data + offset, toCopy);
        m_bufferSize += toCopy;
        offset += toCopy;

        if (m_bufferSize == kBlockSize)
        {
            processBlock(m_buffer.data());
            m_bufferSize = 0;
        }
    }
}

std::array<u8, Sha1Hasher::kDigestSize> Sha1Hasher::finalize()
{
    const u64 bitLength = m_totalBytes * 8ULL;

    m_buffer[m_bufferSize++] = 0x80;
    if (m_bufferSize > 56)
    {
        std::fill(m_buffer.begin() + static_cast<std::ptrdiff_t>(m_bufferSize), m_buffer.end(), 0);
        processBlock(m_buffer.data());
        m_bufferSize = 0;
    }

    std::fill(m_buffer.begin() + static_cast<std::ptrdiff_t>(m_bufferSize),
              m_buffer.begin() + static_cast<std::ptrdiff_t>(56), 0);
    for (size_t i = 0; i < 8; ++i)
    {
        m_buffer[56 + i] = static_cast<u8>((bitLength >> ((7 - i) * 8)) & 0xFFULL);
    }
    processBlock(m_buffer.data());
    m_bufferSize = 0;

    std::array<u8, kDigestSize> digest{};
    for (size_t i = 0; i < m_state.size(); ++i)
    {
        digest[i * 4] = static_cast<u8>((m_state[i] >> 24) & 0xFFU);
        digest[i * 4 + 1] = static_cast<u8>((m_state[i] >> 16) & 0xFFU);
        digest[i * 4 + 2] = static_cast<u8>((m_state[i] >> 8) & 0xFFU);
        digest[i * 4 + 3] = static_cast<u8>(m_state[i] & 0xFFU);
    }
    return digest;
}

void Sha1Hasher::processBlock(const u8* block)
{
    std::array<u32, 80> words{};
    for (size_t i = 0; i < 16; ++i)
    {
        const size_t offset = i * 4;
        words[i] = (static_cast<u32>(block[offset]) << 24) |
                   (static_cast<u32>(block[offset + 1]) << 16) |
                   (static_cast<u32>(block[offset + 2]) << 8) | static_cast<u32>(block[offset + 3]);
    }
    for (size_t i = 16; i < words.size(); ++i)
    {
        words[i] = rotateLeft(words[i - 3] ^ words[i - 8] ^ words[i - 14] ^ words[i - 16], 1);
    }

    u32 a = m_state[0];
    u32 b = m_state[1];
    u32 c = m_state[2];
    u32 d = m_state[3];
    u32 e = m_state[4];

    for (size_t i = 0; i < words.size(); ++i)
    {
        u32 f = 0;
        u32 k = 0;
        if (i < 20)
        {
            f = (b & c) | ((~b) & d);
            k = 0x5A827999U;
        }
        else if (i < 40)
        {
            f = b ^ c ^ d;
            k = 0x6ED9EBA1U;
        }
        else if (i < 60)
        {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8F1BBCDCU;
        }
        else
        {
            f = b ^ c ^ d;
            k = 0xCA62C1D6U;
        }

        const u32 temp = rotateLeft(a, 5) + f + e + k + words[i];
        e = d;
        d = c;
        c = rotateLeft(b, 30);
        b = a;
        a = temp;
    }

    m_state[0] += a;
    m_state[1] += b;
    m_state[2] += c;
    m_state[3] += d;
    m_state[4] += e;
}

std::string sha1DigestToHex(const std::array<u8, Sha1Hasher::kDigestSize>& digest)
{
    static const char* kHex = "0123456789abcdef";
    std::string value;
    value.reserve(Sha1Hasher::kDigestSize * 2);
    for (u8 byte : digest)
    {
        value.push_back(kHex[(byte >> 4) & 0x0F]);
        value.push_back(kHex[byte & 0x0F]);
    }
    return value;
}

bool ensureSha1SelfTest(std::string& outError)
{
    struct SelfTestState
    {
        bool passed = false;
        std::string error;
    };

    static const SelfTestState state = []
    {
        SelfTestState result;
        result.passed = validateSha1Implementation(result.error);
        return result;
    }();

    if (!state.passed)
    {
        outError = "SHA-1 self-test failed: " + state.error;
        return false;
    }
    outError.clear();
    return true;
}

bool computeSha1AndPrefix(const std::filesystem::path& path, std::string& outSha1, u64& outSize,
                          std::vector<u8>& outPrefix, std::string& outError)
{
    outSha1.clear();
    outSize = 0;
    outPrefix.clear();
    outError.clear();

    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
        outError = "Failed to open file for hashing: " + path.string();
        return false;
    }

    Sha1Hasher hasher;
    std::array<char, 8192> buffer{};
    while (input.good())
    {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize readCount = input.gcount();
        if (readCount <= 0)
        {
            break;
        }

        hasher.update(reinterpret_cast<const u8*>(buffer.data()), static_cast<size_t>(readCount));
        outSize += static_cast<u64>(readCount);

        if (outPrefix.size() < kSniffPrefixBytes)
        {
            const size_t remainingPrefix = kSniffPrefixBytes - outPrefix.size();
            const size_t copyCount = std::min(remainingPrefix, static_cast<size_t>(readCount));
            outPrefix.insert(outPrefix.end(), reinterpret_cast<const u8*>(buffer.data()),
                             reinterpret_cast<const u8*>(buffer.data()) + copyCount);
        }
    }

    if (!input.eof() && input.fail())
    {
        outError = "Failed to read file for hashing: " + path.string();
        return false;
    }

    outSha1 = sha1DigestToHex(hasher.finalize());
    return true;
}

} // namespace detail
} // namespace recompiler
} // namespace psxrecomp
