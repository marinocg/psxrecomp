#include "pipeline_workspace_helpers.h"

#include "pipeline_helpers.h"

#include <array>
#include <cctype>
#include <fstream>
#include <map>
#include <sstream>

namespace psxrecomp
{
namespace recompiler
{
namespace detail
{

namespace
{

struct JsonValue
{
    enum class Type
    {
        Object,
        Array,
        String,
        Number,
        Bool,
        Null
    };

    Type type = Type::Null;
    std::map<std::string, JsonValue> objectValue;
    std::vector<JsonValue> arrayValue;
    std::string stringValue;
    bool boolValue = false;

    const JsonValue* find(const std::string& key) const
    {
        auto it = objectValue.find(key);
        if (it == objectValue.end())
        {
            return nullptr;
        }
        return &it->second;
    }
};

class JsonParser
{
  public:
    explicit JsonParser(const std::string& text) : m_text(text) {}

    bool parse(JsonValue& outValue, std::string& outError)
    {
        skipWhitespace();
        if (!parseValue(outValue, outError))
        {
            return false;
        }
        skipWhitespace();
        if (m_position != m_text.size())
        {
            outError = "Unexpected trailing data in JSON document.";
            return false;
        }
        return true;
    }

  private:
    bool parseValue(JsonValue& outValue, std::string& outError)
    {
        skipWhitespace();
        if (m_position >= m_text.size())
        {
            outError = "Unexpected end of JSON input.";
            return false;
        }

        const char ch = m_text[m_position];
        if (ch == '{')
        {
            return parseObject(outValue, outError);
        }
        if (ch == '[')
        {
            return parseArray(outValue, outError);
        }
        if (ch == '"')
        {
            outValue.type = JsonValue::Type::String;
            return parseString(outValue.stringValue, outError);
        }
        if (ch == 't' || ch == 'f')
        {
            outValue.type = JsonValue::Type::Bool;
            return parseBool(outValue.boolValue, outError);
        }
        if (ch == 'n')
        {
            outValue.type = JsonValue::Type::Null;
            return parseNull(outError);
        }
        if (ch == '-' || std::isdigit(static_cast<unsigned char>(ch)) != 0)
        {
            outValue.type = JsonValue::Type::Number;
            return parseNumber(outError);
        }

        outError = "Unsupported JSON token at offset " + std::to_string(m_position) + ".";
        return false;
    }

    bool parseObject(JsonValue& outValue, std::string& outError)
    {
        if (!consume('{'))
        {
            outError = "Expected '{' while parsing object.";
            return false;
        }

        outValue = JsonValue{};
        outValue.type = JsonValue::Type::Object;
        skipWhitespace();
        if (consume('}'))
        {
            return true;
        }

        while (m_position < m_text.size())
        {
            std::string key;
            if (!parseString(key, outError))
            {
                return false;
            }
            skipWhitespace();
            if (!consume(':'))
            {
                outError = "Expected ':' after object key '" + key + "'.";
                return false;
            }

            JsonValue fieldValue;
            if (!parseValue(fieldValue, outError))
            {
                return false;
            }
            outValue.objectValue[key] = std::move(fieldValue);

            skipWhitespace();
            if (consume('}'))
            {
                return true;
            }
            if (!consume(','))
            {
                outError = "Expected ',' or '}' in object.";
                return false;
            }
            skipWhitespace();
        }

        outError = "Unexpected end of JSON input while parsing object.";
        return false;
    }

    bool parseArray(JsonValue& outValue, std::string& outError)
    {
        if (!consume('['))
        {
            outError = "Expected '[' while parsing array.";
            return false;
        }

        outValue = JsonValue{};
        outValue.type = JsonValue::Type::Array;
        skipWhitespace();
        if (consume(']'))
        {
            return true;
        }

        while (m_position < m_text.size())
        {
            JsonValue element;
            if (!parseValue(element, outError))
            {
                return false;
            }
            outValue.arrayValue.push_back(std::move(element));

            skipWhitespace();
            if (consume(']'))
            {
                return true;
            }
            if (!consume(','))
            {
                outError = "Expected ',' or ']' in array.";
                return false;
            }
            skipWhitespace();
        }

        outError = "Unexpected end of JSON input while parsing array.";
        return false;
    }

