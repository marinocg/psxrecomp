#pragma once

#include "psxrecomp/recompiler/pipeline.h"

#include <string>

void printJsonOutput(const psxrecomp::recompiler::PipelineResult& result,
                     const std::string& inputFile, const std::string& outputDir);
