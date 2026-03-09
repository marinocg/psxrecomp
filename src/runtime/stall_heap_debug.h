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

/// Check if the stall report should include heap/validator output.
/// Returns true if the system has configured validators (profile-driven).
bool shouldDumpAllocatorHeap(const PsxSystem* system,
                             const RingBuffer<Address, StallClassifier::PC_RING_SIZE>& pcRing);

/// Format a validator report from the diagnostic engine.
std::string formatAllocatorHeapDump(const PsxSystem& system);

/// Check if heap validation is enabled via env var.
bool fastHeapValidationEnabled();

} // namespace detail
} // namespace runtime
} // namespace psxrecomp