    bool parseString(std::string& outValue, std::string& outError)
    {
        if (!consume('"'))
        {
            outError = "Expected '\"' while parsing string.";
            return false;
        }

        std::string decoded;
        while (m_position < m_text.size())
        {
            const char ch = m_text[m_position++];
            if (ch == '"')
            {
                outValue = std::move(decoded);
                return true;
            }
            if (ch == '\\')
            {
                if (m_position >= m_text.size())
                {
                    outError = "Invalid trailing escape in string literal.";
                    return false;
                }
                const char escaped = m_text[m_position++];
                switch (escaped)
                {
                case '"':
                case '\\':
                case '/':
                    decoded.push_back(escaped);
                    break;
                case 'b':
                    decoded.push_back('\b');
                    break;
                case 'f':
                    decoded.push_back('\f');
                    break;
                case 'n':
                    decoded.push_back('\n');
                    break;
                case 'r':
                    decoded.push_back('\r');
                    break;
                case 't':
                    decoded.push_back('\t');
                    break;
                case 'u':
                {
                    std::array<char, 4> hexDigits{};
                    if (m_position + hexDigits.size() > m_text.size())
                    {
                        outError = "Invalid unicode escape in string literal.";
                        return false;
                    }
                    for (size_t index = 0; index < hexDigits.size(); ++index)
                    {
                        hexDigits[index] = m_text[m_position + index];
                        if (std::isxdigit(static_cast<unsigned char>(hexDigits[index])) == 0)
                        {
                            outError = "Invalid unicode escape digits in string literal.";
                            return false;
                        }
                    }
                    m_position += hexDigits.size();
                    unsigned int codePoint = 0;
                    std::stringstream stream;
                    stream << std::hex << std::string(hexDigits.begin(), hexDigits.end());
                    stream >> codePoint;
                    if (codePoint <= 0x7F)
                    {
                        decoded.push_back(static_cast<char>(codePoint));
                    }
                    else
                    {
                        decoded.push_back('?');
                    }
                    break;
                }
                default:
                    outError = "Unsupported escape sequence in string literal.";
                    return false;
                }
                continue;
            }

            if (static_cast<unsigned char>(ch) < 0x20)
            {
                outError = "Unescaped control character in string literal.";
                return false;
            }
            decoded.push_back(ch);
        }

        outError = "Unexpected end of JSON input while parsing string.";
        return false;
    }

    bool parseBool(bool& outValue, std::string& outError)
    {
        if (m_text.compare(m_position, 4, "true") == 0)
        {
            outValue = true;
            m_position += 4;
            return true;
        }
        if (m_text.compare(m_position, 5, "false") == 0)
        {
            outValue = false;
            m_position += 5;
            return true;
        }
        outError = "Invalid boolean token in JSON input.";
        return false;
    }

    bool parseNull(std::string& outError)
    {
        if (m_text.compare(m_position, 4, "null") == 0)
        {
            m_position += 4;
            return true;
        }
        outError = "Invalid null token in JSON input.";
        return false;
    }

    bool parseNumber(std::string& outError)
    {
        const size_t start = m_position;
        if (consume('-'))
        {
            // sign consumed
        }
        if (m_position >= m_text.size() ||
            std::isdigit(static_cast<unsigned char>(m_text[m_position])) == 0)
        {
            outError = "Invalid numeric token in JSON input.";
            return false;
        }
        if (m_text[m_position] == '0')
        {
            ++m_position;
        }
        else
        {
            while (m_position < m_text.size() &&
                   std::isdigit(static_cast<unsigned char>(m_text[m_position])) != 0)
            {
                ++m_position;
            }
        }
        if (m_position < m_text.size() && m_text[m_position] == '.')
        {
            ++m_position;
            if (m_position >= m_text.size() ||
                std::isdigit(static_cast<unsigned char>(m_text[m_position])) == 0)
            {
                outError = "Invalid fractional numeric token in JSON input.";
                return false;
            }
            while (m_position < m_text.size() &&
                   std::isdigit(static_cast<unsigned char>(m_text[m_position])) != 0)
            {
                ++m_position;
            }
        }
        if (m_position < m_text.size() && (m_text[m_position] == 'e' || m_text[m_position] == 'E'))
        {
            ++m_position;
            if (m_position < m_text.size() &&
                (m_text[m_position] == '+' || m_text[m_position] == '-'))
            {
                ++m_position;
            }
            if (m_position >= m_text.size() ||
                std::isdigit(static_cast<unsigned char>(m_text[m_position])) == 0)
            {
                outError = "Invalid exponent numeric token in JSON input.";
                return false;
            }
            while (m_position < m_text.size() &&
                   std::isdigit(static_cast<unsigned char>(m_text[m_position])) != 0)
            {
                ++m_position;
            }
        }
        if (m_position == start)
        {
            outError = "Invalid numeric token in JSON input.";
            return false;
        }
        return true;
    }

