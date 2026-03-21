#pragma once

#include "psxrecomp/recompiler/codegen.h"

#include <filesystem>
#include <string>

void runCodegenOverlapTest(psxrecomp::recompiler::CodeGenerator& generator);
void runCodegenCompileHarnessTest(psxrecomp::recompiler::CodeGenerator& generator,
                                  const std::string& header, const std::string& source,
                                  const std::filesystem::path& outputDir,
                                  const std::filesystem::path& repoRoot,
                                  const std::string& compiler);
void runCodegenCop2GuardTest(psxrecomp::recompiler::CodeGenerator& generator,
                             const std::filesystem::path& outputDir,
                             const std::filesystem::path& repoRoot, const std::string& compiler);
