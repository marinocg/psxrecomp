#include "psxrecomp/ir/ir.h"

#include "../../src/recompiler/pipeline_validation.h"

#include <cassert>
#include <string>
#include <vector>

int main()
{
    using psxrecomp::ir::BasicBlock;
    using psxrecomp::ir::Function;
    using psxrecomp::ir::Instruction;
    using psxrecomp::ir::Opcode;
    using psxrecomp::ir::Value;
    using psxrecomp::recompiler::PipelineDiagnostic;

    {
        Function invalidPhi{"invalid_phi", 0x3000, {}};
        invalidPhi.blocks.push_back(BasicBlock{"entry", {}, {"join"}, {}});
        invalidPhi.blocks.push_back(BasicBlock{"other", {}, {"join"}, {}});
        invalidPhi.blocks.push_back(BasicBlock{"join", {}, {}, {}});
        invalidPhi.blocks[2].instructions.push_back(
            Instruction{Opcode::PHI, {Value::makeTemporary(1)}, {Value::makeTemporary(2)}, 0x3008});

        std::vector<PipelineDiagnostic> diagnostics;
        const auto error = psxrecomp::recompiler::detail::verifyFunctionForCodegen(
            invalidPhi, "invalid.exe", "unit-test", diagnostics);
        assert(error.has_value());
        assert(error->find("unit-test") != std::string::npos);
        assert(!diagnostics.empty());
        assert(diagnostics[0].code == "IrVerification");
    }

    {
        Function resumeUnsafe{"resume_unsafe", 0x4000, {}};
        resumeUnsafe.blocks.push_back(BasicBlock{"entry", {}, {}, {}});
        resumeUnsafe.blocks[0].instructions.push_back(Instruction{
            Opcode::MOVE, {Value::makeImmediate(1)}, {Value::makeTemporary(7)}, 0x4000});
        resumeUnsafe.blocks[0].instructions.push_back(
            Instruction{Opcode::ADD,
                        {Value::makeTemporary(7), Value::makeImmediate(2)},
                        {Value::makeTemporary(8)},
                        0x4004});

        std::vector<PipelineDiagnostic> diagnostics;
        const auto error = psxrecomp::recompiler::detail::verifyFunctionForCodegen(
            resumeUnsafe, "resume.exe", "unit-test", diagnostics);
        assert(error.has_value());
        bool foundResumeSafety = false;
        for (const auto& diagnostic : diagnostics)
        {
            foundResumeSafety = foundResumeSafety || diagnostic.code == "ResumeSafety";
        }
        assert(foundResumeSafety);
    }

    return 0;
}