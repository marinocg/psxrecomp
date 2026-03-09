#include "stall_heap_debug.h"

#include "psxrecomp/runtime/psx_system.h"

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

bool hasEnvValue(const char* value, std::initializer_list<const char*> candidates)
{
    if (value == nullptr)
    {
        return false;
    }

    std::string text(value);
    for (char& ch : text)
    {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    for (const char* candidate : candidates)
    {
        if (text == candidate)
        {
            return true;
        }
    }
    return false;
}

} // namespace

bool shouldDumpAllocatorHeap(const PsxSystem* system,
                             const RingBuffer<Address, StallClassifier::PC_RING_SIZE>& /*pcRing*/)
{
    if (system == nullptr)
    {
        return false;
    }

    // Check env var override.
    if (const char* env = std::getenv("PSXRECOMP_STALL_HEAP_DUMP"))
    {
        if (hasEnvValue(env, {"1", "true", "yes", "on", "heap"}))
        {
            return system->diagProfile().isLoaded() &&
                   system->diagValidators().validatorCount() > 0;
        }
        if (hasEnvValue(env, {"0", "false", "no", "off"}))
        {
            return false;
        }
    }

    // Default: dump if any validators are configured in the profile.
    return system->diagProfile().isLoaded() && system->diagValidators().validatorCount() > 0;
}

std::string formatAllocatorHeapDump(const PsxSystem& system)
{
    if (!system.diagProfile().isLoaded() || system.diagValidators().validatorCount() == 0)
    {
        return {};
    }

    const u8* ram = system.getRam();
    const size_t ramSize = MemoryMap::RAM_SIZE;
    auto results = system.diagValidators().runAll(ram, ramSize);

    std::ostringstream os;
    os << "Validator heap dump (profile-driven)\n";
    for (const auto& result : results)
    {
        os << "  validator=" << result.validatorName
           << " result=" << (result.passed ? "passed" : "FAILED") << "\n";
        if (!result.report.empty())
        {
            os << result.report;
        }
    }
    return os.str();
}

bool fastHeapValidationEnabled()
{
    if (const char* env = std::getenv("PSXRECOMP_HEAP_VALIDATE"))
    {
        if (hasEnvValue(env, {"1", "true", "yes", "on", "heap", "fast"}))
        {
            return true;
        }
        if (hasEnvValue(env, {"0", "false", "no", "off"}))
        {
            return false;
        }
    }
    return false;
}

} // namespace detail
} // namespace runtime
} // namespace psxrecomp
