#include "psxrecomp/runtime/diag_cdrom_late_buffer_tracker.h"

#include <stdexcept>
#include <string>

int main()
{
    using psxrecomp::runtime::DiagCdromLateBufferTracker;

    DiagCdromLateBufferTracker tracker;
    tracker.setEnabled(true);

    tracker.beginCdromDma(119u, 190u, 0x80025800u, 2048u);
    tracker.noteCdromDmaWord(0x44332211u);
    tracker.noteCdromDmaWord(0x88776655u);
    tracker.endCdromDma(2048u);
    tracker.noteCpuRead(0x150000u, 0x80025810u, 4u);

    tracker.beginCdromDma(120u, 191u, 0x80187158u, 2048u);
    tracker.noteCdromDmaWord(0x04030201u);
    tracker.noteCdromDmaWord(0x08070605u);
    tracker.noteCdromDmaWord(0x0C0B0A09u);
    tracker.noteCdromDmaWord(0x100F0E0Du);
    tracker.endCdromDma(2048u);
    tracker.noteCpuRead(0x1598F0u, 0x80187158u, 4u);
    tracker.noteCpuRead(0x154718u, 0x80187158u, 4u);

    if (tracker.recordCount() != 2u)
    {
        throw std::runtime_error("expected two tracked sector-sized CD DMA writes");
    }

    const auto* newest = tracker.recentRecord(0);
    if (newest == nullptr || newest->sourceGeneration != 120u || !newest->laterCpuRead ||
        newest->firstReaderPc != 0x1598F0u || !newest->laterParserRead ||
        newest->firstParserReadPc != 0x154718u)
    {
        throw std::runtime_error("latest record did not keep generation/read correlation");
    }

    const std::string summary = tracker.formatSummary();
    if (summary.find("gen=120 lba=191 dst=0x80187158 len=2048 class=frame_assembly_buffer "
                     "later_read=yes parser_read=yes consumer=other") == std::string::npos)
    {
        throw std::runtime_error("missing frame-assembly parser-read summary");
    }
    if (summary.find("parser_consumer=parser_hotloop first_parser_pc=0x154718 "
                     "first_parser_addr=0x80187158") == std::string::npos)
    {
        throw std::runtime_error("missing parser-read correlation");
    }
    if (summary.find("payload16=01 02 03 04 05 06 07 08 09 0a 0b 0c 0d 0e 0f 10") ==
        std::string::npos)
    {
        throw std::runtime_error("missing payload16 fingerprint");
    }
    if (summary.find("gen=119 lba=190 dst=0x80025800 len=2048 class=rolling_sector_buffer "
                     "later_read=yes parser_read=no consumer=other") == std::string::npos)
    {
        throw std::runtime_error("missing rolling-buffer summary");
    }
    if (summary.find("last_meaningful_sector: gen=120 lba=191 dst=0x80187158 "
                     "class=frame_assembly_buffer consumer=parser_hotloop parser_read=yes") ==
        std::string::npos)
    {
        throw std::runtime_error("missing last meaningful sector line");
    }

    tracker.beginCdromDma(121u, 192u, 0x80187158u, 2048u);
    tracker.endCdromDma(2048u);
    tracker.noteCpuRead(0x15471Cu, 0x80187158u, 4u);
    const auto* replaced = tracker.recentRecord(0);
    if (replaced == nullptr || replaced->sourceGeneration != 121u || !replaced->laterCpuRead)
    {
        throw std::runtime_error("newest matching destination did not own the later read");
    }

    return 0;
}
