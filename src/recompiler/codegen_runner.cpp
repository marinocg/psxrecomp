#include "psxrecomp/recompiler/codegen.h"

#include "codegen_runner_sections.h"

#include "cpp_emitter.h"

namespace psxrecomp
{
namespace recompiler
{

std::string CodeGenerator::generateRunnerSource(const std::string& moduleName)
{
    CppEmitter emitter;
    emitter.writeLine("#include \"" + moduleName + ".h\"");
    emitter.writeBlank();
    emitter.writeLine("#include \"psxrecomp/runtime/disc_image.h\"");
    emitter.writeLine("#include \"psxrecomp/runtime/gpu_renderer.h\"");
    emitter.writeLine("#include \"psxrecomp/types.h\"");
    emitter.writeLine("#include <algorithm>");
    emitter.writeLine("#include <atomic>");
    emitter.writeLine("#include <chrono>");
    emitter.writeLine("#include <cctype>");
    emitter.writeLine("#include <cstdlib>");
    emitter.writeLine("#include <exception>");
    emitter.writeLine("#include <filesystem>");
    emitter.writeLine("#include <fstream>");
    emitter.writeLine("#include <iostream>");
    emitter.writeLine("#include <memory>");
    emitter.writeLine("#include <optional>");
    emitter.writeLine("#include <string>");
    emitter.writeLine("#include <thread>");
    emitter.writeLine("#include <vector>");
    emitter.writeLine("#if PSXRECOMP_HAS_SDL2");
    emitter.writeLine("#include <SDL.h>");
    emitter.writeLine("#endif");
    emitter.writeLine("#if defined(_WIN32)");
    emitter.writeLine("#include <windows.h>");
    emitter.writeLine("#endif");
    emitter.writeBlank();
    emitRunnerSupportCommon(emitter);
    emitRunnerSupportPresenter(emitter);
    emitRunnerMainFunction(emitter, moduleName);
    return emitter.str();
}

} // namespace recompiler
} // namespace psxrecomp