    bool consume(char token)
    {
        if (m_position < m_text.size() && m_text[m_position] == token)
        {
            ++m_position;
            return true;
        }
        return false;
    }

    void skipWhitespace()
    {
        while (m_position < m_text.size() &&
               std::isspace(static_cast<unsigned char>(m_text[m_position])) != 0)
        {
            ++m_position;
        }
    }

    const std::string& m_text;
    size_t m_position = 0;
};

bool readTextFile(const std::filesystem::path& path, std::string& outContents,
                  std::string& outError)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
        outError = "Failed to open file: " + path.string();
        return false;
    }
    std::ostringstream stream;
    stream << input.rdbuf();
    if (!input.good() && !input.eof())
    {
        outError = "Failed to read file: " + path.string();
        return false;
    }
    outContents = stream.str();
    return true;
}

const JsonValue* expectObjectField(const JsonValue& object, const std::string& key)
{
    const JsonValue* field = object.find(key);
    if (!field || field->type != JsonValue::Type::Object)
    {
        return nullptr;
    }
    return field;
}

const JsonValue* expectArrayField(const JsonValue& object, const std::string& key)
{
    const JsonValue* field = object.find(key);
    if (!field || field->type != JsonValue::Type::Array)
    {
        return nullptr;
    }
    return field;
}

std::string readStringField(const JsonValue& object, const std::string& key)
{
    const JsonValue* field = object.find(key);
    if (!field || field->type != JsonValue::Type::String)
    {
        return "";
    }
    return field->stringValue;
}

std::string normalizeRelativePathString(const std::string& rawPath)
{
    std::filesystem::path path(rawPath);
    return path.generic_string();
}

} // namespace

bool detectResourceWorkspaceRoot(const std::filesystem::path& inputPath,
                                 std::filesystem::path& outWorkspaceRoot)
{
    std::error_code error;
    if (!std::filesystem::is_directory(inputPath, error) || error)
    {
        return false;
    }

    const std::filesystem::path directRecompInputs = inputPath / "index" / "recomp_inputs.json";
    if (std::filesystem::is_regular_file(directRecompInputs, error) && !error)
    {
        outWorkspaceRoot = inputPath;
        return true;
    }

    error.clear();
    const std::filesystem::path nestedWorkspace = inputPath / "resources";
    const std::filesystem::path nestedRecompInputs =
        nestedWorkspace / "index" / "recomp_inputs.json";
    if (std::filesystem::is_regular_file(nestedRecompInputs, error) && !error)
    {
        outWorkspaceRoot = nestedWorkspace;
        return true;
    }

    return false;
}

bool loadResourceWorkspaceInfo(const std::filesystem::path& workspaceRoot,
                               ResourceWorkspaceInfo& outInfo, std::string& outError)
{
    outInfo = ResourceWorkspaceInfo{};
    outInfo.workspaceRoot = workspaceRoot;
    outInfo.recompInputsPath = workspaceRoot / "index" / "recomp_inputs.json";
    outInfo.discTreePath = workspaceRoot / "index" / "disc_tree.json";
    outInfo.discMetaPath = workspaceRoot / "index" / "disc_meta.json";
    outInfo.resourcesManifestPath = workspaceRoot / "index" / "resources_manifest.json";

    std::error_code error;
    if (!std::filesystem::is_regular_file(outInfo.recompInputsPath, error) || error)
    {
        outError = "Missing required workspace index file: " + outInfo.recompInputsPath.string();
        return false;
    }

    std::string recompInputsText;
    if (!readTextFile(outInfo.recompInputsPath, recompInputsText, outError))
    {
        return false;
    }

    JsonValue recompInputsRoot;
    JsonParser recompInputsParser(recompInputsText);
    if (!recompInputsParser.parse(recompInputsRoot, outError))
    {
        outError = "Failed to parse recomp inputs JSON: " + outError;
        return false;
    }
    if (recompInputsRoot.type != JsonValue::Type::Object)
    {
        outError = "Invalid recomp inputs JSON: expected top-level object.";
        return false;
    }

    const JsonValue* bootObject = expectObjectField(recompInputsRoot, "boot");
    if (bootObject)
    {
        outInfo.bootIsoPath = readStringField(*bootObject, "isoPath");
        outInfo.bootExportedPath =
            normalizeRelativePathString(readStringField(*bootObject, "exportedPath"));
    }

    const JsonValue* executableArray = expectArrayField(recompInputsRoot, "executables");
    if (!executableArray)
    {
        outError = "Invalid recomp inputs JSON: missing executables array.";
        return false;
    }
    for (size_t index = 0; index < executableArray->arrayValue.size(); ++index)
    {
        const JsonValue& executableValue = executableArray->arrayValue[index];
        if (executableValue.type != JsonValue::Type::Object)
        {
            outError = "Invalid recomp inputs JSON: executable entry at index " +
                       std::to_string(index) + " is not an object.";
            return false;
        }

        WorkspaceExecutableInfo executableInfo;
        executableInfo.isoPath = readStringField(executableValue, "isoPath");
        executableInfo.exportedPath =
            normalizeRelativePathString(readStringField(executableValue, "exportedPath"));
        if (executableInfo.isoPath.empty())
        {
            outError = "Invalid recomp inputs JSON: executable entry at index " +
                       std::to_string(index) + " is missing isoPath.";
            return false;
        }
        if (executableInfo.exportedPath.empty())
        {
            executableInfo.exportedPath =
                (std::filesystem::path("fs") / std::filesystem::path(executableInfo.isoPath))
                    .generic_string();
        }
        outInfo.executables.push_back(std::move(executableInfo));
    }

    if (outInfo.executables.empty())
    {
        outError = "Invalid recomp inputs JSON: executables array is empty.";
        return false;
    }

    if (outInfo.bootIsoPath.empty() && !outInfo.bootExportedPath.empty())
    {
        const std::string bootExportedPathLower = toLower(outInfo.bootExportedPath);
        for (const auto& executable : outInfo.executables)
        {
            if (toLower(executable.exportedPath) == bootExportedPathLower)
            {
                outInfo.bootIsoPath = executable.isoPath;
                break;
            }
        }
    }
    if (outInfo.bootIsoPath.empty() && outInfo.executables.size() == 1)
    {
        outInfo.bootIsoPath = outInfo.executables.front().isoPath;
    }
    if (outInfo.bootExportedPath.empty() && !outInfo.bootIsoPath.empty())
    {
        outInfo.bootExportedPath =
            (std::filesystem::path("fs") / std::filesystem::path(outInfo.bootIsoPath))
                .generic_string();
    }

    error.clear();
    if (std::filesystem::is_regular_file(outInfo.discMetaPath, error) && !error)
    {
        std::string discMetaText;
        if (!readTextFile(outInfo.discMetaPath, discMetaText, outError))
        {
            return false;
        }

        JsonValue discMetaRoot;
        JsonParser discMetaParser(discMetaText);
        if (!discMetaParser.parse(discMetaRoot, outError))
        {
            outError = "Failed to parse disc meta JSON: " + outError;
            return false;
        }
        if (discMetaRoot.type == JsonValue::Type::Object)
        {
            outInfo.discMetaInputPath = readStringField(discMetaRoot, "inputPath");
            outInfo.discMetaVolumeLabel = readStringField(discMetaRoot, "volumeLabel");
        }
    }

    return true;
}

std::filesystem::path
resolveWorkspaceExecutableHostPath(const ResourceWorkspaceInfo& workspaceInfo,
                                   const WorkspaceExecutableInfo& executableInfo)
{
    std::filesystem::path relativePath(executableInfo.exportedPath);
    if (relativePath.empty())
    {
        relativePath = std::filesystem::path("fs") / std::filesystem::path(executableInfo.isoPath);
    }
    if (relativePath.is_absolute())
    {
        return relativePath;
    }
    return workspaceInfo.workspaceRoot / relativePath;
}

} // namespace detail
} // namespace recompiler
} // namespace psxrecomp
