#include "pipeline_helpers_output_model.h"

#include <sstream>

namespace psxrecomp
{
namespace recompiler
{
namespace detail
{

namespace
{

std::string escapeJson(const std::string& value)
{
    std::string escaped;
    escaped.reserve(value.size());
    for (char ch : value)
    {
        switch (ch)
        {
        case '"':
            escaped += "\\\"";
            break;
        case '\\':
            escaped += "\\\\";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        case '\t':
            escaped += "\\t";
            break;
        default:
            escaped += ch;
            break;
        }
    }
    return escaped;
}

} // namespace

std::string serializeCatalog(const std::vector<CatalogEntry>& entries)
{
    std::ostringstream stream;
    stream << "{\n";
    stream << "  \"schemaVersion\": \"1.0\",\n";
    stream << "  \"entries\": [\n";
    for (size_t index = 0; index < entries.size(); ++index)
    {
        const auto& entry = entries[index];
        stream << "    {\n";
        stream << "      \"id\": \"" << escapeJson(entry.id) << "\",\n";
        stream << "      \"sourceKind\": \"" << escapeJson(entry.sourceKind) << "\",\n";
        stream << "      \"isoPath\": \"" << escapeJson(entry.isoPath) << "\",\n";
        stream << "      \"exportedPath\": \"" << escapeJson(entry.exportedPath) << "\",\n";
        stream << "      \"size\": " << entry.size << ",\n";
        stream << "      \"hashes\": {\n";
        stream << "        \"sha1\": \"" << escapeJson(entry.sha1) << "\"\n";
        stream << "      },\n";
        stream << "      \"source\": {\n";
        stream << "        \"containerIsoPath\": \"" << escapeJson(entry.containerIsoPath)
               << "\",\n";
        stream << "        \"offset\": " << entry.containerOffset << "\n";
        stream << "      },\n";
        stream << "      \"extents\": [\n";
        for (size_t extentIndex = 0; extentIndex < entry.extents.size(); ++extentIndex)
        {
            const auto& extent = entry.extents[extentIndex];
            stream << "        {\n";
            stream << "          \"lba\": " << extent.lba << ",\n";
            stream << "          \"bytes\": " << extent.size << ",\n";
            stream << "          \"continues\": " << (extent.continues ? "true" : "false") << "\n";
            stream << "        }";
            if (extentIndex + 1 < entry.extents.size())
            {
                stream << ",";
            }
            stream << "\n";
        }
        stream << "      ],\n";
        stream << "      \"detectedTypes\": [\n";
        for (size_t typeIndex = 0; typeIndex < entry.detectedTypes.size(); ++typeIndex)
        {
            stream << "        \"" << escapeJson(entry.detectedTypes[typeIndex]) << "\"";
            if (typeIndex + 1 < entry.detectedTypes.size())
            {
                stream << ",";
            }
            stream << "\n";
        }
        stream << "      ]\n";
        stream << "    }";
        if (index + 1 < entries.size())
        {
            stream << ",";
        }
        stream << "\n";
    }
    stream << "  ]\n";
    stream << "}\n";
    return stream.str();
}

std::string serializeRecompInputs(const std::string& bootIsoPath,
                                  const std::string& bootExportedPath,
                                  const std::string& systemCnfExportedPath,
                                  const std::vector<RecompInputExecutable>& executables)
{
    std::ostringstream stream;
    stream << "{\n";
    stream << "  \"schemaVersion\": \"1.0\",\n";
    stream << "  \"boot\": {\n";
    stream << "    \"isoPath\": \"" << escapeJson(bootIsoPath) << "\",\n";
    stream << "    \"exportedPath\": \"" << escapeJson(bootExportedPath) << "\"\n";
    stream << "  },\n";
    stream << "  \"systemCnf\": {\n";
    stream << "    \"exportedPath\": \"" << escapeJson(systemCnfExportedPath) << "\"\n";
    stream << "  },\n";
    stream << "  \"executables\": [\n";
    for (size_t index = 0; index < executables.size(); ++index)
    {
        const auto& executable = executables[index];
        stream << "    {\n";
        stream << "      \"isoPath\": \"" << escapeJson(executable.isoPath) << "\",\n";
        stream << "      \"exportedPath\": \"" << escapeJson(executable.exportedPath) << "\",\n";
        stream << "      \"psxExe\": {\n";
        stream << "        \"loadAddr\": \"0x" << formatHex(executable.loadAddress, 8) << "\",\n";
        stream << "        \"entry\": \"0x" << formatHex(executable.entryPoint, 8) << "\",\n";
        stream << "        \"size\": " << executable.loadSize;
        if (executable.gp != 0)
        {
            stream << ",\n";
            stream << "        \"gp\": \"0x" << formatHex(executable.gp, 8) << "\"";
        }
        if (executable.bssSize != 0)
        {
            stream << ",\n";
            stream << "        \"bssAddr\": \"0x" << formatHex(executable.bssAddress, 8) << "\",\n";
            stream << "        \"bssSize\": " << executable.bssSize;
        }
        if (executable.stackSize != 0 || executable.stackAddress != 0)
        {
            stream << ",\n";
            stream << "        \"stackAddr\": \"0x" << formatHex(executable.stackAddress, 8)
                   << "\",\n";
            stream << "        \"stackSize\": " << executable.stackSize;
        }
        stream << "\n";
        stream << "      }\n";
        stream << "    }";
        if (index + 1 < executables.size())
        {
            stream << ",";
        }
        stream << "\n";
    }
    stream << "  ]\n";
    stream << "}\n";
    return stream.str();
}

} // namespace detail
} // namespace recompiler
} // namespace psxrecomp
