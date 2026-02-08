#pragma once

#include <sstream>
#include <string>
#include <string_view>

namespace psxrecomp
{
namespace recompiler
{

class CppEmitter
{
  public:
    void writeLine(std::string_view line);
    void writeBlank();
    void openBlock(std::string_view header);
    void closeBlock();
    void closeBlock(std::string_view suffix);
    std::string str() const;

  private:
    std::ostringstream m_stream;
    int m_indentLevel = 0;

    void writeIndent();
};

} // namespace recompiler
} // namespace psxrecomp
