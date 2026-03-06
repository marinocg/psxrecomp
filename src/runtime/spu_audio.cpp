#include "psxrecomp/runtime/spu.h"

namespace psxrecomp
{
namespace runtime
{

namespace
{
constexpr size_t kCdAudioRingCapacity = Spu::SamplesPerTick * 2u * 32u;
}

void Spu::pushCdAudioSamples(const std::vector<int16_t>& interleavedStereoPcm)
{
    if (interleavedStereoPcm.empty())
    {
        return;
    }

    for (int16_t sample : interleavedStereoPcm)
    {
        if (m_cdAudioRing.size() >= kCdAudioRingCapacity)
        {
            m_cdAudioRing.pop_front();
        }
        m_cdAudioRing.push_back(sample);
    }
}

size_t Spu::queuedCdAudioSamples() const
{
    return m_cdAudioRing.size();
}

void Spu::setAudioBackend(std::shared_ptr<SpuAudioBackend> backend)
{
    if (backend)
    {
        m_audioBackend = std::move(backend);
    }
}

void Spu::mixQueuedSamples()
{
    if (!m_audioBackend || m_mixedAudioBuffer.empty())
    {
        return;
    }

    m_audioBackend->submitSamples(m_mixedAudioBuffer);
}

float Spu::normalizedSignedVolume(u16 value)
{
    const int16_t signedValue = static_cast<int16_t>(value);
    return static_cast<float>(signedValue) / 32767.0f;
}

} // namespace runtime
} // namespace psxrecomp
