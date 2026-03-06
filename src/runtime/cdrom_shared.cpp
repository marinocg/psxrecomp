#include "cdrom_shared.h"

#include <algorithm>
#include <array>

namespace psxrecomp
{
namespace runtime
{
namespace cdrom_detail
{

namespace
{
constexpr size_t XA_SOUND_GROUP_BYTES = 128;
constexpr size_t XA_SOUND_GROUP_COUNT = 18;
constexpr size_t XA_SOUND_SAMPLE_BYTES_OFFSET = 16;
constexpr std::array<int, 5> XA_FILTER_POS = {0, 60, 115, 98, 122};
constexpr std::array<int, 5> XA_FILTER_NEG = {0, 0, -52, -55, -60};

int16_t clampI16(int value)
{
    return static_cast<int16_t>(std::clamp(value, -32768, 32767));
}

int decodeXaNibble(int nibble, int shift, int filter, int& old, int& older)
{
    int sample = nibble;
    if ((sample & 0x8) != 0)
    {
        sample -= 16;
    }

    int shifted = sample << shift;
    const int predicted =
        ((old * XA_FILTER_POS[filter]) + (older * XA_FILTER_NEG[filter]) + 32) / 64;
    shifted += predicted;
    shifted = std::clamp(shifted, -32768, 32767);
    older = old;
    old = shifted;
    return shifted;
}

void decodeXa28Nibbles(const u8* soundGroup, int block, int nibbleSelect, int& old, int& older,
                       std::vector<int16_t>& out)
{
    const u8 blockAttr = soundGroup[4 + block * 2 + nibbleSelect];
    int shiftNibble = blockAttr & 0x0F;
    if (shiftNibble > 12)
    {
        shiftNibble = 9;
    }
    const int shift = 12 - shiftNibble;
    int filter = (blockAttr >> 4) & 0x03;
    if (filter < 0 || filter > 4)
    {
        filter = 0;
    }

    for (int i = 0; i < 28; ++i)
    {
        const u8 packed = soundGroup[XA_SOUND_SAMPLE_BYTES_OFFSET + block + i * 4];
        const int nibble = (packed >> (nibbleSelect * 4)) & 0x0F;
        out.push_back(clampI16(decodeXaNibble(nibble, shift, filter, old, older)));
    }
}
} // namespace

u8 bcdToInt(u8 value)
{
    return static_cast<u8>(((value >> 4) & 0x0F) * 10 + (value & 0x0F));
}

u8 intToBcd(u8 value)
{
    return static_cast<u8>(((value / 10u) << 4) | (value % 10u));
}

u32 msfToLba(u8 minute, u8 second, u8 frame)
{
    const u32 absoluteFrames =
        (static_cast<u32>(bcdToInt(minute)) * 60u + bcdToInt(second)) * 75u + bcdToInt(frame);
    if (absoluteFrames < PREGAP_FRAMES)
    {
        return 0;
    }
    return absoluteFrames - PREGAP_FRAMES;
}

void lbaToMsf(u32 lba, u8& minute, u8& second, u8& frame)
{
    const u32 absoluteFrames = lba + PREGAP_FRAMES;
    const u32 totalSeconds = absoluteFrames / 75u;
    minute = intToBcd(static_cast<u8>(totalSeconds / 60u));
    second = intToBcd(static_cast<u8>(totalSeconds % 60u));
    frame = intToBcd(static_cast<u8>(absoluteFrames % 75u));
}

void lbaToRelativeMsf(u32 lba, u8& minute, u8& second, u8& frame)
{
    const u32 totalSeconds = lba / 75u;
    minute = intToBcd(static_cast<u8>(totalSeconds / 60u));
    second = intToBcd(static_cast<u8>(totalSeconds % 60u));
    frame = intToBcd(static_cast<u8>(lba % 75u));
}

bool decodeXaSubheader(const std::array<u8, RAW_SECTOR_BYTES>& raw, XaSubheader& out)
{
    if (raw[15] != 2)
    {
        return false;
    }
    if (raw[RAW_SUBHEADER_OFFSET + 0] != raw[RAW_SUBHEADER_OFFSET + 4] ||
        raw[RAW_SUBHEADER_OFFSET + 1] != raw[RAW_SUBHEADER_OFFSET + 5] ||
        raw[RAW_SUBHEADER_OFFSET + 2] != raw[RAW_SUBHEADER_OFFSET + 6] ||
        raw[RAW_SUBHEADER_OFFSET + 3] != raw[RAW_SUBHEADER_OFFSET + 7])
    {
        return false;
    }

    out.fileNumber = raw[RAW_SUBHEADER_OFFSET + 0];
    out.channelNumber = raw[RAW_SUBHEADER_OFFSET + 1];
    out.submode = raw[RAW_SUBHEADER_OFFSET + 2];
    out.codingInfo = raw[RAW_SUBHEADER_OFFSET + 3];
    return true;
}

bool decodeXaAudioSector(const std::array<u8, RAW_SECTOR_BYTES>& raw, const XaSubheader& xa,
                         int& oldLeft, int& olderLeft, int& oldRight, int& olderRight,
                         std::vector<int16_t>& outInterleaved)
{
    outInterleaved.clear();
    const bool stereo = (xa.codingInfo & XA_CODING_STEREO) != 0;
    const bool fourBit = (xa.codingInfo & XA_CODING_BITS_MASK) == XA_CODING_4BIT;
    if (!fourBit)
    {
        return false;
    }

    std::vector<int16_t> left;
    std::vector<int16_t> right;
    left.reserve(XA_SOUND_GROUP_COUNT * 4 * 28);
    right.reserve(XA_SOUND_GROUP_COUNT * 4 * 28);

    for (size_t group = 0; group < XA_SOUND_GROUP_COUNT; ++group)
    {
        const u8* soundGroup = raw.data() + RAW_USER_OFFSET + group * XA_SOUND_GROUP_BYTES;
        for (int block = 0; block < 4; ++block)
        {
            if (stereo)
            {
                decodeXa28Nibbles(soundGroup, block, 0, oldLeft, olderLeft, left);
                decodeXa28Nibbles(soundGroup, block, 1, oldRight, olderRight, right);
            }
            else
            {
                decodeXa28Nibbles(soundGroup, block, 0, oldLeft, olderLeft, left);
                decodeXa28Nibbles(soundGroup, block, 1, oldLeft, olderLeft, left);
            }
        }
    }

    if (stereo)
    {
        if (right.size() != left.size())
        {
            return false;
        }
        outInterleaved.reserve(left.size() * 2);
        for (size_t i = 0; i < left.size(); ++i)
        {
            outInterleaved.push_back(left[i]);
            outInterleaved.push_back(right[i]);
        }
    }
    else
    {
        outInterleaved.reserve(left.size() * 2);
        for (int16_t sample : left)
        {
            outInterleaved.push_back(sample);
            outInterleaved.push_back(sample);
        }
        oldRight = oldLeft;
        olderRight = olderLeft;
    }

    if ((xa.codingInfo & XA_CODING_SAMPLE_18900) != 0)
    {
        std::vector<int16_t> downsampled;
        downsampled.reserve(outInterleaved.size() / 2);
        for (size_t i = 0; i + 1 < outInterleaved.size(); i += 4)
        {
            downsampled.push_back(outInterleaved[i]);
            downsampled.push_back(outInterleaved[i + 1]);
        }
        outInterleaved.swap(downsampled);
    }

    return !outInterleaved.empty();
}

} // namespace cdrom_detail
} // namespace runtime
} // namespace psxrecomp
