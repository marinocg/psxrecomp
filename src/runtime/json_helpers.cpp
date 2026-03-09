#include "json_helpers.h"

#include <cctype>
#include <cstdlib>
#include <sstream>

namespace psxrecomp
{
namespace runtime
{

namespace
{
const std::vector<JsonValue> EMPTY_ARRAY;

struct JsonParser
{
    const std::string& input;
    size_t pos = 0;

    char peek() const
    {
        return pos < input.size() ? input[pos] : '\0';
    }

    char advance()
    {
        return pos < input.size() ? input[pos++] : '\0';
    }

    void skipWhitespace()
    {
        while (pos < input.size() &&
               (input[pos] == ' ' || input[pos] == '\t' || input[pos] == '\n' ||
                input[pos] == '\r'))
        {
            ++pos;
        }
    }

    bool expect(char ch)
    {
        skipWhitespace();
        if (peek() == ch)
        {
            advance();
            return true;
        }
        return false;
    }

    JsonValue parseValue(std::string& error)
    {
        skipWhitespace();
        char ch = peek();
        if (ch == '"')
        {
            return parseString(error);
        }
        if (ch == '{')
        {
            return parseObject(error);
        }
        if (ch == '[')
        {
            return parseArray(error);
        }
        if (ch == 't' || ch == 'f')
        {
            return parseBool(error);
        }
        if (ch == 'n')
        {
            return parseNull(error);
        }
        if (ch == '-' || (ch >= '0' && ch <= '9'))
        {
            return parseNumber(error);
        }
        error = "unexpected character at position " + std::to_string(pos);
        return {};
    }

    JsonValue parseString(std::string& error)
    {
        if (!expect('"'))
        {
            error = "expected '\"' at position " + std::to_string(pos);
            return {};
        }
        std::string result;
        while (pos < input.size() && input[pos] != '"')
        {
            if (input[pos] == '\\')
            {
                ++pos;
                if (pos >= input.size())
                {
                    error = "unterminated escape at position " + std::to_string(pos);
                    return {};
                }
                switch (input[pos])
                {
                case '"':
                    result.push_back('"');
                    break;
                case '\\':
                    result.push_back('\\');
                    break;
                case '/':
                    result.push_back('/');
                    break;
                case 'n':
                    result.push_back('\n');
                    break;
                case 'r':
                    result.push_back('\r');
                    break;
                case 't':
                    result.push_back('\t');
                    break;
                case 'b':
                    result.push_back('\b');
                    break;
                case 'f':
                    result.push_back('\f');
                    break;
                default:
                    result.push_back(input[pos]);
                    break;
                }
            }
            else
            {
                result.push_back(input[pos]);
            }
            ++pos;
        }
        if (!expect('"'))
        {
            error = "unterminated string at position " + std::to_string(pos);
            return {};
        }
        JsonValue val;
        val.type = JsonValue::String;
        val.strVal = std::move(result);
        return val;
    }

    JsonValue parseNumber(std::string& error)
    {
        size_t start = pos;
        if (peek() == '-')
        {
            advance();
        }
        while (pos < input.size() && input[pos] >= '0' && input[pos] <= '9')
        {
            advance();
        }
        if (peek() == '.')
        {
            advance();
            while (pos < input.size() && input[pos] >= '0' && input[pos] <= '9')
            {
                advance();
            }
        }
        if (peek() == 'e' || peek() == 'E')
        {
            advance();
            if (peek() == '+' || peek() == '-')
            {
                advance();
            }
            while (pos < input.size() && input[pos] >= '0' && input[pos] <= '9')
            {
                advance();
            }
        }
        if (pos == start)
        {
            error = "invalid number at position " + std::to_string(pos);
            return {};
        }
        JsonValue val;
        val.type = JsonValue::Number;
        val.numVal = std::strtod(input.c_str() + start, nullptr);
        return val;
    }

    JsonValue parseBool(std::string& error)
    {
        if (input.compare(pos, 4, "true") == 0)
        {
            pos += 4;
            JsonValue val;
            val.type = JsonValue::Bool;
            val.boolVal = true;
            return val;
        }
        if (input.compare(pos, 5, "false") == 0)
        {
            pos += 5;
            JsonValue val;
            val.type = JsonValue::Bool;
            val.boolVal = false;
            return val;
        }
        error = "invalid boolean at position " + std::to_string(pos);
        return {};
    }

