#include "psxrecomp/recompiler/codegen.h"

#include "cpp_emitter.h"

namespace psxrecomp
{
namespace recompiler
{

std::string CodeGenerator::generateRunnerSource(const std::string& moduleName)
{
    CppEmitter emitter;
    emitter.writeLine("#include \"" + moduleName + ".h\"");
    emitter.writeBlank();
    emitter.writeLine("#include \"psxrecomp/runtime/gpu_renderer.h\"");
    emitter.writeLine("#include \"psxrecomp/types.h\"");
    emitter.writeLine("#include <atomic>");
    emitter.writeLine("#include <chrono>");
    emitter.writeLine("#include <cstdlib>");
    emitter.writeLine("#include <exception>");
    emitter.writeLine("#include <filesystem>");
    emitter.writeLine("#include <fstream>");
    emitter.writeLine("#include <iostream>");
    emitter.writeLine("#include <string>");
    emitter.writeLine("#include <thread>");
    emitter.writeLine("#include <vector>");
    emitter.writeLine("#if PSXRECOMP_HAS_SDL2");
    emitter.writeLine("#include <SDL.h>");
    emitter.writeLine("#endif");
    emitter.writeLine("#if defined(_WIN32)");
    emitter.writeLine("#include <windows.h>");
    emitter.writeLine("#endif");
    emitter.writeBlank();
    emitter.writeLine("namespace");
    emitter.openBlock("");
    emitter.writeLine("bool dumpFramebufferToPpm(const std::filesystem::path& outputPath,");
    emitter.writeLine("                          const std::vector<psxrecomp::u16>& framebuffer)");
    emitter.openBlock("");
    emitter.writeLine("constexpr size_t width = psxrecomp::runtime::SoftwareGpuRenderer::Width;");
    emitter.writeLine("constexpr size_t height = psxrecomp::runtime::SoftwareGpuRenderer::Height;");
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
    emitter.closeBlock();
    emitter.writeBlank();
    emitter.writeLine("bool presentFramebufferLive(psxrecomp::runtime::PsxSystem& system,");
    emitter.writeLine("                          const std::atomic<bool>& stopRequested)");
    emitter.openBlock("");
    emitter.writeLine("constexpr size_t width = psxrecomp::runtime::SoftwareGpuRenderer::Width;");
    emitter.writeLine("constexpr size_t height = psxrecomp::runtime::SoftwareGpuRenderer::Height;");
    emitter.writeLine("std::vector<psxrecomp::u8> rgb(width * height * 3, 0);");
    emitter.writeLine("#if PSXRECOMP_HAS_SDL2");
    emitter.writeLine("if (SDL_Init(SDL_INIT_VIDEO) != 0)");
    emitter.openBlock("");
    emitter.writeLine(
        "std::cerr << \"[psxrecomp][warn] SDL init failed: \" << SDL_GetError() << \"\\n\";");
    emitter.writeLine("return false;");
    emitter.closeBlock();
    emitter.writeLine(
        "SDL_Window* window = SDL_CreateWindow(\"PSXRecomp Output\", SDL_WINDOWPOS_CENTERED,");
    emitter.writeLine("                                    SDL_WINDOWPOS_CENTERED, 1024, 512, 0);");
    emitter.writeLine("if (window == nullptr)");
    emitter.openBlock("");
    emitter.writeLine("std::cerr << \"[psxrecomp][warn] SDL window creation failed: \" << "
                      "SDL_GetError() << \"\\n\";");
    emitter.writeLine("SDL_Quit();");
    emitter.writeLine("return false;");
    emitter.closeBlock();
    emitter.writeLine(
        "SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);");
    emitter.writeLine("if (renderer == nullptr)");
    emitter.openBlock("");
    emitter.writeLine("renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);");
    emitter.closeBlock();
    emitter.writeLine("if (renderer == nullptr)");
    emitter.openBlock("");
    emitter.writeLine("std::cerr << \"[psxrecomp][warn] SDL renderer creation failed: \" << "
                      "SDL_GetError() << \"\\n\";");
    emitter.writeLine("SDL_DestroyWindow(window);");
    emitter.writeLine("SDL_Quit();");
    emitter.writeLine("return false;");
    emitter.closeBlock();
    emitter.writeLine("SDL_Texture* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB24,");
    emitter.writeLine(
        "                                        SDL_TEXTUREACCESS_STREAMING, 1024, 512);");
    emitter.writeLine("if (texture == nullptr)");
    emitter.openBlock("");
    emitter.writeLine("std::cerr << \"[psxrecomp][warn] SDL texture creation failed: \" << "
                      "SDL_GetError() << \"\\n\";");
    emitter.writeLine("SDL_DestroyRenderer(renderer);");
    emitter.writeLine("SDL_DestroyWindow(window);");
    emitter.writeLine("SDL_Quit();");
    emitter.writeLine("return false;");
    emitter.closeBlock();
    emitter.writeLine("bool running = true;");
    emitter.writeLine("while (running && !stopRequested.load(std::memory_order_relaxed))");
    emitter.openBlock("");
    emitter.writeLine("auto framebuffer = system.gpu().frameBufferSnapshot();");
    emitter.writeLine("if (framebuffer.size() < width * height)");
    emitter.openBlock("");
    emitter.writeLine("framebuffer.resize(width * height, 0);");
    emitter.closeBlock();
    emitter.writeLine("for (size_t i = 0; i < width * height; ++i)");
    emitter.openBlock("");
    emitter.writeLine("psxrecomp::u16 pixel = framebuffer[i];");
    emitter.writeLine(
        "rgb[i * 3 + 0] = static_cast<psxrecomp::u8>(((pixel >> 0) & 0x1F) * 255 / 31);");
    emitter.writeLine(
        "rgb[i * 3 + 1] = static_cast<psxrecomp::u8>(((pixel >> 5) & 0x1F) * 255 / 31);");
    emitter.writeLine(
        "rgb[i * 3 + 2] = static_cast<psxrecomp::u8>(((pixel >> 10) & 0x1F) * 255 / 31);");
    emitter.closeBlock();
    emitter.writeLine(
        "SDL_UpdateTexture(texture, nullptr, rgb.data(), static_cast<int>(width * 3));");
    emitter.writeLine("SDL_Event event;");
    emitter.writeLine("while (SDL_PollEvent(&event))");
    emitter.openBlock("");
    emitter.writeLine("if (event.type == SDL_QUIT)");
    emitter.openBlock("");
    emitter.writeLine("running = false;");
    emitter.closeBlock();
    emitter.writeLine("if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE)");
    emitter.openBlock("");
    emitter.writeLine("running = false;");
    emitter.closeBlock();
    emitter.closeBlock();
    emitter.writeLine("SDL_RenderClear(renderer);");
    emitter.writeLine("SDL_RenderCopy(renderer, texture, nullptr, nullptr);");
    emitter.writeLine("SDL_RenderPresent(renderer);");
    emitter.writeLine("SDL_Delay(16);");
    emitter.closeBlock();
    emitter.writeLine("SDL_DestroyTexture(texture);");
    emitter.writeLine("SDL_DestroyRenderer(renderer);");
    emitter.writeLine("SDL_DestroyWindow(window);");
    emitter.writeLine("SDL_Quit();");
    emitter.writeLine("return true;");
    emitter.writeLine("#elif defined(_WIN32)");
    emitter.writeLine("const int windowWidth = 1024;");
    emitter.writeLine("const int windowHeight = 512;");
    emitter.writeLine("const wchar_t* className = L\"PSXRecompWindowClass\";");
    emitter.writeLine("WNDCLASSW wc{};");
    emitter.writeLine("wc.lpfnWndProc = DefWindowProcW;");
    emitter.writeLine("wc.hInstance = GetModuleHandleW(nullptr);");
    emitter.writeLine("wc.lpszClassName = className;");
    emitter.writeLine("RegisterClassW(&wc);");
    emitter.writeLine("HWND window = CreateWindowExW(0, className, L\"PSXRecomp Output\", "
                      "WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, windowWidth, "
                      "windowHeight, nullptr, nullptr, wc.hInstance, nullptr);");
    emitter.writeLine("if (window == nullptr)");
    emitter.openBlock("");
    emitter.writeLine("return false;");
    emitter.closeBlock();
    emitter.writeLine("ShowWindow(window, SW_SHOW);");
    emitter.writeLine("UpdateWindow(window);");
    emitter.writeLine("MSG msg{};");
    emitter.writeLine("bool running = true;");
    emitter.writeLine("while (running && !stopRequested.load(std::memory_order_relaxed))");
    emitter.openBlock("");
    emitter.writeLine("while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))");
    emitter.openBlock("");
    emitter.writeLine("if (msg.message == WM_QUIT)");
    emitter.openBlock("");
    emitter.writeLine("running = false;");
    emitter.closeBlock();
    emitter.writeLine("TranslateMessage(&msg);");
    emitter.writeLine("DispatchMessageW(&msg);");
    emitter.closeBlock();
    emitter.writeLine("auto framebuffer = system.gpu().frameBufferSnapshot();");
    emitter.writeLine("if (framebuffer.size() < width * height)");
    emitter.openBlock("");
    emitter.writeLine("framebuffer.resize(width * height, 0);");
    emitter.closeBlock();
    emitter.writeLine("for (size_t i = 0; i < width * height; ++i)");
    emitter.openBlock("");
    emitter.writeLine("psxrecomp::u16 pixel = framebuffer[i];");
    emitter.writeLine(
        "rgb[i * 3 + 0] = static_cast<psxrecomp::u8>(((pixel >> 0) & 0x1F) * 255 / 31);");
    emitter.writeLine(
        "rgb[i * 3 + 1] = static_cast<psxrecomp::u8>(((pixel >> 5) & 0x1F) * 255 / 31);");
    emitter.writeLine(
        "rgb[i * 3 + 2] = static_cast<psxrecomp::u8>(((pixel >> 10) & 0x1F) * 255 / 31);");
    emitter.closeBlock();
    emitter.writeLine("HDC hdc = GetDC(window);");
    emitter.writeLine("BITMAPINFO bmi{};");
    emitter.writeLine("bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);");
    emitter.writeLine("bmi.bmiHeader.biWidth = windowWidth;");
    emitter.writeLine("bmi.bmiHeader.biHeight = -windowHeight;");
    emitter.writeLine("bmi.bmiHeader.biPlanes = 1;");
    emitter.writeLine("bmi.bmiHeader.biBitCount = 24;");
    emitter.writeLine("bmi.bmiHeader.biCompression = BI_RGB;");
    emitter.writeLine("StretchDIBits(hdc, 0, 0, windowWidth, windowHeight, 0, 0, windowWidth, "
                      "windowHeight, rgb.data(), &bmi, DIB_RGB_COLORS, SRCCOPY);");
    emitter.writeLine("ReleaseDC(window, hdc);");
    emitter.writeLine("if ((GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0)");
    emitter.openBlock("");
    emitter.writeLine("running = false;");
    emitter.closeBlock();
    emitter.writeLine("Sleep(16);");
    emitter.closeBlock();
    emitter.writeLine("DestroyWindow(window);");
    emitter.writeLine("UnregisterClassW(className, wc.hInstance);");
    emitter.writeLine("return true;");
    emitter.writeLine("#else");
    emitter.writeLine("(void)system;");
    emitter.writeLine("(void)stopRequested;");
    emitter.writeLine("(void)rgb;");
    emitter.writeLine("return false;");
    emitter.writeLine("#endif");
    emitter.closeBlock();
    emitter.writeBlank();
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
    emitter.writeLine("if (argv != nullptr && argv[0] != nullptr)");
    emitter.openBlock("");
    emitter.writeLine(
        "std::filesystem::path exeDir = std::filesystem::path(argv[0]).parent_path();");
    emitter.writeLine("std::filesystem::path resourcesDir = exeDir / \"resources\";");
    emitter.writeLine("if (!std::filesystem::exists(resourcesDir))");
    emitter.openBlock("");
    emitter.writeLine("std::cerr << \"[psxrecomp][warn] resources directory not found near "
                      "executable: \" << resourcesDir.string() << \"\\n\";");
    emitter.writeLine("std::cerr << \"[psxrecomp][warn] Missing assets can result in a black "
                      "screen or silent startup.\\n\";");
    emitter.closeBlock();
    emitter.closeBlock();
    emitter.writeBlank();
    emitter.writeLine("psxrecomp::runtime::PsxSystem system;");
    emitter.writeLine("std::atomic<bool> stopPresenter{false};");
    emitter.writeLine("std::thread presenterThread;");
    emitter.writeLine("try");
    emitter.openBlock("");
    emitter.writeLine("if (!system.initialize())");
    emitter.openBlock("");
    emitter.writeLine(
        "std::cerr << \"[psxrecomp][error] Failed to initialize runtime system.\" << \"\\n\";");
    emitter.writeLine("return 1;");
    emitter.closeBlock();
    emitter.writeLine("psxrecomp::recompiler::RecompiledModule::configure(system);");
    emitter.writeLine("psxrecomp::recompiler::RecompiledModule::initMemory(system);");
    emitter.writeLine("system.setAutoFrameProgressOnInterruptPoll(true);");
    emitter.writeLine("const char* presentEnv = std::getenv(\"PSXRECOMP_PRESENT_FRAMEBUFFER\");");
    emitter.writeLine("#if PSXRECOMP_HAS_SDL2 || defined(_WIN32)");
    emitter.writeLine("const bool defaultPresent = true;");
    emitter.writeLine("#else");
    emitter.writeLine("const bool defaultPresent = false;");
    emitter.writeLine("#endif");
    emitter.writeLine("const bool enabledPresent = presentEnv == nullptr ? defaultPresent :");
    emitter.writeLine("    (presentEnv[0] == '\\0' || presentEnv[0] == '1');");
    emitter.writeLine("if (enabledPresent)");
    emitter.openBlock("");
    emitter.writeLine("presenterThread = std::thread([&]()");
    emitter.openBlock("");
    emitter.writeLine("if (!presentFramebufferLive(system, stopPresenter))");
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
    emitter.writeLine("constexpr size_t width = psxrecomp::runtime::SoftwareGpuRenderer::Width;");
    emitter.writeLine("constexpr size_t height = psxrecomp::runtime::SoftwareGpuRenderer::Height;");
    emitter.writeLine("const auto& framePixels = system.gpu().frameBuffer();");
    emitter.writeLine(
        "std::vector<psxrecomp::u16> displayPixels(framePixels.begin(), framePixels.end());");
    emitter.writeLine("if (displayPixels.size() < width * height)");
    emitter.openBlock("");
    emitter.writeLine("displayPixels.resize(width * height, 0);");
    emitter.closeBlock();
    emitter.writeLine("size_t nonZeroDisplay = 0;");
    emitter.writeLine("for (size_t i = 0; i < width * height; ++i)");
    emitter.openBlock("");
    emitter.writeLine("if (displayPixels[i] != 0)");
    emitter.openBlock("");
    emitter.writeLine("++nonZeroDisplay;");
    emitter.closeBlock();
    emitter.closeBlock();
    emitter.writeLine("bool usedVramFallback = false;");
    emitter.writeLine("if (nonZeroDisplay == 0)");
    emitter.openBlock("");
    emitter.writeLine("const auto& vramWords = system.gpu().vramWords();");
    emitter.writeLine("if (!vramWords.empty())");
    emitter.openBlock("");
    emitter.writeLine("nonZeroDisplay = 0;");
    emitter.writeLine("for (size_t i = 0; i < width * height; ++i)");
    emitter.openBlock("");
    emitter.writeLine("const psxrecomp::u32 word = vramWords[i / 2];");
    emitter.writeLine("const psxrecomp::u16 pixel = static_cast<psxrecomp::u16>((i % 2 == 0) ? "
                      "(word & 0xFFFF) : ((word >> 16) & 0xFFFF));");
    emitter.writeLine("displayPixels[i] = pixel;");
    emitter.writeLine("if (pixel != 0)");
    emitter.openBlock("");
    emitter.writeLine("++nonZeroDisplay;");
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
        " << (usedVramFallback ? \" (using VRAM fallback)\" : \"\") << \"\\n\";");
    emitter.writeLine("std::cout << \"[psxrecomp] Debug overlay: \" << "
                      "system.debugOverlay().renderText() << \"\\n\";");
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
    emitter.writeLine("if (dumpFramebufferToPpm(dumpPath, displayPixels))");
    emitter.openBlock("");
    emitter.writeLine(
        "std::cout << \"[psxrecomp] Framebuffer dumped to: \" << dumpPath.string() << \"\\n\";");
    emitter.closeBlock();
    emitter.writeLine("else");
    emitter.openBlock("");
    emitter.writeLine("std::cerr << \"[psxrecomp][warn] Failed to dump framebuffer to: \" << "
                      "dumpPath.string() << \"\\n\";");
    emitter.closeBlock();
    emitter.closeBlock();
    emitter.writeLine("std::cout << \"[psxrecomp] Module execution returned.\" << \"\\n\";");
    emitter.writeLine(
        "std::cout << \"[psxrecomp] If nothing appears, the game may still be blocked by "
        "unimplemented hardware paths (GPU/SPU/CDROM/timing).\" << \"\\n\";");
    emitter.writeLine(
        "std::cout << \"[psxrecomp] Tip: set PSXRECOMP_PRESENT_FRAMEBUFFER=1 for an SDL window, "
        "or PSXRECOMP_DUMP_FRAMEBUFFER=/path/frame.ppm for a dump.\" << \"\\n\";");
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
    emitter.writeLine("constexpr size_t w = psxrecomp::runtime::SoftwareGpuRenderer::Width;");
    emitter.writeLine("constexpr size_t h = psxrecomp::runtime::SoftwareGpuRenderer::Height;");
    emitter.writeLine("std::vector<psxrecomp::u16> exPixels = system.gpu().frameBufferSnapshot();");
    emitter.writeLine("if (exPixels.size() < w * h) exPixels.resize(w * h, 0);");
    emitter.writeLine("dumpFramebufferToPpm(exDump[0] != '\\0' ? exDump : "
                      "\"framebuffer.ppm\", exPixels);");
    emitter.writeLine("std::cerr << \"[psxrecomp] Framebuffer dumped (exception path).\\n\";");
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
    return emitter.str();
}

} // namespace recompiler
} // namespace psxrecomp
