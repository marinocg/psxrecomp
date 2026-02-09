#pragma once

#include "psxrecomp/iso/psx_exe_loader.h"

#include <vector>

namespace psxrecomp
{
namespace iso
{
namespace detail
{

bool parseOverlayTable(const std::vector<u8>& data, const PsxExeHeader& header,
                       std::vector<PsxExeImage::Segment>& segments, PsxExeDiagnostics* diagnostics);

void extractSyscalls(const std::vector<PsxExeImage::Segment>& segments,
                     const std::vector<u8>& programData,
                     std::vector<PsxExeImage::SyscallMetadata>& syscalls);

void buildDefaultSymbols(const PsxExeImage& image, std::vector<PsxExeImage::Symbol>& symbols);

} // namespace detail
} // namespace iso
} // namespace psxrecomp
