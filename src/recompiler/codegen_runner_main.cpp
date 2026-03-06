#include "codegen_runner_sections.h"

namespace psxrecomp
{
namespace recompiler
{

void emitRunnerMainFunction(CppEmitter& emitter, const std::string& moduleName)
{
    emitter.writeLine("int main(int argc, char** argv)");
    emitter.openBlock("");
    emitter.writeLine("(void)argc;");
    emitter.writeLine("std::cout << std::unitbuf;");
    emitter.writeLine("std::cerr << std::unitbuf;");
    emitter.writeLine("std::cout << \"[psxrecomp] Starting module: " + moduleName + "\\n\";");
    emitter.writeLine(
        "std::cout << \"[psxrecomp] Runtime checks: \" << PSXRECOMP_ENABLE_CHECKS << \"\\n\";");
    emitter.writeLine(
        "std::cout << \"[psxrecomp] Logging enabled: \" << PSXRECOMP_ENABLE_LOGGING << \"\\n\";");
    emitter.writeBlank();
    emitter.writeLine("std::filesystem::path exeDir = std::filesystem::current_path();");
    emitter.writeLine("if (argv != nullptr && argv[0] != nullptr)");
    emitter.openBlock("");
    emitter.writeLine("exeDir = std::filesystem::path(argv[0]).parent_path();");
    emitter.closeBlock();
    emitter.writeLine("std::filesystem::path resourcesDir = exeDir / \"resources\";");
    emitter.writeLine("std::filesystem::path resourceManifestPath =");
    emitter.writeLine("    resourcesDir / \"index\" / \"resources_manifest.json\";");
    emitter.writeLine("if (!std::filesystem::exists(resourcesDir))");
    emitter.openBlock("");
    emitter.writeLine("std::cerr << \"[psxrecomp][warn] resources directory not found near "
                      "executable: \" << resourcesDir.string() << \"\\n\";");
    emitter.writeLine("std::cerr << \"[psxrecomp][warn] Missing assets can result in a black "
                      "screen or silent startup.\\n\";");
    emitter.closeBlock();
    emitter.writeLine("else if (!std::filesystem::exists(resourceManifestPath))");
    emitter.openBlock("");
    emitter.writeLine("std::cerr << \"[psxrecomp][warn] resources manifest not found: \"");
    emitter.writeLine("          << resourceManifestPath.string() << \"\\n\";");
    emitter.closeBlock();
    emitter.writeLine("else");
    emitter.openBlock("");
    emitter.writeLine("auto manifestText = readTextFile(resourceManifestPath);");
    emitter.writeLine("if (!manifestText.has_value())");
    emitter.openBlock("");
    emitter.writeLine("std::cerr << \"[psxrecomp][warn] Failed to read resources manifest: \"");
    emitter.writeLine("          << resourceManifestPath.string() << \"\\n\";");
    emitter.closeBlock();
    emitter.writeLine("else");
    emitter.openBlock("");
    emitter.writeLine("bool filesystemEnabled = false;");
    emitter.writeLine("psxrecomp::u64 containersScanned = 0;");
    emitter.writeLine("psxrecomp::u64 hitsExtracted = 0;");
    emitter.writeLine("std::string runtimeSummaryJson;");
    emitter.writeLine("const std::string* summaryJson = &(*manifestText);");
    emitter.writeLine(
        "if (extractJsonObjectField(*manifestText, \"runtimeSummary\", &runtimeSummaryJson))");
    emitter.openBlock("");
    emitter.writeLine("summaryJson = &runtimeSummaryJson;");
    emitter.closeBlock();
    emitter.writeLine(
        "extractJsonBoolField(*summaryJson, \"filesystemEnabled\", &filesystemEnabled);");
    emitter.writeLine(
        "extractJsonU64Field(*summaryJson, \"containersScanned\", &containersScanned);");
    emitter.writeLine("extractJsonU64Field(*summaryJson, \"hitsExtracted\", &hitsExtracted);");
    emitter.writeLine("std::cout << \"[psxrecomp] Resource index: fs.enabled=\"");
    emitter.writeLine("          << (filesystemEnabled ? \"true\" : \"false\")");
    emitter.writeLine("          << \", embedded.containersScanned=\" << containersScanned");
    emitter.writeLine("          << \", embedded.hitsExtracted=\" << hitsExtracted << \"\\n\";");
    emitter.closeBlock();
    emitter.closeBlock();
    emitter.writeBlank();
    emitter.writeLine("psxrecomp::runtime::PsxSystem system;");
    emitter.writeLine("std::atomic<bool> stopPresenter{false};");
    emitter.writeLine("std::thread presenterThread;");
    emitter.writeLine("#if PSXRECOMP_ENABLE_LOGGING");
    emitter.writeLine("psxrecomp::runtime::LogLevel defaultLogLevel = "
                      "psxrecomp::runtime::LogLevel::Info;");
    emitter.writeLine("switch (PSXRECOMP_LOG_LEVEL)");
    emitter.openBlock("");
    emitter.writeLine("case 0:");
    emitter.writeLine("defaultLogLevel = psxrecomp::runtime::LogLevel::Debug;");
    emitter.writeLine("break;");
    emitter.writeLine("case 1:");
    emitter.writeLine("defaultLogLevel = psxrecomp::runtime::LogLevel::Info;");
    emitter.writeLine("break;");
    emitter.writeLine("case 2:");
    emitter.writeLine("defaultLogLevel = psxrecomp::runtime::LogLevel::Warn;");
    emitter.writeLine("break;");
    emitter.writeLine("case 3:");
    emitter.writeLine("defaultLogLevel = psxrecomp::runtime::LogLevel::Error;");
    emitter.writeLine("break;");
    emitter.writeLine("default:");
    emitter.writeLine("defaultLogLevel = psxrecomp::runtime::LogLevel::Info;");
    emitter.writeLine("break;");
    emitter.closeBlock();
    emitter.writeLine("#else");
    emitter.writeLine("psxrecomp::runtime::LogLevel defaultLogLevel = "
                      "psxrecomp::runtime::LogLevel::Error;");
    emitter.writeLine("#endif");
    emitter.writeLine("psxrecomp::runtime::LogLevel effectiveLogLevel = defaultLogLevel;");
    emitter.writeLine("if (const char* logLevelEnv = std::getenv(\"PSXRECOMP_LOG_LEVEL\"))");
    emitter.openBlock("");
    emitter.writeLine("bool logLevelOk = false;");
    emitter.writeLine("const auto parsed = decodeLogLevel(logLevelEnv, &logLevelOk);");
    emitter.writeLine("if (logLevelOk)");
    emitter.openBlock("");
    emitter.writeLine("effectiveLogLevel = parsed;");
    emitter.closeBlock();
    emitter.writeLine("else");
    emitter.openBlock("");
    emitter.writeLine("std::cerr << \"[psxrecomp][warn] Invalid PSXRECOMP_LOG_LEVEL='\" "
                      "<< logLevelEnv << \"' (expected: debug/info/warn/error or 0..3).\\n\";");
    emitter.closeBlock();
    emitter.closeBlock();
    emitter.writeLine("system.logger().setMinLevel(effectiveLogLevel);");
    emitter.writeLine("const char* presentEnv = std::getenv(\"PSXRECOMP_PRESENT_FRAMEBUFFER\");");
    emitter.writeLine("const bool renderDebugOverlay = "
                      "envFlagEnabled(std::getenv(\"PSXRECOMP_RENDER_DEBUG_OVERLAY\"), false);");
    emitter.writeLine("std::cout << \"[psxrecomp] Runtime log level: \" << "
                      "logLevelLabel(effectiveLogLevel) << \"\\n\";");
    emitter.writeLine("std::cout << \"[psxrecomp] Render debug overlay: \" << "
                      "(renderDebugOverlay ? \"on\" : \"off\") << \"\\n\";");
    emitter.writeLine("try");
    emitter.openBlock("");
    emitter.writeLine("if (!system.initialize())");
    emitter.openBlock("");
    emitter.writeLine(
        "std::cerr << \"[psxrecomp][error] Failed to initialize runtime system.\" << \"\\n\";");
    emitter.writeLine("return 1;");
    emitter.closeBlock();
    emitter.writeLine("psxrecomp::recompiler::RecompiledModule::configure(system);");
    emitter.writeLine("const std::filesystem::path defaultDiscPath = resourcesDir / \"disc\" / "
                      "\"data_track.bin\";");
    emitter.writeLine(
        "const std::filesystem::path defaultDiscLayoutPath = resourcesDir / \"disc\" / "
        "\"disc_layout.json\";");
    emitter.writeLine("std::filesystem::path mountedDiscPath = defaultDiscPath;");
    emitter.writeLine("bool usingDefaultDiscPath = true;");
    emitter.writeLine("if (const char* discPathEnv = std::getenv(\"PSXRECOMP_DISC_IMAGE\"))");
    emitter.openBlock("");
    emitter.writeLine("if (discPathEnv[0] != '\\0')");
    emitter.openBlock("");
    emitter.writeLine("mountedDiscPath = std::filesystem::path(discPathEnv);");
    emitter.writeLine("usingDefaultDiscPath = false;");
    emitter.closeBlock();
    emitter.closeBlock();
    emitter.writeLine("psxrecomp::runtime::DiscImage::Layout mountedDiscLayout =");
    emitter.writeLine("    psxrecomp::runtime::DiscImage::Layout::Auto;");
    emitter.writeLine("if (usingDefaultDiscPath)");
    emitter.openBlock("");
    emitter.writeLine("if (auto layoutText = readTextFile(defaultDiscLayoutPath))");
    emitter.openBlock("");
    emitter.writeLine("psxrecomp::u64 sectorSize = 0;");
    emitter.writeLine("if (extractJsonU64Field(*layoutText, \"sectorSize\", &sectorSize))");
    emitter.openBlock("");
    emitter.writeLine("if (sectorSize == 2048)");
    emitter.openBlock("");
    emitter.writeLine("mountedDiscLayout = psxrecomp::runtime::DiscImage::Layout::User2048;");
    emitter.closeBlock();
    emitter.writeLine("else if (sectorSize == 2352)");
    emitter.openBlock("");
    emitter.writeLine("mountedDiscLayout = psxrecomp::runtime::DiscImage::Layout::Raw2352;");
    emitter.closeBlock();
    emitter.writeLine("else");
    emitter.openBlock("");
    emitter.writeLine("std::cerr << \"[psxrecomp][warn] Unsupported disc layout sectorSize=\"");
    emitter.writeLine("          << sectorSize << \" in \" << defaultDiscLayoutPath.string()");
    emitter.writeLine("          << \"; falling back to auto-detect.\\n\";");
    emitter.closeBlock();
    emitter.closeBlock();
    emitter.writeLine("else");
    emitter.openBlock("");
    emitter.writeLine("std::cerr << \"[psxrecomp][warn] disc_layout.json missing sectorSize: \"");
    emitter.writeLine("          << defaultDiscLayoutPath.string()");
    emitter.writeLine("          << \"; falling back to auto-detect.\\n\";");
    emitter.closeBlock();
    emitter.closeBlock();
    emitter.writeLine("else");
    emitter.openBlock("");
    emitter.writeLine("std::cerr << \"[psxrecomp][warn] disc layout metadata not found: \"");
    emitter.writeLine("          << defaultDiscLayoutPath.string()");
    emitter.writeLine("          << \"; falling back to auto-detect.\\n\";");
    emitter.closeBlock();
    emitter.closeBlock();
    emitter.writeLine("if (std::filesystem::exists(mountedDiscPath))");
    emitter.openBlock("");
    emitter.writeLine("auto disc = std::make_shared<psxrecomp::runtime::DiscImage>();");
    emitter.writeLine("if (disc->open(mountedDiscPath, mountedDiscLayout))");
    emitter.openBlock("");
    emitter.writeLine("system.setDisc(disc);");
    emitter.writeLine(
        "std::cout << \"[psxrecomp] disc mounted: \" << mountedDiscPath.string() << \"\\n\";");
    emitter.closeBlock();
    emitter.writeLine("else");
    emitter.openBlock("");
    emitter.writeLine("std::cerr << \"[psxrecomp][warn] Failed to mount disc image: \" << "
                      "mountedDiscPath.string() << \"\\n\";");
    emitter.closeBlock();
    emitter.closeBlock();
    emitter.writeLine("else");
    emitter.openBlock("");
    emitter.writeLine("std::cerr << \"[psxrecomp][warn] Runtime disc blob not found: \" << "
                      "mountedDiscPath.string() << \"\\n\";");
    emitter.closeBlock();
    emitter.writeLine("psxrecomp::recompiler::RecompiledModule::initMemory(system);");
    emitter.writeLine("#if PSXRECOMP_HAS_SDL2 || defined(_WIN32)");
    emitter.writeLine("const bool defaultPresent = true;");
    emitter.writeLine("#else");
    emitter.writeLine("const bool defaultPresent = false;");
    emitter.writeLine("#endif");
    emitter.writeLine("const bool enabledPresent = envFlagEnabled(presentEnv, defaultPresent);");
    emitter.writeLine("if (enabledPresent)");
    emitter.openBlock("");
    emitter.writeLine("presenterThread = std::thread([&]()");
    emitter.openBlock("");
    emitter.writeLine("if (!presentFramebufferLive(system, stopPresenter, renderDebugOverlay))");
    emitter.openBlock("");
    emitter.writeLine(
        "std::cerr << \"[psxrecomp][warn] Live framebuffer presenter unavailable; install SDL2 "
        "or disable PSXRECOMP_PRESENT_FRAMEBUFFER.\\n\";");
    emitter.closeBlock();
    emitter.closeBlock(");");
    emitter.closeBlock();
    emitter.writeLine("auto runStart = std::chrono::steady_clock::now();");
    emitter.writeLine("psxrecomp::recompiler::RecompiledModule::run(system);");
    emitter.writeLine("auto runEnd = std::chrono::steady_clock::now();");
    emitter.writeLine("stopPresenter.store(true, std::memory_order_relaxed);");
    emitter.writeLine("if (presenterThread.joinable())");
    emitter.openBlock("");
    emitter.writeLine("presenterThread.join();");
    emitter.closeBlock();
    emitter.writeLine(
        "auto runMs = std::chrono::duration_cast<std::chrono::milliseconds>(runEnd - runStart)"
        ".count();");
    emitter.writeLine("const auto framePixels = system.gpu().frameBufferSnapshot();");
    emitter.writeLine("const auto displayWindow = system.gpu().displayWindow();");
    emitter.writeLine("size_t displayWidth = 0;");
    emitter.writeLine("size_t displayHeight = 0;");
    emitter.writeLine(
        "std::vector<psxrecomp::u16> displayPixels = extractDisplayPixels(framePixels, "
        "displayWindow, &displayWidth, &displayHeight);");
    emitter.writeLine("size_t nonZeroDisplay = countNonZeroPixels(displayPixels);");
    emitter.writeLine("bool usedAlternateDisplayPage = false;");
    emitter.writeLine("if (nonZeroDisplay == 0 && displayWidth > 0 && displayHeight > 0)");
    emitter.openBlock("");
    emitter.writeLine("const size_t fullHeight = psxrecomp::runtime::SoftwareGpuRenderer::Height;");
    emitter.writeLine("psxrecomp::runtime::Gpu::DisplayWindow altWindow = displayWindow;");
    emitter.writeLine("bool hasAlternate = false;");
    emitter.writeLine("if (static_cast<size_t>(displayWindow.y) + displayHeight < fullHeight)");
    emitter.openBlock("");
    emitter.writeLine(
        "altWindow.y = static_cast<psxrecomp::u16>(displayWindow.y + displayHeight);");
    emitter.writeLine("hasAlternate = true;");
    emitter.closeBlock();
    emitter.writeLine("else if (static_cast<size_t>(displayWindow.y) >= displayHeight)");
    emitter.openBlock("");
    emitter.writeLine(
        "altWindow.y = static_cast<psxrecomp::u16>(displayWindow.y - displayHeight);");
    emitter.writeLine("hasAlternate = true;");
    emitter.closeBlock();
    emitter.writeLine("if (hasAlternate)");
    emitter.openBlock("");
    emitter.writeLine("size_t altWidth = 0;");
    emitter.writeLine("size_t altHeight = 0;");
    emitter.writeLine(
        "auto altPixels = extractDisplayPixels(framePixels, altWindow, &altWidth, &altHeight);");
    emitter.writeLine("if (altWidth == displayWidth && altHeight == displayHeight)");
    emitter.openBlock("");
    emitter.writeLine("const size_t altNonZero = countNonZeroPixels(altPixels);");
    emitter.writeLine("if (altNonZero > nonZeroDisplay)");
    emitter.openBlock("");
    emitter.writeLine("displayPixels.swap(altPixels);");
    emitter.writeLine("nonZeroDisplay = altNonZero;");
    emitter.writeLine("usedAlternateDisplayPage = true;");
    emitter.closeBlock();
    emitter.closeBlock();
    emitter.closeBlock();
    emitter.closeBlock();
    emitter.writeLine("bool usedVramFallback = false;");
    emitter.writeLine("if (nonZeroDisplay == 0)");
    emitter.openBlock("");
    emitter.writeLine("const auto& vramWords = system.gpu().vramWords();");
    emitter.writeLine("if (!vramWords.empty() && displayWidth > 0 && displayHeight > 0)");
    emitter.openBlock("");
    emitter.writeLine("const size_t fullWidth = psxrecomp::runtime::SoftwareGpuRenderer::Width;");
    emitter.writeLine("displayPixels.assign(displayWidth * displayHeight, 0);");
    emitter.writeLine("nonZeroDisplay = 0;");
    emitter.writeLine("for (size_t y = 0; y < displayHeight; ++y)");
    emitter.openBlock("");
    emitter.writeLine("for (size_t x = 0; x < displayWidth; ++x)");
    emitter.openBlock("");
    emitter.writeLine("const size_t srcPixel =");
    emitter.writeLine("    (static_cast<size_t>(displayWindow.y) + y) * fullWidth +");
    emitter.writeLine("    (static_cast<size_t>(displayWindow.x) + x);");
    emitter.writeLine("const psxrecomp::u32 word = vramWords[srcPixel / 2];");
    emitter.writeLine("const psxrecomp::u16 pixel = static_cast<psxrecomp::u16>(");
    emitter.writeLine("    ((srcPixel % 2) == 0) ? (word & 0xFFFF) : ((word >> 16) & 0xFFFF));");
    emitter.writeLine("displayPixels[y * displayWidth + x] = pixel;");
    emitter.writeLine("if (pixel != 0)");
    emitter.openBlock("");
    emitter.writeLine("++nonZeroDisplay;");
    emitter.closeBlock();
    emitter.closeBlock();
    emitter.closeBlock();
    emitter.writeLine("if (nonZeroDisplay > 0)");
    emitter.openBlock("");
    emitter.writeLine("usedVramFallback = true;");
    emitter.closeBlock();
    emitter.closeBlock();
    emitter.closeBlock();
    emitter.writeLine(
        "std::cout << \"[psxrecomp] Module execution returned in \" << runMs << \" ms.\\n\";");
    emitter.writeLine(
        "std::cout << \"[psxrecomp] GPU command count: \" << system.gpu().commandTrace().size()"
        " << \", non-zero display pixels: \" << nonZeroDisplay"
        " << \" (\" << displayWidth << \"x\" << displayHeight << \")\""
        " << (usedAlternateDisplayPage ? \" (using alternate display page)\" : \"\")"
        " << (usedVramFallback ? \" (using VRAM fallback)\" : \"\") << \"\\n\";");
    emitter.writeLine("std::cout << \"[psxrecomp] Debug overlay: \" << "
                      "system.debugOverlay().renderText() << \"\\n\";");
    emitter.writeLine("if (renderDebugOverlay)");
    emitter.openBlock("");
    emitter.writeLine(
        "system.debugOverlay().drawOnFrameBuffer(displayPixels, displayWidth, displayHeight);");
    emitter.closeBlock();
    emitter.writeLine("std::cout << \"[psxrecomp] Last PC: 0x\" << std::hex << std::uppercase"
                      " << system.debugOverlay().lastProgramCounter() << std::dec << \"\\n\";");
    emitter.writeLine("if (runMs == 0 && system.gpu().commandTrace().empty())");
    emitter.openBlock("");
    emitter.writeLine(
        "std::cerr << \"[psxrecomp][warn] Module returned immediately with no GPU commands. \""
        " << \"Entrypoint may have returned or execution never reached rendering code.\\n\";");
    emitter.closeBlock();
    emitter.writeLine("if (const char* dumpPathEnv = std::getenv(\"PSXRECOMP_DUMP_FRAMEBUFFER\"))");
    emitter.openBlock("");
    emitter.writeLine("std::filesystem::path dumpPath = dumpPathEnv[0] != '\\0' ? dumpPathEnv : "
                      "\"framebuffer.ppm\";");
    emitter.writeLine(
        "const char* fullDumpPathEnv = std::getenv(\"PSXRECOMP_DUMP_FULL_FRAMEBUFFER\");");
    emitter.writeLine("std::filesystem::path fullDumpPath =");
    emitter.writeLine("    (fullDumpPathEnv != nullptr && fullDumpPathEnv[0] != '\\0')");
    emitter.writeLine("        ? std::filesystem::path(fullDumpPathEnv)");
    emitter.writeLine("        : appendSuffixBeforeExtension(dumpPath, \".full\");");
    emitter.writeLine("if (dumpFramebufferToPpm(dumpPath, displayPixels, displayWidth, "
                      "displayHeight))");
    emitter.openBlock("");
    emitter.writeLine(
        "std::cout << \"[psxrecomp] Active display dumped to: \" << dumpPath.string() << \"\\n\";");
    emitter.closeBlock();
    emitter.writeLine("else");
    emitter.openBlock("");
    emitter.writeLine("std::cerr << \"[psxrecomp][warn] Failed to dump active display to: \" << "
                      "dumpPath.string() << \"\\n\";");
    emitter.closeBlock();
    emitter.writeLine("if (dumpFramebufferToPpm(fullDumpPath, framePixels,");
    emitter.writeLine("                         psxrecomp::runtime::SoftwareGpuRenderer::Width,");
    emitter.writeLine("                         psxrecomp::runtime::SoftwareGpuRenderer::Height))");
    emitter.openBlock("");
    emitter.writeLine(
        "std::cout << \"[psxrecomp] Full framebuffer dumped to: \" << fullDumpPath.string() << "
        "\"\\n\";");
    emitter.closeBlock();
    emitter.writeLine("else");
    emitter.openBlock("");
    emitter.writeLine("std::cerr << \"[psxrecomp][warn] Failed to dump full framebuffer to: \" << "
                      "fullDumpPath.string() << \"\\n\";");
    emitter.closeBlock();
    emitter.closeBlock();
    emitter.writeLine("std::cout << \"[psxrecomp] Module execution returned.\" << \"\\n\";");
    emitter.writeLine(
        "std::cout << \"[psxrecomp] If nothing appears, the game may still be blocked by "
        "unimplemented hardware paths (GPU/SPU/CDROM/timing).\" << \"\\n\";");
    emitter.writeLine(
        "std::cout << \"[psxrecomp] Tip: set PSXRECOMP_PRESENT_FRAMEBUFFER=1 for an SDL window, "
        "or PSXRECOMP_DUMP_FRAMEBUFFER=/path/frame.ppm for a dump. "
        "PSXRECOMP_DUMP_FULL_FRAMEBUFFER can override the full-frame dump path. "
        "Set PSXRECOMP_RENDER_DEBUG_OVERLAY=1 to draw overlay/fps into presented/dumped frames.\" "
        "<< \"\\n\";");
    emitter.writeLine("return 0;");
    emitter.closeBlock();
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
