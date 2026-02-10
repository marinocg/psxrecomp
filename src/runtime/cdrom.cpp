#include "psxrecomp/runtime/cdrom.h"

namespace psxrecomp
{
namespace runtime
{

void Cdrom::reset()
{
    m_status = 0;
    m_params.clear();
    m_responses.clear();
    m_interruptFlags = 0;
    m_interruptEnable = 0;
    m_lastDmaWord = 0;
}

u8 Cdrom::readStatus() const
{
    return m_status;
}

u8 Cdrom::readData()
{
    if (m_responses.empty())
    {
        return 0;
    }

    u8 value = m_responses.front();
    m_responses.pop_front();
    return value;
}

u8 Cdrom::readInterruptFlags() const
{
    return m_interruptFlags;
}

u8 Cdrom::readInterruptEnable() const
{
    return m_interruptEnable;
}

void Cdrom::writeCommand(u8 value)
{
    m_status = value;
    if (m_responses.size() < RESPONSE_CAPACITY)
    {
        m_responses.push_back(value);
    }
    m_interruptFlags |= 0x1;
}

void Cdrom::writeParam(u8 value)
{
    if (m_params.size() < MAX_PARAMS)
    {
        m_params.push_back(value);
    }
}

void Cdrom::writeInterruptFlags(u8 value)
{
    m_interruptFlags &= static_cast<u8>(~value);
}

void Cdrom::writeInterruptEnable(u8 value)
{
    m_interruptEnable = value;
}

void Cdrom::writeDma(u32 value)
{
    m_lastDmaWord = value;
}

u32 Cdrom::lastDmaWord() const
{
    return m_lastDmaWord;
}

} // namespace runtime
} // namespace psxrecomp
