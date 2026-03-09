#include "irq_trace_utils.h"
#include "psxrecomp/runtime/psx_system.h"
#include <sstream>
#include <utility>

#include <vector>

namespace
{
constexpr psxrecomp::u32 CYCLES_PER_FRAME = 564480;

void appendU32(std::vector<psxrecomp::u8>& out, psxrecomp::u32 value)
{
    out.push_back(static_cast<psxrecomp::u8>(value & 0xFF));
    out.push_back(static_cast<psxrecomp::u8>((value >> 8) & 0xFF));
    out.push_back(static_cast<psxrecomp::u8>((value >> 16) & 0xFF));
    out.push_back(static_cast<psxrecomp::u8>((value >> 24) & 0xFF));
}
} // namespace

namespace psxrecomp
{
namespace runtime
{

u32 PsxSystem::frameCount() const
{
    return m_frameCount;
}

u32 PsxSystem::advanceFrame()
{
    tickCpuCycles(CYCLES_PER_FRAME);
    return m_frameCount;
}

u8* PsxSystem::getRam()
{
    return m_ram.data();
}

const u8* PsxSystem::getRam() const
{
    return m_ram.data();
}

Gpu& PsxSystem::gpu()
{
    return m_gpu;
}

Spu& PsxSystem::spu()
{
    return m_spu;
}

Cdrom& PsxSystem::cdrom()
{
    return m_cdrom;
}

Mdec& PsxSystem::mdec()
{
    return m_mdec;
}

InputController& PsxSystem::input()
{
    return m_input;
}

Sio0& PsxSystem::sio0()
{
    return m_sio0;
}

DmaController& PsxSystem::dma()
{
    return m_dma;
}

InterruptController& PsxSystem::interrupts()
{
    return m_interrupts;
}

Scheduler& PsxSystem::scheduler()
{
    return m_scheduler;
}

RuntimeLogger& PsxSystem::logger()
{
    return m_logger;
}

RuntimeDebugOverlay& PsxSystem::debugOverlay()
{
    return m_debugOverlay;
}

TimerController& PsxSystem::timers()
{
    return m_timers;
}

Cop0& PsxSystem::cop0()
{
    return m_cop0;
}

Gte& PsxSystem::gte()
{
    return m_gte;
}

StallClassifier& PsxSystem::stallClassifier()
{
    return m_stallClassifier;
}

bool PsxSystem::loadDiagProfile(const std::string& path)
{
    const std::string resolved = path.empty() ? DiagProfile::resolveProfilePath() : path;
    if (resolved.empty())
    {
        return false;
    }

    if (!m_diagProfile.loadFromFile(resolved, &m_logger))
    {
        return false;
    }

    const auto& data = m_diagProfile.data();
    m_diagWatchpoints.configure(data.watchpoints, data.memoryMap);
    m_diagWatchpoints.mergeEnvWatchedRanges();
    m_diagTracepoints.configure(data.tracepoints);
    m_diagExplainers.configure(data.explainers);
    m_diagValidators.configure(data.validators);
    m_diagBoundaries.configure(data.boundaries, &m_diagValidators, &m_diagExplainers);
    m_diagMetadataWatch.configure(data.metadataWatches);
    return true;
}

const DiagProfile& PsxSystem::diagProfile() const
{
    return m_diagProfile;
}

DiagWatchpointEngine& PsxSystem::diagWatchpoints()
{
    return m_diagWatchpoints;
}

DiagTracepointEngine& PsxSystem::diagTracepoints()
{
    return m_diagTracepoints;
}

DiagValidatorEngine& PsxSystem::diagValidators()
{
    return m_diagValidators;
}

const DiagValidatorEngine& PsxSystem::diagValidators() const
{
    return m_diagValidators;
}

DiagBoundaryDispatcher& PsxSystem::diagBoundaries()
{
    return m_diagBoundaries;
}

DiagExplainerEngine& PsxSystem::diagExplainers()
{
    return m_diagExplainers;
}

DiagMetadataWatchEngine& PsxSystem::diagMetadataWatch()
{
    return m_diagMetadataWatch;
}

void PsxSystem::setDisc(std::shared_ptr<Disc> disc)
{
    m_disc = std::move(disc);
    m_cdrom.setDiscBackend(m_disc.get());
    m_biosFt.setDisc(m_disc.get());
    if (m_cpuCycles == 0)
    {
        m_cdrom.primeBootState(m_disc != nullptr);
    }
}

KernelEventTable& PsxSystem::events()
{
    return m_events;
}

InterruptDispatcher& PsxSystem::dispatcher()
{
    return m_dispatcher;
}

void PsxSystem::setDiscSwapInfo(DiscSwapInfo info)
{
    if (!m_discSwapInfoInitialized)
    {
        m_discSwapInfo = std::move(info);
        m_discSwapInfoInitialized = true;
        return;
    }

    const bool activeChanged = info.activeDiscIndex != m_discSwapInfo.activeDiscIndex;
    const bool discCountChanged = info.discs.size() != m_discSwapInfo.discs.size();
    m_discSwapInfo = std::move(info);
    if (activeChanged || discCountChanged)
    {
        m_cdrom.notifyDiscSwap();
    }
}

const PsxSystem::DiscSwapInfo& PsxSystem::discSwapInfo() const
{
    return m_discSwapInfo;
}

std::vector<u8> PsxSystem::dumpRam() const
{
    return m_ram;
}

std::vector<u8> PsxSystem::dumpVram() const
{
    std::vector<u8> bytes;
    const auto& words = m_gpu.vramWords();
    bytes.reserve(words.size() * sizeof(u32));
    for (u32 value : words)
    {
        appendU32(bytes, value);
    }
    return bytes;
}

std::vector<u8> PsxSystem::dumpSpuRam() const
{
    std::vector<u8> bytes;
    const auto& words = m_spu.ramWords();
    bytes.reserve(words.size() * sizeof(u32));
    for (u32 value : words)
    {
        appendU32(bytes, value);
    }
    return bytes;
}

} // namespace runtime
} // namespace psxrecomp
