#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace psxrecomp
{
namespace recompiler
{
namespace detail
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

    const JsonValue* find(const std::string& key) const;
};

class JsonParser
{
  public:
    explicit JsonParser(const std::string& text);

    bool parse(JsonValue& outValue, std::string& outError);

  private:
    bool parseValue(JsonValue& outValue, std::string& outError);
    bool parseObject(JsonValue& outValue, std::string& outError);
    bool parseArray(JsonValue& outValue, std::string& outError);
    bool parseString(std::string& outValue, std::string& outError);
    bool parseBool(bool& outValue, std::string& outError);
    bool parseNull(std::string& outError);
    bool parseNumber(std::string& outError);
    bool consume(char token);
    void skipWhitespace();

    const std::string& m_text;
    size_t m_position = 0;
};

bool readTextFile(const std::filesystem::path& path, std::string& outContents,
                  std::string& outError);
const JsonValue* expectObjectField(const JsonValue& object, const std::string& key);
const JsonValue* expectArrayField(const JsonValue& object, const std::string& key);
std::string readStringField(const JsonValue& object, const std::string& key);

} // namespace detail
} // namespace recompiler
} // namespace psxrecomp
