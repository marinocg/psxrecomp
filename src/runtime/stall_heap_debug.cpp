#include "stall_heap_debug.h"

#include "psxrecomp/runtime/memory_map.h"
#include "psxrecomp/runtime/psx_system.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <sstream>
#include <string>
#include <unordered_set>

namespace psxrecomp
{
namespace runtime
{
namespace detail
{
namespace
{
constexpr Address AllocatorPcStart = 0x80011A58u;
constexpr Address AllocatorPcEndInclusive = 0x80011C88u;
constexpr std::array<Address, 3> AllocatorBoundaryFunctions = {
    0x80011A58u,
    0x80011C8Cu,
    0x80011CA0u,
};
constexpr Address AllocatorScanPointerGlobal = 0x800577BCu;
constexpr Address AllocatorBackupPointerGlobal = 0x80057F38u;
constexpr Address AllocatorActiveFlagGlobal = 0x800565B0u;
constexpr std::array<Address, 4> AllocatorGlobals = {
    0x80056598u,
    0x800565A0u,
    0x800565A8u,
    0x800565B0u,
};
constexpr u32 HeapSentinel = 0xFFFFFFFEu;
constexpr size_t HeapDumpPcWindow = 8;
constexpr size_t HeapDumpNodeLimit = 64;
constexpr u32 HeapHeaderFlagMask = 1u;
constexpr u32 HeapHeaderSizeMask = ~HeapHeaderFlagMask;

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

bool isRamAddress(Address address)
{
    const Address physical = address & 0x1FFFFFFFu;
    return physical < MemoryMap::RAM_SIZE;
}

bool isAlignedRamPointer(Address address)
{
    return (address & 0x3u) == 0u && address >= 0x80000000u && address < 0x80000000u + MemoryMap::RAM_SIZE;
}

u32 readRamWord(const PsxSystem& system, Address address)
{
    const Address physical = address & 0x1FFFFFFFu;
    if (physical + sizeof(u32) > MemoryMap::RAM_SIZE)
    {
        return 0;
    }

    const u8* ram = system.getRam();
    return static_cast<u32>(ram[physical + 0]) | (static_cast<u32>(ram[physical + 1]) << 8) |
           (static_cast<u32>(ram[physical + 2]) << 16) |
           (static_cast<u32>(ram[physical + 3]) << 24);
}

bool isAllocatorPc(Address pc)
{
    return pc >= AllocatorPcStart && pc <= AllocatorPcEndInclusive;
}

bool isAllocatorBoundaryAddress(Address address)
{
    const Address normalized = (address & 0x1FFFFFFFu) | 0x80000000u;
    return std::find(AllocatorBoundaryFunctions.begin(), AllocatorBoundaryFunctions.end(),
                     normalized) != AllocatorBoundaryFunctions.end();
}

std::string validateHeapChain(const PsxSystem& system, Address start, const char* label)
{
    std::ostringstream os;
    if (!isRamAddress(start))
    {
        os << label << ": start pointer 0x" << std::hex << start << " is out of RAM range\n";
        return os.str();
    }

    std::unordered_set<Address> visited;
    Address current = start;
    for (size_t index = 0; index < HeapDumpNodeLimit; ++index)
    {
        if (!visited.insert(current).second)
        {
            os << label << ": cycle at block 0x" << std::hex << current << "\n";
            return os.str();
        }
        if (!isRamAddress(current))
        {
            os << label << ": block 0x" << std::hex << current << " is out of RAM range\n";
            return os.str();
        }

        const u32 rawHeader = readRamWord(system, current);
        if (rawHeader == HeapSentinel)
        {
            return {};
        }

        const u32 sizeField = rawHeader & HeapHeaderSizeMask;
        const Address next = current + sizeField + sizeof(u32);
        if ((current & 0x3u) != 0)
        {
            os << label << ": block 0x" << std::hex << current << " is not 4-byte aligned\n";
            return os.str();
        }
        if ((sizeField & 0x3u) != 0)
        {
            os << label << ": header size not 4-byte aligned at block 0x" << std::hex
               << current << " raw=0x" << rawHeader << "\n";
            return os.str();
        }
        if (sizeField == 0 || sizeField > MemoryMap::RAM_SIZE)
        {
            os << label << ": header size absurd: 0x" << std::hex << sizeField
               << " at block 0x" << current << "\n";
            return os.str();
        }
        if (next <= current)
        {
            os << label << ": next pointer moved backwards at block 0x" << std::hex << current
               << " next=0x" << next << "\n";
            return os.str();
        }
        if (!isRamAddress(next))
        {
            os << label << ": next pointer out of RAM range at block 0x" << std::hex << current
               << " next=0x" << next << "\n";
            return os.str();
        }
        current = next;
    }

    os << label << ": missing sentinel within " << std::dec << HeapDumpNodeLimit << " nodes\n";
    return os.str();
}

std::string describeHeapChain(const PsxSystem& system, Address start, const char* label)
{
    std::ostringstream os;
    os << label << " = 0x" << std::hex << start << "\n";
    if (!isRamAddress(start))
    {
        os << "  out-of-range start pointer\n";
        return os.str();
    }

    std::unordered_set<Address> visited;
    Address current = start;
    for (size_t index = 0; index < HeapDumpNodeLimit; ++index)
    {
        if (!visited.insert(current).second)
        {
            os << "  cycle at block 0x" << std::hex << current << "\n";
            return os.str();
        }
        if (!isRamAddress(current))
        {
            os << "  block 0x" << std::hex << current << " is out of RAM range\n";
            return os.str();
        }

        const u32 rawHeader = readRamWord(system, current);
        if (rawHeader == HeapSentinel)
        {
            os << "  node[" << std::dec << index << "] addr=0x" << std::hex << current
               << " raw=0x" << rawHeader << " sentinel=yes\n";
            return os.str();
        }

        const u32 sizeField = rawHeader & HeapHeaderSizeMask;
        const bool freeBit = (rawHeader & HeapHeaderFlagMask) != 0;
        const Address next = current + sizeField + sizeof(u32);
        os << "  node[" << std::dec << index << "] addr=0x" << std::hex << current << " raw=0x"
           << rawHeader << " size=0x" << sizeField << " free=" << (freeBit ? 1 : 0)
           << " next=0x" << next << "\n";

        if ((sizeField & 0x3u) != 0)
        {
            os << "    header size not 4-byte aligned\n";
            return os.str();
        }
        if (sizeField == 0 || sizeField > MemoryMap::RAM_SIZE)
        {
            os << "    header size absurd: 0x" << std::hex << sizeField << "\n";
            return os.str();
        }
        if (next <= current)
        {
            os << "    next pointer moved backwards\n";
            return os.str();
        }
        if (!isRamAddress(next))
        {
            os << "    next pointer out of RAM range\n";
            return os.str();
        }
        current = next;
    }

    os << "  missing sentinel within " << std::dec << HeapDumpNodeLimit << " nodes\n";
    return os.str();
}

} // namespace

bool shouldDumpAllocatorHeap(const PsxSystem* system,
                             const RingBuffer<Address, StallClassifier::PC_RING_SIZE>& pcRing)
{
    if (system == nullptr)
    {
        return false;
    }

    if (const char* env = std::getenv("PSXRECOMP_STALL_HEAP_DUMP"))
    {
        if (hasEnvValue(env, {"1", "true", "yes", "on", "heap"}))
        {
            return true;
        }
        if (hasEnvValue(env, {"0", "false", "no", "off"}))
        {
            return false;
        }
    }

    const size_t window = std::min<size_t>(pcRing.count(), HeapDumpPcWindow);
    for (size_t i = 0; i < window; ++i)
    {
        if (isAllocatorPc(pcRing.recent(i)))
        {
            return true;
        }
    }
    return false;
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

AllocatorHeapState classifyAllocatorHeapState(const PsxSystem& system)
{
    const u32 activeFlag = readRamWord(system, AllocatorActiveFlagGlobal);
    const u32 scanPointer = readRamWord(system, AllocatorScanPointerGlobal);
    const u32 backupPointer = readRamWord(system, AllocatorBackupPointerGlobal);
    const bool scanSane = isAlignedRamPointer(scanPointer);
    const bool backupSane = isAlignedRamPointer(backupPointer);

    if (activeFlag == 0)
    {
        if (scanPointer == 0 && backupPointer == 0)
        {
            return AllocatorHeapState::Uninitialized;
        }
        if ((scanPointer == 0 || !scanSane) && (backupPointer == 0 || !backupSane))
        {
            return AllocatorHeapState::Uninitialized;
        }
    }

    if (activeFlag != 0 || (scanSane && backupSane))
    {
        return AllocatorHeapState::Active;
    }

    return AllocatorHeapState::Suspicious;
}

const char* allocatorHeapStateLabel(AllocatorHeapState state)
{
    switch (state)
    {
    case AllocatorHeapState::Uninitialized:
        return "allocator_not_initialized";
    case AllocatorHeapState::Active:
        return "active";
    case AllocatorHeapState::Suspicious:
        return "suspicious";
    }
    return "unknown";
}

std::string formatAllocatorHeapDump(const PsxSystem& system)
{
    std::ostringstream os;
    os << "Allocator heap dump (0x80011A58 range)\n";
    os << "Allocator globals:\n";
    for (Address address : AllocatorGlobals)
    {
        os << "  [0x" << std::hex << address << "] = 0x" << readRamWord(system, address)
           << "\n";
    }

    const u32 scanPointer = readRamWord(system, AllocatorScanPointerGlobal);
    const u32 backupPointer = readRamWord(system, AllocatorBackupPointerGlobal);
    os << "  [0x" << std::hex << AllocatorScanPointerGlobal << "] current scan pointer = 0x"
       << scanPointer << "\n";
    os << "  [0x" << std::hex << AllocatorBackupPointerGlobal << "] backup pointer = 0x"
       << backupPointer << "\n";
    os << describeHeapChain(system, scanPointer, "Heap chain from current scan pointer");
    if (backupPointer != scanPointer)
    {
        os << describeHeapChain(system, backupPointer, "Heap chain from backup pointer");
    }
    return os.str();
}

bool isAllocatorBoundaryFunction(Address address)
{
    return isAllocatorBoundaryAddress(address);
}

std::string validateAllocatorHeap(const PsxSystem& system)
{
    std::ostringstream issues;

    const u32 scanPointer = readRamWord(system, AllocatorScanPointerGlobal);
    const u32 backupPointer = readRamWord(system, AllocatorBackupPointerGlobal);
    issues << validateHeapChain(system, scanPointer, "current scan pointer chain");
    if (backupPointer != scanPointer)
    {
        issues << validateHeapChain(system, backupPointer, "backup pointer chain");
    }

    return issues.str();
}

} // namespace detail
} // namespace runtime
} // namespace psxrecomp
