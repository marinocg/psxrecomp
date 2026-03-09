#include "psxrecomp/runtime/diag_profile.h"

#include "json_helpers.h"
#include "psxrecomp/runtime/logger.h"

#include <cstdlib>
#include <fstream>
#include <sstream>

namespace psxrecomp
{
namespace runtime
{

namespace
{

Address parseHexAddress(const std::string& text)
{
    if (text.empty())
    {
        return 0;
    }
    return static_cast<Address>(std::strtoull(text.c_str(), nullptr, 0));
}

void parseAddressRange(const std::string& text, Address& start, Address& end)
{
    const auto dash = text.find('-');
    if (dash == std::string::npos)
    {
        start = end = parseHexAddress(text);
        return;
    }
    start = parseHexAddress(text.substr(0, dash));
    end = parseHexAddress(text.substr(dash + 1));
}

WatchpointKind parseWatchpointKind(const std::string& text)
{
    if (text == "ram_read")
        return WatchpointKind::RamRead;
    if (text == "ram_write")
        return WatchpointKind::RamWrite;
    if (text == "mmio_read")
        return WatchpointKind::MmioRead;
    if (text == "mmio_write")
        return WatchpointKind::MmioWrite;
    return WatchpointKind::RamWrite;
}

WatchpointAction parseWatchpointAction(const std::string& text)
{
    if (text == "log")
        return WatchpointAction::Log;
    if (text == "summarize")
        return WatchpointAction::Summarize;
    if (text == "trap")
        return WatchpointAction::Trap;
    if (text == "trap_on_first_violation")
        return WatchpointAction::TrapOnFirstViolation;
    return WatchpointAction::Log;
}

ValidatorType parseValidatorType(const std::string& text)
{
    if (text == "pointer_cell")
        return ValidatorType::PointerCell;
    if (text == "linked_list")
        return ValidatorType::LinkedList;
    if (text == "sentinel_block_chain")
        return ValidatorType::SentinelBlockChain;
    if (text == "bounded_structure_walk")
        return ValidatorType::BoundedStructureWalk;
    return ValidatorType::SentinelBlockChain;
}

BoundaryKind parseBoundaryKind(const std::string& text)
{
    if (text == "irq_callback_return")
        return BoundaryKind::IrqCallbackReturn;
    if (text == "bios_call_return")
        return BoundaryKind::BiosCallReturn;
    if (text == "bios_memory_op")
        return BoundaryKind::BiosMemoryOp;
    if (text == "dma_completion")
        return BoundaryKind::DmaCompletion;
    if (text == "mmio_poll_suspicion")
        return BoundaryKind::MmioPollSuspicion;
    if (text == "function_return")
        return BoundaryKind::FunctionReturn;
    if (text == "stall_detected")
        return BoundaryKind::StallDetected;
    return BoundaryKind::IrqCallbackReturn;
}

ExplainerKind parseExplainerKind(const std::string& text)
{
    if (text == "gpustat")
        return ExplainerKind::Gpustat;
    if (text == "cdrom_irq")
        return ExplainerKind::CdromIrq;
    if (text == "irq_controller")
        return ExplainerKind::IrqController;
    if (text == "dma_channel")
        return ExplainerKind::DmaChannel;
    return ExplainerKind::Gpustat;
}

WatchpointConfig parseWatchpoint(const JsonValue& obj)
{
    WatchpointConfig wp;
    wp.name = obj.getString("name");
    wp.kind = parseWatchpointKind(obj.getString("kind"));
    parseAddressRange(obj.getString("range"), wp.rangeStart, wp.rangeEnd);
    wp.action = parseWatchpointAction(obj.getString("action"));

    if (const auto* pred = obj.get("predicate"))
    {
        wp.predicate.type = pred->getString("type");
        parseAddressRange(pred->getString("region"), wp.predicate.regionStart,
                          wp.predicate.regionEnd);
        wp.predicate.valueMin = static_cast<u32>(pred->getNumber("value_min"));
        wp.predicate.valueMax = static_cast<u32>(pred->getNumber("value_max"));
        wp.predicate.bitmask = parseHexAddress(pred->getString("bitmask"));
    }
    return wp;
}

TracepointConfig parseTracepoint(const JsonValue& obj)
{
    TracepointConfig tp;
    tp.name = obj.getString("name");
    parseAddressRange(obj.getString("pc_range"), tp.pcRangeStart, tp.pcRangeEnd);
    tp.logBranches = obj.getBool("log_branches");
    for (const auto& reg : obj.getArray("registers"))
    {
        if (reg.isString())
        {
            tp.registers.push_back(reg.strVal);
        }
    }
    for (const auto& addr : obj.getArray("mmio_reads"))
    {
        if (addr.isString())
        {
            tp.mmioReads.push_back(parseHexAddress(addr.strVal));
        }
    }
    return tp;
}

ValidatorConfig parseValidator(const JsonValue& obj)
{
    ValidatorConfig vc;
    vc.name = obj.getString("name");
    vc.type = parseValidatorType(obj.getString("type"));
    if (const auto* ew = obj.get("enabled_when"))
    {
        vc.enabledWhen.type = ew->getString("type");
        vc.enabledWhen.address = parseHexAddress(ew->getString("address"));
    }
    if (const auto* roots = obj.get("roots"))
    {
        vc.currentRoot = parseHexAddress(roots->getString("current"));
        vc.backupRoot = parseHexAddress(roots->getString("backup"));
    }
    parseAddressRange(obj.getString("region"), vc.regionStart, vc.regionEnd);
    if (const auto* hdr = obj.get("header"))
    {
        vc.header.sizeMask = parseHexAddress(hdr->getString("size_mask", "0xfffffffc"));
        vc.header.freeBit = parseHexAddress(hdr->getString("free_bit", "0x1"));
        vc.header.sentinel = parseHexAddress(hdr->getString("sentinel", "0xfffffffe"));
    }
    vc.maxNodes = static_cast<size_t>(obj.getNumber("max_nodes", 64.0));
    return vc;
}

BoundaryConfig parseBoundary(const JsonValue& obj)
{
    BoundaryConfig bc;
    bc.kind = parseBoundaryKind(obj.getString("kind"));
    bc.trapOnFailure = obj.getBool("trap_on_failure");
    for (const auto& name : obj.getArray("run"))
    {
        if (name.isString())
        {
            bc.runValidators.push_back(name.strVal);
        }
    }
    for (const auto& name : obj.getArray("run_explainers"))
    {
        if (name.isString())
        {
            bc.runExplainers.push_back(name.strVal);
        }
    }
    return bc;
}

ExplainerConfig parseExplainer(const JsonValue& obj)
{
    ExplainerConfig ec;
    ec.kind = parseExplainerKind(obj.getString("kind"));
    ec.address = parseHexAddress(obj.getString("address"));
    return ec;
}

MetadataWatchConfig parseMetadataWatch(const JsonValue& obj)
{
    MetadataWatchConfig mw;
    mw.name = obj.getString("name");
    parseAddressRange(obj.getString("region"), mw.regionStart, mw.regionEnd);
    mw.alignmentMask = static_cast<u32>(obj.getNumber("alignment_mask", 3.0));
    mw.sentinelValue = parseHexAddress(obj.getString("sentinel", "0xfffffffe"));
    mw.checkAlignment = obj.getBool("check_alignment", true);
    mw.checkNonzeroSize = obj.getBool("check_nonzero_size", true);
    mw.checkNextInRegion = obj.getBool("check_next_in_region", true);
    mw.sizeMask = parseHexAddress(obj.getString("size_mask", "0xfffffffc"));
    return mw;
}

SuspectFunctionConfig parseSuspectFunction(const JsonValue& obj)
{
    SuspectFunctionConfig sf;
    sf.name = obj.getString("name");
    parseAddressRange(obj.getString("pc_range"), sf.pcStart, sf.pcEnd);
    for (const auto& cell : obj.getArray("watched_cells"))
    {
        if (cell.isString())
        {
            sf.watchedCells.push_back(parseHexAddress(cell.strVal));
        }
    }
    return sf;
}

} // namespace

DiagProfile::DiagProfile() = default;
DiagProfile::~DiagProfile() = default;

bool DiagProfile::loadFromFile(const std::string& path, RuntimeLogger* logger)
{
    std::ifstream file(path);
    if (!file.is_open())
    {
        if (logger != nullptr)
        {
            logger->log(LogLevel::Warn, "diag_profile",
                        "could not open profile file: " + path);
        }
        return false;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return loadFromString(buffer.str(), logger);
}

bool DiagProfile::loadFromString(const std::string& json, RuntimeLogger* logger)
{
    return parseProfileData(json, logger);
}

bool DiagProfile::isLoaded() const
{
    return m_loaded;
}

const DiagProfileData& DiagProfile::data() const
{
    return m_data;
}

const std::string& DiagProfile::name() const
{
    return m_data.name;
}

const ValidatorConfig* DiagProfile::findValidator(const std::string& name) const
{
    for (const auto& v : m_data.validators)
    {
        if (v.name == name)
        {
            return &v;
        }
    }
    return nullptr;
}

const TracepointConfig* DiagProfile::findTracepoint(const std::string& name) const
{
    for (const auto& t : m_data.tracepoints)
    {
        if (t.name == name)
        {
            return &t;
        }
    }
    return nullptr;
}

const WatchpointConfig* DiagProfile::findWatchpoint(const std::string& name) const
{
    for (const auto& w : m_data.watchpoints)
    {
        if (w.name == name)
        {
            return &w;
        }
    }
    return nullptr;
}

std::string DiagProfile::resolveProfilePath(const std::string& cliArg)
{
    if (!cliArg.empty())
    {
        return cliArg;
    }
    if (const char* env = std::getenv("PSXRECOMP_DIAG_PROFILE"))
    {
        if (env[0] != '\0')
        {
            return env;
        }
    }
    return {};
}

bool DiagProfile::parseProfileData(const std::string& json, RuntimeLogger* logger)
{
    std::string parseError;
    auto root = parseJson(json, &parseError);
    if (!parseError.empty())
    {
        if (logger != nullptr)
        {
            logger->log(LogLevel::Error, "diag_profile", "JSON parse error: " + parseError);
        }
        m_loaded = false;
        return false;
    }
    if (!root.isObject())
    {
        if (logger != nullptr)
        {
            logger->log(LogLevel::Error, "diag_profile", "profile root must be a JSON object");
        }
        m_loaded = false;
        return false;
    }

    m_data = {};
    m_data.name = root.getString("name");

    if (const auto* mm = root.get("memory_map"))
    {
        m_data.memoryMap.ramBase = parseHexAddress(mm->getString("ram_base", "0x80000000"));
        m_data.memoryMap.ramEnd = parseHexAddress(mm->getString("ram_end", "0x80200000"));
    }

    for (const auto& item : root.getArray("watchpoints"))
    {
        m_data.watchpoints.push_back(parseWatchpoint(item));
    }
    for (const auto& item : root.getArray("tracepoints"))
    {
        m_data.tracepoints.push_back(parseTracepoint(item));
    }
    for (const auto& item : root.getArray("validators"))
    {
        m_data.validators.push_back(parseValidator(item));
    }
    for (const auto& item : root.getArray("boundaries"))
    {
        m_data.boundaries.push_back(parseBoundary(item));
    }
    for (const auto& item : root.getArray("explainers"))
    {
        m_data.explainers.push_back(parseExplainer(item));
    }
    for (const auto& item : root.getArray("metadata_watches"))
    {
        m_data.metadataWatches.push_back(parseMetadataWatch(item));
    }
    for (const auto& item : root.getArray("suspect_functions"))
    {
        m_data.suspectFunctions.push_back(parseSuspectFunction(item));
    }

    m_loaded = true;
    if (logger != nullptr)
    {
        std::ostringstream msg;
        msg << "loaded profile '" << m_data.name << "': " << m_data.watchpoints.size()
            << " watchpoints, " << m_data.tracepoints.size() << " tracepoints, "
            << m_data.validators.size() << " validators, " << m_data.boundaries.size()
            << " boundaries, " << m_data.explainers.size() << " explainers";
        logger->log(LogLevel::Info, "diag_profile", msg.str());
    }
    return true;
}

} // namespace runtime
} // namespace psxrecomp
