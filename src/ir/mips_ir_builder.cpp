#include "psxrecomp/ir/mips_ir_builder.h"

#include "mips_ir_translation.h"

namespace psxrecomp
{
namespace ir
{

MipsIrBuildResult buildIrFromMips(const std::vector<disasm::Instruction>& instructions,
                                  const MipsIrBuildOptions& options)
{
    MipsIrBuildResult result;
    Program program;
    Builder builder(program);

    detail::MipsIrTranslator translator(builder, result, options);
    translator.translate(instructions);

    return result;
}

} // namespace ir
} // namespace psxrecomp
