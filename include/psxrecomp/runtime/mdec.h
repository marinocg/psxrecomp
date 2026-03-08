#pragma once

#include "psxrecomp/runtime/logger.h"
#include "psxrecomp/types.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <vector>

namespace psxrecomp
{
namespace runtime
{

class Mdec
{
  public:
    using LogCallback = std::function<void(LogLevel level, const std::string& category,
                                           const std::string& message)>;

    void reset();
    void setLogCallback(LogCallback callback);

    u32 readData();
    u32 readStatus() const;

    void writeCommand(u32 value);
    void writeControl(u32 value);

    void writeDma(u32 value);
    u32 readDma();
    u32 lastDmaWord() const;

    bool dmaInRequest() const;
    bool dmaOutRequest() const;

    std::vector<u8> serializeState() const;
    bool deserializeState(const std::vector<u8>& state);

  private:
    enum class CommandKind : u8
    {
        None = 0,
        DecodeMacroblock = 1,
        SetQuantTable = 2,
        SetScaleTable = 3,
        NoFunction = 4,
    };

    static constexpr size_t MAX_PARAMETER_WORDS = 0x4000;
    static constexpr size_t MAX_OUTPUT_FIFO_WORDS = 0x400;

    std::deque<u32> m_outputFifo;
    std::vector<u32> m_parameterWords;
    std::array<u8, 64> m_luminanceQuantTable{};
    std::array<u8, 64> m_colorQuantTable{};
    std::array<int16_t, 64> m_scaleTable{};
    LogCallback m_logCallback;
    u32 m_lastCommandWord = 0;
    u32 m_lastDmaWord = 0;
    u32 m_placeholderOutputWords = 0;
    u16 m_remainingParameterWords = 0;
    u16 m_statusLow16Override = 0xFFFFu;
    u8 m_currentBlock = 4;
    u8 m_outputDepth = 0;
    bool m_outputSigned = false;
    bool m_outputBit15 = false;
    bool m_dmaInEnabled = false;
    bool m_dmaOutEnabled = false;
    bool m_commandBusy = false;
    bool m_decodeStubActive = false;
    bool m_statusLow16OverrideValid = false;

    bool hasOutputData() const;
    bool dataInFull() const;
    void log(LogLevel level, const std::string& message) const;
    void beginCommand(u32 value);
    void acceptParameter(u32 value);
    void finishCommand();
    void finishDecodeCommand();
    void finishQuantTableCommand();
    void finishScaleTableCommand();
    u32 placeholderWordsForDecode() const;
};

} // namespace runtime
} // namespace psxrecomp