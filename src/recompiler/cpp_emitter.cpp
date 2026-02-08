#include "cpp_emitter.h"

namespace psxrecomp
{
namespace recompiler
{

void CppEmitter::writeLine(std::string_view line)
{
    writeIndent();
    m_stream << line << "\n";
}

void CppEmitter::writeLines(std::string_view text)
{
    if (text.empty())
    {
        return;
    }
    size_t start = 0;
    while (start < text.size())
    {
        size_t end = text.find('\n', start);
        if (end == std::string_view::npos)
        {
            end = text.size();
        }
        std::string_view line = text.substr(start, end - start);
        writeLine(line);
        if (end == text.size())
        {
            break;
        }
        start = end + 1;
    }
}

void CppEmitter::writeBlank()
{
    m_stream << "\n";
}

void CppEmitter::openBlock(std::string_view header)
{
    if (header.empty())
    {
        writeLine("{");
    }
    else
    {
        writeLine(std::string(header) + " {");
    }
    ++m_indentLevel;
}

void CppEmitter::closeBlock()
{
    if (m_indentLevel > 0)
    {
        --m_indentLevel;
    }
    writeLine("}");
}

void CppEmitter::closeBlock(std::string_view suffix)
{
    if (m_indentLevel > 0)
    {
        --m_indentLevel;
    }
    writeIndent();
    m_stream << "}" << suffix << "\n";
}

std::string CppEmitter::str() const
{
    return m_stream.str();
}

void CppEmitter::writeIndent()
{
    for (int i = 0; i < m_indentLevel; ++i)
    {
        m_stream << "    ";
    }
}

} // namespace recompiler
} // namespace psxrecomp
