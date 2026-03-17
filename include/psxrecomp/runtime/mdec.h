#pragma once

#include "psxrecomp/runtime/logger.h"
#include "psxrecomp/types.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <string>
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

    // Command type decoded from bits 31-29 of the command word.
    enum class CommandKind : u8
    {
        None             = 0,
        DecodeMacroblock = 1, // MDEC(1) – produces output; obeys BFRD-like data-ready cycle
        SetQuantTable    = 2, // MDEC(2) – consumes payload; never produces output
        SetScaleTable    = 3, // MDEC(3) – consumes payload; never produces output
        Invalid          = 4, // bits 31-29 ≥ 4
    };

    // Lifecycle phase of the active or most-recently completed command.
    enum class CommandPhase : u8
    {
        Idle              = 0, // No active command; no pending output.
        AwaitingParameters = 1, // Command word accepted; consuming parameter words.
        Processing        = 2, // Final parameter received; generating output (stub: instant).
        OutputAvailable   = 3, // Decoded output ready for drain (MDEC(1) only).
        OutputDrained     = 4, // All output consumed; decode lifecycle complete.
    };

    // Cumulative statistics that survive hardware (warm) resets.
    // Cleared only by reset() (cold reset / test setup).
    struct LifetimeStats
    {
        u32  decodeCommandsIssued              = 0;
        u32  quantTableCommandsIssued          = 0;
        u32  scaleTableCommandsIssued          = 0;
        bool anyDecodeReachedOutputAvailable   = false;
        bool anyDma1Drain                      = false;
    };

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

    // ---- Diagnostic accessors ----

    LifetimeStats lifetimeStats() const;
    CommandPhase  currentPhase() const;
    CommandKind   lastCommandKind() const;

    // Returns true if commands of this kind can ever generate decoded output.
    static bool commandIsOutputCapable(CommandKind kind);

    std::vector<u8> serializeState() const;
    bool deserializeState(const std::vector<u8>& state);

  private:
    static constexpr size_t MAX_PARAMETER_WORDS   = 0x4000;
    static constexpr size_t MAX_OUTPUT_FIFO_WORDS = 0x400;

    std::deque<u32>      m_outputFifo;
    std::vector<u32>     m_parameterWords;
    std::array<u8, 64>      m_luminanceQuantTable{};
    std::array<u8, 64>      m_colorQuantTable{};
    std::array<int16_t, 64> m_scaleTable{};
    LogCallback   m_logCallback;
    LifetimeStats m_lifetimeStats;
    u32 m_lastCommandWord         = 0;
    u32 m_lastDmaWord             = 0;
    u32 m_placeholderOutputWords  = 0;
    u16 m_remainingParameterWords = 0;
    u16 m_statusLow16Override     = 0xFFFFu;
    u8  m_currentBlock            = 4;
    u8  m_outputDepth             = 0;
    CommandKind  m_lastCommandKind = CommandKind::None;
    CommandPhase m_commandPhase    = CommandPhase::Idle;
    bool m_outputSigned           = false;
    bool m_outputBit15            = false;
    bool m_dmaInEnabled           = false;
    bool m_dmaOutEnabled          = false;
    bool m_commandBusy            = false;
    bool m_decodeStubActive       = false;
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
    u32  placeholderWordsForDecode() const;
    static CommandKind classifyCommand(u32 commandWord);
};

} // namespace runtime
} // namespace psxrecomp