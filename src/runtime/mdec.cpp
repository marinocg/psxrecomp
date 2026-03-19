#include "psxrecomp/runtime/mdec.h"

#include <algorithm>
#include <string>

namespace psxrecomp
{
namespace runtime
{

namespace
{
constexpr u32 MDEC_STATE_MAGIC = 0x4345444Du; // "MDEC"
constexpr u32 MDEC_STATE_VERSION = 2u;

void appendU16(std::vector<u8>& out, u16 value)
{
    out.push_back(static_cast<u8>(value & 0xFFu));
    out.push_back(static_cast<u8>((value >> 8) & 0xFFu));
}

void appendU32(std::vector<u8>& out, u32 value)
{
    out.push_back(static_cast<u8>(value & 0xFFu));
    out.push_back(static_cast<u8>((value >> 8) & 0xFFu));
    out.push_back(static_cast<u8>((value >> 16) & 0xFFu));
    out.push_back(static_cast<u8>((value >> 24) & 0xFFu));
}

bool consumeU16(const std::vector<u8>& data, size_t& cursor, u16& out)
{
    if (cursor + sizeof(u16) > data.size())
    {
        return false;
    }
    out = static_cast<u16>(data[cursor]) | (static_cast<u16>(data[cursor + 1]) << 8);
    cursor += sizeof(u16);
    return true;
}

bool consumeU32(const std::vector<u8>& data, size_t& cursor, u32& out)
{
    if (cursor + sizeof(u32) > data.size())
    {
        return false;
    }
    out = static_cast<u32>(data[cursor]) | (static_cast<u32>(data[cursor + 1]) << 8) |
          (static_cast<u32>(data[cursor + 2]) << 16) | (static_cast<u32>(data[cursor + 3]) << 24);
    cursor += sizeof(u32);
    return true;
}

} // namespace

void Mdec::reset()
{
    m_outputFifo.clear();
    m_parameterWords.clear();
    m_lastCommandWord = 0;
    m_lastDmaWord = 0;
    m_placeholderOutputWords = 0;
    m_remainingParameterWords = 0;
    m_statusLow16Override = 0xFFFFu;
    m_currentBlock = 4;
    m_outputDepth = 0;
    m_outputSigned = false;
    m_outputBit15 = false;
    m_dmaInEnabled = false;
    m_dmaOutEnabled = false;
    m_commandBusy = false;
    m_decodeStubActive = false;
    m_statusLow16OverrideValid = false;
    m_lastCommandKind = CommandKind::None;
    m_commandPhase = CommandPhase::Idle;
    m_lifetimeStats = LifetimeStats{};
    m_luminanceQuantTable.fill(0);
    m_colorQuantTable.fill(0);
    m_scaleTable.fill(0);
}

void Mdec::setLogCallback(LogCallback callback)
{
    m_logCallback = std::move(callback);
}

u32 Mdec::readData()
{
    return readDma();
}

u32 Mdec::readStatus() const
{
    u32 status = 0;
    if (!hasOutputData())
    {
        status |= 1u << 31;
    }
    if (dataInFull())
    {
        status |= 1u << 30;
    }
    if (m_commandBusy)
    {
        status |= 1u << 29;
    }
    if (dmaInRequest())
    {
        status |= 1u << 28;
    }
    if (dmaOutRequest())
    {
        status |= 1u << 27;
    }

    status |= (static_cast<u32>(m_outputDepth & 0x3u) << 25);
    status |= (m_outputSigned ? 1u : 0u) << 24;
    status |= (m_outputBit15 ? 1u : 0u) << 23;
    status |= (static_cast<u32>(m_currentBlock & 0x7u) << 16);

    if (m_commandBusy && m_remainingParameterWords > 0)
    {
        status |= static_cast<u32>(m_remainingParameterWords - 1u);
    }
    else if (m_statusLow16OverrideValid)
    {
        status |= m_statusLow16Override;
    }
    else
    {
        status |= 0xFFFFu;
    }

    return status;
}

void Mdec::writeCommand(u32 value)
{
    if (m_commandBusy)
    {
        acceptParameter(value);
        return;
    }
    beginCommand(value);
}

void Mdec::writeControl(u32 value)
{
    if ((value & (1u << 31)) != 0)
    {
        // Hardware warm reset: abort any active command and restore documented
        // idle status, but preserve lifetime diagnostic statistics so an
        // end-of-run report can answer whether MDEC(1) was ever issued.
        const LifetimeStats savedStats = m_lifetimeStats;
        reset();
        m_lifetimeStats = savedStats;
        m_statusLow16OverrideValid = true;
        m_statusLow16Override = 0x0000u;
        m_outputDepth = 0;
        m_currentBlock = 4;
        return;
    }

    m_dmaInEnabled = (value & (1u << 30)) != 0;
    m_dmaOutEnabled = (value & (1u << 29)) != 0;
}

void Mdec::writeDma(u32 value)
{
    m_lastDmaWord = value;
    writeCommand(value);
}

u32 Mdec::readDma()
{
    u32 value = 0;
    if (!m_outputFifo.empty())
    {
        value = m_outputFifo.front();
        m_outputFifo.pop_front();
        m_lifetimeStats.anyDma1Drain = true;
    }
    else if (m_placeholderOutputWords > 0)
    {
        --m_placeholderOutputWords;
        m_lifetimeStats.anyDma1Drain = true;
    }

    if (!hasOutputData())
    {
        m_currentBlock = (m_outputDepth <= 1) ? 4u : 0u;
        if (m_commandPhase == CommandPhase::OutputAvailable)
        {
            m_commandPhase = CommandPhase::OutputDrained;
        }
    }

    m_lastDmaWord = value;
    return value;
}

u32 Mdec::lastDmaWord() const
{
    return m_lastDmaWord;
}

bool Mdec::dmaInRequest() const
{
    return m_dmaInEnabled && m_commandBusy && m_remainingParameterWords > 0;
}

bool Mdec::dmaOutRequest() const
{
    return m_dmaOutEnabled && hasOutputData();
}

Mdec::LifetimeStats Mdec::lifetimeStats() const
{
    return m_lifetimeStats;
}

Mdec::CommandPhase Mdec::currentPhase() const
{
    return m_commandPhase;
}

Mdec::CommandKind Mdec::lastCommandKind() const
{
    return m_lastCommandKind;
}

// static
bool Mdec::commandIsOutputCapable(CommandKind kind)
{
    return kind == CommandKind::DecodeMacroblock;
}

std::vector<u8> Mdec::serializeState() const
{
    std::vector<u8> state;
    state.reserve(1024 + m_parameterWords.size() * sizeof(u32) + m_outputFifo.size() * sizeof(u32));

    appendU32(state, MDEC_STATE_MAGIC);
    appendU32(state, MDEC_STATE_VERSION);
    appendU32(state, m_lastCommandWord);
    appendU32(state, m_lastDmaWord);
    appendU32(state, m_placeholderOutputWords);
    appendU16(state, m_remainingParameterWords);
    appendU16(state, m_statusLow16Override);
    appendU32(state, static_cast<u32>(m_currentBlock));
    appendU32(state, static_cast<u32>(m_outputDepth));
    appendU32(state, m_outputSigned ? 1u : 0u);
    appendU32(state, m_outputBit15 ? 1u : 0u);
    appendU32(state, m_dmaInEnabled ? 1u : 0u);
    appendU32(state, m_dmaOutEnabled ? 1u : 0u);
    appendU32(state, m_commandBusy ? 1u : 0u);
    appendU32(state, m_decodeStubActive ? 1u : 0u);
    appendU32(state, m_statusLow16OverrideValid ? 1u : 0u);
    // Version-2 additions: lifecycle phase, command kind, lifetime stats.
    appendU32(state, static_cast<u32>(m_commandPhase));
    appendU32(state, static_cast<u32>(m_lastCommandKind));
    appendU32(state, m_lifetimeStats.decodeCommandsIssued);
    appendU32(state, m_lifetimeStats.quantTableCommandsIssued);
    appendU32(state, m_lifetimeStats.scaleTableCommandsIssued);
    appendU32(state, m_lifetimeStats.anyDecodeReachedOutputAvailable ? 1u : 0u);
    appendU32(state, m_lifetimeStats.anyDma1Drain ? 1u : 0u);
    appendU32(state, static_cast<u32>(m_parameterWords.size()));
    for (u32 word : m_parameterWords)
    {
        appendU32(state, word);
    }
    appendU32(state, static_cast<u32>(m_outputFifo.size()));
    for (u32 word : m_outputFifo)
    {
        appendU32(state, word);
    }
    state.insert(state.end(), m_luminanceQuantTable.begin(), m_luminanceQuantTable.end());
    state.insert(state.end(), m_colorQuantTable.begin(), m_colorQuantTable.end());
    for (int16_t value : m_scaleTable)
    {
        appendU16(state, static_cast<u16>(value));
    }
    return state;
}

bool Mdec::deserializeState(const std::vector<u8>& state)
{
    size_t cursor = 0;
    u32 magic = 0;
    u32 version = 0;
    u32 parameterWordCount = 0;
    u32 outputWordCount = 0;
    u32 currentBlock = 0;
    u32 outputDepth = 0;
    Mdec candidate;
    candidate.m_logCallback = m_logCallback;

    if (!consumeU32(state, cursor, magic) || !consumeU32(state, cursor, version) ||
        magic != MDEC_STATE_MAGIC || version != MDEC_STATE_VERSION ||
        !consumeU32(state, cursor, candidate.m_lastCommandWord) ||
        !consumeU32(state, cursor, candidate.m_lastDmaWord) ||
        !consumeU32(state, cursor, candidate.m_placeholderOutputWords) ||
        !consumeU16(state, cursor, candidate.m_remainingParameterWords) ||
        !consumeU16(state, cursor, candidate.m_statusLow16Override) ||
        !consumeU32(state, cursor, currentBlock) || !consumeU32(state, cursor, outputDepth))
    {
        return false;
    }

    candidate.m_currentBlock = static_cast<u8>(currentBlock & 0x7u);
    candidate.m_outputDepth = static_cast<u8>(outputDepth & 0x3u);

    auto consumeBool = [&state, &cursor](bool& out)
    {
        u32 raw = 0;
        if (!consumeU32(state, cursor, raw))
        {
            return false;
        }
        out = raw != 0;
        return true;
    };

    u32 commandPhaseRaw = 0;
    u32 lastCommandKindRaw = 0;
    u32 lifetimeDecodeIssued = 0;
    u32 lifetimeQuantIssued = 0;
    u32 lifetimeScaleIssued = 0;

    if (!consumeBool(candidate.m_outputSigned) || !consumeBool(candidate.m_outputBit15) ||
        !consumeBool(candidate.m_dmaInEnabled) || !consumeBool(candidate.m_dmaOutEnabled) ||
        !consumeBool(candidate.m_commandBusy) || !consumeBool(candidate.m_decodeStubActive) ||
        !consumeBool(candidate.m_statusLow16OverrideValid) ||
        !consumeU32(state, cursor, commandPhaseRaw) ||
        !consumeU32(state, cursor, lastCommandKindRaw) ||
        !consumeU32(state, cursor, lifetimeDecodeIssued) ||
        !consumeU32(state, cursor, lifetimeQuantIssued) ||
        !consumeU32(state, cursor, lifetimeScaleIssued))
    {
        return false;
    }

    candidate.m_commandPhase = static_cast<CommandPhase>(commandPhaseRaw & 0xFFu);
    candidate.m_lastCommandKind = static_cast<CommandKind>(lastCommandKindRaw & 0xFFu);
    candidate.m_lifetimeStats.decodeCommandsIssued = lifetimeDecodeIssued;
    candidate.m_lifetimeStats.quantTableCommandsIssued = lifetimeQuantIssued;
    candidate.m_lifetimeStats.scaleTableCommandsIssued = lifetimeScaleIssued;

    if (!consumeBool(candidate.m_lifetimeStats.anyDecodeReachedOutputAvailable) ||
        !consumeBool(candidate.m_lifetimeStats.anyDma1Drain) ||
        !consumeU32(state, cursor, parameterWordCount))
    {
        return false;
    }

    if (parameterWordCount > MAX_PARAMETER_WORDS)
    {
        return false;
    }
    candidate.m_parameterWords.clear();
    candidate.m_parameterWords.reserve(parameterWordCount);
    for (u32 i = 0; i < parameterWordCount; ++i)
    {
        u32 word = 0;
        if (!consumeU32(state, cursor, word))
        {
            return false;
        }
        candidate.m_parameterWords.push_back(word);
    }

    if (!consumeU32(state, cursor, outputWordCount) || outputWordCount > MAX_OUTPUT_FIFO_WORDS)
    {
        return false;
    }
    candidate.m_outputFifo.clear();
    for (u32 i = 0; i < outputWordCount; ++i)
    {
        u32 word = 0;
        if (!consumeU32(state, cursor, word))
        {
            return false;
        }
        candidate.m_outputFifo.push_back(word);
    }

    if (cursor + candidate.m_luminanceQuantTable.size() + candidate.m_colorQuantTable.size() +
            candidate.m_scaleTable.size() * sizeof(u16) !=
        state.size())
    {
        return false;
    }

    std::copy_n(state.begin() + static_cast<std::ptrdiff_t>(cursor),
                static_cast<std::ptrdiff_t>(candidate.m_luminanceQuantTable.size()),
                candidate.m_luminanceQuantTable.begin());
    cursor += candidate.m_luminanceQuantTable.size();
    std::copy_n(state.begin() + static_cast<std::ptrdiff_t>(cursor),
                static_cast<std::ptrdiff_t>(candidate.m_colorQuantTable.size()),
                candidate.m_colorQuantTable.begin());
    cursor += candidate.m_colorQuantTable.size();
    for (size_t i = 0; i < candidate.m_scaleTable.size(); ++i)
    {
        u16 raw = 0;
        if (!consumeU16(state, cursor, raw))
        {
            return false;
        }
        candidate.m_scaleTable[i] = static_cast<int16_t>(raw);
    }

    if (cursor != state.size())
    {
        return false;
    }

    *this = std::move(candidate);
    return true;
}

bool Mdec::hasOutputData() const
{
    return !m_outputFifo.empty() || m_placeholderOutputWords > 0;
}

bool Mdec::dataInFull() const
{
    return m_commandBusy && m_remainingParameterWords <= 1;
}

void Mdec::log(LogLevel level, const std::string& message) const
{
    if (m_logCallback)
    {
        m_logCallback(level, "mdec", message);
    }
}

// static
Mdec::CommandKind Mdec::classifyCommand(u32 commandWord)
{
    switch ((commandWord >> 29) & 0x7u)
    {
    case 0:
        return CommandKind::None;
    case 1:
        return CommandKind::DecodeMacroblock;
    case 2:
        return CommandKind::SetQuantTable;
    case 3:
        return CommandKind::SetScaleTable;
    default:
        return CommandKind::Invalid;
    }
}

void Mdec::beginCommand(u32 value)
{
    const u32 command = (value >> 29) & 0x7u;

    m_lastCommandKind = classifyCommand(value);
    m_lastCommandWord = value;
    m_parameterWords.clear();
    m_outputFifo.clear();
    m_placeholderOutputWords = 0;
    m_decodeStubActive = false;
    m_statusLow16OverrideValid = false;
    m_outputDepth = static_cast<u8>((value >> 27) & 0x3u);
    m_outputSigned = (value & (1u << 26)) != 0;
    m_outputBit15 = (value & (1u << 25)) != 0;
    m_currentBlock = (m_outputDepth <= 1) ? 4u : 0u;

    switch (command)
    {
    case 1:
        ++m_lifetimeStats.decodeCommandsIssued;
        m_commandBusy = true;
        m_remainingParameterWords = static_cast<u16>(value & 0xFFFFu);
        if (m_remainingParameterWords == 0)
        {
            finishCommand();
        }
        else
        {
            m_commandPhase = CommandPhase::AwaitingParameters;
            log(LogLevel::Info, "MDEC(1) DecodeMacroblock accepted: params=" +
                                    std::to_string(m_remainingParameterWords) + " depth=" +
                                    std::to_string(m_outputDepth) + " output-capable=true");
        }
        return;
    case 2:
        ++m_lifetimeStats.quantTableCommandsIssued;
        m_commandBusy = true;
        m_remainingParameterWords = (value & 0x1u) != 0 ? 32u : 16u;
        m_commandPhase = CommandPhase::AwaitingParameters;
        log(LogLevel::Info,
            "MDEC(2) SetQuantTable accepted: params=" + std::to_string(m_remainingParameterWords) +
                " output-capable=false");
        return;
    case 3:
        ++m_lifetimeStats.scaleTableCommandsIssued;
        m_commandBusy = true;
        m_remainingParameterWords = 32u;
        m_commandPhase = CommandPhase::AwaitingParameters;
        log(LogLevel::Info, "MDEC(3) SetScaleTable accepted: params=32 output-capable=false");
        return;
    default:
        m_commandBusy = false;
        m_remainingParameterWords = 0;
        m_commandPhase = CommandPhase::Idle;
        m_statusLow16OverrideValid = true;
        m_statusLow16Override = static_cast<u16>(value & 0xFFFFu);
        return;
    }
}

void Mdec::acceptParameter(u32 value)
{
    if (m_parameterWords.size() < MAX_PARAMETER_WORDS)
    {
        m_parameterWords.push_back(value);
    }

    if (m_remainingParameterWords > 0)
    {
        --m_remainingParameterWords;
    }

    if (m_remainingParameterWords == 0)
    {
        finishCommand();
    }
}

void Mdec::finishCommand()
{
    const u32 command = (m_lastCommandWord >> 29) & 0x7u;
    switch (command)
    {
    case 1:
        finishDecodeCommand();
        break;
    case 2:
        finishQuantTableCommand();
        break;
    case 3:
        finishScaleTableCommand();
        break;
    default:
        break;
    }

    m_commandBusy = false;
    m_remainingParameterWords = 0;
    // Non-decode commands have no output; lifecycle returns to idle immediately.
    if (m_commandPhase != CommandPhase::OutputAvailable)
    {
        m_commandPhase = CommandPhase::Idle;
    }
}

void Mdec::finishDecodeCommand()
{
    m_decodeStubActive = true;
    m_placeholderOutputWords = placeholderWordsForDecode();
    m_currentBlock = (m_outputDepth <= 1) ? 4u : 0u;
    m_commandPhase = CommandPhase::OutputAvailable;
    m_lifetimeStats.anyDecodeReachedOutputAvailable = true;
    log(LogLevel::Info, "MDEC(1) decode complete (stub): output-available words=" +
                            std::to_string(m_placeholderOutputWords) +
                            " depth=" + std::to_string(m_outputDepth));
}

void Mdec::finishQuantTableCommand()
{
    std::vector<u8> bytes;
    bytes.reserve(m_parameterWords.size() * sizeof(u32));
    for (u32 word : m_parameterWords)
    {
        bytes.push_back(static_cast<u8>(word & 0xFFu));
        bytes.push_back(static_cast<u8>((word >> 8) & 0xFFu));
        bytes.push_back(static_cast<u8>((word >> 16) & 0xFFu));
        bytes.push_back(static_cast<u8>((word >> 24) & 0xFFu));
    }

    if (bytes.size() >= m_luminanceQuantTable.size())
    {
        std::copy_n(bytes.begin(), static_cast<std::ptrdiff_t>(m_luminanceQuantTable.size()),
                    m_luminanceQuantTable.begin());
    }
    if (bytes.size() >= m_luminanceQuantTable.size() + m_colorQuantTable.size())
    {
        std::copy_n(bytes.begin() + static_cast<std::ptrdiff_t>(m_luminanceQuantTable.size()),
                    static_cast<std::ptrdiff_t>(m_colorQuantTable.size()),
                    m_colorQuantTable.begin());
    }
}

void Mdec::finishScaleTableCommand()
{
    size_t index = 0;
    for (u32 word : m_parameterWords)
    {
        if (index < m_scaleTable.size())
        {
            m_scaleTable[index++] = static_cast<int16_t>(word & 0xFFFFu);
        }
        if (index < m_scaleTable.size())
        {
            m_scaleTable[index++] = static_cast<int16_t>((word >> 16) & 0xFFFFu);
        }
    }
}

u32 Mdec::placeholderWordsForDecode() const
{
    u32 wordsPerBlock = 8u;
    switch (m_outputDepth)
    {
    case 0:
        wordsPerBlock = 8u;
        break;
    case 1:
        wordsPerBlock = 16u;
        break;
    case 2:
        wordsPerBlock = 192u;
        break;
    case 3:
        wordsPerBlock = 128u;
        break;
    default:
        break;
    }

    const u32 parameterWords = std::max<u32>(1u, m_lastCommandWord & 0xFFFFu);
    const u32 alignedInputWords = (parameterWords + 0x1Fu) & ~0x1Fu;
    return std::max(wordsPerBlock, std::min<u32>(alignedInputWords * 4u, 0x2000u));
}

} // namespace runtime
} // namespace psxrecomp