#include "pipeline_validation.h"

#include "psxrecomp/ir/resume_safety.h"
#include "psxrecomp/ir/verify.h"

#include <sstream>

namespace psxrecomp
{
namespace recompiler
{
namespace detail
{

std::optional<std::string> verifyFunctionForCodegen(const ir::Function& function,
                                                    std::string_view activeDiscPath,
                                                    std::string_view stage,
                                                    std::vector<PipelineDiagnostic>& diagnostics)
{
    std::vector<std::string> stageErrors;

    const ir::VerificationResult verification = ir::verifyFunction(function);
    for (const auto& error : verification.errors)
    {
        PipelineDiagnostic entry;
        entry.code = "IrVerification";
        entry.severity = "error";
        entry.message =
            std::string(stage) + " verification failed for " + function.name + ": " + error;
        entry.context.file = std::string(activeDiscPath);
        entry.context.offset = function.entryAddress;
        diagnostics.push_back(entry);
        stageErrors.push_back(entry.message);
    }

    const auto resumeDiagnostics = ir::verifyResumeSafety(function);
    for (const auto& resumeDiagnostic : resumeDiagnostics)
    {
        PipelineDiagnostic entry;
        entry.code = "ResumeSafety";
        entry.severity = "error";
        entry.message = std::string(stage) + " resume-safety failed for " + function.name + ": " +
                        resumeDiagnostic.message;
        entry.context.file = std::string(activeDiscPath);
        entry.context.offset = resumeDiagnostic.useSourceAddress;
        diagnostics.push_back(entry);
        stageErrors.push_back(entry.message);
    }

    if (stageErrors.empty())
    {
        return std::nullopt;
    }

    std::ostringstream stream;
    stream << "IR verification failed during " << stage << " for " << function.name << ":\n";
    for (const auto& error : stageErrors)
    {
        stream << " - " << error << "\n";
    }
    return stream.str();
}

} // namespace detail
} // namespace recompiler
} // namespace psxrecomp