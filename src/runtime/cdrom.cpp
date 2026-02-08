#include "psxrecomp/runtime/cdrom.h"

namespace psxrecomp
{
namespace runtime
{

void Cdrom::reset()
{
    m_status = 0;
    m_params.clear();
    m_lastDmaWord = 0;
}

u8 Cdrom::readStatus() const
{
    return m_status;
}

void Cdrom::writeCommand(u8 value)
{
    m_status = value;
}

void Cdrom::writeParam(u8 value)
{
    if (m_params.size() < MAX_PARAMS)
    {
        m_params.push_back(value);
    }
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
