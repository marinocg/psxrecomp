#include "psxrecomp/runtime/psx_system.h"

#include <cstdint>
#include <sstream>

namespace psxrecomp
{
namespace runtime
{

namespace
{
constexpr u32 DMA_DIRECTION_FROM_RAM = 0x00000001;
constexpr u32 DMA_ADDRESS_DECREMENT = 0x00000002;
constexpr u32 DMA_SYNC_MODE_SHIFT = 9;
constexpr u32 DMA_SYNC_MODE_MASK = 0x3;
constexpr u32 DMA_REQUEST_MODE = 0x1;
constexpr u32 DMA_LINKED_LIST_MODE = 0x2;
constexpr u32 DMA_LIST_END_MARKER = 0x00FFFFFF;
constexpr u32 DMA_MAX_TRANSFER_WORDS = 0x200000u;

const char* dmaPortName(DmaPort port)
{
    switch (port)
    {
    case DmaPort::MdecIn:
        return "MDECin";
    case DmaPort::MdecOut:
        return "MDECout";
    case DmaPort::Gpu:
        return "GPU";
    case DmaPort::Cdrom:
        return "CDROM";
    case DmaPort::Spu:
        return "SPU";
    case DmaPort::Pio:
        return "PIO";
    case DmaPort::Otc:
        return "OTC";
    default:
        return "Unknown";
    }
}

u32 normalTransferWordCount(const DmaChannel& channel, u32 syncMode)
{
    const u32 wordsPerBlock = channel.blockControl & 0xFFFF;
    uint64_t totalWords = wordsPerBlock;
    if (syncMode == DMA_REQUEST_MODE)
    {
        const u32 blockCount = (channel.blockControl >> 16) & 0xFFFF;
        totalWords = static_cast<uint64_t>(wordsPerBlock) * blockCount;
    }

    if (totalWords > DMA_MAX_TRANSFER_WORDS)
    {
        return DMA_MAX_TRANSFER_WORDS;
    }
    return static_cast<u32>(totalWords);
}

} // namespace

void PsxSystem::handleDmaTransfer(DmaPort port)
{
    const auto& channel = m_dma.channel(port);
    const bool fromRam = (channel.channelControl & DMA_DIRECTION_FROM_RAM) != 0;

    m_stallClassifier.recordDmaTrigger(static_cast<u8>(port), channel.baseAddress,
                                       channel.blockControl);

    m_logger.log(LogLevel::Info, "dma",
                 "DMA transfer triggered: port=" + std::to_string(static_cast<int>(port)) +
                 " fromRam=" + std::to_string(fromRam) +
                 " base=0x" + ([&]{ std::ostringstream s; s << std::hex << channel.baseAddress; return s.str(); })() +
                 " block=0x" + ([&]{ std::ostringstream s; s << std::hex << channel.blockControl; return s.str(); })() +
                 " ctrl=0x" + ([&]{ std::ostringstream s; s << std::hex << channel.channelControl; return s.str(); })());

    u32 transferredWords = 0;
    if (!fromRam)
    {
        const u32 syncMode = (channel.channelControl >> DMA_SYNC_MODE_SHIFT) & DMA_SYNC_MODE_MASK;
        if (port == DmaPort::Otc)
        {
            const u32 wordCount = normalTransferWordCount(channel, syncMode);
            if (wordCount == 0)
            {
                m_dma.clearTrigger(port);
                return;
            }

            Address current = channel.baseAddress & 0x1FFFFC;
            for (u32 i = 0; i < wordCount; ++i)
            {
                const bool last = (i + 1) == wordCount;
                const u32 next = last ? DMA_LIST_END_MARKER
                                      : static_cast<u32>(((current - sizeof(u32)) & 0x1FFFFC) &
                                                         DMA_LIST_END_MARKER);
                write<u32>(current, next);
                current = (current - sizeof(u32)) & 0x1FFFFC;
            }

            m_dma.clearTrigger(port);
            m_dma.notifyTransferComplete(port);
            m_debugOverlay.incrementDmaTransfers();
            return;
        }

        if (syncMode == DMA_LINKED_LIST_MODE)
        {
            m_dma.clearTrigger(port);
            return;
        }

        const u32 wordCount = normalTransferWordCount(channel, syncMode);
        if (wordCount == 0)
        {
            m_dma.clearTrigger(port);
            return;
        }
        if (port == DmaPort::Spu && !m_spu.canTransferDma(false))
        {
            transferredWords = 0;
            goto dma_transfer_complete;
        }
        // Gate DMA3 (CD-ROM→RAM) on DRQSTS (STATUS_DATA_READY, bit 6 of HSTS).
        // Per PSX-SPX, DMA3 only transfers when BFRD=1 and the data FIFO has
        // bytes to serve.  Initiating a transfer before BFRD=1 yields no data;
        // mirroring the hardware stall with an immediate skip is correct here.
        if (port == DmaPort::Cdrom &&
            (m_cdrom.readStatus() & 0x40u) == 0u)
        {
            transferredWords = 0;
            goto dma_transfer_complete;
        }

        const Address base = channel.baseAddress & 0x1FFFFC;
        const bool decrementAddress = (channel.channelControl & DMA_ADDRESS_DECREMENT) != 0;
        Address current = base;
        for (u32 i = 0; i < wordCount; ++i)
        {
            u32 value = 0;
            switch (port)
            {
            case DmaPort::MdecOut:
                value = m_mdec.readDma();
                break;
            case DmaPort::Gpu:
                value = m_gpu.readData();
                break;
            case DmaPort::Cdrom:
                value = m_cdrom.readDma();
                break;
            case DmaPort::Spu:
                value = m_spu.readDma();
                break;
            default:
                m_dma.clearTrigger(port);
                return;
            }

            write<u32>(current, value);
            if (decrementAddress)
            {
                current = (current - sizeof(u32)) & 0x1FFFFC;
            }
            else
            {
                current = (current + sizeof(u32)) & 0x1FFFFC;
            }
        }

        transferredWords = wordCount;

        std::ostringstream detail;
        detail << "port=" << dmaPortName(port) << " sync=" << syncMode << " words=" << wordCount
               << " base=0x" << std::hex << base;
        const u32 requestedBytes = wordCount * sizeof(u32);
        const bool wraps = requestedBytes > (MemoryMap::RAM_SIZE - base);
        if (wraps)
        {
            std::ostringstream warn;
            warn << "DMA to RAM wraps base=0x" << std::hex << (0x80000000u | base)
                 << " bytes=" << std::dec << requestedBytes << " port=" << dmaPortName(port);
            m_logger.log(LogLevel::Warn, "load", warn.str());
        }
        m_stallClassifier.recordRamCopyProvenance(std::string("DMA:") + dmaPortName(port),
                                                  detail.str(), m_debugOverlay.lastProgramCounter(),
                                                  0x80000000u | base, requestedBytes,
                                                  requestedBytes, true, wraps, false);
    }
    else
    {
        const u32 syncMode = (channel.channelControl >> DMA_SYNC_MODE_SHIFT) & DMA_SYNC_MODE_MASK;

        if (port == DmaPort::Gpu && syncMode == DMA_LINKED_LIST_MODE)
        {
            Address nodeAddress = channel.baseAddress & 0x1FFFFC;
            u32 nodeTotal = 0;
            for (u32 nodeCount = 0; nodeCount < 0x2000; ++nodeCount)
            {
                const u32 header = read<u32>(nodeAddress);
                const u32 commandCount = (header >> 24) & 0xFF;
                // Debug: dump each node's words
                if (commandCount > 0)
                {
                    std::string wordsStr;
                    for (u32 i = 0; i < commandCount; ++i)
                    {
                        const Address commandAddress =
                            (nodeAddress + (i + 1) * sizeof(u32)) & 0x1FFFFC;
                        const u32 word = read<u32>(commandAddress);
                        char buf[20];
                        std::snprintf(buf, sizeof(buf), "0x%08x", word);
                        if (!wordsStr.empty())
                            wordsStr += ",";
                        wordsStr += buf;
                    }
                    char nodeBuf[64];
                    std::snprintf(nodeBuf, sizeof(nodeBuf), "0x%06x", nodeAddress);
                    m_logger.log(LogLevel::Info, "dma",
                                 std::string("DMA node@") + nodeBuf + " hdr=0x" +
                                 ([&]{ char h[12]; std::snprintf(h, sizeof(h), "%08x", header); return std::string(h); })() +
                                 " cmds=" + std::to_string(commandCount) + " [" + wordsStr + "]");
                }
                for (u32 i = 0; i < commandCount; ++i)
                {
                    const Address commandAddress = (nodeAddress + (i + 1) * sizeof(u32)) & 0x1FFFFC;
                    m_gpu.writeDma(read<u32>(commandAddress));
                }
                transferredWords += commandCount;
                ++nodeTotal;

                const u32 nextAddress = header & DMA_LIST_END_MARKER;
                if (nextAddress == DMA_LIST_END_MARKER)
                {
                    break;
                }
                nodeAddress = nextAddress & 0x1FFFFC;
            }
            m_logger.log(LogLevel::Info, "dma",
                         "GPU linked-list: nodes=" + std::to_string(nodeTotal) +
                             " words=" + std::to_string(transferredWords));
        }
        else
        {
            const u32 wordCount = normalTransferWordCount(channel, syncMode);
            if (wordCount == 0)
            {
                m_dma.clearTrigger(port);
                return;
            }
            if (port == DmaPort::Spu && !m_spu.canTransferDma(true))
            {
                transferredWords = 0;
                goto dma_transfer_complete;
            }

            const Address base = channel.baseAddress & 0x1FFFFC;
            const bool decrementAddress = (channel.channelControl & DMA_ADDRESS_DECREMENT) != 0;
            Address current = base;
            for (u32 i = 0; i < wordCount; ++i)
            {
                const u32 value = read<u32>(current);
                switch (port)
                {
                case DmaPort::MdecIn:
                    m_mdec.writeDma(value);
                    break;
                case DmaPort::Gpu:
                    m_gpu.writeDma(value);
                    break;
                case DmaPort::Spu:
                    m_spu.writeDma(value);
                    break;
                case DmaPort::Cdrom:
                    m_cdrom.writeDma(value);
                    break;
                default:
                    break;
                }

                if (decrementAddress)
                {
                    current = (current - sizeof(u32)) & 0x1FFFFC;
                }
                else
                {
                    current = (current + sizeof(u32)) & 0x1FFFFC;
                }
            }

            transferredWords = wordCount;
        }
    }

dma_transfer_complete:
    m_dma.clearTrigger(port);
    m_dma.notifyTransferComplete(port);
    m_debugOverlay.incrementDmaTransfers();

    if (port == DmaPort::Spu)
    {
        m_spu.noteDmaTransfer(fromRam, transferredWords);
        if (transferredWords > 0)
        {
            // libsnd-style callers wait on the SPU completion event after a
            // real DMA4 transfer completes, so synthesize the completion edge
            // asynchronously on the hardware tick path.
            m_pendingSpuDmaCompletion = true;
            m_pendingSpuDmaCompletionCycles = 2048;
        }
    }

    std::ostringstream message;
    message << "DMA transfer on port " << static_cast<int>(port) << " words=" << transferredWords;
    m_logger.log(LogLevel::Debug, "dma", message.str());
}

} // namespace runtime
} // namespace psxrecomp