    JsonValue parseNull(std::string& error)
    {
        if (input.compare(pos, 4, "null") == 0)
        {
            pos += 4;
            return {};
        }
        error = "invalid null at position " + std::to_string(pos);
        return {};
    }

    JsonValue parseArray(std::string& error)
    {
        if (!expect('['))
        {
            error = "expected '[' at position " + std::to_string(pos);
            return {};
        }
        JsonValue val;
        val.type = JsonValue::Array;
        skipWhitespace();
        if (peek() == ']')
        {
            advance();
            return val;
        }
        while (true)
        {
            auto element = parseValue(error);
            if (!error.empty())
            {
                return {};
            }
            val.arrVal.push_back(std::move(element));
            skipWhitespace();
            if (peek() == ',')
            {
                advance();
                continue;
            }
            break;
        }
        if (!expect(']'))
        {
            error = "expected ']' at position " + std::to_string(pos);
            return {};
        }
        return val;
    }

    JsonValue parseObject(std::string& error)
    {
        if (!expect('{'))
        {
            error = "expected '{' at position " + std::to_string(pos);
            return {};
        }
        JsonValue val;
        val.type = JsonValue::Object;
        skipWhitespace();
        if (peek() == '}')
        {
            advance();
            return val;
        }
        while (true)
        {
            auto key = parseString(error);
            if (!error.empty())
            {
                return {};
            }
            if (!expect(':'))
            {
                error = "expected ':' at position " + std::to_string(pos);
                return {};
            }
            auto value = parseValue(error);
            if (!error.empty())
            {
                return {};
            }
            val.objVal[key.strVal] = std::move(value);
            skipWhitespace();
            if (peek() == ',')
            {
                advance();
                skipWhitespace();
                continue;
            }
            break;
        }
        if (!expect('}'))
        {
            error = "expected '}' at position " + std::to_string(pos);
            return {};
        }
        return val;
    }
};

} // namespace

const JsonValue* JsonValue::get(const std::string& key) const
{
    if (type != Type::Object)
    {
        return nullptr;
    }
    auto it = objVal.find(key);
    return it != objVal.end() ? &it->second : nullptr;
}

std::string JsonValue::getString(const std::string& key, const std::string& defaultValue) const
{
    const auto* val = get(key);
    return (val != nullptr && val->type == Type::String) ? val->strVal : defaultValue;
}

bool JsonValue::getBool(const std::string& key, bool defaultValue) const
{
    const auto* val = get(key);
    return (val != nullptr && val->type == Type::Bool) ? val->boolVal : defaultValue;
}

double JsonValue::getNumber(const std::string& key, double defaultValue) const
{
    const auto* val = get(key);
    return (val != nullptr && val->type == Type::Number) ? val->numVal : defaultValue;
}

const std::vector<JsonValue>& JsonValue::getArray(const std::string& key) const
{
    const auto* val = get(key);
    return (val != nullptr && val->type == Type::Array) ? val->arrVal : EMPTY_ARRAY;
}

bool JsonValue::hasKey(const std::string& key) const
{
    return type == Type::Object && objVal.find(key) != objVal.end();
}

bool JsonValue::isNull() const
{
    return type == Type::Null;
}

bool JsonValue::isString() const
{
    return type == Type::String;
}

bool JsonValue::isObject() const
{
    return type == Type::Object;
}

bool JsonValue::isArray() const
{
    return type == Type::Array;
}

bool JsonValue::isBool() const
{
    return type == Type::Bool;
}

bool JsonValue::isNumber() const
{
    return type == Type::Number;
}

JsonValue parseJson(const std::string& input, std::string* errorOut)
{
    std::string error;
    JsonParser parser{input};
    auto result = parser.parseValue(error);
    if (!error.empty())
    {
        if (errorOut != nullptr)
        {
            *errorOut = error;
        }
        return {};
    }
    parser.skipWhitespace();
    if (parser.pos < input.size())
    {
        if (errorOut != nullptr)
        {
            *errorOut = "trailing content at position " + std::to_string(parser.pos);
        }
        return {};
    }
    return result;
}

} // namespace runtime
} // namespace psxrecomp
