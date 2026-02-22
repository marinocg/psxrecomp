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
    emitter.writeLine("#include <algorithm>");
    emitter.writeLine("#include <atomic>");
    emitter.writeLine("#include <chrono>");
    emitter.writeLine("#include <cctype>");
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
    emitter.writeLine("bool presentFramebufferLive(psxrecomp::runtime::PsxSystem& system,");
    emitter.writeLine("                          const std::atomic<bool>& stopRequested,");
    emitter.writeLine("                          bool renderDebugOverlay)");
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
    emitter.writeLine("if (renderDebugOverlay)");
    emitter.openBlock("");
    emitter.writeLine("system.debugOverlay().drawOnFrameBuffer(framebuffer, width, height);");
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
    emitter.writeLine("if (renderDebugOverlay)");
    emitter.openBlock("");
    emitter.writeLine("system.debugOverlay().drawOnFrameBuffer(framebuffer, width, height);");
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
    emitter.writeLine("(void)renderDebugOverlay;");
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
    return emitter.str();
}

} // namespace recompiler
} // namespace psxrecomp
