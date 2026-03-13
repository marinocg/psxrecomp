#pragma once

#include "psxrecomp/runtime/callback_trace.h"

#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace psxrecomp
{
namespace runtime
{
namespace callback_trace_internal
{

constexpr size_t MAX_SIGNATURE_SUMMARY = 4;

std::vector<CallbackTraceEntry::WriteHotspot>
summarizeWrites(const std::unordered_map<Address, CallbackTraceEngine::ActiveWriteInfo>& writes);

std::string formatWriteSummary(const std::vector<CallbackTraceEntry::WriteHotspot>& writes);

std::vector<std::pair<Address, u32>>
summarizeReturnSites(const std::unordered_map<Address, u32>& counts);

std::string formatReturnSiteSummary(const std::vector<std::pair<Address, u32>>& sites);

std::vector<std::string> summarizeRegisters(const std::unordered_map<std::string, u32>& counts);

bool isLikelyCallbackStackWrite(Address address, Address entryStackPointer);

} // namespace callback_trace_internal
} // namespace runtime
} // namespace psxrecomp
