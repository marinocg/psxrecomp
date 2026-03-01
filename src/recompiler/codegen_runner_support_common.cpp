#include "codegen_runner_sections.h"

namespace psxrecomp
{
namespace recompiler
{

void emitRunnerSupportCommon(CppEmitter& emitter)
{
    emitter.writeLine("namespace");
    emitter.openBlock("");
    emitter.writeLine("bool dumpFramebufferToPpm(const std::filesystem::path& outputPath,");
    emitter.writeLine("                          const std::vector<psxrecomp::u16>& framebuffer,");
    emitter.writeLine("                          size_t width,");
    emitter.writeLine("                          size_t height)");
    emitter.openBlock("");
    emitter.writeLine("if (width == 0 || height == 0)");
    emitter.openBlock("");
    emitter.writeLine("return false;");
    emitter.closeBlock();
    emitter.writeLine("if (framebuffer.size() < width * height)");
    emitter.openBlock("");
    emitter.writeLine("return false;");
    emitter.closeBlock();
    emitter.writeLine("std::ofstream out(outputPath, std::ios::binary);");
    emitter.writeLine("if (!out)");
    emitter.openBlock("");
    emitter.writeLine("return false;");
    emitter.closeBlock();
    emitter.writeLine("out << \"P6\\n\" << width << \" \" << height << \"\\n255\\n\";");
    emitter.writeLine("for (size_t i = 0; i < width * height; ++i)");
    emitter.openBlock("");
    emitter.writeLine("psxrecomp::u16 pixel = framebuffer[i];");
    emitter.writeLine(
        "psxrecomp::u8 r = static_cast<psxrecomp::u8>(((pixel >> 0) & 0x1F) * 255 / 31);");
    emitter.writeLine(
        "psxrecomp::u8 g = static_cast<psxrecomp::u8>(((pixel >> 5) & 0x1F) * 255 / 31);");
    emitter.writeLine(
        "psxrecomp::u8 b = static_cast<psxrecomp::u8>(((pixel >> 10) & 0x1F) * 255 / 31);");
    emitter.writeLine("out.put(static_cast<char>(r));");
    emitter.writeLine("out.put(static_cast<char>(g));");
    emitter.writeLine("out.put(static_cast<char>(b));");
    emitter.closeBlock();
    emitter.writeLine("return out.good();");
    emitter.closeBlock();
    emitter.writeBlank();
    emitter.writeLine(
        "std::vector<psxrecomp::u16> extractDisplayPixels(const std::vector<psxrecomp::u16>& "
        "framebuffer,");
    emitter.writeLine(
        "                                           psxrecomp::runtime::Gpu::DisplayWindow "
        "displayWindow,");
    emitter.writeLine("                                           size_t* outWidth,");
    emitter.writeLine("                                           size_t* outHeight)");
    emitter.openBlock("");
    emitter.writeLine(
        "constexpr size_t fullWidth = psxrecomp::runtime::SoftwareGpuRenderer::Width;");
    emitter.writeLine(
        "constexpr size_t fullHeight = psxrecomp::runtime::SoftwareGpuRenderer::Height;");
    emitter.writeLine("if (outWidth == nullptr || outHeight == nullptr)");
    emitter.openBlock("");
    emitter.writeLine("return {};");
    emitter.closeBlock();
    emitter.writeLine("if (displayWindow.x >= fullWidth || displayWindow.y >= fullHeight)");
    emitter.openBlock("");
    emitter.writeLine("*outWidth = 0;");
    emitter.writeLine("*outHeight = 0;");
    emitter.writeLine("return {};");
    emitter.closeBlock();
    emitter.writeLine("size_t width = std::max<size_t>(1, displayWindow.width);");
    emitter.writeLine("size_t height = std::max<size_t>(1, displayWindow.height);");
    emitter.writeLine("width = std::min(width, fullWidth - displayWindow.x);");
    emitter.writeLine("height = std::min(height, fullHeight - displayWindow.y);");
    emitter.writeLine("if (width == 0 || height == 0)");
    emitter.openBlock("");
    emitter.writeLine("*outWidth = 0;");
    emitter.writeLine("*outHeight = 0;");
    emitter.writeLine("return {};");
    emitter.closeBlock();
    emitter.writeLine("if (framebuffer.size() < fullWidth * fullHeight)");
    emitter.openBlock("");
    emitter.writeLine("*outWidth = width;");
    emitter.writeLine("*outHeight = height;");
    emitter.writeLine("return std::vector<psxrecomp::u16>(width * height, 0);");
    emitter.closeBlock();
    emitter.writeLine("std::vector<psxrecomp::u16> output(width * height, 0);");
    emitter.writeLine("for (size_t y = 0; y < height; ++y)");
    emitter.openBlock("");
    emitter.writeLine(
        "const size_t srcRow = (static_cast<size_t>(displayWindow.y) + y) * fullWidth +");
    emitter.writeLine("                      static_cast<size_t>(displayWindow.x);");
    emitter.writeLine("const size_t dstRow = y * width;");
    emitter.writeLine("std::copy_n(framebuffer.begin() + static_cast<std::ptrdiff_t>(srcRow),");
    emitter.writeLine("            width,");
    emitter.writeLine("            output.begin() + static_cast<std::ptrdiff_t>(dstRow));");
    emitter.closeBlock();
    emitter.writeLine("*outWidth = width;");
    emitter.writeLine("*outHeight = height;");
    emitter.writeLine("return output;");
    emitter.closeBlock();
    emitter.writeBlank();
    emitter.writeLine("size_t countNonZeroPixels(const std::vector<psxrecomp::u16>& pixels)");
    emitter.openBlock("");
    emitter.writeLine("size_t count = 0;");
    emitter.writeLine("for (size_t i = 0; i < pixels.size(); ++i)");
    emitter.openBlock("");
    emitter.writeLine("if (pixels[i] != 0)");
    emitter.openBlock("");
    emitter.writeLine("++count;");
    emitter.closeBlock();
    emitter.closeBlock();
    emitter.writeLine("return count;");
    emitter.closeBlock();
    emitter.writeBlank();
    emitter.writeLine("std::filesystem::path appendSuffixBeforeExtension(");
    emitter.writeLine("    const std::filesystem::path& path, const std::string& suffix)");
    emitter.openBlock("");
    emitter.writeLine("const std::filesystem::path parent = path.parent_path();");
    emitter.writeLine("const std::string stem = path.stem().string();");
    emitter.writeLine("const std::string ext = path.extension().string();");
    emitter.writeLine("if (ext.empty())");
    emitter.openBlock("");
    emitter.writeLine("return parent / (path.filename().string() + suffix);");
    emitter.closeBlock();
    emitter.writeLine("return parent / (stem + suffix + ext);");
    emitter.closeBlock();
    emitter.writeBlank();
    emitter.writeLine("bool envFlagEnabled(const char* value, bool defaultValue)");
    emitter.openBlock("");
    emitter.writeLine("if (value == nullptr || value[0] == '\\0')");
    emitter.openBlock("");
    emitter.writeLine("return defaultValue;");
    emitter.closeBlock();
    emitter.writeLine("std::string text(value);");
    emitter.writeLine("for (char& ch : text)");
    emitter.openBlock("");
    emitter.writeLine("ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));");
    emitter.closeBlock();
    emitter.writeLine(
        "return text == \"1\" || text == \"true\" || text == \"yes\" || text == \"on\";");
    emitter.closeBlock();
    emitter.writeBlank();
    emitter.writeLine(
        "psxrecomp::runtime::LogLevel decodeLogLevel(const std::string& raw, bool* ok)");
    emitter.openBlock("");
    emitter.writeLine("*ok = true;");
    emitter.writeLine("std::string text(raw);");
    emitter.writeLine(
        "while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())))");
    emitter.openBlock("");
    emitter.writeLine("text.erase(text.begin());");
    emitter.closeBlock();
    emitter.writeLine(
        "while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))");
    emitter.openBlock("");
    emitter.writeLine("text.pop_back();");
    emitter.closeBlock();
    emitter.writeLine("for (char& ch : text)");
    emitter.openBlock("");
    emitter.writeLine("ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));");
    emitter.closeBlock();
    emitter.writeLine(
        "if (text == \"0\" || text == \"debug\") return psxrecomp::runtime::LogLevel::Debug;");
    emitter.writeLine(
        "if (text == \"1\" || text == \"info\") return psxrecomp::runtime::LogLevel::Info;");
    emitter.writeLine("if (text == \"2\" || text == \"warn\" || text == \"warning\") return "
                      "psxrecomp::runtime::LogLevel::Warn;");
    emitter.writeLine(
        "if (text == \"3\" || text == \"error\") return psxrecomp::runtime::LogLevel::Error;");
    emitter.writeLine("*ok = false;");
    emitter.writeLine("return psxrecomp::runtime::LogLevel::Info;");
    emitter.closeBlock();
    emitter.writeBlank();
    emitter.writeLine("const char* logLevelLabel(psxrecomp::runtime::LogLevel level)");
    emitter.openBlock("");
    emitter.writeLine("switch (level)");
    emitter.openBlock("");
    emitter.writeLine("case psxrecomp::runtime::LogLevel::Debug:");
    emitter.writeLine("return \"debug\";");
    emitter.writeLine("case psxrecomp::runtime::LogLevel::Info:");
    emitter.writeLine("return \"info\";");
    emitter.writeLine("case psxrecomp::runtime::LogLevel::Warn:");
    emitter.writeLine("return \"warn\";");
    emitter.writeLine("case psxrecomp::runtime::LogLevel::Error:");
    emitter.writeLine("return \"error\";");
    emitter.writeLine("default:");
    emitter.writeLine("return \"info\";");
    emitter.closeBlock();
    emitter.closeBlock();
    emitter.closeBlock();
    emitter.writeBlank();
    emitter.writeLine("std::optional<std::string> readTextFile(const std::filesystem::path& path)");
    emitter.openBlock("");
    emitter.writeLine("std::ifstream in(path, std::ios::binary);");
    emitter.writeLine("if (!in)");
    emitter.openBlock("");
    emitter.writeLine("return std::nullopt;");
    emitter.closeBlock();
    emitter.writeLine("std::string data((std::istreambuf_iterator<char>(in)),");
    emitter.writeLine("                 std::istreambuf_iterator<char>());");
    emitter.writeLine("return data;");
    emitter.closeBlock();
    emitter.writeBlank();
    emitter.writeLine("bool extractJsonObjectField(const std::string& json, const std::string& "
                      "field, std::string* value)");
    emitter.openBlock("");
    emitter.writeLine("if (value == nullptr)");
    emitter.openBlock("");
    emitter.writeLine("return false;");
    emitter.closeBlock();
    emitter.writeLine("const std::string needle = \"\\\"\" + field + \"\\\"\";");
    emitter.writeLine("size_t pos = json.find(needle);");
    emitter.writeLine("if (pos == std::string::npos)");
    emitter.openBlock("");
    emitter.writeLine("return false;");
    emitter.closeBlock();
    emitter.writeLine("pos = json.find(':', pos + needle.size());");
    emitter.writeLine("if (pos == std::string::npos)");
    emitter.openBlock("");
    emitter.writeLine("return false;");
    emitter.closeBlock();
    emitter.writeLine("++pos;");
    emitter.writeLine(
        "while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos])))");
    emitter.openBlock("");
    emitter.writeLine("++pos;");
    emitter.closeBlock();
    emitter.writeLine("if (pos >= json.size() || json[pos] != '{')");
    emitter.openBlock("");
    emitter.writeLine("return false;");
    emitter.closeBlock();
    emitter.writeLine("const size_t objectStart = pos;");
    emitter.writeLine("size_t depth = 0;");
    emitter.writeLine("bool inString = false;");
    emitter.writeLine("bool escaped = false;");
    emitter.writeLine("for (size_t i = objectStart; i < json.size(); ++i)");
    emitter.openBlock("");
    emitter.writeLine("const char ch = json[i];");
    emitter.writeLine("if (inString)");
    emitter.openBlock("");
    emitter.writeLine("if (escaped)");
    emitter.openBlock("");
    emitter.writeLine("escaped = false;");
    emitter.writeLine("continue;");
    emitter.closeBlock();
    emitter.writeLine("if (ch == '\\\\')");
    emitter.openBlock("");
    emitter.writeLine("escaped = true;");
    emitter.writeLine("continue;");
    emitter.closeBlock();
    emitter.writeLine("if (ch == '\"')");
    emitter.openBlock("");
    emitter.writeLine("inString = false;");
    emitter.closeBlock();
    emitter.writeLine("continue;");
    emitter.closeBlock();
    emitter.writeLine("if (ch == '\"')");
    emitter.openBlock("");
    emitter.writeLine("inString = true;");
    emitter.writeLine("continue;");
    emitter.closeBlock();
    emitter.writeLine("if (ch == '{')");
    emitter.openBlock("");
    emitter.writeLine("++depth;");
    emitter.writeLine("continue;");
    emitter.closeBlock();
    emitter.writeLine("if (ch == '}')");
    emitter.openBlock("");
    emitter.writeLine("if (depth == 0)");
    emitter.openBlock("");
    emitter.writeLine("return false;");
    emitter.closeBlock();
    emitter.writeLine("--depth;");
    emitter.writeLine("if (depth == 0)");
    emitter.openBlock("");
    emitter.writeLine("*value = json.substr(objectStart, i - objectStart + 1);");
    emitter.writeLine("return true;");
    emitter.closeBlock();
    emitter.closeBlock();
    emitter.closeBlock();
    emitter.writeLine("return false;");
    emitter.closeBlock();
    emitter.writeBlank();
    emitter.writeLine("bool extractJsonBoolField(const std::string& json, const std::string& "
                      "field, bool* value)");
    emitter.openBlock("");
    emitter.writeLine("if (value == nullptr)");
    emitter.openBlock("");
    emitter.writeLine("return false;");
    emitter.closeBlock();
    emitter.writeLine("const std::string needle = \"\\\"\" + field + \"\\\"\";");
    emitter.writeLine("size_t pos = json.find(needle);");
    emitter.writeLine("if (pos == std::string::npos)");
    emitter.openBlock("");
    emitter.writeLine("return false;");
    emitter.closeBlock();
    emitter.writeLine("pos = json.find(':', pos + needle.size());");
    emitter.writeLine("if (pos == std::string::npos)");
    emitter.openBlock("");
    emitter.writeLine("return false;");
    emitter.closeBlock();
    emitter.writeLine("++pos;");
    emitter.writeLine(
        "while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos])))");
    emitter.openBlock("");
    emitter.writeLine("++pos;");
    emitter.closeBlock();
    emitter.writeLine("if (pos + 4 <= json.size() && json.compare(pos, 4, \"true\") == 0)");
    emitter.openBlock("");
    emitter.writeLine("*value = true;");
    emitter.writeLine("return true;");
    emitter.closeBlock();
    emitter.writeLine("if (pos + 5 <= json.size() && json.compare(pos, 5, \"false\") == 0)");
    emitter.openBlock("");
    emitter.writeLine("*value = false;");
    emitter.writeLine("return true;");
    emitter.closeBlock();
    emitter.writeLine("return false;");
    emitter.closeBlock();
    emitter.writeBlank();
    emitter.writeLine(
        "bool extractJsonU64Field(const std::string& json, const std::string& field,");
    emitter.writeLine("                         psxrecomp::u64* value)");
    emitter.openBlock("");
    emitter.writeLine("if (value == nullptr)");
    emitter.openBlock("");
    emitter.writeLine("return false;");
    emitter.closeBlock();
    emitter.writeLine("const std::string needle = \"\\\"\" + field + \"\\\"\";");
    emitter.writeLine("size_t pos = json.find(needle);");
    emitter.writeLine("if (pos == std::string::npos)");
    emitter.openBlock("");
    emitter.writeLine("return false;");
    emitter.closeBlock();
    emitter.writeLine("pos = json.find(':', pos + needle.size());");
    emitter.writeLine("if (pos == std::string::npos)");
    emitter.openBlock("");
    emitter.writeLine("return false;");
    emitter.closeBlock();
    emitter.writeLine("++pos;");
    emitter.writeLine(
        "while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos])))");
    emitter.openBlock("");
    emitter.writeLine("++pos;");
    emitter.closeBlock();
    emitter.writeLine(
        "if (pos >= json.size() || !std::isdigit(static_cast<unsigned char>(json[pos])))");
    emitter.openBlock("");
    emitter.writeLine("return false;");
    emitter.closeBlock();
    emitter.writeLine("size_t end = pos;");
    emitter.writeLine(
        "while (end < json.size() && std::isdigit(static_cast<unsigned char>(json[end])))");
    emitter.openBlock("");
    emitter.writeLine("++end;");
    emitter.closeBlock();
    emitter.writeLine("try");
    emitter.openBlock("");
    emitter.writeLine(
        "*value = static_cast<psxrecomp::u64>(std::stoull(json.substr(pos, end - pos)));");
    emitter.writeLine("return true;");
    emitter.closeBlock();
    emitter.writeLine("catch (...)");
    emitter.openBlock("");
    emitter.writeLine("return false;");
    emitter.closeBlock();
    emitter.closeBlock();
    emitter.writeBlank();
}

} // namespace recompiler
} // namespace psxrecomp
