#pragma once

#include "psxrecomp/types.h"

#include <deque>
#include <vector>

namespace psxrecomp
{
namespace runtime
{

class Cdrom
{
  public:
    void reset();
    void tick(u32 cpuCycles);

    u8 readStatus() const;
    u8 readData();
    u8 readInterruptFlags() const;
    u8 readInterruptEnable() const;

    void writeCommand(u8 value);
    void writeParam(u8 value);
    void writeInterruptFlags(u8 value);
    void writeInterruptEnable(u8 value);

    void writeDma(u32 value);
    u32 readDma();
    u32 lastDmaWord() const;

    void enqueueDataSector(const std::vector<u8>& data);
    bool hasIrqRequest() const;

  private:
    static constexpr size_t MAX_PARAMS = 16;
    static constexpr size_t RESPONSE_CAPACITY = 32;
    static constexpr size_t DATA_FIFO_CAPACITY = 4096;
    static constexpr size_t MAX_QUEUED_SECTORS = 64;
    static constexpr u32 CDROM_READ_CYCLES = 451584; // 33.8688MHz / 75 sectors/sec (1x)

    u8 m_status = 0;
    std::deque<u8> m_params;
    std::deque<u8> m_responses;
    std::deque<u8> m_dataFifo;
    std::deque<std::vector<u8>> m_sectorQueue;
    std::vector<u8> m_activeSector;
    size_t m_activeSectorOffset = 0;
    u8 m_interruptFlags = 0;
    u8 m_interruptEnable = 0;
    u32 m_lastDmaWord = 0;
    u32 m_cyclesUntilSector = CDROM_READ_CYCLES;
    bool m_readActive = false;
    bool m_xaStreamingEnabled = false;

    void pushResponse(u8 value);
    void setInterruptFlag(u8 mask);
    void pumpSectorToDataFifo();
    void loadActiveSector();
};

} // namespace runtime
} // namespace psxrecomp
