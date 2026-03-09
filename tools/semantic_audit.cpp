#include "psxrecomp/recompiler/semantic_audit.h"

#include <cstring>
#include <iomanip>
#include <sstream>

namespace psxrecomp
{
namespace runtime
{

namespace
{

u32 readWordSafe(const u8* ram, Address physicalAddress, size_t ramSize)
{
    if (physicalAddress + sizeof(u32) > ramSize)
    {
        return 0;
    }
    u32 value = 0;
    std::memcpy(&value, ram + physicalAddress, sizeof(u32));
    return value;
}

Address toPhysical(Address address)
{
    return address & 0x1FFFFFFFu;
}

const char* mipsRegisterName(u32 reg)
{
    static const char* names[] = {"zero", "at", "v0", "v1", "a0", "a1", "a2", "a3",
                                  "t0",   "t1", "t2", "t3", "t4", "t5", "t6", "t7",
                                  "s0",   "s1", "s2", "s3", "s4", "s5", "s6", "s7",
                                  "t8",   "t9", "k0", "k1", "gp", "sp", "fp", "ra"};
    return (reg < 32) ? names[reg] : "?";
}

const char* mipsOpName(u32 opcode)
{
    switch (opcode)
    {
    case 0x00:
        return "SPECIAL";
    case 0x02:
        return "J";
    case 0x03:
        return "JAL";
    case 0x04:
        return "BEQ";
    case 0x05:
        return "BNE";
    case 0x08:
        return "ADDI";
    case 0x09:
        return "ADDIU";
    case 0x0A:
        return "SLTI";
    case 0x0B:
        return "SLTIU";
    case 0x0C:
        return "ANDI";
    case 0x0D:
        return "ORI";
    case 0x0F:
        return "LUI";
    case 0x20:
        return "LB";
    case 0x21:
        return "LH";
    case 0x23:
        return "LW";
    case 0x24:
        return "LBU";
    case 0x25:
        return "LHU";
    case 0x28:
        return "SB";
    case 0x29:
        return "SH";
    case 0x2B:
        return "SW";
    default:
        return nullptr;
    }
}

const char* mipsSpecialName(u32 funct)
{
    switch (funct)
    {
    case 0x00:
        return "SLL";
    case 0x02:
        return "SRL";
    case 0x03:
        return "SRA";
    case 0x08:
        return "JR";
    case 0x09:
        return "JALR";
    case 0x20:
        return "ADD";
    case 0x21:
        return "ADDU";
    case 0x22:
        return "SUB";
    case 0x23:
        return "SUBU";
    case 0x24:
        return "AND";
    case 0x25:
        return "OR";
    case 0x26:
        return "XOR";
    case 0x27:
        return "NOR";
    case 0x2A:
        return "SLT";
    case 0x2B:
        return "SLTU";
    default:
        return nullptr;
    }
}

bool isLoadStore(u32 opcode)
{
    return opcode >= 0x20 && opcode <= 0x2B;
}

} // namespace

SemanticAuditTool::SemanticAuditTool() = default;

void SemanticAuditTool::configure(const std::vector<SuspectFunctionConfig>& configs)
{
    m_configs = configs;
}

SemanticAuditReport SemanticAuditTool::audit(const std::string& name, const u8* ram,
                                             size_t ramSize) const
{
    SemanticAuditReport report;
    report.functionName = name;

    for (const auto& config : m_configs)
    {
        if (config.name != name)
        {
            continue;
        }
        report.pcStart = config.pcStart;
        report.pcEnd = config.pcEnd;
        report.watchedCells = config.watchedCells;
        report.disassembly = disassembleRange(config.pcStart, config.pcEnd, ram, ramSize);
        report.accessAnnotations =
            annotateAccesses(config.pcStart, config.pcEnd, config.watchedCells, ram, ramSize);

        std::ostringstream summary;
        summary << "Function: " << config.name << " [0x" << std::hex << config.pcStart
                << "-0x" << config.pcEnd << "]\n";
        summary << "Watched cells: " << std::dec << config.watchedCells.size() << "\n";
        const size_t instrCount =
            (config.pcEnd >= config.pcStart)
                ? (config.pcEnd - config.pcStart + sizeof(u32)) / sizeof(u32)
                : 0;
        summary << "Instructions: " << instrCount << "\n";
        report.summary = summary.str();
        return report;
    }

    report.summary = "function not found in profile: " + name;
    return report;
}

std::vector<SemanticAuditReport> SemanticAuditTool::auditAll(const u8* ram,
                                                             size_t ramSize) const
{
    std::vector<SemanticAuditReport> reports;
    for (const auto& config : m_configs)
    {
        reports.push_back(audit(config.name, ram, ramSize));
    }
    return reports;
}

std::string SemanticAuditTool::formatReport(const SemanticAuditReport& report)
{
    std::ostringstream os;
    os << "=== Semantic Audit: " << report.functionName << " ===\n";
    os << report.summary;
    if (!report.disassembly.empty())
    {
        os << "\nDisassembly:\n" << report.disassembly;
    }
    if (!report.accessAnnotations.empty())
    {
        os << "\nAccess annotations:\n" << report.accessAnnotations;
    }
    return os.str();
}

size_t SemanticAuditTool::functionCount() const
{
    return m_configs.size();
}

std::string SemanticAuditTool::disassembleRange(Address start, Address end, const u8* ram,
                                                size_t ramSize) const
{
    if (start > end)
    {
        return {};
    }

    std::ostringstream os;
    for (Address pc = start; pc <= end; pc += sizeof(u32))
    {
        const Address phys = toPhysical(pc);
        const u32 word = readWordSafe(ram, phys, ramSize);
        const u32 opcode = (word >> 26) & 0x3F;
        const u32 rs = (word >> 21) & 0x1F;
        const u32 rt = (word >> 16) & 0x1F;
        const u32 rd = (word >> 11) & 0x1F;
        const u32 funct = word & 0x3F;
        const s16 imm = static_cast<s16>(word & 0xFFFF);

        os << "  0x" << std::hex << std::setw(8) << std::setfill('0') << pc << ": 0x"
           << std::setw(8) << word << "  ";

        if (opcode == 0)
        {
            const char* name = mipsSpecialName(funct);
            os << (name != nullptr ? name : "???") << " " << mipsRegisterName(rd) << ","
               << mipsRegisterName(rs) << "," << mipsRegisterName(rt);
        }
        else if (isLoadStore(opcode))
        {
            const char* name = mipsOpName(opcode);
            os << (name != nullptr ? name : "???") << " " << mipsRegisterName(rt) << ","
               << std::dec << static_cast<int>(imm) << "(" << mipsRegisterName(rs) << ")";
        }
        else
        {
            const char* name = mipsOpName(opcode);
            os << (name != nullptr ? name : "???") << " " << mipsRegisterName(rs) << ","
               << mipsRegisterName(rt) << "," << std::dec << static_cast<int>(imm);
        }
        os << "\n";
    }
    return os.str();
}

std::string SemanticAuditTool::annotateAccesses(Address start, Address end,
                                                const std::vector<Address>& cells,
                                                const u8* ram, size_t ramSize) const
{
    if (cells.empty() || start > end)
    {
        return {};
    }

    std::ostringstream os;
    for (Address cell : cells)
    {
        const Address phys = toPhysical(cell);
        const u32 value = readWordSafe(ram, phys, ramSize);
        os << "  cell 0x" << std::hex << cell << " = 0x" << value << "\n";
    }
    return os.str();
}

} // namespace runtime
} // namespace psxrecomp
