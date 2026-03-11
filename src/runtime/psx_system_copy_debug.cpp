#include "psxrecomp/runtime/psx_system.h"

#include <algorithm>
#include <cstring>
#include <sstream>
#include <stdexcept>

namespace psxrecomp
{
namespace runtime
{
namespace
{
constexpr Address CanonicalRamBase = 0x80000000u;

Address canonicalRamAddress(Address physical)
{
    return CanonicalRamBase | (physical & 0x1FFFFFFFu);
}

} // namespace

PsxSystem::RamCopyBounds PsxSystem::planRamCopy(Address destination, u32 requestedLength) const
{
    RamCopyBounds bounds{};
    bounds.physicalDestination = normalizeAddress(destination);
    bounds.destinationInRam = bounds.physicalDestination < MemoryMap::RAM_SIZE;
    if (!bounds.destinationInRam)
    {
        return bounds;
    }

    const u32 remaining = MemoryMap::RAM_SIZE - bounds.physicalDestination;
    bounds.writableLength = std::min(requestedLength, remaining);
    bounds.destinationOverflow = requestedLength > remaining;
    return bounds;
}

void PsxSystem::logRamCopyWarning(const std::string& sourceTag, Address destination,
                                  u32 requestedLength, const RamCopyBounds& bounds,
                                  u32 actualLength)
{
    if (!bounds.destinationInRam)
    {
        std::ostringstream os;
        os << sourceTag << " destination outside RAM dst=0x" << std::hex << destination
           << " len=" << std::dec << requestedLength
           << " (this memory op is a consumer of a bad pointer, not the root cause)";
        if (m_lastResumeAddress != 0)
        {
            os << " resumed_at=0x" << std::hex << m_lastResumeAddress;
        }
        m_logger.log(LogLevel::Warn, "load", os.str());
        return;
    }

    if (bounds.destinationOverflow)
    {
        std::ostringstream os;
        os << sourceTag << " destination+length overflow dst=0x" << std::hex
           << canonicalRamAddress(bounds.physicalDestination) << " len=" << std::dec
           << requestedLength << " writable=" << bounds.writableLength;
        m_logger.log(LogLevel::Warn, "load", os.str());
    }

    if (actualLength < requestedLength)
    {
        std::ostringstream os;
        os << sourceTag << " short read/copy actual=" << std::dec << actualLength
           << " requested=" << requestedLength << " dst=0x" << std::hex
           << canonicalRamAddress(bounds.physicalDestination);
        m_logger.log(LogLevel::Warn, "load", os.str());
    }
}

u32 PsxSystem::copyBufferToRam(Address destination, const u8* source, u32 actualLength,
                               u32 requestedLength, Address writerPc, const std::string& sourceTag,
                               const std::string& detail)
{
    const RamCopyBounds bounds = planRamCopy(destination, requestedLength);
    const u32 copiedLength = (bounds.destinationInRam && source != nullptr)
                                 ? std::min(actualLength, bounds.writableLength)
                                 : 0u;
    if (copiedLength > 0)
    {
        std::memcpy(m_ram.data() + bounds.physicalDestination, source, copiedLength);
    }

    if (!bounds.destinationInRam || bounds.destinationOverflow || actualLength < requestedLength)
    {
        logRamCopyWarning(sourceTag, destination, requestedLength, bounds, actualLength);
    }

    m_stallClassifier.recordRamCopyProvenance(
        sourceTag, detail, writerPc,
        bounds.destinationInRam ? canonicalRamAddress(bounds.physicalDestination) : destination,
        copiedLength, requestedLength, bounds.destinationInRam, bounds.destinationOverflow,
        actualLength < requestedLength);
    return copiedLength;
}

u32 PsxSystem::fillBufferToRam(Address destination, u8 value, u32 requestedLength, Address writerPc,
                               const std::string& sourceTag, const std::string& detail)
{
    const RamCopyBounds bounds = planRamCopy(destination, requestedLength);
    const u32 writtenLength = bounds.destinationInRam ? bounds.writableLength : 0u;
    if (writtenLength > 0)
    {
        std::memset(m_ram.data() + bounds.physicalDestination, static_cast<int>(value),
                    writtenLength);
    }

    if (!bounds.destinationInRam || bounds.destinationOverflow)
    {
        logRamCopyWarning(sourceTag, destination, requestedLength, bounds, requestedLength);
    }

    m_stallClassifier.recordRamCopyProvenance(
        sourceTag, detail, writerPc,
        bounds.destinationInRam ? canonicalRamAddress(bounds.physicalDestination) : destination,
        writtenLength, requestedLength, bounds.destinationInRam, bounds.destinationOverflow, false);
    return writtenLength;
}

void PsxSystem::validateAllocatorHeapBoundary(const std::string& source, Address relatedAddress)
{
    if (m_diagValidators.validatorCount() == 0)
    {
        return;
    }

    const auto results = m_diagValidators.runAll(m_ram.data(), m_ram.size());
    bool anyFailed = false;
    std::ostringstream failureReport;

    for (const auto& result : results)
    {
        if (result.passed)
        {
            std::ostringstream os;
            os << "heap validation passed" << " boundary=" << source
               << " validator=" << result.validatorName << " result=passed";
            if (relatedAddress != 0)
            {
                os << " address=0x" << std::hex << relatedAddress;
            }
            if (!result.report.empty())
            {
                os << " " << result.report;
            }
            m_logger.log(LogLevel::Info, "heap", os.str());
            continue;
        }

        anyFailed = true;
        failureReport << result.report;
    }

    if (!anyFailed)
    {
        return;
    }

    std::ostringstream os;
    os << "Heap validation failed after " << source;
    if (relatedAddress != 0)
    {
        os << " 0x" << std::hex << relatedAddress;
    }
    if (m_lastResumeAddress != 0)
    {
        os << " (inside resumed function, resumed_at=0x" << std::hex << m_lastResumeAddress << ")";
    }
    os << "\nresult=failed\n" << failureReport.str();
    os << m_stallClassifier.formatRecentMemoryActivity();
    if (m_diagWatchpoints.eventCount() > 0)
    {
        os << m_diagWatchpoints.formatSummary();
    }
    throw std::runtime_error(os.str());
}

void PsxSystem::validateAllocatorHeapCallBoundary(Address address)
{
    // If no validators are configured, this is a no-op.
    if (m_diagValidators.validatorCount() > 0)
    {
        validateAllocatorHeapBoundary("allocator call return", address);
    }
}

} // namespace runtime
} // namespace psxrecomp
