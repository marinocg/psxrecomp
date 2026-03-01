#pragma once

#include "psxrecomp/disasm/instruction.h"
#include "psxrecomp/ir/mips_ir_builder.h"

#include <unordered_set>
#include <vector>

namespace psxrecomp
{
namespace ir
{
namespace detail
{

class MipsIrTranslator
{
  public:
    MipsIrTranslator(Builder& builder, MipsIrBuildResult& result,
                     const MipsIrBuildOptions& options);

    void translate(const std::vector<disasm::Instruction>& instructions);

  private:
    void translateNoDelay(const disasm::Instruction& instr,
                          std::optional<Address> sourceAddressOverride = std::nullopt);
    void translateWithDelay(const disasm::Instruction& instr, const disasm::Instruction* delaySlot);
    void emitInstruction(Opcode opcode, std::vector<Value> inputs, std::vector<Value> outputs,
                         Address sourceAddress, const std::string& sourceAsm);
    void addWarning(const disasm::Instruction& instruction, const std::string& message);
    void addError(const disasm::Instruction& instruction, const std::string& message);
    static bool isMipsNop(const disasm::Instruction& instruction);
    static bool isMmioImmediate(Register base, s16 immediate);
    static bool isBiosStubAddress(Address address);
    static std::string formatAddress(Address address);

    Builder& m_builder;
    MipsIrBuildResult& m_result;
    MipsIrBuildOptions m_options;
    std::unordered_set<Address> m_targetedAddresses;
};

} // namespace detail
} // namespace ir
} // namespace psxrecomp
