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

void CppEmitter::writeBlank()
{
    m_stream << "\n";
}

void CppEmitter::openBlock(std::string_view header)
{
    writeLine(std::string(header) + " {");
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
