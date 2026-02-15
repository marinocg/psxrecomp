#include "psxrecomp/recompiler/codegen.h"

#include "codegen_helpers.h"
#include "cpp_emitter.h"

#include <sstream>
#include <unordered_set>

namespace psxrecomp
{
namespace recompiler
{
namespace
{
std::string escapeStringLiteral(const std::string& value)
{
    std::string escaped;
    escaped.reserve(value.size());
    for (char ch : value)
    {
        switch (ch)
        {
        case '\\':
            escaped += "\\\\";
            break;
        case '\"':
            escaped += "\\\"";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        case '\t':
            escaped += "\\t";
            break;
        default:
            escaped += ch;
            break;
        }
    }
    return escaped;
}
} // namespace

CodeGenerator::CodeGenerator(const CodeGenOptions& options) : m_options(options) {}
std::string CodeGenerator::generateHeader(const ir::Program& program, const std::string& moduleName)
{
    (void)moduleName;
    CppEmitter emitter;
    emitter.writeLine("#pragma once");
    emitter.writeBlank();
    emitter.writeLine("#include \"psxrecomp/runtime/psx_system.h\"");
    emitter.writeLine("#include \"psxrecomp/types.h\"");
    emitter.writeBlank();
    emitter.openBlock("namespace psxrecomp");
    emitter.openBlock("namespace recompiler");

    emitter.writeLine("struct RecompilerContext;");
    emitter.openBlock("struct RecompiledModule");
    emitter.writeLine("static void configure(runtime::PsxSystem& system);");
    emitter.writeLine("static void run(runtime::PsxSystem& system);");
    emitter.closeBlock(";");
    emitter.writeBlank();
    emitter.writeLines(generateFunctionDeclarations(program));
    emitter.closeBlock();
    emitter.closeBlock();
    return emitter.str();
}
std::string CodeGenerator::generateSource(const ir::Program& program, const std::string& moduleName,
                                          const ModuleMetadata& metadata)
{
    CppEmitter emitter;
    std::vector<std::pair<Address, std::string>> functionSymbols;
    functionSymbols.reserve(program.functions.size());
    std::unordered_set<std::string> usedFunctionNames;
    for (const auto& function : program.functions)
    {
        functionSymbols.emplace_back(function.entryAddress,
                                     uniquifyIdentifier(function.name, usedFunctionNames));
    }
    emitter.writeLine("#include \"" + moduleName + ".h\"");
    emitter.writeBlank();
    emitter.writeLine("#include <array>");
    emitter.writeLine("#include <cstdlib>");
    emitter.writeLine("#include <cstdint>");
    emitter.writeLine("#include <cstring>");
    emitter.writeLine("#include <iostream>");
    emitter.writeLine("#include <sstream>");
    emitter.writeLine("#include <stdexcept>");
    emitter.writeLine("#include <string>");
    emitter.writeLine("#include <vector>");
    emitter.writeBlank();
    emitter.writeLine("#ifndef PSXRECOMP_ENABLE_LOGGING");
    emitter.writeLine("#define PSXRECOMP_ENABLE_LOGGING 0");
    emitter.writeLine("#endif");
    emitter.writeLine("#ifndef PSXRECOMP_LOG_LEVEL");
    emitter.writeLine("#define PSXRECOMP_LOG_LEVEL 1");
    emitter.writeLine("#endif");
    emitter.writeLine("#ifndef PSXRECOMP_ENABLE_CHECKS");
    emitter.writeLine("#define PSXRECOMP_ENABLE_CHECKS 1");
    emitter.writeLine("#endif");
    emitter.writeBlank();
    emitter.openBlock("namespace psxrecomp");
    emitter.openBlock("namespace recompiler");
    emitter.openBlock("struct RecompilerContext");
    emitter.writeLine("runtime::PsxSystem& system;");
    emitter.writeLine("std::array<u32, Registers::NUM_REGISTERS> regs{};");
    emitter.writeLine("u32 hi = 0;");
    emitter.writeLine("u32 lo = 0;");
    emitter.closeBlock(";");
    emitter.writeBlank();
    emitter.openBlock("namespace");
    emitter.writeLine("inline u32 readMemory32(runtime::PsxSystem& system, Address address)");
    emitter.openBlock("");
    emitter.writeLine("return system.read<u32>(address);");
    emitter.closeBlock();
    emitter.writeBlank();
    emitter.writeLine(
        "inline void writeMemory32(runtime::PsxSystem& system, Address address, u32 value)");
    emitter.openBlock("");
    emitter.writeLine("system.write<u32>(address, value);");
    emitter.closeBlock();
    emitter.writeBlank();
    emitter.writeLine("inline u32 readMmio32(runtime::PsxSystem& system, Address address)");
    emitter.openBlock("");
    emitter.writeLine("return system.readMmioExplicit<u32>(address);");
    emitter.closeBlock();
    emitter.writeBlank();
    emitter.writeLine(
        "inline void writeMmio32(runtime::PsxSystem& system, Address address, u32 value)");
    emitter.openBlock("");
    emitter.writeLine("system.writeMmioExplicit<u32>(address, value);");
    emitter.closeBlock();
    emitter.writeBlank();
    emitter.writeLine(
        "inline void callSyscall(runtime::PsxSystem& system, u32 code, std::array<u32, "
        "Registers::NUM_REGISTERS>& regs)");
    emitter.openBlock("");
    emitter.writeLine("system.callBiosSyscall(code, regs.data(), regs.size());");
    emitter.closeBlock();
    emitter.writeBlank();
    emitter.writeLine("inline bool callIntrinsic(runtime::PsxSystem& system, Address address)");
    emitter.openBlock("");
    emitter.writeLine("Address physical = address & 0x1FFFFFFF;");
    emitter.writeLine("if (physical >= 0x1F801810 && physical <= 0x1F801817)");
    emitter.openBlock("");
    emitter.writeLine("system.callGpuIntrinsic(physical);");
    emitter.writeLine("return true;");
    emitter.closeBlock();
    emitter.writeLine("if (physical >= 0x1F801800 && physical <= 0x1F801803)");
    emitter.openBlock("");
    emitter.writeLine("system.callCdromIntrinsic(physical);");
    emitter.writeLine("return true;");
    emitter.closeBlock();
    emitter.writeLine("if (physical >= 0x1F801C00 && physical <= 0x1F801DFF)");
    emitter.openBlock("");
    emitter.writeLine("system.callSpuIntrinsic(physical);");
    emitter.writeLine("return true;");
    emitter.closeBlock();
    emitter.writeLine("return false;");
    emitter.closeBlock();
    emitter.writeBlank();
    emitter.writeLine("[[noreturn]] inline void failUnsupportedCall(Address target, Address pc)");
    emitter.openBlock("");
    emitter.writeLine("std::ostringstream stream;");
    emitter.writeLine(
        "stream << \"Unsupported CALL target 0x\" << std::hex << target << \" at PC 0x\" << pc;");
    emitter.writeLine("throw std::runtime_error(stream.str());");
    emitter.closeBlock();
    emitter.writeBlank();
    emitter.writeLine("[[noreturn]] inline void failUnsupportedJump(Address target, Address pc)");
    emitter.openBlock("");
    emitter.writeLine("std::ostringstream stream;");
    emitter.writeLine("stream << \"Unsupported JR/JUMP target 0x\" << std::hex << target << \" at "
                      "PC 0x\" << pc;");
    emitter.writeLine("throw std::runtime_error(stream.str());");
    emitter.closeBlock();
    emitter.writeBlank();
    emitter.writeLine("[[noreturn]] inline void triggerTrap(u32 code, Address pc)");
    emitter.openBlock("");
    emitter.writeLine("std::ostringstream stream;");
    emitter.writeLine(
        "stream << \"BREAK/TRAP reached (code=0x\" << std::hex << code << \") at PC 0x\" << pc;");
    emitter.writeLine("throw std::runtime_error(stream.str());");
    emitter.closeBlock();
    emitter.writeBlank();
    emitter.writeLine("inline void logWarning(const char* message)");
    emitter.openBlock("");
    emitter.writeLine("if (PSXRECOMP_ENABLE_LOGGING)");
    emitter.openBlock("");
    emitter.writeLine("std::cerr << message << \"\\n\";");
    emitter.closeBlock();
    emitter.closeBlock();
    emitter.closeBlock();
    emitter.writeBlank();

    emitter.writeLines(generateGlobals(program));
    emitter.writeBlank();
    emitter.writeLine("static const std::vector<const char*> kPipelineWarnings = {");
    if (metadata.warnings.empty())
    {
        emitter.writeLine("};");
    }
    else
    {
        for (size_t index = 0; index < metadata.warnings.size(); ++index)
        {
            std::string line = "    \"" + escapeStringLiteral(metadata.warnings[index]) + "\"";
            if (index + 1 < metadata.warnings.size())
            {
                line += ",";
            }
            emitter.writeLine(line);
        }
        emitter.writeLine("};");
    }
    emitter.writeBlank();

    emitter.writeLine(
        "inline bool callRecompiledFunction(RecompilerContext& context, Address address)");
    emitter.openBlock("");
    emitter.writeLine("Address physical = address & 0x1FFFFFFF;");
    emitter.writeLine("switch (physical)");
    emitter.openBlock("");
    {
        std::unordered_set<Address> emittedEntries;
        for (const auto& entry : functionSymbols)
        {
            const Address normalizedEntry = entry.first & 0x1FFFFFFFu;
            if (!emittedEntries.insert(normalizedEntry).second)
            {
                continue;
            }
            std::ostringstream caseLine;
            caseLine << "case 0x" << std::hex << normalizedEntry << ":";
            emitter.writeLine(caseLine.str());
            emitter.openBlock("");
            emitter.writeLine(entry.second + "(context);");
            emitter.writeLine("return true;");
            emitter.closeBlock();
        }
    }
    emitter.writeLine("default:");
    emitter.openBlock("");
    emitter.writeLine("return false;");
    emitter.closeBlock();
    emitter.closeBlock();
    emitter.closeBlock();
    emitter.writeBlank();

    emitter.writeLine("void RecompiledModule::configure(runtime::PsxSystem& system)");
    emitter.openBlock("");
    emitter.writeLine("runtime::PsxSystem::DiscSwapInfo info;");
    emitter.writeLine("info.setName = \"" + escapeStringLiteral(metadata.discSetName) + "\";");
    emitter.writeLine("info.activeDiscIndex = " + std::to_string(metadata.activeDiscIndex) + ";");
    if (metadata.discs.empty())
    {
        emitter.writeLine("info.discs = {};");
    }
    else
    {
        emitter.writeLine("info.discs = {");
        for (size_t index = 0; index < metadata.discs.size(); ++index)
        {
            const auto& disc = metadata.discs[index];
            std::ostringstream line;
            line << "    {" << disc.index << ", \"" << escapeStringLiteral(disc.label) << "\", \""
                 << escapeStringLiteral(disc.path) << "\"}";
            if (index + 1 < metadata.discs.size())
            {
                line << ",";
            }
            emitter.writeLine(line.str());
        }
        emitter.writeLine("};");
    }
    emitter.writeLine("system.setDiscSwapInfo(info);");
    emitter.writeLine("if (PSXRECOMP_ENABLE_LOGGING)");
    emitter.openBlock("");
    emitter.writeLine("for (const auto* warning : kPipelineWarnings)");
    emitter.openBlock("");
    emitter.writeLine("logWarning(warning);");
    emitter.closeBlock();
    emitter.closeBlock();
    emitter.closeBlock();
    emitter.writeBlank();

    emitter.writeLine("void RecompiledModule::run(runtime::PsxSystem& system)");
    emitter.openBlock("");
    emitter.writeLine("RecompilerContext context{system, {}};");
    if (!program.functions.empty())
    {
        emitter.writeLine(functionSymbols.front().second + "(context);");
    }
    emitter.closeBlock();
    emitter.writeBlank();

    emitter.writeLines(generateFunctionDefinitions(program));
    emitter.closeBlock();
    emitter.closeBlock();
    return emitter.str();
}
std::string CodeGenerator::generateBuildFile(const std::string& projectName)
{
    std::ostringstream stream;
    stream << "cmake_minimum_required(VERSION 3.15)\n";
    stream << "set(CMAKE_CXX_STANDARD 17)\n";
    stream << "set(CMAKE_CXX_STANDARD_REQUIRED ON)\n";
    stream << "set(CMAKE_CXX_EXTENSIONS OFF)\n\n";
    stream << "set(PSXRECOMP_RUNTIME_INCLUDE_DIR \"${CMAKE_CURRENT_LIST_DIR}/runtime/include\" "
              "CACHE PATH \"Path to runtime/include\")\n";
    stream << "set(PSXRECOMP_RUNTIME_SOURCE_DIR \"${CMAKE_CURRENT_LIST_DIR}/runtime/src\" CACHE "
              "PATH \"Path to runtime/src\")\n";
    stream << "set(PSXRECOMP_RESOURCE_DIR \"${CMAKE_CURRENT_LIST_DIR}/resources\" CACHE PATH "
              "\"Path to resource directory\")\n";
    stream << "option(PSXRECOMP_ENABLE_LOGGING \"Enable recompiled logging\" OFF)\n";
    stream << "set(PSXRECOMP_LOG_LEVEL 1 CACHE STRING \"Logging level\")\n";
    stream << "option(PSXRECOMP_ENABLE_CHECKS \"Enable runtime checks\" ON)\n";
    stream << "set(PSXRECOMP_OPT_LEVEL 2 CACHE STRING \"Optimization level hint\")\n";
    stream << "if(NOT EXISTS ${PSXRECOMP_RUNTIME_INCLUDE_DIR}/psxrecomp/types.h)\n";
    stream << "    message(FATAL_ERROR \"PSX runtime include directory is invalid: "
              "${PSXRECOMP_RUNTIME_INCLUDE_DIR}\")\n";
    stream << "endif()\n\n";
    stream << "file(GLOB PSXRECOMP_RUNTIME_SOURCES CONFIGURE_DEPENDS "
              "${PSXRECOMP_RUNTIME_SOURCE_DIR}/*.cpp)\n";
    stream << "if(NOT PSXRECOMP_RUNTIME_SOURCES)\n";
    stream << "    message(FATAL_ERROR \"No runtime sources found in "
              "${PSXRECOMP_RUNTIME_SOURCE_DIR}\")\n";
    stream << "endif()\n\n";
    stream << "add_library(psxrecomp_runtime STATIC ${PSXRECOMP_RUNTIME_SOURCES})\n";
    stream << "target_include_directories(psxrecomp_runtime PUBLIC "
              "${PSXRECOMP_RUNTIME_INCLUDE_DIR})\n\n";
    stream << "project(" << projectName << " LANGUAGES CXX)\n";
    stream << "if(WIN32 AND NOT MSVC)\n";
    stream << "    add_link_options(-static -static-libgcc -static-libstdc++)\n";
    stream << "endif()\n\n";
    stream << "add_library(" << projectName << " " << projectName << ".cpp)\n";
    stream << "target_compile_definitions(" << projectName << " PUBLIC\n";
    stream << "    PSXRECOMP_ENABLE_LOGGING=$<BOOL:${PSXRECOMP_ENABLE_LOGGING}>\n";
    stream << "    PSXRECOMP_LOG_LEVEL=${PSXRECOMP_LOG_LEVEL}\n";
    stream << "    PSXRECOMP_ENABLE_CHECKS=$<BOOL:${PSXRECOMP_ENABLE_CHECKS}>\n";
    stream << "    PSXRECOMP_OPT_LEVEL=${PSXRECOMP_OPT_LEVEL}\n";
    stream << ")\n";
    stream << "target_include_directories(" << projectName << " PUBLIC\n";
    stream << "    ${PSXRECOMP_RUNTIME_INCLUDE_DIR}\n";
    stream << "    ${CMAKE_CURRENT_LIST_DIR}\n";
    stream << ")\n";
    stream << "target_link_libraries(" << projectName << " PUBLIC psxrecomp_runtime)\n\n";
    stream << "add_executable(" << projectName << "_runner " << projectName << "_runner.cpp)\n";
    stream << "target_link_libraries(" << projectName << "_runner PRIVATE " << projectName << ")\n";
    stream << "set_target_properties(" << projectName << "_runner PROPERTIES OUTPUT_NAME \""
           << projectName << "\")\n\n";
    stream << "option(PSXRECOMP_ENABLE_SDL_PRESENTER \"Enable SDL2 framebuffer presenter\" ON)\n";
    stream << "set(PSXRECOMP_HAS_SDL2 0)\n";
    stream << "if(PSXRECOMP_ENABLE_SDL_PRESENTER)\n";
    stream << "    find_package(SDL2 QUIET)\n";
    stream << "    if(SDL2_FOUND)\n";
    stream << "        set(PSXRECOMP_HAS_SDL2 1)\n";
    stream << "        target_link_libraries(" << projectName << "_runner PRIVATE SDL2::SDL2)\n";
    stream << "    else()\n";
    stream << "        message(STATUS \"SDL2 not found; runner will use headless mode only.\")\n";
    stream << "    endif()\n";
    stream << "endif()\n";
    stream << "target_compile_definitions(" << projectName
           << "_runner PRIVATE PSXRECOMP_HAS_SDL2=${PSXRECOMP_HAS_SDL2})\n";
    stream << "if(WIN32)\n";
    stream << "    target_link_libraries(" << projectName << "_runner PRIVATE user32 gdi32)\n";
    stream << "endif()\n\n";
    stream << "if(EXISTS ${PSXRECOMP_RESOURCE_DIR})\n";
    stream << "    add_custom_command(TARGET " << projectName << "_runner POST_BUILD\n";
    stream << "        COMMAND ${CMAKE_COMMAND} -E copy_directory\n";
    stream << "            ${PSXRECOMP_RESOURCE_DIR}\n";
    stream << "            $<TARGET_FILE_DIR:" << projectName << "_runner>/resources)\n";
    stream << "endif()\n";
    return stream.str();
}

std::string CodeGenerator::generateRunnerSource(const std::string& moduleName)
{
    CppEmitter emitter;
    emitter.writeLine("#include \"" + moduleName + ".h\"");
    emitter.writeBlank();
    emitter.writeLine("#include \"psxrecomp/runtime/gpu_renderer.h\"");
    emitter.writeLine("#include \"psxrecomp/types.h\"");
    emitter.writeLine("#include <cstdlib>");
    emitter.writeLine("#include <exception>");
    emitter.writeLine("#include <filesystem>");
    emitter.writeLine("#include <fstream>");
    emitter.writeLine("#include <iostream>");
    emitter.writeLine("#include <string>");
    emitter.writeLine("#include <vector>");
    emitter.writeLine("#if PSXRECOMP_HAS_SDL2");
    emitter.writeLine("#include <SDL2/SDL.h>");
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
    emitter.writeLine(
        "bool presentFramebufferWithSdl(const std::vector<psxrecomp::u16>& framebuffer)");
    emitter.openBlock("");
    emitter.writeLine("constexpr size_t width = psxrecomp::runtime::SoftwareGpuRenderer::Width;");
    emitter.writeLine("constexpr size_t height = psxrecomp::runtime::SoftwareGpuRenderer::Height;");
    emitter.writeLine("if (framebuffer.size() < width * height)");
    emitter.openBlock("");
    emitter.writeLine("return false;");
    emitter.closeBlock();
    emitter.writeLine("std::vector<psxrecomp::u8> rgb(width * height * 3, 0);");
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
    emitter.writeLine(
        "SDL_UpdateTexture(texture, nullptr, rgb.data(), static_cast<int>(width * 3));");
    emitter.writeLine("bool running = true;");
    emitter.writeLine("while (running)");
    emitter.openBlock("");
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
    emitter.writeLine("while (running)");
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
    emitter.writeLine("(void)rgb;");
    emitter.writeLine("return false;");
    emitter.writeLine("#endif");
    emitter.closeBlock();
    emitter.writeBlank();
    emitter.writeLine("int main(int argc, char** argv)");
    emitter.openBlock("");
    emitter.writeLine("(void)argc;");
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
    emitter.writeLine("try");
    emitter.openBlock("");
    emitter.writeLine("psxrecomp::runtime::PsxSystem system;");
    emitter.writeLine("if (!system.initialize())");
    emitter.openBlock("");
    emitter.writeLine(
        "std::cerr << \"[psxrecomp][error] Failed to initialize runtime system.\" << \"\\n\";");
    emitter.writeLine("return 1;");
    emitter.closeBlock();
    emitter.writeLine("psxrecomp::recompiler::RecompiledModule::configure(system);");
    emitter.writeLine("psxrecomp::recompiler::RecompiledModule::run(system);");
    emitter.writeLine("const char* presentEnv = std::getenv(\"PSXRECOMP_PRESENT_FRAMEBUFFER\");");
    emitter.writeLine("#if PSXRECOMP_HAS_SDL2 || defined(_WIN32)");
    emitter.writeLine("const bool defaultPresent = true;");
    emitter.writeLine("#else");
    emitter.writeLine("const bool defaultPresent = false;");
    emitter.writeLine("#endif");
    emitter.writeLine("const bool enabledPresent = presentEnv == nullptr ? defaultPresent : ");
    emitter.writeLine("    (presentEnv[0] == '\\0' || presentEnv[0] == '1');");
    emitter.writeLine("if (enabledPresent)");
    emitter.openBlock("");
    emitter.writeLine("if (presentFramebufferWithSdl(system.gpu().frameBuffer()))");
    emitter.openBlock("");
    emitter.writeLine("std::cout << \"[psxrecomp] SDL presenter window closed.\\n\";");
    emitter.closeBlock();
    emitter.writeLine("else");
    emitter.openBlock("");
    emitter.writeLine(
        "std::cerr << \"[psxrecomp][warn] SDL presenter unavailable; install SDL2 or disable "
        "PSXRECOMP_PRESENT_FRAMEBUFFER.\\n\";");
    emitter.closeBlock();
    emitter.closeBlock();
    emitter.writeLine("if (const char* dumpPathEnv = std::getenv(\"PSXRECOMP_DUMP_FRAMEBUFFER\"))");
    emitter.openBlock("");
    emitter.writeLine("std::filesystem::path dumpPath = dumpPathEnv[0] != '\\0' ? dumpPathEnv : "
                      "\"framebuffer.ppm\";");
    emitter.writeLine("if (dumpFramebufferToPpm(dumpPath, system.gpu().frameBuffer()))");
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
    emitter.writeLine(
        "std::cerr << \"[psxrecomp][error] Unhandled exception: \" << ex.what() << \"\\n\";");
    emitter.writeLine("return 1;");
    emitter.closeBlock();
    emitter.writeLine("catch (...)");
    emitter.openBlock("");
    emitter.writeLine(
        "std::cerr << \"[psxrecomp][error] Unknown failure during module execution.\" << \"\\n\";");
    emitter.writeLine("return 1;");
    emitter.closeBlock();
    emitter.closeBlock();
    return emitter.str();
}
std::string CodeGenerator::generateGlobals(const ir::Program& program) const
{
    CppEmitter emitter;
    if (program.globals.empty())
    {
        emitter.writeLine("// No global data.");
        return emitter.str();
    }
    std::unordered_set<std::string> usedNames;
    for (const auto& global : program.globals)
    {
        std::string name = uniquifyIdentifier(global.name, usedNames);
        emitter.writeLine("static const std::array<u8, " + std::to_string(global.bytes.size()) +
                          "> " + name + " = {");
        if (!global.bytes.empty())
        {
            std::ostringstream stream;
            stream << "    ";
            for (size_t index = 0; index < global.bytes.size(); ++index)
            {
                stream << static_cast<int>(global.bytes[index]);
                if (index + 1 < global.bytes.size())
                {
                    stream << ", ";
                }
            }
            emitter.writeLine(stream.str());
        }
        emitter.writeLine("};");
    }
    return emitter.str();
}
std::string CodeGenerator::generateFunctionDeclarations(const ir::Program& program) const
{
    std::ostringstream stream;
    std::unordered_set<std::string> usedNames;
    for (const auto& function : program.functions)
    {
        std::string name = uniquifyIdentifier(function.name, usedNames);
        stream << "void " << name << "(RecompilerContext& context);\n";
    }
    return stream.str();
}
} // namespace recompiler
} // namespace psxrecomp
