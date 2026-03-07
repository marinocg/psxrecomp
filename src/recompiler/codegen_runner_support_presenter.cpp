#include "codegen_runner_sections.h"

namespace psxrecomp
{
namespace recompiler
{

void emitRunnerSupportPresenter(CppEmitter& emitter)
{
    emitter.writeLine("bool presentFramebufferLive(psxrecomp::runtime::PsxSystem& system,");
    emitter.writeLine("                          const std::atomic<bool>& stopRequested,");
    emitter.writeLine("                          bool renderDebugOverlay)");
    emitter.openBlock("");
    emitter.writeLine("std::vector<psxrecomp::u8> rgb;");
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
    emitter.writeLine("SDL_Texture* texture = nullptr;");
    emitter.writeLine("size_t textureWidth = 0;");
    emitter.writeLine("size_t textureHeight = 0;");
    emitter.writeLine("bool running = true;");
    emitter.writeLine("while (running && !stopRequested.load(std::memory_order_relaxed))");
    emitter.openBlock("");
    emitter.writeLine("size_t displayWidth = 0;");
    emitter.writeLine("size_t displayHeight = 0;");
    emitter.writeLine("auto framebuffer = captureBestDisplayPixels(system.gpu(),");
    emitter.writeLine("                                         &displayWidth,");
    emitter.writeLine("                                         &displayHeight);");
    emitter.writeLine("if (displayWidth == 0 || displayHeight == 0)");
    emitter.openBlock("");
    emitter.writeLine("displayWidth = 1;");
    emitter.writeLine("displayHeight = 1;");
    emitter.writeLine("framebuffer.assign(1, 0);");
    emitter.closeBlock();
    emitter.writeLine("if (renderDebugOverlay)");
    emitter.openBlock("");
    emitter.writeLine(
        "system.debugOverlay().drawOnFrameBuffer(framebuffer, displayWidth, displayHeight);");
    emitter.closeBlock();
    emitter.writeLine("if (texture == nullptr || textureWidth != displayWidth || textureHeight != "
                      "displayHeight)");
    emitter.openBlock("");
    emitter.writeLine("if (texture != nullptr)");
    emitter.openBlock("");
    emitter.writeLine("SDL_DestroyTexture(texture);");
    emitter.closeBlock();
    emitter.writeLine("texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB24,");
    emitter.writeLine(
        "                            SDL_TEXTUREACCESS_STREAMING, static_cast<int>(displayWidth),");
    emitter.writeLine("                            static_cast<int>(displayHeight));");
    emitter.writeLine("if (texture == nullptr)");
    emitter.openBlock("");
    emitter.writeLine("std::cerr << \"[psxrecomp][warn] SDL texture creation failed: \" << ");
    emitter.writeLine("             SDL_GetError() << \"\\n\";");
    emitter.writeLine("break;");
    emitter.closeBlock();
    emitter.writeLine("textureWidth = displayWidth;");
    emitter.writeLine("textureHeight = displayHeight;");
    emitter.writeLine("rgb.assign(displayWidth * displayHeight * 3, 0);");
    emitter.closeBlock();
    emitter.writeLine("else if (rgb.size() != displayWidth * displayHeight * 3)");
    emitter.openBlock("");
    emitter.writeLine("rgb.assign(displayWidth * displayHeight * 3, 0);");
    emitter.closeBlock();
    emitter.writeLine("for (size_t i = 0; i < displayWidth * displayHeight; ++i)");
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
        "SDL_UpdateTexture(texture, nullptr, rgb.data(), static_cast<int>(displayWidth * 3));");
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
    emitter.writeLine("if (texture != nullptr) SDL_DestroyTexture(texture);");
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
    emitter.writeLine("size_t displayWidth = 0;");
    emitter.writeLine("size_t displayHeight = 0;");
    emitter.writeLine("auto framebuffer = captureBestDisplayPixels(system.gpu(),");
    emitter.writeLine("                                         &displayWidth,");
    emitter.writeLine("                                         &displayHeight);");
    emitter.writeLine("if (displayWidth == 0 || displayHeight == 0)");
    emitter.openBlock("");
    emitter.writeLine("displayWidth = 1;");
    emitter.writeLine("displayHeight = 1;");
    emitter.writeLine("framebuffer.assign(1, 0);");
    emitter.closeBlock();
    emitter.writeLine("if (renderDebugOverlay)");
    emitter.openBlock("");
    emitter.writeLine(
        "system.debugOverlay().drawOnFrameBuffer(framebuffer, displayWidth, displayHeight);");
    emitter.closeBlock();
    emitter.writeLine("if (rgb.size() != displayWidth * displayHeight * 3)");
    emitter.openBlock("");
    emitter.writeLine("rgb.assign(displayWidth * displayHeight * 3, 0);");
    emitter.closeBlock();
    emitter.writeLine("for (size_t i = 0; i < displayWidth * displayHeight; ++i)");
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
    emitter.writeLine("bmi.bmiHeader.biWidth = static_cast<LONG>(displayWidth);");
    emitter.writeLine("bmi.bmiHeader.biHeight = -static_cast<LONG>(displayHeight);");
    emitter.writeLine("bmi.bmiHeader.biPlanes = 1;");
    emitter.writeLine("bmi.bmiHeader.biBitCount = 24;");
    emitter.writeLine("bmi.bmiHeader.biCompression = BI_RGB;");
    emitter.writeLine(
        "StretchDIBits(hdc, 0, 0, windowWidth, windowHeight, 0, 0, static_cast<int>(displayWidth), "
        "static_cast<int>(displayHeight), rgb.data(), &bmi, DIB_RGB_COLORS, SRCCOPY);");
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
}

} // namespace recompiler
} // namespace psxrecomp
