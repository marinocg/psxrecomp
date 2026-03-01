#include "psxrecomp/recompiler/codegen.h"

#include "codegen_runner_sections.h"

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
    emitter.writeLine("#include <optional>");
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
    emitRunnerMainFunction(emitter, moduleName);
    return emitter.str();
}

} // namespace recompiler
} // namespace psxrecomp
