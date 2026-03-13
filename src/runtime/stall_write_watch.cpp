#include "stall_write_watch.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>
#include <string>

namespace psxrecomp
{
namespace runtime
{
namespace detail
{
namespace
{
constexpr Address CanonicalRamBase = 0x80000000u;
constexpr size_t WatchedWriteSummaryLimit = 16;

Address normalizeWatchAddress(Address address)
{
    return address & 0x1FFFFFFFu;
}

Address canonicalRamAddress(Address address)
{
    return CanonicalRamBase | (address & 0x1FFFFFFFu);
}

bool parseAddressToken(const std::string& text, Address& address)
{
    if (text.empty())
    {
        return false;
    }

    size_t consumed = 0;
    try
    {
        const auto parsed = std::stoull(text, &consumed, 0);
        if (consumed != text.size())
        {
            return false;
        }
        address = static_cast<Address>(parsed);
        return true;
    }
    catch (...)
    {
        return false;
    }
}

u32 maskValueForSize(u32 value, u8 size)
{
    switch (size)
    {
    case 1:
        return value & 0xFFu;
    case 2:
        return value & 0xFFFFu;
    default:
        return value;
    }
}

std::vector<std::string> splitWatchSpec(const std::string& spec)
{
    std::vector<std::string> tokens;
    std::string current;
    for (char ch : spec)
    {
        if (ch == ',' || ch == ';' || std::isspace(static_cast<unsigned char>(ch)))
        {
            if (!current.empty())
            {
                tokens.push_back(current);
                current.clear();
            }
            continue;
        }
        current.push_back(ch);
    }
    if (!current.empty())
    {
        tokens.push_back(current);
    }
    return tokens;
}

} // namespace

std::vector<WatchedWriteRange> parseWatchedWriteRangesFromEnv()
{
    std::vector<WatchedWriteRange> ranges;
    const char* env = std::getenv("PSXRECOMP_WATCH_WRITE");
    if (env == nullptr || env[0] == '\0')
    {
        return ranges;
    }

    for (const std::string& token : splitWatchSpec(env))
    {
        WatchedWriteRange range{};
        const size_t dash = token.find('-');
        if (dash == std::string::npos)
        {
            Address address = 0;
            if (!parseAddressToken(token, address))
            {
                continue;
            }
            range.start = normalizeWatchAddress(address);
            range.end = range.start;
        }
        else
        {
            Address start = 0;
            Address end = 0;
            if (!parseAddressToken(token.substr(0, dash), start) ||
                !parseAddressToken(token.substr(dash + 1), end))
            {
                continue;
            }
            range.start = normalizeWatchAddress(start);
            range.end = normalizeWatchAddress(end);
            if (range.end < range.start)
            {
                std::swap(range.start, range.end);
            }
        }

        if (range.start >= MemoryMap::RAM_SIZE)
        {
            continue;
        }
        range.end = std::min<Address>(range.end, MemoryMap::RAM_SIZE - 1);
        ranges.push_back(range);
    }

    std::sort(ranges.begin(), ranges.end(),
              [](const auto& lhs, const auto& rhs) { return lhs.start < rhs.start; });

    std::vector<WatchedWriteRange> merged;
    for (const auto& range : ranges)
    {
        if (!merged.empty() && range.start <= merged.back().end + 1)
        {
            merged.back().end = std::max(merged.back().end, range.end);
            continue;
        }
        merged.push_back(range);
    }
    return merged;
}

bool overlapsWatchedWrite(const std::vector<WatchedWriteRange>& ranges, Address address, u8 size)
{
    if (ranges.empty() || size == 0)
    {
        return false;
    }

    const Address physical = normalizeWatchAddress(address);
    if (physical >= MemoryMap::RAM_SIZE)
    {
        return false;
    }

    const Address writeEnd =
        static_cast<Address>(std::min<u64>(static_cast<u64>(physical) + static_cast<u64>(size) - 1,
                                           static_cast<u64>(MemoryMap::RAM_SIZE - 1)));
    for (const auto& range : ranges)
    {
        if (writeEnd < range.start)
        {
            break;
        }
        if (physical <= range.end && writeEnd >= range.start)
        {
            return true;
        }
    }
    return false;
}

std::string formatWatchedRamWrites(
    const RingBuffer<WatchedRamWriteEntry, StallClassifier::WATCHED_WRITE_RING_SIZE>& ring)
{
    std::ostringstream os;
    os << "Last watched RAM writes (newest first):\n";
    if (ring.count() == 0)
    {
        os << "  none\n";
        return os.str();
    }

    const size_t count = std::min<size_t>(ring.count(), WatchedWriteSummaryLimit);
    for (size_t i = 0; i < count; ++i)
    {
        const auto& entry = ring.recent(i);
        os << "  pc=0x" << std::hex << entry.writerPc << " addr=0x"
           << canonicalRamAddress(entry.address) << " size=" << std::dec
           << static_cast<unsigned>(entry.size) << " old=0x" << std::hex
           << maskValueForSize(entry.oldValue, entry.size) << " new=0x"
           << maskValueForSize(entry.newValue, entry.size) << "\n";
    }
    return os.str();
}

} // namespace detail
} // namespace runtime
} // namespace psxrecomp
