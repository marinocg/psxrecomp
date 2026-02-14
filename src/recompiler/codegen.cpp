#include "psxrecomp/recompiler/codegen.h"

#include "codegen_helpers.h"
#include "cpp_emitter.h"

#include <sstream>
#include <unordered_set>

namespace psxrecomp
{
namespace recompiler
{
namespace
{
std::string escapeStringLiteral(const std::string& value)
{
    std::string escaped;
    escaped.reserve(value.size());
    for (char ch : value)
    {
        switch (ch)
        {
        case '\\':
            escaped += "\\\\";
            break;
        case '\"':
            escaped += "\\\"";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        case '\t':
            escaped += "\\t";
            break;
        default:
            escaped += ch;
            break;
        }
    }
    return escaped;
}
} // namespace

CodeGenerator::CodeGenerator(const CodeGenOptions& options) : m_options(options) {}
std::string CodeGenerator::generateHeader(const ir::Program& program, const std::string& moduleName)
{
    (void)moduleName;
    CppEmitter emitter;
    emitter.writeLine("#pragma once");
    emitter.writeBlank();
    emitter.writeLine("#include \"psxrecomp/runtime/psx_system.h\"");
    emitter.writeLine("#include \"psxrecomp/types.h\"");
    emitter.writeBlank();
    emitter.openBlock("namespace psxrecomp");
    emitter.openBlock("namespace recompiler");
    emitter.writeLine("struct RecompilerContext;");
    emitter.openBlock("struct RecompiledModule");
    emitter.writeLine("static void configure(runtime::PsxSystem& system);");
    emitter.writeLine("static void run(runtime::PsxSystem& system);");
    emitter.closeBlock(";");
    emitter.writeBlank();
    emitter.writeLines(generateFunctionDeclarations(program));
    emitter.closeBlock();
    emitter.closeBlock();
    return emitter.str();
}
std::string CodeGenerator::generateSource(const ir::Program& program, const std::string& moduleName,
                                          const ModuleMetadata& metadata)
{
    CppEmitter emitter;
    emitter.writeLine("#include \"" + moduleName + ".h\"");
    emitter.writeBlank();
    emitter.writeLine("#include <array>");
    emitter.writeLine("#include <cstdlib>");
    emitter.writeLine("#include <cstdint>");
    emitter.writeLine("#include <cstring>");
    emitter.writeLine("#include <iostream>");
    emitter.writeLine("#include <string>");
    emitter.writeLine("#include <vector>");
    emitter.writeBlank();
    emitter.writeLine("#ifndef PSXRECOMP_ENABLE_LOGGING");
    emitter.writeLine("#define PSXRECOMP_ENABLE_LOGGING 0");
    emitter.writeLine("#endif");
    emitter.writeLine("#ifndef PSXRECOMP_LOG_LEVEL");
    emitter.writeLine("#define PSXRECOMP_LOG_LEVEL 1");
    emitter.writeLine("#endif");
    emitter.writeLine("#ifndef PSXRECOMP_ENABLE_CHECKS");
    emitter.writeLine("#define PSXRECOMP_ENABLE_CHECKS 1");
    emitter.writeLine("#endif");
    emitter.writeBlank();
    emitter.openBlock("namespace psxrecomp");
    emitter.openBlock("namespace recompiler");
    emitter.openBlock("struct RecompilerContext");
    emitter.writeLine("runtime::PsxSystem& system;");
    emitter.writeLine("std::array<u32, Registers::NUM_REGISTERS> regs{};");
    emitter.writeLine("u32 hi = 0;");
    emitter.writeLine("u32 lo = 0;");
    emitter.closeBlock(";");
    emitter.writeBlank();
    emitter.openBlock("namespace");
    emitter.writeLine("inline u32 readMemory32(runtime::PsxSystem& system, Address address)");
    emitter.openBlock("");
    emitter.writeLine("return system.read<u32>(address);");
    emitter.closeBlock();
    emitter.writeBlank();
    emitter.writeLine(
        "inline void writeMemory32(runtime::PsxSystem& system, Address address, u32 value)");
    emitter.openBlock("");
    emitter.writeLine("system.write<u32>(address, value);");
    emitter.closeBlock();
    emitter.writeBlank();
    emitter.writeLine("inline u32 readMmio32(runtime::PsxSystem& system, Address address)");
    emitter.openBlock("");
    emitter.writeLine("return system.readMmioExplicit<u32>(address);");
    emitter.closeBlock();
    emitter.writeBlank();
    emitter.writeLine(
        "inline void writeMmio32(runtime::PsxSystem& system, Address address, u32 value)");
    emitter.openBlock("");
    emitter.writeLine("system.writeMmioExplicit<u32>(address, value);");
    emitter.closeBlock();
    emitter.writeBlank();
    emitter.writeLine(
        "inline void callSyscall(runtime::PsxSystem& system, u32 code, std::array<u32, "
        "Registers::NUM_REGISTERS>& regs)");
    emitter.openBlock("");
    emitter.writeLine("system.callBiosSyscall(code, regs.data(), regs.size());");
    emitter.closeBlock();
    emitter.writeBlank();
    emitter.writeLine("inline bool callIntrinsic(runtime::PsxSystem& system, Address address)");
    emitter.openBlock("");
    emitter.writeLine("Address physical = address & 0x1FFFFFFF;");
    emitter.writeLine("if (physical >= 0x1F801810 && physical <= 0x1F801817)");
    emitter.openBlock("");
    emitter.writeLine("system.callGpuIntrinsic(physical);");
    emitter.writeLine("return true;");
    emitter.closeBlock();
    emitter.writeLine("if (physical >= 0x1F801800 && physical <= 0x1F801803)");
    emitter.openBlock("");
    emitter.writeLine("system.callCdromIntrinsic(physical);");
    emitter.writeLine("return true;");
    emitter.closeBlock();
    emitter.writeLine("if (physical >= 0x1F801C00 && physical <= 0x1F801DFF)");
    emitter.openBlock("");
    emitter.writeLine("system.callSpuIntrinsic(physical);");
    emitter.writeLine("return true;");
    emitter.closeBlock();
    emitter.writeLine("return false;");
    emitter.closeBlock();
    emitter.writeBlank();
    emitter.writeLine("inline void logWarning(const char* message)");
    emitter.openBlock("");
    emitter.writeLine("if (PSXRECOMP_ENABLE_LOGGING)");
    emitter.openBlock("");
    emitter.writeLine("std::cerr << message << \"\\n\";");
    emitter.closeBlock();
    emitter.closeBlock();
    emitter.closeBlock();
    emitter.writeBlank();

    emitter.writeLines(generateGlobals(program));
    emitter.writeBlank();
    emitter.writeLine("static const std::vector<const char*> kPipelineWarnings = {");
    if (metadata.warnings.empty())
    {
        emitter.writeLine("};");
    }
    else
    {
        for (size_t index = 0; index < metadata.warnings.size(); ++index)
        {
            std::string line = "    \"" + escapeStringLiteral(metadata.warnings[index]) + "\"";
            if (index + 1 < metadata.warnings.size())
            {
                line += ",";
            }
            emitter.writeLine(line);
        }
        emitter.writeLine("};");
    }
    emitter.writeBlank();

    emitter.writeLine("void RecompiledModule::configure(runtime::PsxSystem& system)");
    emitter.openBlock("");
    emitter.writeLine("runtime::PsxSystem::DiscSwapInfo info;");
    emitter.writeLine("info.setName = \"" + escapeStringLiteral(metadata.discSetName) + "\";");
    emitter.writeLine("info.activeDiscIndex = " + std::to_string(metadata.activeDiscIndex) + ";");
    if (metadata.discs.empty())
    {
        emitter.writeLine("info.discs = {};");
    }
    else
    {
        emitter.writeLine("info.discs = {");
        for (size_t index = 0; index < metadata.discs.size(); ++index)
        {
            const auto& disc = metadata.discs[index];
            std::ostringstream line;
            line << "    {" << disc.index << ", \"" << escapeStringLiteral(disc.label) << "\", \""
                 << escapeStringLiteral(disc.path) << "\"}";
            if (index + 1 < metadata.discs.size())
            {
                line << ",";
            }
            emitter.writeLine(line.str());
        }
        emitter.writeLine("};");
    }
    emitter.writeLine("system.setDiscSwapInfo(info);");
    emitter.writeLine("if (PSXRECOMP_ENABLE_LOGGING)");
    emitter.openBlock("");
    emitter.writeLine("for (const auto* warning : kPipelineWarnings)");
    emitter.openBlock("");
    emitter.writeLine("logWarning(warning);");
    emitter.closeBlock();
    emitter.closeBlock();
    emitter.closeBlock();
    emitter.writeBlank();

    emitter.writeLine("void RecompiledModule::run(runtime::PsxSystem& system)");
    emitter.openBlock("");
    emitter.writeLine("RecompilerContext context{system, {}};");
    if (!program.functions.empty())
    {
        emitter.writeLine(toIdentifier(program.functions.front().name) + "(context);");
    }
    emitter.closeBlock();
    emitter.writeBlank();

    emitter.writeLines(generateFunctionDefinitions(program));
    emitter.closeBlock();
    emitter.closeBlock();
    return emitter.str();
}
std::string CodeGenerator::generateBuildFile(const std::string& projectName)
{
    std::ostringstream stream;
    stream << "cmake_minimum_required(VERSION 3.15)\n";
    stream << "set(CMAKE_CXX_STANDARD 17)\n";
    stream << "set(CMAKE_CXX_STANDARD_REQUIRED ON)\n";
    stream << "set(CMAKE_CXX_EXTENSIONS OFF)\n\n";
    stream << "set(PSXRECOMP_RUNTIME_INCLUDE_DIR \"${CMAKE_CURRENT_LIST_DIR}/runtime/include\" "
              "CACHE PATH \"Path to runtime/include\")\n";
    stream << "set(PSXRECOMP_RUNTIME_SOURCE_DIR \"${CMAKE_CURRENT_LIST_DIR}/runtime/src\" CACHE "
              "PATH \"Path to runtime/src\")\n";
    stream << "set(PSXRECOMP_RESOURCE_DIR \"${CMAKE_CURRENT_LIST_DIR}/resources\" CACHE PATH "
              "\"Path to resource directory\")\n";
    stream << "option(PSXRECOMP_ENABLE_LOGGING \"Enable recompiled logging\" OFF)\n";
    stream << "set(PSXRECOMP_LOG_LEVEL 1 CACHE STRING \"Logging level\")\n";
    stream << "option(PSXRECOMP_ENABLE_CHECKS \"Enable runtime checks\" ON)\n";
    stream << "set(PSXRECOMP_OPT_LEVEL 2 CACHE STRING \"Optimization level hint\")\n";
    stream << "if(NOT EXISTS ${PSXRECOMP_RUNTIME_INCLUDE_DIR}/psxrecomp/types.h)\n";
    stream << "    message(FATAL_ERROR \"PSX runtime include directory is invalid: "
              "${PSXRECOMP_RUNTIME_INCLUDE_DIR}\")\n";
    stream << "endif()\n\n";
    stream << "file(GLOB PSXRECOMP_RUNTIME_SOURCES CONFIGURE_DEPENDS "
              "${PSXRECOMP_RUNTIME_SOURCE_DIR}/*.cpp)\n";
    stream << "if(NOT PSXRECOMP_RUNTIME_SOURCES)\n";
    stream << "    message(FATAL_ERROR \"No runtime sources found in "
              "${PSXRECOMP_RUNTIME_SOURCE_DIR}\")\n";
    stream << "endif()\n\n";
    stream << "add_library(psxrecomp_runtime STATIC ${PSXRECOMP_RUNTIME_SOURCES})\n";
    stream << "target_include_directories(psxrecomp_runtime PUBLIC "
              "${PSXRECOMP_RUNTIME_INCLUDE_DIR})\n\n";
    stream << "project(" << projectName << " LANGUAGES CXX)\n";
    stream << "if(WIN32 AND NOT MSVC)\n";
    stream << "    add_link_options(-static -static-libgcc -static-libstdc++)\n";
    stream << "endif()\n\n";
    stream << "add_library(" << projectName << " " << projectName << ".cpp)\n";
    stream << "target_compile_definitions(" << projectName << " PUBLIC\n";
    stream << "    PSXRECOMP_ENABLE_LOGGING=$<BOOL:${PSXRECOMP_ENABLE_LOGGING}>\n";
    stream << "    PSXRECOMP_LOG_LEVEL=${PSXRECOMP_LOG_LEVEL}\n";
    stream << "    PSXRECOMP_ENABLE_CHECKS=$<BOOL:${PSXRECOMP_ENABLE_CHECKS}>\n";
    stream << "    PSXRECOMP_OPT_LEVEL=${PSXRECOMP_OPT_LEVEL}\n";
    stream << ")\n";
    stream << "target_include_directories(" << projectName << " PUBLIC\n";
    stream << "    ${PSXRECOMP_RUNTIME_INCLUDE_DIR}\n";
    stream << "    ${CMAKE_CURRENT_LIST_DIR}\n";
    stream << ")\n";
    stream << "target_link_libraries(" << projectName << " PUBLIC psxrecomp_runtime)\n\n";
    stream << "add_executable(" << projectName << "_runner " << projectName << "_runner.cpp)\n";
    stream << "target_link_libraries(" << projectName << "_runner PRIVATE " << projectName << ")\n";
    stream << "set_target_properties(" << projectName << "_runner PROPERTIES OUTPUT_NAME \""
           << projectName << "\")\n\n";
    stream << "if(EXISTS ${PSXRECOMP_RESOURCE_DIR})\n";
    stream << "    add_custom_command(TARGET " << projectName << "_runner POST_BUILD\n";
    stream << "        COMMAND ${CMAKE_COMMAND} -E copy_directory\n";
    stream << "            ${PSXRECOMP_RESOURCE_DIR}\n";
    stream << "            $<TARGET_FILE_DIR:" << projectName << "_runner>/resources)\n";
    stream << "endif()\n";
    return stream.str();
}

std::string CodeGenerator::generateRunnerSource(const std::string& moduleName)
{
    CppEmitter emitter;
    emitter.writeLine("#include \"" + moduleName + ".h\"");
    emitter.writeBlank();
    emitter.writeLine("#include \"psxrecomp/types.h\"");
    emitter.writeLine("#include <exception>");
    emitter.writeLine("#include <filesystem>");
    emitter.writeLine("#include <iostream>");
    emitter.writeLine("#include <string>");
    emitter.writeBlank();
    emitter.writeLine("int main(int argc, char** argv)");
    emitter.openBlock("");
    emitter.writeLine("(void)argc;");
    emitter.writeLine("std::cout << \"[psxrecomp] Starting module: " + moduleName + "\\n\";");
    emitter.writeLine(
        "std::cout << \"[psxrecomp] Runtime checks: \" << PSXRECOMP_ENABLE_CHECKS << \"\\n\";");
    emitter.writeLine(
        "std::cout << \"[psxrecomp] Logging enabled: \" << PSXRECOMP_ENABLE_LOGGING << \"\\n\";");
    emitter.writeBlank();
    emitter.writeLine("if (argv != nullptr && argv[0] != nullptr)");
    emitter.openBlock("");
    emitter.writeLine(
        "std::filesystem::path exeDir = std::filesystem::path(argv[0]).parent_path();");
    emitter.writeLine("std::filesystem::path resourcesDir = exeDir / \"resources\";");
    emitter.writeLine("if (!std::filesystem::exists(resourcesDir))");
    emitter.openBlock("");
    emitter.writeLine("std::cerr << \"[psxrecomp][warn] resources directory not found near "
                      "executable: \" << resourcesDir.string() << \"\\n\";");
    emitter.writeLine("std::cerr << \"[psxrecomp][warn] Missing assets can result in a black "
                      "screen or silent startup.\\n\";");
    emitter.closeBlock();
    emitter.closeBlock();
    emitter.writeBlank();
    emitter.writeLine("try");
    emitter.openBlock("");
    emitter.writeLine("psxrecomp::runtime::PsxSystem system;");
    emitter.writeLine("if (!system.initialize())");
    emitter.openBlock("");
    emitter.writeLine(
        "std::cerr << \"[psxrecomp][error] Failed to initialize runtime system.\" << \"\\n\";");
    emitter.writeLine("return 1;");
    emitter.closeBlock();
    emitter.writeLine("psxrecomp::recompiler::RecompiledModule::configure(system);");
    emitter.writeLine("psxrecomp::recompiler::RecompiledModule::run(system);");
    emitter.writeLine("std::cout << \"[psxrecomp] Module execution returned.\" << \"\\n\";");
    emitter.writeLine(
        "std::cout << \"[psxrecomp] If nothing appears, the game may still be blocked by "
        "unimplemented hardware paths (GPU/SPU/CDROM/timing).\" << \"\\n\";");
    emitter.writeLine("return 0;");
    emitter.closeBlock();
    emitter.writeLine("catch (const std::exception& ex)");
    emitter.openBlock("");
    emitter.writeLine(
        "std::cerr << \"[psxrecomp][error] Unhandled exception: \" << ex.what() << \"\\n\";");
    emitter.writeLine("return 1;");
    emitter.closeBlock();
    emitter.writeLine("catch (...)");
    emitter.openBlock("");
    emitter.writeLine(
        "std::cerr << \"[psxrecomp][error] Unknown failure during module execution.\" << \"\\n\";");
    emitter.writeLine("return 1;");
    emitter.closeBlock();
    emitter.closeBlock();
    return emitter.str();
}
std::string CodeGenerator::generateGlobals(const ir::Program& program) const
{
    CppEmitter emitter;
    if (program.globals.empty())
    {
        emitter.writeLine("// No global data.");
        return emitter.str();
    }
    std::unordered_set<std::string> usedNames;
    for (const auto& global : program.globals)
    {
        std::string name = uniquifyIdentifier(global.name, usedNames);
        emitter.writeLine("static const std::array<u8, " + std::to_string(global.bytes.size()) +
                          "> " + name + " = {");
        if (!global.bytes.empty())
        {
            std::ostringstream stream;
            stream << "    ";
            for (size_t index = 0; index < global.bytes.size(); ++index)
            {
                stream << static_cast<int>(global.bytes[index]);
                if (index + 1 < global.bytes.size())
                {
                    stream << ", ";
                }
            }
            emitter.writeLine(stream.str());
        }
        emitter.writeLine("};");
    }
    return emitter.str();
}
std::string CodeGenerator::generateFunctionDeclarations(const ir::Program& program) const
{
    std::ostringstream stream;
    std::unordered_set<std::string> usedNames;
    for (const auto& function : program.functions)
    {
        std::string name = uniquifyIdentifier(function.name, usedNames);
        stream << "void " << name << "(RecompilerContext& context);\n";
    }
    return stream.str();
}
} // namespace recompiler
} // namespace psxrecomp
