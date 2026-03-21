#include "codegen_source_internal.h"

#include "cpp_emitter.h"

#include <sstream>

namespace psxrecomp
{
namespace recompiler
{
namespace
{

void emitAddressArray(CppEmitter& emitter, const std::string& name,
                      const std::vector<Address>& values)
{
    std::ostringstream declaration;
    declaration << "static constexpr std::array<Address, " << values.size() << "> " << name
                << " = {{";
    emitter.writeLine(declaration.str());
    for (size_t index = 0; index < values.size(); ++index)
    {
        std::ostringstream line;
        line << "    0x" << std::hex << values[index];
        if (index + 1 < values.size())
        {
            line << ",";
        }
        emitter.writeLine(line.str());
    }
    emitter.writeLine("}};");
}

} // namespace

void emitGeneratedSourceDebugSupport(CppEmitter& emitter, const ModuleMetadata& metadata)
{
    emitter.openBlock("struct IndirectCallSiteInfo");
    emitter.writeLine("Address callerPc;");
    emitter.writeLine("Address containingFunction;");
    emitter.writeLine("Address pointerWordAddress;");
    emitter.writeLine("u8 sourceRegister;");
    emitter.writeLine("s8 pointerBaseRegister;");
    emitter.writeLine("s16 pointerOffset;");
    emitter.writeLine("bool hasStaticPointerWordAddress;");
    emitter.writeLine("bool pointerLoadClobbersBase;");
    emitter.closeBlock(";");
    emitter.writeBlank();

    std::ostringstream siteDecl;
    siteDecl << "static constexpr std::array<IndirectCallSiteInfo, "
             << metadata.indirectCallSites.size() << "> kIndirectCallSites = {{";
    emitter.writeLine(siteDecl.str());
    for (size_t index = 0; index < metadata.indirectCallSites.size(); ++index)
    {
        const auto& site = metadata.indirectCallSites[index];
        std::ostringstream line;
        line << "    {0x" << std::hex << site.callerPc << ", 0x" << site.containingFunction
             << ", 0x" << site.pointerWordAddress << ", " << std::dec
             << static_cast<unsigned>(site.sourceRegister) << ", "
             << static_cast<int>(site.pointerBaseRegister) << ", "
             << static_cast<int>(site.pointerOffset) << ", "
             << (site.hasStaticPointerWordAddress ? "true" : "false") << ", "
             << (site.pointerLoadClobbersBase ? "true" : "false") << "}";
        if (index + 1 < metadata.indirectCallSites.size())
        {
            line << ",";
        }
        emitter.writeLine(line.str());
    }
    emitter.writeLine("}};");
    emitter.writeBlank();

    emitAddressArray(emitter, "kHarvestedFunctionEntries", metadata.harvestedFunctionEntries);
    emitter.writeBlank();
    emitAddressArray(emitter, "kKnownPointerTableWords", metadata.knownPointerTableWords);
    emitter.writeBlank();

    std::ostringstream moduleStart;
    moduleStart << "static constexpr Address kModuleLoadAddress = 0x" << std::hex
                << (metadata.loadAddress & 0x1FFFFFFFu) << ";";
    emitter.writeLine(moduleStart.str());
    std::ostringstream moduleEnd;
    moduleEnd << "static constexpr Address kModuleLoadEnd = 0x" << std::hex
              << ((metadata.loadAddress & 0x1FFFFFFFu) + metadata.loadSize) << ";";
    emitter.writeLine(moduleEnd.str());
    emitter.writeBlank();

    emitter.writeLine("inline const char* registerName(Register reg)");
    emitter.openBlock("");
    emitter.openBlock("switch (reg)");
    emitter.writeLine("case Registers::ZERO: return \"ZERO\";");
    emitter.writeLine("case Registers::AT: return \"AT\";");
    emitter.writeLine("case Registers::V0: return \"V0\";");
    emitter.writeLine("case Registers::V1: return \"V1\";");
    emitter.writeLine("case Registers::A0: return \"A0\";");
    emitter.writeLine("case Registers::A1: return \"A1\";");
    emitter.writeLine("case Registers::A2: return \"A2\";");
    emitter.writeLine("case Registers::A3: return \"A3\";");
    emitter.writeLine("case Registers::T0: return \"T0\";");
    emitter.writeLine("case Registers::T1: return \"T1\";");
    emitter.writeLine("case Registers::T2: return \"T2\";");
    emitter.writeLine("case Registers::T3: return \"T3\";");
    emitter.writeLine("case Registers::T4: return \"T4\";");
    emitter.writeLine("case Registers::T5: return \"T5\";");
    emitter.writeLine("case Registers::T6: return \"T6\";");
    emitter.writeLine("case Registers::T7: return \"T7\";");
    emitter.writeLine("case Registers::S0: return \"S0\";");
    emitter.writeLine("case Registers::S1: return \"S1\";");
    emitter.writeLine("case Registers::S2: return \"S2\";");
    emitter.writeLine("case Registers::S3: return \"S3\";");
    emitter.writeLine("case Registers::S4: return \"S4\";");
    emitter.writeLine("case Registers::S5: return \"S5\";");
    emitter.writeLine("case Registers::S6: return \"S6\";");
    emitter.writeLine("case Registers::S7: return \"S7\";");
    emitter.writeLine("case Registers::T8: return \"T8\";");
    emitter.writeLine("case Registers::T9: return \"T9\";");
    emitter.writeLine("case Registers::K0: return \"K0\";");
    emitter.writeLine("case Registers::K1: return \"K1\";");
    emitter.writeLine("case Registers::GP: return \"GP\";");
    emitter.writeLine("case Registers::SP: return \"SP\";");
    emitter.writeLine("case Registers::FP: return \"FP\";");
    emitter.writeLine("case Registers::RA: return \"RA\";");
    emitter.writeLine("default: return \"?\";");
    emitter.closeBlock();
    emitter.closeBlock();
    emitter.writeBlank();

    emitter.writeLine("template <typename Container>");
    emitter.writeLine("inline bool containsAddress(const Container& values, Address address)");
    emitter.openBlock("");
    emitter.writeLine("return std::binary_search(values.begin(), values.end(), address);");
    emitter.closeBlock();
    emitter.writeBlank();

    emitter.writeLine("inline const IndirectCallSiteInfo* findIndirectCallSite(Address callerPc)");
    emitter.openBlock("");
    emitter.writeLine("auto it = std::lower_bound(");
    emitter.writeLine("    kIndirectCallSites.begin(), kIndirectCallSites.end(), callerPc,");
    emitter.writeLine("    [](const IndirectCallSiteInfo& site, Address target)");
    emitter.writeLine("    { return site.callerPc < target; });");
    emitter.writeLine("if (it != kIndirectCallSites.end() && it->callerPc == callerPc)");
    emitter.openBlock("");
    emitter.writeLine("return &(*it);");
    emitter.closeBlock();
    emitter.writeLine("return nullptr;");
    emitter.closeBlock();
    emitter.writeBlank();

    emitter.writeLine("inline bool canReadDescriptorWord(Address address)");
    emitter.openBlock("");
    emitter.writeLine("const Address physical = address & 0x1FFFFFFF;");
    emitter.writeLine("if ((address & 0x3u) != 0)");
    emitter.openBlock("");
    emitter.writeLine("return false;");
    emitter.closeBlock();
    emitter.writeLine("if (psxrecomp::runtime::isMainRamAddress(physical, sizeof(u32)))");
    emitter.openBlock("");
    emitter.writeLine("return true;");
    emitter.closeBlock();
    emitter.writeLine("if (address >= psxrecomp::MemoryMap::SCRATCHPAD_BASE &&");
    emitter.writeLine("    address <= psxrecomp::MemoryMap::SCRATCHPAD_BASE +");
    emitter.writeLine("                   psxrecomp::MemoryMap::SCRATCHPAD_SIZE - sizeof(u32))");
    emitter.openBlock("");
    emitter.writeLine("return true;");
    emitter.closeBlock();
    emitter.writeLine("if (address >= psxrecomp::MemoryMap::BIOS_BASE &&");
    emitter.writeLine("    address <= psxrecomp::MemoryMap::BIOS_BASE +");
    emitter.writeLine("                   psxrecomp::MemoryMap::BIOS_SIZE - sizeof(u32))");
    emitter.openBlock("");
    emitter.writeLine("return true;");
    emitter.closeBlock();
    emitter.writeLine("return false;");
    emitter.closeBlock();
    emitter.writeBlank();

    emitter.writeLine("inline std::string formatDescriptorWords(RecompilerContext& context,");
    emitter.writeLine("                                        const IndirectCallSiteInfo* site,");
    emitter.writeLine("                                        bool* outKnownPointerWord)");
    emitter.openBlock("");
    emitter.writeLine("*outKnownPointerWord = false;");
    emitter.openBlock("if (site == nullptr)");
    emitter.writeLine("return \"descriptor=unavailable\";");
    emitter.closeBlock();
    emitter.writeLine("Address descriptorAddress = 0;");
    emitter.writeLine("const char* descriptorSource = \"unavailable\";");
    emitter.openBlock("if (site->hasStaticPointerWordAddress)");
    emitter.writeLine("descriptorAddress = site->pointerWordAddress;");
    emitter.writeLine("descriptorSource = \"static\";");
    emitter.closeBlock();
    emitter.openBlock("else if (site->pointerBaseRegister >= 0 && !site->pointerLoadClobbersBase)");
    emitter.writeLine("descriptorAddress =");
    emitter.writeLine("    static_cast<Address>(context.regs[site->pointerBaseRegister] +");
    emitter.writeLine("                         static_cast<s32>(site->pointerOffset));");
    emitter.writeLine("descriptorSource = \"runtime\";");
    emitter.closeBlock();
    emitter.openBlock("else");
    emitter.writeLine("return \"descriptor=unavailable(base-overwritten-by-load)\";");
    emitter.closeBlock();
    emitter.writeLine(
        "*outKnownPointerWord = containsAddress(kKnownPointerTableWords, descriptorAddress);");
    emitter.openBlock("if (!canReadDescriptorWord(descriptorAddress))");
    emitter.writeLine("std::ostringstream stream;");
    emitter.writeLine("stream << \"descriptor=unreadable@0x\" << std::hex << descriptorAddress");
    emitter.writeLine("       << \" source=\" << descriptorSource;");
    emitter.writeLine("return stream.str();");
    emitter.closeBlock();
    emitter.writeLine("std::ostringstream stream;");
    emitter.writeLine("const Address descriptorStart =");
    emitter.writeLine("    descriptorAddress >= 8 ? descriptorAddress - 8 : descriptorAddress;");
    emitter.writeLine("stream << \"descriptor@0x\" << std::hex << descriptorAddress");
    emitter.writeLine("       << \" source=\" << descriptorSource << \" words=[\";");
    emitter.writeLine("for (int wordIndex = 0; wordIndex < 5; ++wordIndex)");
    emitter.openBlock("");
    emitter.writeLine(
        "const Address wordAddress = descriptorStart + static_cast<Address>(wordIndex * 4);");
    emitter.writeLine("if (wordIndex > 0)");
    emitter.openBlock("");
    emitter.writeLine("stream << \", \";");
    emitter.closeBlock();
    emitter.writeLine("if (!canReadDescriptorWord(wordAddress))");
    emitter.openBlock("");
    emitter.writeLine("stream << \"?\";");
    emitter.writeLine("continue;");
    emitter.closeBlock();
    emitter.writeLine("stream << \"0x\" << context.system.read<u32>(wordAddress);");
    emitter.closeBlock();
    emitter.writeLine("stream << \"]\";");
    emitter.writeLine("return stream.str();");
    emitter.closeBlock();
    emitter.writeBlank();

    emitter.writeLine("inline bool interestingCallsiteTraceEnabled()");
    emitter.openBlock("");
    emitter.writeLine("static const bool enabled = []()");
    emitter.openBlock("");
    emitter.writeLine("if (const char* env = std::getenv(\"PSXRECOMP_TRACE_CD_CALLBACK\"))");
    emitter.openBlock("");
    emitter.writeLine("return env[0] == '1';");
    emitter.closeBlock();
    emitter.writeLine("return false;");
    emitter.closeBlock("();");
    emitter.writeLine("return enabled;");
    emitter.closeBlock();
    emitter.writeBlank();

    const std::string traceInterestingLoadDecl =
        "inline void traceInterestingLoad(RecompilerContext& context, Address address,";
    emitter.writeLine(traceInterestingLoadDecl);
    emitter.writeLine("                                 u32 value, Address pc)");
    emitter.openBlock("");
    emitter.openBlock("if (!interestingCallsiteTraceEnabled())");
    emitter.writeLine("return;");
    emitter.closeBlock();
    emitter.writeLine("const Address callerPc = pc & 0x1FFFFFFF;");
    emitter.openBlock("if (callerPc != 0x3E734u)");
    emitter.writeLine("return;");
    emitter.closeBlock();
    emitter.writeLine("const Address descriptorWordAddress = address & 0x1FFFFFFF;");
    emitter.writeLine("const Address descriptorBase = descriptorWordAddress >= 8u ? "
                      "descriptorWordAddress - 8u : 0u;");
    const std::string traceLoadStream =
        "std::cerr << \"[psxrecomp][cdcb] phase=load caller=0x\" << std::hex << callerPc";
    emitter.writeLine(traceLoadStream);
    emitter.writeLine("          << \" descriptor-base=0x\" << descriptorBase");
    emitter.writeLine("          << \" function-ptr@+8=0x\" << value");
    emitter.writeLine("          << \" address=0x\" << descriptorWordAddress");
    emitter.writeLine("          << \" \" << context.system.describeBiosCdromState() << \"\\n\";");
    emitter.closeBlock();
    emitter.writeBlank();

    const std::string traceInterestingCallsiteDecl =
        "inline void traceInterestingCallsite(RecompilerContext& context, Address target,";
    emitter.writeLine(traceInterestingCallsiteDecl);
    emitter.writeLine("                                     Address pc, bool afterCall)");
    emitter.openBlock("");
    emitter.openBlock("if (!interestingCallsiteTraceEnabled())");
    emitter.writeLine("return;");
    emitter.closeBlock();
    emitter.writeLine("const Address callerPc = pc & 0x1FFFFFFF;");
    emitter.writeLine("const Address physicalTarget = target & 0x1FFFFFFF;");
    emitter.openBlock("if (callerPc != 0x3E73Cu && physicalTarget != 0x3EB50u)");
    emitter.writeLine("return;");
    emitter.closeBlock();
    emitter.writeLine("const IndirectCallSiteInfo* site = findIndirectCallSite(callerPc);");
    emitter.writeLine("Address descriptorWordAddress = 0;");
    emitter.writeLine("Address descriptorBase = 0;");
    emitter.writeLine("u32 descriptorFunction = 0;");
    emitter.writeLine("bool haveDescriptorFunction = false;");
    emitter.openBlock("if (site != nullptr)");
    emitter.writeLine("if (site->hasStaticPointerWordAddress)");
    emitter.openBlock("");
    emitter.writeLine("descriptorWordAddress = site->pointerWordAddress;");
    emitter.closeBlock();
    emitter.writeLine("else if (site->pointerBaseRegister >= 0 && !site->pointerLoadClobbersBase)");
    emitter.openBlock("");
    emitter.writeLine("descriptorWordAddress =");
    emitter.writeLine("    static_cast<Address>(context.regs[site->pointerBaseRegister] +");
    emitter.writeLine("                         static_cast<s32>(site->pointerOffset));");
    const std::string descriptorBaseAssign =
        "descriptorBase = static_cast<Address>(context.regs[site->pointerBaseRegister]);";
    emitter.writeLine(descriptorBaseAssign);
    emitter.closeBlock();
    emitter.openBlock("if (canReadDescriptorWord(descriptorWordAddress))");
    emitter.writeLine("descriptorFunction = context.system.read<u32>(descriptorWordAddress);");
    emitter.writeLine("haveDescriptorFunction = true;");
    emitter.closeBlock();
    emitter.closeBlock();
    emitter.writeLine("bool knownPointerWord = false;");
    const std::string descriptorLine =
        "const std::string descriptor = formatDescriptorWords(context, site, &knownPointerWord);";
    emitter.writeLine(descriptorLine);
    const std::string traceCallsiteStream =
        "std::cerr << \"[psxrecomp][cdcb] phase=\" << (afterCall ? \"after\" : \"before\")";
    emitter.writeLine(traceCallsiteStream);
    emitter.writeLine("          << \" caller=0x\" << std::hex << callerPc");
    emitter.writeLine("          << \" target=0x\" << physicalTarget");
    emitter.writeLine("          << \" descriptor-base=0x\" << descriptorBase");
    const std::string functionPtrLine =
        "          << \" function-ptr@+8=0x\" << (haveDescriptorFunction ? descriptorFunction : 0)";
    emitter.writeLine(functionPtrLine);
    emitter.writeLine("          << \" v0=0x\" << context.regs[Registers::V0]");
    emitter.writeLine("          << \" a0=0x\" << context.regs[Registers::A0]");
    emitter.writeLine("          << \" a1=0x\" << context.regs[Registers::A1]");
    emitter.writeLine("          << \" known-pointer=\" << (knownPointerWord ? \"yes\" : \"no\")");
    emitter.writeLine("          << \" \" << descriptor");
    emitter.writeLine("          << \" \" << context.system.describeBiosCdromState() << \"\\n\";");
    emitter.closeBlock();
    emitter.writeBlank();

    emitter.openBlock("struct UnsupportedIndirectCallRecord");
    emitter.writeLine("Address target = 0;");
    emitter.writeLine("Address firstCallerPc = 0;");
    emitter.writeLine("u32 hits = 0;");
    emitter.closeBlock(";");
    emitter.writeBlank();

    emitter.openBlock("class UnsupportedIndirectCallTracker");
    emitter.writeLine("public:");
    emitter.writeLine("    void record(RecompilerContext& context, Address target, Address pc)");
    emitter.writeLine("    {");
    emitter.writeLine("        const Address physicalTarget = target & 0x1FFFFFFF;");
    emitter.writeLine("        const Address normalizedPc = pc & 0x1FFFFFFF;");
    emitter.writeLine(
        "        const IndirectCallSiteInfo* site = findIndirectCallSite(normalizedPc);");
    emitter.writeLine("        auto it = std::find_if(records.begin(), records.end(),");
    emitter.writeLine(
        "                               [physicalTarget](const UnsupportedIndirectCallRecord&"
        " record)");
    emitter.writeLine(
        "                               { return record.target == physicalTarget; });");
    emitter.writeLine("        if (it == records.end())");
    emitter.writeLine("        {");
    emitter.writeLine("            records.push_back({physicalTarget, normalizedPc, 1});");
    emitter.writeLine(
        "            emitFirstOccurrence(context, target, physicalTarget, normalizedPc, site);");
    emitter.writeLine("            return;");
    emitter.writeLine("        }");
    emitter.writeLine("        ++it->hits;");
    emitter.writeLine("    }");
    emitter.writeLine("    ~UnsupportedIndirectCallTracker()");
    emitter.writeLine("    {");
    emitter.writeLine("        if (records.empty())");
    emitter.writeLine("        {");
    emitter.writeLine("            return;");
    emitter.writeLine("        }");
    emitter.writeLine("        u32 totalHits = 0;");
    emitter.writeLine("        for (const auto& record : records)");
    emitter.writeLine("        {");
    emitter.writeLine("            totalHits += record.hits;");
    emitter.writeLine("        }");
    emitter.writeLine("        std::cerr <<");
    emitter.writeLine("            \"[psxrecomp][summary] Unsupported indirect calls: unique=\"");
    emitter.writeLine("            << std::dec");
    emitter.writeLine(
        "                  << records.size() << \" totalHits=\" << totalHits << \"\\n\";");
    emitter.writeLine("        std::vector<UnsupportedIndirectCallRecord> sorted = records;");
    emitter.writeLine("        std::sort(sorted.begin(), sorted.end(),");
    emitter.writeLine("                  [](const UnsupportedIndirectCallRecord& lhs,");
    emitter.writeLine("                     const UnsupportedIndirectCallRecord& rhs)");
    emitter.writeLine("                  {");
    emitter.writeLine("                      if (lhs.hits != rhs.hits)");
    emitter.writeLine("                      {");
    emitter.writeLine("                          return lhs.hits > rhs.hits;");
    emitter.writeLine("                      }");
    emitter.writeLine("                      return lhs.target < rhs.target;");
    emitter.writeLine("                  });");
    emitter.writeLine("        for (const auto& record : sorted)");
    emitter.writeLine("        {");
    emitter.writeLine(
        "            std::cerr << \"[psxrecomp][summary] target=0x\" << std::hex << record.target");
    emitter.writeLine("                      << \" hits=\" << std::dec << record.hits");
    emitter.writeLine(
        "                      << \" firstCaller=0x\" << std::hex << record.firstCallerPc");
    emitter.writeLine("                      << \"\\n\";");
    emitter.writeLine("        }");
    emitter.writeLine("    }");
    emitter.writeLine("private:");
    emitter.writeLine(
        "    void emitFirstOccurrence(RecompilerContext& context, Address rawTarget,");
    emitter.writeLine("                             Address physicalTarget, Address callerPc,");
    emitter.writeLine("                             const IndirectCallSiteInfo* site)");
    emitter.writeLine("    {");
    emitter.writeLine("        bool knownPointerWord = false;");
    emitter.writeLine("        const std::string descriptor =");
    emitter.writeLine("            formatDescriptorWords(context, site, &knownPointerWord);");
    emitter.writeLine("        const bool targetAligned = (physicalTarget & 0x3u) == 0;");
    emitter.writeLine(
        "        const bool targetInExecutable = physicalTarget >= kModuleLoadAddress &&");
    emitter.writeLine("                                       physicalTarget < kModuleLoadEnd;");
    emitter.writeLine("        const bool targetHarvested =");
    emitter.writeLine("            containsAddress(kHarvestedFunctionEntries, physicalTarget);");
    emitter.writeLine("        std::cerr <<");
    emitter.writeLine(
        "            \"[psxrecomp][warn] Unsupported CALL target 0x\" << std::hex << rawTarget");
    emitter.writeLine("                  << \" at PC 0x\" << callerPc");
    emitter.writeLine("                  << \" caller=0x\" << callerPc");
    emitter.writeLine("                  << \" function=0x\" <<");
    emitter.writeLine("                     (site != nullptr ? site->containingFunction : 0)");
    emitter.writeLine("                  << \" src=\" <<");
    emitter.writeLine(
        "                     (site != nullptr ? registerName(site->sourceRegister) : \"?\")");
    emitter.writeLine(
        "                  << \" executable=\" << (targetInExecutable ? \"yes\" : \"no\")");
    emitter.writeLine("                  << \" aligned=\" << (targetAligned ? \"yes\" : \"no\")");
    emitter.writeLine(
        "                  << \" harvested=\" << (targetHarvested ? \"yes\" : \"no\")");
    emitter.writeLine("                  << \" pointer-word=0x\" <<");
    emitter.writeLine("                     (site != nullptr ? site->pointerWordAddress : 0)");
    emitter.writeLine(
        "                  << \" pointer-table=\" << (knownPointerWord ? \"yes\" : \"no\")");
    emitter.writeLine("                  << \" \" << descriptor << \"\\n\";");
    emitter.writeLine("    }");
    emitter.writeLine("    std::vector<UnsupportedIndirectCallRecord> records;");
    emitter.closeBlock(";");
    emitter.writeBlank();

    emitter.writeLine("inline UnsupportedIndirectCallTracker& unsupportedIndirectCallTracker()");
    emitter.openBlock("");
    emitter.writeLine("static UnsupportedIndirectCallTracker tracker;");
    emitter.writeLine("return tracker;");
    emitter.closeBlock();
    emitter.writeBlank();

    emitter.writeLine(
        "inline void failUnsupportedCall(RecompilerContext& context, Address target, Address pc)");
    emitter.openBlock("");
    emitter.writeLine("unsupportedIndirectCallTracker().record(context, target, pc);");
    emitter.closeBlock();
    emitter.writeBlank();
}

} // namespace recompiler
} // namespace psxrecomp
