#pragma once

#include "psxrecomp/types.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace psxrecomp
{
namespace runtime
{
namespace cdrom_detail
{

inline constexpr u8 INT1 = 0x1u;
inline constexpr u8 INT2 = 0x2u;
inline constexpr u8 INT3 = 0x3u;
inline constexpr u8 INT5 = 0x5u;

inline constexpr u8 STATUS_RESPONSE_READY = 1u << 5;
inline constexpr u8 STATUS_DATA_READY = 1u << 6;
inline constexpr u8 STATUS_PARAM_FIFO_EMPTY = 1u << 3;
inline constexpr u8 STATUS_PARAM_FIFO_WRITE_READY = 1u << 4;
inline constexpr u8 STATUS_COMMAND_BUSY = 1u << 7;

inline constexpr u8 REQUEST_ENABLE_BUFFER_READ = 1u << 7;
inline constexpr u8 REQUEST_SEND_SECTOR_MIXED = 1u << 6;

inline constexpr u8 STAT_MOTOR_ON = 1u << 1;
inline constexpr u8 STAT_ID_ERROR = 1u << 3;
inline constexpr u8 STAT_SHELL_OPEN = 1u << 4;
inline constexpr u8 STAT_READ_ACTIVE = 1u << 5;
inline constexpr u8 STAT_SEEK_ACTIVE = 1u << 6;

inline constexpr u32 PREGAP_FRAMES = 150;
inline constexpr u32 DOOR_CLOSE_TRANSITION_CYCLES = 451584u * 2u;

inline constexpr size_t USER_SECTOR_BYTES = 2048;
inline constexpr size_t RAW_SECTOR_BYTES = 2352;
inline constexpr size_t RAW_USER_OFFSET = 24;
inline constexpr size_t RAW_SYNC_OFFSET = 12;
inline constexpr size_t RAW_SUBHEADER_OFFSET = 16;
inline constexpr size_t WHOLE_SECTOR_BYTES = RAW_SECTOR_BYTES - RAW_SYNC_OFFSET;
inline constexpr size_t XA_FORM2_USER_BYTES = 2324;
inline constexpr size_t MAX_FILTER_SCAN_SECTORS = 32;

inline constexpr u8 SETMODE_XA_FILTER_ENABLE = 0x08;
inline constexpr u8 SETMODE_SECTOR_SIZE_2340 = 0x20;
inline constexpr u8 SETMODE_XA_STREAM_ENABLE = 0x40;

inline constexpr u8 XA_SUBMODE_AUDIO = 0x04;
inline constexpr u8 XA_SUBMODE_FORM2 = 0x20;
inline constexpr u8 XA_SUBMODE_REALTIME = 0x40;
inline constexpr u8 XA_CODING_STEREO = 0x01;
inline constexpr u8 XA_CODING_SAMPLE_18900 = 0x04;
inline constexpr u8 XA_CODING_BITS_MASK = 0x30;
inline constexpr u8 XA_CODING_4BIT = 0x00;

inline constexpr u8 ERR_NO_DISC = 0x40;
inline constexpr u8 ERR_READ_FAIL = 0x80;

inline constexpr u32 CDROM_STATE_MAGIC = 0x4D524443u; // "CDRM"
inline constexpr u32 CDROM_STATE_VERSION = 2u;
inline constexpr size_t MAX_SERIALIZED_SECTOR_BYTES = 4096;

struct XaSubheader
{
    u8 fileNumber = 0;
    u8 channelNumber = 0;
    u8 submode = 0;
    u8 codingInfo = 0;
};

u8 bcdToInt(u8 value);
u8 intToBcd(u8 value);
u32 msfToLba(u8 minute, u8 second, u8 frame);
void lbaToMsf(u32 lba, u8& minute, u8& second, u8& frame);
void lbaToRelativeMsf(u32 lba, u8& minute, u8& second, u8& frame);

bool decodeXaSubheader(const std::array<u8, RAW_SECTOR_BYTES>& raw, XaSubheader& out);
bool decodeXaAudioSector(const std::array<u8, RAW_SECTOR_BYTES>& raw, const XaSubheader& xa,
                         int& oldLeft, int& olderLeft, int& oldRight, int& olderRight,
                         std::vector<int16_t>& outInterleaved);

} // namespace cdrom_detail
} // namespace runtime
} // namespace psxrecomp
