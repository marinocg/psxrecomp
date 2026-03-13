#include "psxrecomp/runtime/diag_metadata_watch.h"

#include "psxrecomp/runtime/logger.h"
#include "psxrecomp/runtime/memory_map.h"

#include <cstring>
#include <sstream>

namespace psxrecomp
{
namespace runtime
{

DiagMetadataWatchEngine::DiagMetadataWatchEngine() = default;

void DiagMetadataWatchEngine::configure(const std::vector<MetadataWatchConfig>& configs)
{
    m_configs = configs;
    m_violations.clear();
}

void DiagMetadataWatchEngine::recordWrite(Address address, u32 newValue, Address writerPc,
                                          RuntimeLogger* logger)
{
    const Address canonical = (address & 0x1FFFFFFFu) | 0x80000000u;

    for (const auto& config : m_configs)
    {
        if (canonical < config.regionStart || canonical >= config.regionEnd)
        {
            continue;
        }

        // Check alignment violation.
        if (config.checkAlignment && (newValue & config.alignmentMask) != 0 &&
            newValue != config.sentinelValue && newValue != 0)
        {
            MetadataViolation violation;
            violation.watchName = config.name;
            violation.address = canonical;
            violation.value = newValue;
            violation.writerPc = writerPc;
            violation.reason = "alignment violation";

            if (m_violations.size() < MAX_VIOLATIONS)
            {
                m_violations.push_back(violation);
            }

            if (logger != nullptr)
            {
                std::ostringstream msg;
                msg << "metadata_watch=" << config.name << " addr=0x" << std::hex << canonical
                    << " value=0x" << newValue << " pc=0x" << writerPc << " violation=alignment";
                logger->log(LogLevel::Warn, "metadata_watch", msg.str());
            }
        }

        // Check zero size that isn't a sentinel.
        if (config.checkNonzeroSize)
        {
            const u32 sizeField = newValue & config.sizeMask;
            if (sizeField == 0 && newValue != config.sentinelValue && newValue != 0)
            {
                MetadataViolation violation;
                violation.watchName = config.name;
                violation.address = canonical;
                violation.value = newValue;
                violation.writerPc = writerPc;
                violation.reason = "zero size field";

                if (m_violations.size() < MAX_VIOLATIONS)
                {
                    m_violations.push_back(violation);
                }

                if (logger != nullptr)
                {
                    std::ostringstream msg;
                    msg << "metadata_watch=" << config.name << " addr=0x" << std::hex << canonical
                        << " value=0x" << newValue << " pc=0x" << writerPc
                        << " violation=zero_size";
                    logger->log(LogLevel::Warn, "metadata_watch", msg.str());
                }
            }
        }
    }
}

std::string DiagMetadataWatchEngine::checkIntegrity(const std::string& name, const u8* ram,
                                                    size_t ramSize) const
{
    for (const auto& config : m_configs)
    {
        if (config.name != name)
        {
            continue;
        }

        std::ostringstream os;
        const Address startPhys = config.regionStart & 0x1FFFFFFFu;
        const Address endPhys = config.regionEnd & 0x1FFFFFFFu;

        if (startPhys >= ramSize || endPhys > ramSize)
        {
            os << "metadata region out of RAM bounds\n";
            return os.str();
        }

        // Walk the region checking for obviously corrupted headers.
        Address current = config.regionStart;
        size_t nodeCount = 0;
        while (current < config.regionEnd && nodeCount < 128)
        {
            const Address phys = current & 0x1FFFFFFFu;
            if (phys + sizeof(u32) > ramSize)
            {
                break;
            }

            u32 rawHeader = 0;
            std::memcpy(&rawHeader, ram + phys, sizeof(u32));
            if (rawHeader == config.sentinelValue)
            {
                break;
            }

            const u32 sizeField = rawHeader & config.sizeMask;
            if (sizeField == 0 || sizeField > ramSize)
            {
                os << "integrity: bad size 0x" << std::hex << sizeField << " at 0x" << current
                   << "\n";
                return os.str();
            }

            if (config.checkAlignment && (sizeField & config.alignmentMask) != 0)
            {
                os << "integrity: unaligned size at 0x" << std::hex << current << "\n";
                return os.str();
            }

            current = current + sizeField + sizeof(u32);
            ++nodeCount;
        }

        return {};
    }

    return "metadata watch not found: " + name;
}

std::string DiagMetadataWatchEngine::formatViolations() const
{
    std::ostringstream os;
    os << "Metadata integrity violations:\n";
    if (m_violations.empty())
    {
        os << "  none\n";
        return os.str();
    }

    for (const auto& v : m_violations)
    {
        os << "  [" << v.watchName << "] addr=0x" << std::hex << v.address << " value=0x" << v.value
           << " pc=0x" << v.writerPc << " reason=" << v.reason << "\n";
    }
    return os.str();
}

size_t DiagMetadataWatchEngine::watchCount() const
{
    return m_configs.size();
}

size_t DiagMetadataWatchEngine::violationCount() const
{
    return m_violations.size();
}

void DiagMetadataWatchEngine::clearViolations()
{
    m_violations.clear();
}

bool DiagMetadataWatchEngine::isInWatchedRegion(Address address) const
{
    const Address canonical = (address & 0x1FFFFFFFu) | 0x80000000u;
    for (const auto& config : m_configs)
    {
        if (canonical >= config.regionStart && canonical < config.regionEnd)
        {
            return true;
        }
    }
    return false;
}

} // namespace runtime
} // namespace psxrecomp
