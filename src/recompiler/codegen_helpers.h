#pragma once

#include <cctype>
#include <set>
#include <string>
#include <string_view>
#include <unordered_set>

namespace psxrecomp
{
namespace recompiler
{

inline std::string toIdentifier(std::string_view name)
{
    std::string identifier;
    identifier.reserve(name.size());
    for (char ch : name)
    {
        if (std::isalnum(static_cast<unsigned char>(ch)) || ch == '_')
        {
            identifier.push_back(ch);
        }
        else
        {
            identifier.push_back('_');
        }
    }
    if (identifier.empty())
    {
        identifier = "unnamed";
    }
    if (std::isdigit(static_cast<unsigned char>(identifier.front())))
    {
        identifier.insert(identifier.begin(), '_');
    }
    static const std::set<std::string> reserved = {"alignas",      "alignof",
                                                   "and",          "and_eq",
                                                   "asm",          "auto",
                                                   "bitand",       "bitor",
                                                   "bool",         "break",
                                                   "case",         "catch",
                                                   "char",         "char16_t",
                                                   "char32_t",     "class",
                                                   "compl",        "const",
                                                   "constexpr",    "const_cast",
                                                   "continue",     "decltype",
                                                   "default",      "delete",
                                                   "do",           "double",
                                                   "dynamic_cast", "else",
                                                   "enum",         "explicit",
                                                   "export",       "extern",
                                                   "false",        "float",
                                                   "for",          "friend",
                                                   "goto",         "if",
                                                   "inline",       "int",
                                                   "long",         "mutable",
                                                   "namespace",    "new",
                                                   "noexcept",     "not",
                                                   "not_eq",       "nullptr",
                                                   "operator",     "or",
                                                   "or_eq",        "private",
                                                   "protected",    "public",
                                                   "register",     "reinterpret_cast",
                                                   "return",       "short",
                                                   "signed",       "sizeof",
                                                   "static",       "static_assert",
                                                   "static_cast",  "struct",
                                                   "switch",       "template",
                                                   "this",         "thread_local",
                                                   "throw",        "true",
                                                   "try",          "typedef",
                                                   "typeid",       "typename",
                                                   "union",        "unsigned",
                                                   "using",        "virtual",
                                                   "void",         "volatile",
                                                   "wchar_t",      "while",
                                                   "xor",          "xor_eq"};
    if (reserved.find(identifier) != reserved.end())
    {
        identifier.insert(identifier.begin(), '_');
    }
    return identifier;
}

inline std::string uniquifyIdentifier(std::string_view name, std::unordered_set<std::string>& used)
{
    std::string base = toIdentifier(name);
    std::string candidate = base;
    size_t suffix = 1;
    while (!used.insert(candidate).second)
    {
        candidate = base + "_" + std::to_string(suffix++);
    }
    return candidate;
}

} // namespace recompiler
} // namespace psxrecomp
