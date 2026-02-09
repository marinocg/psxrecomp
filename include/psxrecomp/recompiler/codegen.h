#pragma once

#include "psxrecomp/ir/ir.h"
#include "psxrecomp/types.h"
#include <string>
#include <vector>

namespace psxrecomp
{
namespace recompiler
{

/**
 * @brief Code generation options
 */
struct CodeGenOptions
{
    bool enableOptimizations = true;
    bool preserveSymbols = false;
    bool generateComments = true;
    bool inlineFunctions = false;
    int optimizationLevel = 2;
};

struct ModuleMetadata
{
    struct DiscEntry
    {
        u32 index = 0;
        std::string label;
        std::string path;
    };
    std::string discSetName;
    u32 activeDiscIndex = 0;
    std::vector<DiscEntry> discs;
};

/**
 * @brief Generates C++ code from intermediate representation
 */
class CodeGenerator
{
  public:
    /**
     * @brief Construct a new Code Generator
     * @param options Code generation options
     */
    explicit CodeGenerator(const CodeGenOptions& options = CodeGenOptions{});

    /**
     * @brief Generate C++ header file
     * @param program IR program to emit declarations for
     * @param moduleName Name of the module
     * @return Generated header content
     */
    std::string generateHeader(const ir::Program& program, const std::string& moduleName);

    /**
     * @brief Generate C++ source file
     * @param program IR program to lower and emit definitions for
     * @param moduleName Name of the module
     * @param metadata Runtime metadata to embed
     * @return Generated source content
     */
    std::string generateSource(const ir::Program& program, const std::string& moduleName,
                               const ModuleMetadata& metadata = ModuleMetadata{});

    /**
     * @brief Generate CMakeLists.txt for recompiled code
     * @param projectName Name of the project
     * @return Generated CMake content
     */
    std::string generateBuildFile(const std::string& projectName);

  private:
    CodeGenOptions m_options;

    std::string generateGlobals(const ir::Program& program) const;
    std::string generateFunctionDeclarations(const ir::Program& program) const;
    std::string generateFunctionDefinitions(const ir::Program& program) const;
};

} // namespace recompiler
} // namespace psxrecomp
