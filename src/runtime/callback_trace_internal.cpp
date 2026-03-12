#include "callback_trace_internal.h"

#include <algorithm>
#include <sstream>

namespace psxrecomp
{
namespace runtime
{
namespace callback_trace_internal
{

namespace
{

constexpr size_t MAX_WRITE_SUMMARY = 3;
constexpr size_t MAX_REGISTER_SUMMARY = 8;
constexpr size_t MAX_RETURN_SITE_SUMMARY = 4;
constexpr Address CALLBACK_STACK_WINDOW_BELOW = 0x800u;
constexpr Address CALLBACK_STACK_WINDOW_ABOVE = 0x80u;

} // namespace

std::vector<CallbackTraceEntry::WriteHotspot> summarizeWrites(
    const std::unordered_map<Address, CallbackTraceEngine::ActiveWriteInfo>& writes)
{
    std::vector<std::pair<Address, CallbackTraceEngine::ActiveWriteInfo>> sorted(writes.begin(),
                                                                                 writes.end());
    std::sort(sorted.begin(), sorted.end(), [](const auto& lhs, const auto& rhs)
              {
                  if (lhs.second.count != rhs.second.count)
                  {
                      return lhs.second.count > rhs.second.count;
                  }
                  return lhs.first < rhs.first;
              });

    std::vector<CallbackTraceEntry::WriteHotspot> result;
    const size_t count = std::min<size_t>(sorted.size(), MAX_WRITE_SUMMARY);
    result.reserve(count);
    for (size_t index = 0; index < count; ++index)
    {
        CallbackTraceEntry::WriteHotspot hotspot;
        hotspot.address = sorted[index].first;
        hotspot.count = sorted[index].second.count;
        hotspot.lastOldValue = sorted[index].second.lastOldValue;
        hotspot.lastNewValue = sorted[index].second.lastNewValue;
        result.push_back(hotspot);
    }
    return result;
}

std::string formatWriteSummary(const std::vector<CallbackTraceEntry::WriteHotspot>& writes)
{
    if (writes.empty())
    {
        return "none";
    }
    std::ostringstream os;
    for (size_t index = 0; index < writes.size(); ++index)
    {
        const auto& write = writes[index];
        if (index != 0)
        {
            os << ",";
        }
        os << "0x" << std::hex << write.address << "x" << std::dec << write.count << "(0x"
           << std::hex << write.lastOldValue << "->0x" << write.lastNewValue << ")";
    }
    return os.str();
}

std::vector<std::pair<Address, u32>> summarizeReturnSites(
    const std::unordered_map<Address, u32>& counts)
{
    std::vector<std::pair<Address, u32>> sorted(counts.begin(), counts.end());
    std::sort(sorted.begin(), sorted.end(), [](const auto& lhs, const auto& rhs)
              {
                  if (lhs.second != rhs.second)
                  {
                      return lhs.second > rhs.second;
                  }
                  return lhs.first < rhs.first;
              });
    if (sorted.size() > MAX_RETURN_SITE_SUMMARY)
    {
        sorted.resize(MAX_RETURN_SITE_SUMMARY);
    }
    return sorted;
}

std::string formatReturnSiteSummary(const std::vector<std::pair<Address, u32>>& sites)
{
    if (sites.empty())
    {
        return "none";
    }
    std::ostringstream os;
    for (size_t index = 0; index < sites.size(); ++index)
    {
        if (index != 0)
        {
            os << ",";
        }
        os << "0x" << std::hex << sites[index].first << "x" << std::dec << sites[index].second;
    }
    return os.str();
}

std::vector<std::string> summarizeRegisters(const std::unordered_map<std::string, u32>& counts)
{
    std::vector<std::pair<std::string, u32>> sorted(counts.begin(), counts.end());
    std::sort(sorted.begin(), sorted.end(), [](const auto& lhs, const auto& rhs)
              {
                  if (lhs.second != rhs.second)
                  {
                      return lhs.second > rhs.second;
                  }
                  return lhs.first < rhs.first;
              });

    std::vector<std::string> result;
    const size_t count = std::min<size_t>(sorted.size(), MAX_REGISTER_SUMMARY);
    result.reserve(count);
    for (size_t index = 0; index < count; ++index)
    {
        result.push_back(sorted[index].first + "x" + std::to_string(sorted[index].second));
    }
    return result;
}

bool isLikelyCallbackStackWrite(Address address, Address entryStackPointer)
{
    if (entryStackPointer == 0)
    {
        return false;
    }

    const Address normalizedAddress = address & 0x1FFFFFFFu;
    const Address normalizedSp = entryStackPointer & 0x1FFFFFFFu;
    const Address stackStart =
        (normalizedSp > CALLBACK_STACK_WINDOW_BELOW) ? (normalizedSp - CALLBACK_STACK_WINDOW_BELOW)
                                                     : 0;
    const Address stackEnd = normalizedSp + CALLBACK_STACK_WINDOW_ABOVE;
    return normalizedAddress >= stackStart && normalizedAddress <= stackEnd;
}

} // namespace callback_trace_internal
} // namespace runtime
} // namespace psxrecomp
