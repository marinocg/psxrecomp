#include "psxrecomp/recompiler/codegen.h"

#include "codegen_helpers.h"
#include "cpp_emitter.h"

#include <sstream>
#include <unordered_set>

namespace psxrecomp
{
namespace recompiler
{

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
    stream << "option(PSXRECOMP_ENABLE_SDL_PRESENTER \"Enable SDL2 framebuffer presenter\" ON)\n";
    stream << "set(PSXRECOMP_HAS_SDL2 0)\n";
    stream << "if(PSXRECOMP_ENABLE_SDL_PRESENTER)\n";
    stream << "    find_package(SDL2 QUIET)\n";
    stream << "    if(SDL2_FOUND)\n";
    stream << "        set(PSXRECOMP_HAS_SDL2 1)\n";
    stream << "        target_compile_definitions(" << projectName
           << "_runner PRIVATE SDL_MAIN_HANDLED)\n";
    stream << "        target_link_libraries(" << projectName << "_runner PRIVATE SDL2::SDL2)\n";
    stream << "        if(TARGET SDL2::SDL2main)\n";
    stream << "            target_link_libraries(" << projectName
           << "_runner PRIVATE SDL2::SDL2main)\n";
    stream << "        endif()\n";
    stream << "    else()\n";
    stream << "        message(STATUS \"SDL2 not found; runner will use headless mode only.\")\n";
    stream << "    endif()\n";
    stream << "endif()\n";
    stream << "target_compile_definitions(" << projectName
           << "_runner PRIVATE PSXRECOMP_HAS_SDL2=${PSXRECOMP_HAS_SDL2})\n";
    stream << "if(WIN32)\n";
    stream << "    target_link_libraries(" << projectName << "_runner PRIVATE user32 gdi32)\n";
    stream << "endif()\n\n";
    stream << "if(EXISTS ${PSXRECOMP_RESOURCE_DIR})\n";
    stream << "    add_custom_command(TARGET " << projectName << "_runner POST_BUILD\n";
    stream << "        COMMAND ${CMAKE_COMMAND} -E copy_directory\n";
    stream << "            ${PSXRECOMP_RESOURCE_DIR}\n";
    stream << "            $<TARGET_FILE_DIR:" << projectName << "_runner>/resources)\n";
    stream << "endif()\n";
    return stream.str();
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
