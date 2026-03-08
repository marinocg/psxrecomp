#include "codegen_runner_sections.h"

namespace psxrecomp
{
namespace recompiler
{

void emitRunnerExceptionBlock(CppEmitter& emitter)
{
    emitter.writeLine("catch (const std::exception& ex)");
    emitter.openBlock("");
    emitter.writeLine("stopPresenter.store(true, std::memory_order_relaxed);");
    emitter.writeLine("if (presenterThread.joinable())");
    emitter.openBlock("");
    emitter.writeLine("presenterThread.join();");
    emitter.closeBlock();
    emitter.writeLine(
        "std::cerr << \"[psxrecomp][error] Unhandled exception: \" << ex.what() << \"\\n\";");
    emitter.writeLine("std::cerr << std::dec;");
    emitter.writeLine("std::cerr << \"[psxrecomp] GPU command count: \""
                      " << system.gpu().commandTrace().size() << \"\\n\";");
    emitter.writeLine(
        "std::cerr << \"[psxrecomp] Frame count: \" << system.frameCount() << \"\\n\";");
    emitter.writeLine("const auto& exFbCheck = system.gpu().frameBuffer();");
    emitter.writeLine("size_t exFbNonZero = 0;");
    emitter.writeLine(
        "for (size_t i = 0; i < exFbCheck.size(); ++i) { if (exFbCheck[i] != 0) ++exFbNonZero; }");
    emitter.writeLine(
        "std::cerr << \"[psxrecomp] Non-zero framebuffer pixels: \" << exFbNonZero << \"\\n\";");
    emitter.writeLine("const auto& exVram = system.gpu().vramWords();");
    emitter.writeLine("size_t exNonZero = 0;");
    emitter.writeLine(
        "for (size_t i = 0; i < exVram.size(); ++i) { if (exVram[i] != 0) ++exNonZero; }");
    emitter.writeLine(
        "std::cerr << \"[psxrecomp] Non-zero VRAM words: \" << exNonZero << \"\\n\";");
    emitter.writeLine(
        "for (size_t ci = 0; ci < system.gpu().commandTrace().size() && ci < 40; ++ci)");
    emitter.openBlock("");
    emitter.writeLine("const auto& cmd = system.gpu().commandTrace()[ci];");
    emitter.writeLine(
        "std::cerr << \"[gpu-trace] #\" << ci << \" kind=\" << static_cast<int>(cmd.kind)"
        " << \" gp1=\" << cmd.fromGp1 << \" words=[\";");
    emitter.writeLine(
        "for (size_t wi = 0; wi < cmd.words.size(); ++wi)"
        " { std::cerr << (wi ? \",\" : \"\") << \"0x\" << std::hex << cmd.words[wi]; }");
    emitter.writeLine("std::cerr << std::dec << \"]\" << \"\\n\";");
    emitter.closeBlock();
    emitter.writeLine("if (const char* exDump = std::getenv(\"PSXRECOMP_DUMP_FRAMEBUFFER\"))");
    emitter.openBlock("");
    emitter.writeLine("std::filesystem::path exDumpPath = exDump[0] != '\\0' ? exDump : "
                      "\"framebuffer.ppm\";");
    emitter.writeLine("const char* exFullDump = std::getenv(\"PSXRECOMP_DUMP_FULL_FRAMEBUFFER\");");
    emitter.writeLine("std::filesystem::path exFullDumpPath =");
    emitter.writeLine("    (exFullDump != nullptr && exFullDump[0] != '\\0')");
    emitter.writeLine("        ? std::filesystem::path(exFullDump)");
    emitter.writeLine("        : appendSuffixBeforeExtension(exDumpPath, \".full\");");
    emitter.writeLine("size_t exWidth = 0;");
    emitter.writeLine("size_t exHeight = 0;");
    emitter.writeLine("const auto exWindow = system.gpu().displayWindow();");
    emitter.writeLine("const auto exFrame = system.gpu().frameBufferSnapshot();");
    emitter.writeLine("std::vector<psxrecomp::u16> exPixels = extractDisplayPixels(");
    emitter.writeLine("    exFrame, exWindow, &exWidth, &exHeight);");
    emitter.writeLine("if (countNonZeroPixels(exPixels) == 0 && exWidth > 0 && exHeight > 0)");
    emitter.openBlock("");
    emitter.writeLine("const size_t fullHeight = psxrecomp::runtime::SoftwareGpuRenderer::Height;");
    emitter.writeLine("psxrecomp::runtime::Gpu::DisplayWindow altWindow = exWindow;");
    emitter.writeLine("bool hasAlternate = false;");
    emitter.writeLine("if (static_cast<size_t>(exWindow.y) + exHeight < fullHeight)");
    emitter.openBlock("");
    emitter.writeLine("altWindow.y = static_cast<psxrecomp::u16>(exWindow.y + exHeight);");
    emitter.writeLine("hasAlternate = true;");
    emitter.closeBlock();
    emitter.writeLine("else if (static_cast<size_t>(exWindow.y) >= exHeight)");
    emitter.openBlock("");
    emitter.writeLine("altWindow.y = static_cast<psxrecomp::u16>(exWindow.y - exHeight);");
    emitter.writeLine("hasAlternate = true;");
    emitter.closeBlock();
    emitter.writeLine("if (hasAlternate)");
    emitter.openBlock("");
    emitter.writeLine("size_t altWidth = 0;");
    emitter.writeLine("size_t altHeight = 0;");
    emitter.writeLine(
        "auto altPixels = extractDisplayPixels(exFrame, altWindow, &altWidth, &altHeight);");
    emitter.writeLine("if (altWidth == exWidth && altHeight == exHeight && "
                      "countNonZeroPixels(altPixels) > 0)");
    emitter.openBlock("");
    emitter.writeLine("exPixels.swap(altPixels);");
    emitter.closeBlock();
    emitter.closeBlock();
    emitter.closeBlock();
    emitter.writeLine("if (countNonZeroPixels(exPixels) == 0 && exWidth > 0 && exHeight > 0)");
    emitter.openBlock("");
    emitter.writeLine("const auto& exVramWords = system.gpu().vramWords();");
    emitter.writeLine("if (!exVramWords.empty())");
    emitter.openBlock("");
    emitter.writeLine("const size_t fullWidth = psxrecomp::runtime::SoftwareGpuRenderer::Width;");
    emitter.writeLine("exPixels.assign(exWidth * exHeight, 0);");
    emitter.writeLine("for (size_t y = 0; y < exHeight; ++y)");
    emitter.openBlock("");
    emitter.writeLine("for (size_t x = 0; x < exWidth; ++x)");
    emitter.openBlock("");
    emitter.writeLine("const size_t srcPixel =");
    emitter.writeLine("    (static_cast<size_t>(exWindow.y) + y) * fullWidth +");
    emitter.writeLine("    (static_cast<size_t>(exWindow.x) + x);");
    emitter.writeLine("const psxrecomp::u32 word = exVramWords[srcPixel / 2];");
    emitter.writeLine("const psxrecomp::u16 pixel = static_cast<psxrecomp::u16>(");
    emitter.writeLine("    ((srcPixel % 2) == 0) ? (word & 0xFFFF) : ((word >> 16) & 0xFFFF));");
    emitter.writeLine("exPixels[y * exWidth + x] = pixel;");
    emitter.closeBlock();
    emitter.closeBlock();
    emitter.closeBlock();
    emitter.closeBlock();
    emitter.writeLine(
        "if (renderDebugOverlay) system.debugOverlay().drawOnFrameBuffer(exPixels, exWidth, "
        "exHeight);");
    emitter.writeLine("dumpFramebufferToPpm(exDumpPath, exPixels, exWidth, exHeight);");
    emitter.writeLine("dumpFramebufferToPpm(exFullDumpPath, exFrame,");
    emitter.writeLine("                     psxrecomp::runtime::SoftwareGpuRenderer::Width,");
    emitter.writeLine("                     psxrecomp::runtime::SoftwareGpuRenderer::Height);");
    emitter.writeLine(
        "std::cerr << \"[psxrecomp] Framebuffer dumped (exception path): active='\" << "
        "exDumpPath.string() << \"', full='\" << exFullDumpPath.string() << \"'\\n\";");
    emitter.closeBlock();
    emitter.writeLine("return 1;");
    emitter.closeBlock();
    emitter.writeLine("catch (...)");
    emitter.openBlock("");
    emitter.writeLine("stopPresenter.store(true, std::memory_order_relaxed);");
    emitter.writeLine("if (presenterThread.joinable())");
    emitter.openBlock("");
    emitter.writeLine("presenterThread.join();");
    emitter.closeBlock();
    emitter.writeLine(
        "std::cerr << \"[psxrecomp][error] Unknown failure during module execution.\" << \"\\n\";");
    emitter.writeLine("return 1;");
    emitter.closeBlock();
    emitter.closeBlock();
}

} // namespace recompiler
} // namespace psxrecomp
