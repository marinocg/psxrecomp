#pragma once

#include "psxrecomp/runtime/stall_classifier.h"

#include <string>

namespace psxrecomp
{
namespace runtime
{
class PsxSystem;

namespace detail
{

enum class AllocatorHeapState
{
    Uninitialized,
    Active,
    Suspicious,
};

bool shouldDumpAllocatorHeap(const PsxSystem* system,
                             const RingBuffer<Address, StallClassifier::PC_RING_SIZE>& pcRing);
std::string formatAllocatorHeapDump(const PsxSystem& system);
bool isAllocatorBoundaryFunction(Address address);
bool fastHeapValidationEnabled();
AllocatorHeapState classifyAllocatorHeapState(const PsxSystem& system);
const char* allocatorHeapStateLabel(AllocatorHeapState state);
std::string validateAllocatorHeap(const PsxSystem& system);

} // namespace detail
} // namespace runtime
} // namespace psxrecomp
