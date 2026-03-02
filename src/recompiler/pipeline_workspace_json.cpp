#include "pipeline_workspace_json.h"

#include <array>
#include <cctype>
#include <fstream>
#include <sstream>

namespace psxrecomp
{
namespace recompiler
{
namespace detail
{

const JsonValue* JsonValue::find(const std::string& key) const
{
    auto it = objectValue.find(key);
    if (it == objectValue.end())
    {
        return nullptr;
    }
    return &it->second;
}

JsonParser::JsonParser(const std::string& text) : m_text(text) {}

bool JsonParser::parse(JsonValue& outValue, std::string& outError)
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

bool JsonParser::parseValue(JsonValue& outValue, std::string& outError)
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

bool JsonParser::parseObject(JsonValue& outValue, std::string& outError)
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

bool JsonParser::parseArray(JsonValue& outValue, std::string& outError)
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

bool JsonParser::parseString(std::string& outValue, std::string& outError)
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

bool JsonParser::parseBool(bool& outValue, std::string& outError)
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

bool JsonParser::parseNull(std::string& outError)
{
    if (m_text.compare(m_position, 4, "null") == 0)
    {
        m_position += 4;
        return true;
    }
    outError = "Invalid null token in JSON input.";
    return false;
}

bool JsonParser::parseNumber(std::string& outError)
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
        if (m_position < m_text.size() && (m_text[m_position] == '+' || m_text[m_position] == '-'))
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

bool JsonParser::consume(char token)
{
    if (m_position < m_text.size() && m_text[m_position] == token)
    {
        ++m_position;
        return true;
    }
    return false;
}

void JsonParser::skipWhitespace()
{
    while (m_position < m_text.size() &&
           std::isspace(static_cast<unsigned char>(m_text[m_position])) != 0)
    {
        ++m_position;
    }
}

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

} // namespace detail
} // namespace recompiler
} // namespace psxrecomp
