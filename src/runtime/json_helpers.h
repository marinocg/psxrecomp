#pragma once

#include <map>
#include <string>
#include <vector>

namespace psxrecomp
{
namespace runtime
{

/// Minimal JSON value type for diagnostic profile parsing.
struct JsonValue
{
    enum Type
    {
        Null,
        Bool,
        Number,
        String,
        Array,
        Object
    };

    Type type = Type::Null;
    bool boolVal = false;
    double numVal = 0.0;
    std::string strVal;
    std::vector<JsonValue> arrVal;
    std::map<std::string, JsonValue> objVal;

    /// Lookup a key in an object. Returns nullptr if missing or not an object.
    const JsonValue* get(const std::string& key) const;

    /// Get a string value for a key, or defaultValue if missing/wrong type.
    std::string getString(const std::string& key, const std::string& defaultValue = "") const;

    /// Get a boolean value for a key, or defaultValue if missing/wrong type.
    bool getBool(const std::string& key, bool defaultValue = false) const;

    /// Get a number value for a key, or defaultValue if missing/wrong type.
    double getNumber(const std::string& key, double defaultValue = 0.0) const;

    /// Get an array value for a key. Returns empty vector if missing/wrong type.
    const std::vector<JsonValue>& getArray(const std::string& key) const;

    /// Check if this value is an object containing the given key.
    bool hasKey(const std::string& key) const;

    /// Type predicates.
    bool isNull() const;
    bool isString() const;
    bool isObject() const;
    bool isArray() const;
    bool isBool() const;
    bool isNumber() const;
};

/// Parse a JSON string into a JsonValue tree.
/// Returns a Null value on parse error, with errorOut set if provided.
JsonValue parseJson(const std::string& input, std::string* errorOut = nullptr);

} // namespace runtime
} // namespace psxrecomp
