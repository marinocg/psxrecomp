#pragma once

#include "psxrecomp/recompiler/pipeline.h"

#include <filesystem>

namespace pipeline_candidate_test_support
{

void runEmbeddedScanDeterminismScenario(const psxrecomp::recompiler::PipelineOptions& baseOptions,
                                        const std::filesystem::path& outputDir);

} // namespace pipeline_candidate_test_support
