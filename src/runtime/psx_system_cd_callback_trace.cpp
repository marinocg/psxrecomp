#include "psxrecomp/runtime/psx_system.h"

#include <cstdlib>
#include <sstream>

namespace psxrecomp
{
namespace runtime
{

namespace
{
constexpr Address CrashCdCallbackLoadPc = 0x8003E734u;
constexpr Address CrashCdCallbackCallPc = 0x8003E73Cu;
constexpr Address CrashCdCallbackEntryPc = 0x8003EB50u;

const char* cdromCommandName(u8 command)
{
    switch (command)
    {
    case 0x00:
        return "None";
    case 0x01:
        return "CdlNop";
    case 0x0A:
        return "CdlInit";
    case 0x1C:
        return "CdlReset";
    default:
        return "Other";
    }
}

const char* eventStatusName(const KernelEvent* event)
{
    if (event == nullptr)
    {
        return "missing";
    }

    switch (event->status)
    {
    case EventStatus::Free:
        return "Free";
    case EventStatus::Disabled:
        return "Disabled";
    case EventStatus::Enabled:
        return "Enabled";
    case EventStatus::Delivered:
        return "Delivered";
    default:
        return "Unknown";
    }
}

const char* biosCdEventLabel(size_t index)
{
    switch (index)
    {
    case 0:
        return "ack";
    case 1:
        return "done";
    case 2:
        return "ready";
    case 3:
        return "end";
    case 4:
        return "error";
    default:
        return "?";
    }
}

bool traceCdCallbackEnabled()
{
    if (const char* env = std::getenv("PSXRECOMP_TRACE_CD_CALLBACK"))
    {
        return env[0] == '1';
    }
    return false;
}
} // namespace

void PsxSystem::traceCdCallbackProgramCounter(Address pc)
{
    if (!traceCdCallbackEnabled())
    {
        return;
    }

    std::ostringstream msg;
    if (pc == CrashCdCallbackLoadPc)
    {
        msg << "event=pc_trace phase=descriptor_load pc=0x" << std::hex << pc << " "
            << describeBiosCdromState();
        m_logger.log(LogLevel::Info, "cdcb_trace", msg.str());
        return;
    }

    if (pc == CrashCdCallbackCallPc)
    {
        msg << "event=pc_trace phase=jalr pc=0x" << std::hex << pc << " "
            << describeBiosCdromState();
        m_logger.log(LogLevel::Info, "cdcb_trace", msg.str());
        return;
    }

    if (pc == CrashCdCallbackEntryPc)
    {
        msg << "event=pc_trace phase=helper_entry pc=0x" << std::hex << pc << " "
            << describeBiosCdromState();
        m_logger.log(LogLevel::Info, "cdcb_trace", msg.str());
    }
}

std::string PsxSystem::describeBiosCdromState() const
{
    const Cdrom::DebugSnapshot snapshot = m_cdrom.debugSnapshot();
    const unsigned currentCommand = static_cast<unsigned>(snapshot.currentCommand);
    const unsigned interruptFlags = static_cast<unsigned>(snapshot.interruptFlags & 0x1Fu);
    const unsigned interruptEnable = static_cast<unsigned>(snapshot.interruptEnable & 0x1Fu);
    const unsigned status = static_cast<unsigned>(snapshot.status);
    const unsigned requestControl = static_cast<unsigned>(snapshot.requestControl);
    const unsigned mode = static_cast<unsigned>(snapshot.mode);

    std::ostringstream stream;
    stream << "cd={cmd=";
    stream << cdromCommandName(snapshot.currentCommand);
    stream << "(0x" << std::hex << currentCommand << ")";
    stream << " irq=0x" << interruptFlags;
    stream << " ien=0x" << interruptEnable;
    stream << " stat=0x" << status;
    stream << " req=0x" << requestControl;
    stream << " mode=0x" << mode;
    stream << std::dec << " cmdFifo=" << snapshot.commandFifoSize;
    stream << " resp=" << snapshot.responseFifoSize;
    stream << " data=" << snapshot.dataFifoSize;
    stream << " pendingIrq=" << snapshot.pendingIrqCount;
    stream << " motor=" << (snapshot.motorOn ? 1 : 0);
    stream << " read=" << (snapshot.readActive ? 1 : 0);
    stream << " seek=" << (snapshot.seekActive ? 1 : 0) << "}";

    stream << " bios={init=" << (m_biosCdrom.initialized ? 1 : 0);
    stream << " handleBase=0x" << std::hex << m_biosCdrom.handleStorageAddress;
    stream << " asyncResult=0x" << m_biosCdrom.asyncResultPtr << std::dec;
    stream << " asyncRemaining=" << m_biosCdrom.asyncReadCount;
    stream << " asyncRead=" << m_biosCdrom.asyncSectorsRead << "}";

    stream << " events=[";
    for (size_t index = 0; index < m_biosCdrom.eventHandles.size(); ++index)
    {
        if (index != 0)
        {
            stream << ", ";
        }
        const u32 handle = m_biosCdrom.eventHandles[index];
        stream << biosCdEventLabel(index) << ":0x" << std::hex << handle << std::dec << "/"
               << eventStatusName(m_events.getEvent(handle));
    }
    stream << "]";

    return stream.str();
}

} // namespace runtime
} // namespace psxrecomp
