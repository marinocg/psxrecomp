#pragma once

#include "psxrecomp/iso/psx_exe_loader.h"
#include "psxrecomp/recompiler/pipeline.h"
#include "psxrecomp/types.h"

#include <filesystem>
#include <string>
#include <vector>

namespace psxrecomp
{
namespace recompiler
{
namespace detail
{

std::string toLower(std::string value);
bool isIsoLikePath(const std::filesystem::path& path);
std::string sanitizeModuleName(const std::string& name);
std::string formatHex(u64 value, size_t width);
bool writeFile(const std::filesystem::path& path, const std::string& contents,
               std::string& outError);
Address resolveEntryAddress(const iso::PsxExeImage& image);
std::string toDiagnosticCode(iso::PsxExeErrorCode code);
PipelineDiagnostic toPipelineDiagnostic(const iso::PsxExeDiagnostic& diagnostic,
                                        const std::string& sourcePath);
void appendDiagnostics(std::vector<PipelineDiagnostic>& diagnostics,
                       std::vector<std::string>& warnings,
                       const iso::PsxExeDiagnostics& exeDiagnostics, const std::string& sourcePath);
u64 fnv1a64(const std::vector<u8>& data);
std::string buildTimestamp(const std::string& overrideTimestamp);
DiscSetMetadata buildDiscSetMetadata(const std::vector<std::string>& discPaths, size_t activeIndex,
                                     std::vector<std::string>& warnings);
std::string makeDeterministicTag(u32 loadAddress, const std::string& hash);
std::string buildOutputDirectory(const std::filesystem::path& baseDir, const std::string& inputStem,
                                 const std::string& volumeLabel, const std::string& exeTag);
bool copyDirectoryRecursive(const std::filesystem::path& source,
                            const std::filesystem::path& destination, std::string& outError);
std::filesystem::path repositoryRootFromSourcePath(const std::filesystem::path& sourcePath);
std::string serializeManifest(const PipelineResult& result, const std::string& inputPath,
                              const std::string& outputDir, const std::string& timestamp,
                              const std::string& pipelineVersion);

/**
 * @brief Write generated source artifacts and export ISO resources.
 *
 * @param result Pipeline result populated so far (artifacts are updated).
 * @param outputDir Resolved output directory.
 * @param moduleName Module identifier.
 * @param header Generated header source.
 * @param source Generated source source.
 * @param runnerSource Generated runner source.
 * @param buildFile Generated CMakeLists.txt content.
 * @param activeDiscPath Path to the active disc image.
 * @param inputFsPath Input path (used to decide whether to export resources).
 * @param warnings Mutable warnings list.
 * @param diagnostics Mutable diagnostics list.
 * @param manifestTimestamp Override for manifest timestamp.
 * @param pipelineVersion Pipeline version string.
 * @param outError Error string on failure.
 * @return true on success.
 */
bool writeOutputArtifacts(PipelineResult& result, const std::filesystem::path& outputDir,
                          const std::string& moduleName, const std::string& header,
                          const std::string& source, const std::string& runnerSource,
                          const std::string& buildFile, const std::string& activeDiscPath,
                          const std::filesystem::path& inputFsPath,
                          std::vector<std::string>& warnings,
                          std::vector<PipelineDiagnostic>& diagnostics,
                          const std::string& manifestTimestamp, const std::string& pipelineVersion,
                          std::string& outError);

} // namespace detail
} // namespace recompiler
} // namespace psxrecomp
