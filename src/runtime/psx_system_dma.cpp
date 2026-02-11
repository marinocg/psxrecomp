#include "psxrecomp/runtime/psx_system.h"

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

u32 normalTransferWordCount(const DmaChannel& channel, u32 syncMode)
{
    const u32 wordsPerBlock = channel.blockControl & 0xFFFF;
    if (syncMode == DMA_REQUEST_MODE)
    {
        const u32 blockCount = (channel.blockControl >> 16) & 0xFFFF;
        return wordsPerBlock * blockCount;
    }
    return wordsPerBlock;
}

} // namespace

void PsxSystem::handleDmaTransfer(DmaPort port)
{
    const auto& channel = m_dma.channel(port);
    const bool fromRam = (channel.channelControl & DMA_DIRECTION_FROM_RAM) != 0;

    u32 transferredWords = 0;
    if (!fromRam)
    {
        if (port != DmaPort::Gpu)
        {
            m_dma.clearTrigger(port);
            return;
        }

        const u32 syncMode = (channel.channelControl >> DMA_SYNC_MODE_SHIFT) & DMA_SYNC_MODE_MASK;
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

        const Address base = channel.baseAddress & 0x1FFFFC;
        const bool decrementAddress = (channel.channelControl & DMA_ADDRESS_DECREMENT) != 0;
        Address current = base;
        for (u32 i = 0; i < wordCount; ++i)
        {
            write<u32>(current, m_gpu.readData());
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
    else
    {
        const u32 syncMode = (channel.channelControl >> DMA_SYNC_MODE_SHIFT) & DMA_SYNC_MODE_MASK;

        if (port == DmaPort::Gpu && syncMode == DMA_LINKED_LIST_MODE)
        {
            Address nodeAddress = channel.baseAddress & 0x1FFFFC;
            for (u32 nodeCount = 0; nodeCount < 0x2000; ++nodeCount)
            {
                const u32 header = read<u32>(nodeAddress);
                const u32 commandCount = (header >> 24) & 0xFF;
                for (u32 i = 0; i < commandCount; ++i)
                {
                    m_gpu.writeDma(read<u32>(nodeAddress + (i + 1) * sizeof(u32)));
                }
                transferredWords += commandCount;

                const u32 nextAddress = header & DMA_LIST_END_MARKER;
                if (nextAddress == DMA_LIST_END_MARKER)
                {
                    break;
                }
                nodeAddress = nextAddress & 0x1FFFFC;
            }
        }
        else
        {
            const u32 wordCount = normalTransferWordCount(channel, syncMode);
            if (wordCount == 0)
            {
                m_dma.clearTrigger(port);
                return;
            }

            const Address base = channel.baseAddress & 0x1FFFFC;
            const bool decrementAddress = (channel.channelControl & DMA_ADDRESS_DECREMENT) != 0;
            Address current = base;
            for (u32 i = 0; i < wordCount; ++i)
            {
                const u32 value = read<u32>(current);
                switch (port)
                {
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

    m_dma.clearTrigger(port);
    m_interrupts.raise(InterruptLine::Dma);
    m_debugOverlay.incrementDmaTransfers();
    m_debugOverlay.incrementInterruptsRaised();

    std::ostringstream message;
    message << "DMA transfer on port " << static_cast<int>(port) << " words=" << transferredWords;
    m_logger.log(LogLevel::Debug, "dma", message.str());
}

} // namespace runtime
} // namespace psxrecomp
