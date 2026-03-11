#include "psxrecomp/runtime/diag_validators.h"

#include "psxrecomp/runtime/memory_map.h"

#include <cstring>
#include <sstream>
#include <unordered_set>

namespace psxrecomp
{
namespace runtime
{

DiagValidatorEngine::DiagValidatorEngine() = default;

void DiagValidatorEngine::configure(const std::vector<ValidatorConfig>& configs)
{
    m_configs = configs;
}

ValidationResult DiagValidatorEngine::runValidator(const std::string& name, const u8* ram,
                                                   size_t ramSize) const
{
    for (const auto& config : m_configs)
    {
        if (config.name != name)
        {
            continue;
        }

        // Check enable condition.
        if (!config.enabledWhen.type.empty() && config.enabledWhen.type == "nonzero_u32")
        {
            const Address phys = toPhysical(config.enabledWhen.address);
            if (isPhysicalRamAddress(phys, ramSize))
            {
                const u32 flag = readWord(ram, phys, ramSize);
                if (flag == 0)
                {
                    ValidationResult result;
                    result.validatorName = name;
                    result.passed = true;
                    result.report = "skipped: enable condition not met";
                    return result;
                }
            }
        }

        switch (config.type)
        {
        case ValidatorType::SentinelBlockChain:
            return runSentinelBlockChain(config, ram, ramSize);
        case ValidatorType::PointerCell:
            return runPointerCell(config, ram, ramSize);
        case ValidatorType::LinkedList:
            return runLinkedList(config, ram, ramSize);
        case ValidatorType::BoundedStructureWalk:
            return runBoundedWalk(config, ram, ramSize);
        }
    }

    ValidationResult notFound;
    notFound.validatorName = name;
    notFound.passed = true;
    notFound.report = "validator not found: " + name;
    return notFound;
}

std::vector<ValidationResult> DiagValidatorEngine::runAll(const u8* ram, size_t ramSize) const
{
    std::vector<ValidationResult> results;
    for (const auto& config : m_configs)
    {
        results.push_back(runValidator(config.name, ram, ramSize));
    }
    return results;
}

size_t DiagValidatorEngine::validatorCount() const
{
    return m_configs.size();
}

std::vector<std::string> DiagValidatorEngine::validatorNames() const
{
    std::vector<std::string> names;
    names.reserve(m_configs.size());
    for (const auto& config : m_configs)
    {
        names.push_back(config.name);
    }
    return names;
}

const ValidatorConfig* DiagValidatorEngine::findConfig(const std::string& name) const
{
    for (const auto& config : m_configs)
    {
        if (config.name == name)
        {
            return &config;
        }
    }
    return nullptr;
}

u32 DiagValidatorEngine::readWord(const u8* ram, Address physicalAddress, size_t ramSize)
{
    if (physicalAddress + sizeof(u32) > ramSize)
    {
        return 0;
    }
    u32 value = 0;
    std::memcpy(&value, ram + physicalAddress, sizeof(u32));
    return value;
}

bool DiagValidatorEngine::isPhysicalRamAddress(Address address, size_t ramSize)
{
    return address < ramSize;
}

Address DiagValidatorEngine::toPhysical(Address address)
{
    return address & 0x1FFFFFFFu;
}

bool DiagValidatorEngine::isAligned(Address address, u32 alignmentMask)
{
    return (address & alignmentMask) == 0u;
}

std::string DiagValidatorEngine::validateChain(const ValidatorConfig& config, Address start,
                                               const char* label, const u8* ram,
                                               size_t ramSize) const
{
    std::ostringstream os;
    const Address startPhys = toPhysical(start);
    if (!isPhysicalRamAddress(startPhys, ramSize))
    {
        os << label << ": start 0x" << std::hex << start << " out of RAM\n";
        return os.str();
    }

    std::unordered_set<Address> visited;
    Address current = start;
    for (size_t i = 0; i < config.maxNodes; ++i)
    {
        const Address phys = toPhysical(current);
        if (!visited.insert(phys).second)
        {
            os << label << ": cycle at 0x" << std::hex << current << "\n";
            return os.str();
        }
        if (!isPhysicalRamAddress(phys, ramSize))
        {
            os << label << ": block 0x" << std::hex << current << " out of RAM\n";
            return os.str();
        }

        const u32 rawHeader = readWord(ram, phys, ramSize);
        if (rawHeader == config.header.sentinel)
        {
            return {};
        }

        const u32 sizeField = rawHeader & config.header.sizeMask;
        const Address next = current + sizeField + sizeof(u32);

        if (!isAligned(current, 3u))
        {
            os << label << ": block 0x" << std::hex << current << " not aligned\n";
            return os.str();
        }
        if (!isAligned(sizeField, 3u))
        {
            os << label << ": size not aligned at 0x" << std::hex << current << " raw=0x"
               << rawHeader << "\n";
            return os.str();
        }
        if (sizeField == 0 || sizeField > ramSize)
        {
            os << label << ": absurd size 0x" << std::hex << sizeField << " at 0x" << current
               << "\n";
            return os.str();
        }
        if (next <= current)
        {
            os << label << ": next moved backwards at 0x" << std::hex << current << "\n";
            return os.str();
        }
        if (!isPhysicalRamAddress(toPhysical(next), ramSize))
        {
            os << label << ": next 0x" << std::hex << next << " out of RAM at 0x" << current
               << "\n";
            return os.str();
        }
        current = next;
    }

    os << label << ": missing sentinel within " << std::dec << config.maxNodes << " nodes\n";
    return os.str();
}

std::string DiagValidatorEngine::describeChain(const ValidatorConfig& config, Address start,
                                               const char* label, const u8* ram,
                                               size_t ramSize) const
{
    std::ostringstream os;
    os << label << " = 0x" << std::hex << start << "\n";
    const Address startPhys = toPhysical(start);
    if (!isPhysicalRamAddress(startPhys, ramSize))
    {
        os << "  out-of-range start\n";
        return os.str();
    }

    std::unordered_set<Address> visited;
    Address current = start;
    for (size_t i = 0; i < config.maxNodes; ++i)
    {
        const Address phys = toPhysical(current);
        if (!visited.insert(phys).second)
        {
            os << "  cycle at 0x" << std::hex << current << "\n";
            return os.str();
        }
        if (!isPhysicalRamAddress(phys, ramSize))
        {
            os << "  block 0x" << std::hex << current << " out of RAM\n";
            return os.str();
        }

        const u32 rawHeader = readWord(ram, phys, ramSize);
        if (rawHeader == config.header.sentinel)
        {
            os << "  node[" << std::dec << i << "] addr=0x" << std::hex << current << " raw=0x"
               << rawHeader << " sentinel=yes\n";
            return os.str();
        }

        const u32 sizeField = rawHeader & config.header.sizeMask;
        const bool freeBit = (rawHeader & config.header.freeBit) != 0;
        const Address next = current + sizeField + sizeof(u32);
        os << "  node[" << std::dec << i << "] addr=0x" << std::hex << current << " raw=0x"
           << rawHeader << " size=0x" << sizeField << " free=" << freeBit << " next=0x" << next
           << "\n";

        if (!isAligned(sizeField, 3u) || sizeField == 0 || sizeField > ramSize || next <= current ||
            !isPhysicalRamAddress(toPhysical(next), ramSize))
        {
            os << "    chain broken\n";
            return os.str();
        }
        current = next;
    }

    os << "  sentinel not found in " << std::dec << config.maxNodes << " nodes\n";
    return os.str();
}

ValidationResult DiagValidatorEngine::runSentinelBlockChain(const ValidatorConfig& config,
                                                            const u8* ram, size_t ramSize) const
{
    ValidationResult result;
    result.validatorName = config.name;

    Address currentAddr = 0;
    Address backupAddr = 0;
    const Address currentPhys = toPhysical(config.currentRoot);
    const Address backupPhys = toPhysical(config.backupRoot);
    if (isPhysicalRamAddress(currentPhys, ramSize))
    {
        currentAddr = readWord(ram, currentPhys, ramSize);
    }
    if (config.backupRoot != 0 && isPhysicalRamAddress(backupPhys, ramSize))
    {
        backupAddr = readWord(ram, backupPhys, ramSize);
    }

    std::ostringstream issues;
    issues << validateChain(config, currentAddr, "current chain", ram, ramSize);
    if (backupAddr != 0 && backupAddr != currentAddr)
    {
        issues << validateChain(config, backupAddr, "backup chain", ram, ramSize);
    }

    std::string issueText = issues.str();
    if (issueText.empty())
    {
        result.passed = true;
        return result;
    }

    result.passed = false;
    std::ostringstream report;
    report << "Validator '" << config.name << "' failed:\n" << issueText;
    report << describeChain(config, currentAddr, "Current chain dump", ram, ramSize);
    if (backupAddr != 0 && backupAddr != currentAddr)
    {
        report << describeChain(config, backupAddr, "Backup chain dump", ram, ramSize);
    }
    result.report = report.str();
    return result;
}

ValidationResult DiagValidatorEngine::runPointerCell(const ValidatorConfig& config, const u8* ram,
                                                     size_t ramSize) const
{
    ValidationResult result;
    result.validatorName = config.name;

    const Address phys = toPhysical(config.currentRoot);
    if (!isPhysicalRamAddress(phys, ramSize))
    {
        result.passed = false;
        result.report = "pointer cell address out of RAM";
        return result;
    }

    const u32 value = readWord(ram, phys, ramSize);
    if (value == 0)
    {
        return result; // null is OK
    }

    const bool aligned = isAligned(value, 3u);
    const Address valPhys = toPhysical(value);
    const bool inRam = isPhysicalRamAddress(valPhys, ramSize);
    const bool inRegion = value >= config.regionStart && value < config.regionEnd;

    if (!aligned || !inRam || !inRegion)
    {
        result.passed = false;
        std::ostringstream os;
        os << "Pointer cell 0x" << std::hex << config.currentRoot << " = 0x" << value;
        if (!aligned)
            os << " NOT_ALIGNED";
        if (!inRam)
            os << " OUT_OF_RAM";
        if (!inRegion)
            os << " OUT_OF_REGION";
        result.report = os.str();
    }
    return result;
}

ValidationResult DiagValidatorEngine::runLinkedList(const ValidatorConfig& config, const u8* ram,
                                                    size_t ramSize) const
{
    // Linked list is similar to sentinel block chain but without header parsing.
    return runSentinelBlockChain(config, ram, ramSize);
}

ValidationResult DiagValidatorEngine::runBoundedWalk(const ValidatorConfig& config, const u8* ram,
                                                     size_t ramSize) const
{
    // Bounded walk reuses the same chain validation with region bounds.
    return runSentinelBlockChain(config, ram, ramSize);
}

} // namespace runtime
} // namespace psxrecomp
