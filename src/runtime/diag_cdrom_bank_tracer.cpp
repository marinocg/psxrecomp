#include "psxrecomp/runtime/diag_cdrom_bank_tracer.h"

#include <sstream>

namespace psxrecomp
{
namespace runtime
{

void DiagCdromBankTracer::enable()
{
    m_enabled = true;
}

bool DiagCdromBankTracer::isEnabled() const
{
    return m_enabled;
}

void DiagCdromBankTracer::reset()
{
    m_bankSelectWriteCount = 0;
    m_lastBankSelectWrite = 0;
    m_statusReadCount = 0;
    m_lastStatusRead = 0;
    m_commandWriteCount = 0;
    m_lastCommandWrite = 0;
    m_soundMapWriteCount = 0;
    m_hintStsReadCount = 0;
    m_lastHintStsRead = 0;
    m_responseFifoReadCount = 0;
    m_lastResponseFifoRead = 0;
    m_paramPushCount = 0;
    m_hintMskWriteCount = 0;
    m_lastHintMskWrite = 0;
    m_audioVolWriteCount = 0;
    m_hintMskReadCount = 0;
    m_lastHintMskRead = 0;
    m_requestWriteCount = 0;
    m_lastRequestWrite = 0;
    m_hclrctlWriteCount = 0;
    m_lastHclrctlWrite = 0;
    m_audioVol3WriteCount = 0;
    m_hintSts3ReadCount = 0;
    m_lastHintSts3Read = 0;
    m_dma3TriggerCount = 0;
    m_dma0WriteCount = 0;
    m_dma1WriteCount = 0;
    m_sawInt1 = false;
    m_sawInt3 = false;
    m_lastBankSelected = 0;
}

void DiagCdromBankTracer::recordWrite(u8 offset, u8 bank, u8 value)
{
    switch (offset & 0x3u)
    {
    case 0: // bank select
        ++m_bankSelectWriteCount;
        m_lastBankSelectWrite = value;
        m_lastBankSelected = value & 0x3u;
        break;
    case 1:
        if (bank == 0)
        {
            ++m_commandWriteCount;
            m_lastCommandWrite = value;
        }
        else
        {
            ++m_soundMapWriteCount;
        }
        break;
    case 2:
        if (bank == 0)
        {
            ++m_paramPushCount;
        }
        else if (bank == 1)
        {
            ++m_hintMskWriteCount;
            m_lastHintMskWrite = value;
        }
        else
        {
            ++m_audioVolWriteCount;
        }
        break;
    case 3:
        if (bank == 0)
        {
            ++m_requestWriteCount;
            m_lastRequestWrite = value;
            if ((value & 0x80u) != 0)
            {
                ++m_dma3TriggerCount; // BFRD bit
            }
        }
        else if (bank == 1)
        {
            ++m_hclrctlWriteCount;
            m_lastHclrctlWrite = value;
        }
        else
        {
            ++m_audioVol3WriteCount;
        }
        break;
    default:
        break;
    }
}

void DiagCdromBankTracer::recordRead(u8 offset, u8 bank, u8 value)
{
    switch (offset & 0x3u)
    {
    case 0: // status (always)
        ++m_statusReadCount;
        m_lastStatusRead = value;
        break;
    case 1:
        // reads: bank 1/3 → HINTSTS, bank 0/2 → RESPONSE FIFO
        if ((bank & 0x1u) == 1)
        {
            ++m_hintStsReadCount;
            m_lastHintStsRead = value;
            const u8 intType = value & 0x07u;
            if (intType == 1)
            {
                m_sawInt1 = true;
            }
            else if (intType == 3)
            {
                m_sawInt3 = true;
            }
        }
        else
        {
            ++m_responseFifoReadCount;
            m_lastResponseFifoRead = value;
        }
        break;
    case 2:
        // reads: bank 0/2 → HINTMSK, bank 1/3 → HINTSTS
        if ((bank & 0x1u) == 0)
        {
            ++m_hintMskReadCount;
            m_lastHintMskRead = value;
        }
        else
        {
            // treated as HINTSTS alternate read
            ++m_hintStsReadCount;
            m_lastHintStsRead = value;
            const u8 intType = value & 0x07u;
            if (intType == 1)
            {
                m_sawInt1 = true;
            }
            else if (intType == 3)
            {
                m_sawInt3 = true;
            }
        }
        break;
    case 3:
        // reads: bank 0/2 → HINTMSK, bank 1/3 → HINTSTS
        if ((bank & 0x1u) == 0)
        {
            ++m_hintMskReadCount;
            m_lastHintMskRead = value;
        }
        else
        {
            ++m_hintSts3ReadCount;
            m_lastHintSts3Read = value;
            const u8 intType = value & 0x07u;
            if (intType == 1)
            {
                m_sawInt1 = true;
            }
            else if (intType == 3)
            {
                m_sawInt3 = true;
            }
        }
        break;
    default:
        break;
    }
}

std::string DiagCdromBankTracer::formatSummary() const
{
    std::ostringstream os;
    os << "=== CDROM Bank-Aware Register Trace ===\n";

    os << "last_bank=" << static_cast<int>(m_lastBankSelected);
    os << " status_reads=" << m_statusReadCount;
    if (m_statusReadCount > 0)
    {
        os << " last_status=0x" << std::hex << static_cast<int>(m_lastStatusRead) << std::dec;
    }
    os << "\n";

    // Offset-0: bank select
    os << "BANK_SEL writes=" << m_bankSelectWriteCount;
    if (m_bankSelectWriteCount > 0)
    {
        os << " last=0x" << std::hex << static_cast<int>(m_lastBankSelectWrite) << std::dec;
    }
    os << "\n";

    // Offset-1 writes
    os << "COMMAND  writes=" << m_commandWriteCount;
    if (m_commandWriteCount > 0)
    {
        os << " last=0x" << std::hex << static_cast<int>(m_lastCommandWrite) << std::dec;
    }
    os << "  SOUND_MAP writes=" << m_soundMapWriteCount << "\n";

    // Offset-1 reads
    os << "HINTSTS  reads=" << m_hintStsReadCount;
    if (m_hintStsReadCount > 0)
    {
        os << " last=0x" << std::hex << static_cast<int>(m_lastHintStsRead) << std::dec;
    }
    os << "  RESULT_FIFO reads=" << m_responseFifoReadCount;
    if (m_responseFifoReadCount > 0)
    {
        os << " last=0x" << std::hex << static_cast<int>(m_lastResponseFifoRead) << std::dec;
    }
    os << "\n";

    // INT observation
    os << "INT1_observed=" << (m_sawInt1 ? "yes" : "no")
       << "  INT3_observed=" << (m_sawInt3 ? "yes" : "no") << "\n";

    // Offset-2 writes
    os << "PARAM    writes=" << m_paramPushCount << "  HINTMSK writes=" << m_hintMskWriteCount;
    if (m_hintMskWriteCount > 0)
    {
        os << " last=0x" << std::hex << static_cast<int>(m_lastHintMskWrite) << std::dec;
    }
    os << "  audio_vol writes=" << m_audioVolWriteCount << "\n";

    // Offset-2/3 reads
    os << "HINTMSK  reads=" << m_hintMskReadCount;
    if (m_hintMskReadCount > 0)
    {
        os << " last=0x" << std::hex << static_cast<int>(m_lastHintMskRead) << std::dec;
    }
    os << "\n";

    // Offset-3 writes
    os << "REQUEST  writes=" << m_requestWriteCount;
    if (m_requestWriteCount > 0)
    {
        os << " last=0x" << std::hex << static_cast<int>(m_lastRequestWrite) << std::dec;
    }
    os << "  HCLRCTL writes=" << m_hclrctlWriteCount;
    if (m_hclrctlWriteCount > 0)
    {
        os << " last=0x" << std::hex << static_cast<int>(m_lastHclrctlWrite) << std::dec;
    }
    os << "  audio_vol3 writes=" << m_audioVol3WriteCount << "\n";

    // DMA-related
    os << "BFRD_set count=" << m_dma3TriggerCount << "  (last REQUEST with bit7 set)\n";

    if (m_hintSts3ReadCount > 0)
    {
        os << "HINTSTS(off3) reads=" << m_hintSts3ReadCount << " last=0x" << std::hex
           << static_cast<int>(m_lastHintSts3Read) << std::dec << "\n";
    }

    return os.str();
}

} // namespace runtime
} // namespace psxrecomp
