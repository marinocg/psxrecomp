#pragma once

#include "psxrecomp/runtime/stall_classifier.h"

#include <string>
#include <vector>

namespace psxrecomp
{
namespace runtime
{
namespace detail
{

std::vector<WatchedWriteRange> parseWatchedWriteRangesFromEnv();

bool overlapsWatchedWrite(const std::vector<WatchedWriteRange>& ranges, Address address,
						 u8 size);

std::string formatWatchedRamWrites(
	const RingBuffer<WatchedRamWriteEntry, StallClassifier::WATCHED_WRITE_RING_SIZE>& ring);

} // namespace detail
} // namespace runtime
} // namespace psxrecomp
