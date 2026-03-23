#include "psxrecomp/ir/ir.h"
#include "psxrecomp/recompiler/codegen.h"
#include "recompiler_codegen_test_sections.h"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

#if !defined(PSXRECOMP_SOURCE_DIR)
#define PSXRECOMP_SOURCE_DIR ""
#endif

#if !defined(PSXRECOMP_TEST_CXX)
#define PSXRECOMP_TEST_CXX ""
#endif

int main()
{
    using psxrecomp::Address;
    using psxrecomp::Register;
    using psxrecomp::ir::Builder;
    using psxrecomp::ir::Opcode;
    using psxrecomp::ir::Program;
    using psxrecomp::ir::Value;
    using psxrecomp::recompiler::CodeGenerator;

    Program program;
    program.addGlobal("table_data", {0x1, 0x2, 0x3});

    Builder builder(program);
    auto& function = builder.createFunction("main_func", 0x80010000);
    auto& entry = builder.createBlock(function, "entry");
    auto& thenBlock = builder.createBlock(function, "then");
    auto& elseBlock = builder.createBlock(function, "else");

    Value temp0 = builder.createTemporary();
    Value temp1 = builder.createTemporary();
    Value temp2 = builder.createTemporary();
    Value mulHi0 = builder.createTemporary();
    Value mulLo0 = builder.createTemporary();
    Value mulHi1 = builder.createTemporary();
    Value mulLo1 = builder.createTemporary();
    Value divHi0 = builder.createTemporary();
    Value divLo0 = builder.createTemporary();
    Value regA0 = Value::makeRegister(static_cast<Register>(4));

    entry.instructions.push_back(
        builder.makeInstruction(Opcode::MOVE, {Value::makeImmediate(1)}, {temp0}, 0x80010000));
    entry.instructions.push_back(builder.makeInstruction(
        Opcode::ADD, {temp0, Value::makeImmediate(4)}, {temp1}, 0x80010004));
    entry.instructions.push_back(
        builder.makeInstruction(Opcode::MUL, {Value::makeImmediate(7), Value::makeImmediate(3)},
                                {mulHi0, mulLo0}, 0x80010006));
    entry.instructions.push_back(
        builder.makeInstruction(Opcode::MULU, {Value::makeImmediate(5), Value::makeImmediate(2)},
                                {mulHi1, mulLo1}, 0x80010007));
    entry.instructions.push_back(
        builder.makeInstruction(Opcode::DIV, {Value::makeImmediate(21), Value::makeImmediate(4)},
                                {divHi0, divLo0}, 0x80010008));
    entry.instructions.push_back(builder.makeInstruction(Opcode::BRANCH, {temp1}, {}, 0x8001000C));
    entry.successors = {"then", "else"};

    thenBlock.instructions.push_back(
        builder.makeInstruction(Opcode::LOAD, {Value::makeAddress(0x80010012)}, {temp2}));
    thenBlock.instructions.push_back(
        builder.makeInstruction(Opcode::STORE, {Value::makeAddress(0x80010014), temp2}, {}));
    thenBlock.instructions.push_back(builder.makeInstruction(Opcode::RETURN, {}, {}));

    elseBlock.instructions.push_back(builder.makeInstruction(Opcode::MOVE, {regA0}, {temp2}));
    elseBlock.instructions.push_back(builder.makeInstruction(Opcode::RETURN, {}, {}));

    auto& duplicateNameFunction = builder.createFunction("duplicate_block_names", 0x80012000);
    auto& duplicateEntry = builder.createBlock(duplicateNameFunction, "loop");
    auto& duplicateLoop = builder.createBlock(duplicateNameFunction, "loop");
    duplicateEntry.instructions.push_back(
        builder.makeInstruction(Opcode::JUMP, {}, {}, 0x80012000));
    duplicateEntry.successors = {"loop"};
    duplicateLoop.instructions.push_back(
        builder.makeInstruction(Opcode::RETURN, {}, {}, 0x80012004));

    CodeGenerator generator;
    const std::string header = generator.generateHeader(program, "module");
    const std::string source = generator.generateSource(program, "module");
    const std::string buildFile = generator.generateBuildFile("module");
    const std::string runner = generator.generateRunnerSource("module");
    const auto countOccurrences = [](const std::string& haystack, const std::string& needle)
    {
        if (needle.empty())
        {
            return std::string::size_type{0};
        }

        std::string::size_type count = 0;
        std::string::size_type pos = 0;
        while ((pos = haystack.find(needle, pos)) != std::string::npos)
        {
            ++count;
            pos += needle.size();
        }
        return count;
    };

    assert(header.find("RecompiledModule") != std::string::npos);
    assert(header.find("bool main_func(RecompilerContext& context, Address startAddress = 0);") !=
           std::string::npos);
    assert(source.find("readMemory32") != std::string::npos);
    assert(source.find("writeMemory32") != std::string::npos);
    assert(source.find("table_data") != std::string::npos);
    assert(source.find("switch (block)") != std::string::npos);
    assert(source.find("case BlockId::loop_1:") != std::string::npos);
    assert(source.find("static constexpr Address kBlockStarts[]") != std::string::npos);
    assert(source.find("static constexpr BlockId kBlockIds[]") != std::string::npos);
    assert(source.find("static constexpr Address kResumeRangeStarts[]") != std::string::npos);
    assert(source.find("static constexpr Address kResumeRangeEnds[]") != std::string::npos);
    assert(source.find("std::upper_bound(rangeStartsBegin, rangeStartsEnd, physical)") !=
           std::string::npos);
    assert(source.find("(physical & 0x3u) != 0 || physical >= kResumeRangeEnds[rangeIdx]") !=
           std::string::npos);
    assert(source.find("std::upper_bound(startsBegin, startsEnd, physical)") != std::string::npos);
    assert(source.find("case 0x10004: block = BlockId::") == std::string::npos);
    assert(source.find("if (") != std::string::npos);
    assert(source.find("callRecompiledFunction") != std::string::npos);
    assert(source.find("restoreCalleeSaved") != std::string::npos);
    assert(source.find("context.regs[Registers::S0] = preservedS0;") != std::string::npos);
    assert(source.find("failUnsupportedCall") != std::string::npos);
    assert(source.find("struct FnRange") != std::string::npos);
    assert(source.find("bool (*cachedRangeFn)(RecompilerContext&, Address) = nullptr;") !=
           std::string::npos);
    assert(source.find("bool (*fn)(RecompilerContext&, Address);") != std::string::npos);
    assert(source.find("static constexpr FnRange kFnRanges[]") != std::string::npos);
    assert(source.find("const size_t rangeCount = sizeof(kFnRanges) / sizeof(kFnRanges[0]);") !=
           std::string::npos);
    assert(source.find("if (context.cachedRangeFn != nullptr)") != std::string::npos);
    assert(source.find("if (context.cachedRangeFn(context, physical))") != std::string::npos);
    assert(source.find("context.cachedRangeStart = range.start;") != std::string::npos);
    assert(source.find("context.cachedRangeEndExclusive = range.endExclusive;") !=
           std::string::npos);
    assert(source.find("context.cachedRangeFn = range.fn;") != std::string::npos);
    assert(source.find("if (range.fn(context, physical))") != std::string::npos);
    assert(source.find("const auto interruptSavedRegs = context.regs;") != std::string::npos);
    assert(source.find("const u32 interruptCallbackCommitGeneration =") != std::string::npos);
    assert(source.find("context.system.callbackContextCommitGeneration() !=") != std::string::npos);
    assert(source.find("context.regs = interruptSavedRegs;") != std::string::npos);
    assert(source.find("context.hi = interruptSavedHi;") != std::string::npos);
    assert(source.find("context.lo = interruptSavedLo;") != std::string::npos);
    assert(source.find("bool pendingLoadValid = false;") != std::string::npos);
    assert(source.find("Register pendingLoadRegister = Registers::ZERO;") != std::string::npos);
    assert(source.find("bool stagedLoadValid = false;") != std::string::npos);
    assert(
        source.find(
            "inline void stagePendingLoad(RecompilerContext& context, Register reg, u32 value)") !=
        std::string::npos);
    assert(source.find("inline void commitPendingLoad(RecompilerContext& context)") !=
           std::string::npos);
    assert(source.find("inline void finishLoadDelayCycle(RecompilerContext& context, u32 "
                       "completedGprWriteMask)") != std::string::npos);
    assert(source.find("finishLoadDelayCycle(context, 0x") != std::string::npos);
    assert(source.find("main_func(context, 0x10004);") == std::string::npos);
    assert(source.find("main_func(context, 0x10007);") == std::string::npos);
    assert(source.find("main_func(context, 0x10006);") == std::string::npos);
    assert(source.find("main_func(context, 0x10008);") == std::string::npos);
    assert(source.find("main_func(context, 0x1000c);") == std::string::npos);
    assert(source.find("kModuleEntryAddress") != std::string::npos);
    assert(source.find("callRecompiledFunction(context, kModuleEntryAddress)") !=
           std::string::npos);
    assert(countOccurrences(source, "setCallbackInvoker(") == 1);
    assert(source.find("VSync") == std::string::npos);
    assert(source.find("DrawSync") == std::string::npos);
    assert(source.find("PSXRECOMP_AUTO_FRAME_PROGRESS") == std::string::npos);
    assert(source.find("setAutoFrameProgress") == std::string::npos);
    assert(source.find("triggerTrap") != std::string::npos);
    assert(source.find("ExceptionCode::Breakpoint") != std::string::npos);
    assert(source.find("BREAK exception code=0x") != std::string::npos);
    assert(source.find("readMemory16s(RecompilerContext& context, Address address,") !=
           std::string::npos);
    assert(source.find("Address pc, bool inDelaySlot") != std::string::npos);
    assert(buildFile.find("add_library") != std::string::npos);
    assert(buildFile.find("add_executable") != std::string::npos);
    assert(buildFile.find("_runner.cpp") != std::string::npos);
    assert(buildFile.find("if(WIN32 AND NOT MSVC)") != std::string::npos);
    assert(buildFile.find("-static -static-libgcc -static-libstdc++") != std::string::npos);
    assert(buildFile.find("PSXRECOMP_ENABLE_SDL_PRESENTER") != std::string::npos);
    assert(buildFile.find("PSXRECOMP_HAS_SDL2") != std::string::npos);
    assert(buildFile.find("user32 gdi32") != std::string::npos);
    assert(runner.find("PSXRECOMP_DUMP_FRAMEBUFFER") != std::string::npos);
    assert(runner.find("PSXRECOMP_DUMP_FULL_FRAMEBUFFER") != std::string::npos);
    assert(runner.find("PSXRECOMP_PRESENT_FRAMEBUFFER") != std::string::npos);
    assert(runner.find("PSXRECOMP_DISC_IMAGE") != std::string::npos);
    assert(runner.find("PSXRECOMP_AUTO_FRAME_PROGRESS") == std::string::npos);
    assert(runner.find("dumpFramebufferToPpm") != std::string::npos);
    assert(runner.find("extractDisplayPixels") != std::string::npos);
    assert(runner.find("appendSuffixBeforeExtension") != std::string::npos);
    assert(runner.find("countNonZeroPixels") != std::string::npos);
    assert(runner.find("captureBestDisplayPixels") != std::string::npos);
    assert(runner.find("presentFramebufferLive") != std::string::npos);
    assert(runner.find("displayWindow()") != std::string::npos);
    assert(runner.find("SDL_TEXTUREACCESS_STREAMING, static_cast<int>(displayWidth)") !=
           std::string::npos);
    assert(runner.find("auto framebuffer = captureBestDisplayPixels(system.gpu(),") !=
           std::string::npos);
    assert(runner.find("using alternate display page") != std::string::npos);
    assert(runner.find("std::thread presenterThread") != std::string::npos);
    assert(runner.find("#if defined(_WIN32)") != std::string::npos);
    assert(runner.find("Debug overlay") != std::string::npos);
    assert(runner.find("Last PC") != std::string::npos);
    assert(runner.find("envFlagEnabled(presentEnv, defaultPresent)") != std::string::npos);
    assert(runner.find("PSXRECOMP_RENDER_DEBUG_OVERLAY") != std::string::npos);
    assert(runner.find("resourcesDir / \"disc\" / \"data_track.bin\"") != std::string::npos);
    assert(runner.find("resourcesDir / \"disc\" / \"disc_layout.json\"") != std::string::npos);
    assert(runner.find("extractJsonU64Field(*layoutText, \"sectorSize\", &sectorSize)") !=
           std::string::npos);
    assert(runner.find("system.discSwapInfo()") == std::string::npos);
    assert(runner.find("system.setDisc(disc);") != std::string::npos);
    assert(runner.find("disc mounted") != std::string::npos);
    assert(runner.find("Runtime disc blob not found") != std::string::npos);
    assert(runner.find("decodeLogLevel") != std::string::npos);
    assert(runner.find("const auto& exVramWords = system.gpu().vramWords();") != std::string::npos);
    assert(runner.find("exPixels[y * exWidth + x] = pixel;") != std::string::npos);

    Program trapProgram;
    Builder trapBuilder(trapProgram);
    auto& trapFunction = trapBuilder.createFunction("trap_func", 0x80013000);
    auto& trapBlock = trapBuilder.createBlock(trapFunction, "entry");
    Value trapAddOut = trapBuilder.createTemporary();
    Value trapSubOut = trapBuilder.createTemporary();
    trapBlock.instructions.push_back(trapBuilder.makeInstruction(
        Opcode::ADD_TRAP, {Value::makeRegister(8), Value::makeRegister(9)}, {trapAddOut},
        0x80013000));
    trapBlock.instructions.push_back(trapBuilder.makeInstruction(
        Opcode::SUB_TRAP, {Value::makeRegister(10), Value::makeRegister(11)}, {trapSubOut},
        0x80013004));
    trapBlock.instructions.push_back(trapBuilder.makeInstruction(Opcode::RETURN, {}, {}));
    const std::string trapSource = generator.generateSource(trapProgram, "trap_module");
    assert(trapSource.find("ExceptionCode::ArithmeticOverflow") != std::string::npos);
    assert(trapSource.find("(~(lhsValue ^ rhsValue) & (lhsValue ^ resultValue))") !=
           std::string::npos);
    assert(trapSource.find("((lhsValue ^ rhsValue) & (lhsValue ^ resultValue))") !=
           std::string::npos);

    Program mmioProgram;
    Builder mmioBuilder(mmioProgram);
    auto& mmioFunction = mmioBuilder.createFunction("mmio_func", 0x80014000);
    auto& mmioBlock = mmioBuilder.createBlock(mmioFunction, "entry");
    Value mmio8sOut = mmioBuilder.createTemporary();
    Value mmio8uOut = mmioBuilder.createTemporary();
    Value mmio16sOut = mmioBuilder.createTemporary();
    Value mmio16uOut = mmioBuilder.createTemporary();
    mmioBlock.instructions.push_back(mmioBuilder.makeInstruction(
        Opcode::MMIO_LOAD8, {Value::makeAddress(0x1F801040)}, {mmio8sOut}, 0x80014000));
    mmioBlock.instructions.push_back(mmioBuilder.makeInstruction(
        Opcode::MMIO_LOAD8U, {Value::makeAddress(0x1F801041)}, {mmio8uOut}, 0x80014004));
    mmioBlock.instructions.push_back(mmioBuilder.makeInstruction(
        Opcode::MMIO_LOAD16, {Value::makeAddress(0x1F801044)}, {mmio16sOut}, 0x80014008));
    mmioBlock.instructions.push_back(mmioBuilder.makeInstruction(
        Opcode::MMIO_LOAD16U, {Value::makeAddress(0x1F801046)}, {mmio16uOut}, 0x8001400C));
    mmioBlock.instructions.push_back(mmioBuilder.makeInstruction(
        Opcode::MMIO_STORE8, {Value::makeAddress(0x1F801048), Value::makeRegister(2)}, {},
        0x80014010));
    mmioBlock.instructions.push_back(mmioBuilder.makeInstruction(
        Opcode::MMIO_STORE16, {Value::makeAddress(0x1F80104A), Value::makeRegister(3)}, {},
        0x80014014));
    mmioBlock.instructions.push_back(
        mmioBuilder.makeInstruction(Opcode::MMIO_LOAD, {Value::makeAddress(0x1F80104C)},
                                    {mmioBuilder.createTemporary()}, 0x80014018));
    mmioBlock.instructions.push_back(mmioBuilder.makeInstruction(
        Opcode::MMIO_STORE, {Value::makeAddress(0x1F801050), Value::makeRegister(4)}, {},
        0x8001401C));
    mmioBlock.instructions.push_back(mmioBuilder.makeInstruction(Opcode::RETURN, {}, {}));
    const std::string mmioSource = generator.generateSource(mmioProgram, "mmio_module");
    assert(mmioSource.find("readMmio8s(context") != std::string::npos);
    assert(mmioSource.find("readMmio8(context") != std::string::npos);
    assert(mmioSource.find("readMmio16s(context") != std::string::npos);
    assert(mmioSource.find("readMmio16(context") != std::string::npos);
    assert(mmioSource.find("writeMmio8(context") != std::string::npos);
    assert(mmioSource.find("writeMmio16(context") != std::string::npos);
    assert(mmioSource.find("readMmio32(context") != std::string::npos);
    assert(mmioSource.find("writeMmio32(context") != std::string::npos);

    Program malformedCallProgram;
    Builder malformedCallBuilder(malformedCallProgram);
    auto& malformedCallFunction = malformedCallBuilder.createFunction("malformed_call", 0x80015000);
    auto& malformedCallBlock = malformedCallBuilder.createBlock(malformedCallFunction, "entry");
    malformedCallBlock.instructions.push_back(
        malformedCallBuilder.makeInstruction(Opcode::CALL, {}, {}, 0x80015000));
    malformedCallBlock.instructions.push_back(
        malformedCallBuilder.makeInstruction(Opcode::RETURN, {}, {}));
    bool malformedCallDetected = false;
    try
    {
        (void)generator.generateSource(malformedCallProgram, "malformed_call_module");
    }
    catch (const std::runtime_error& error)
    {
        malformedCallDetected =
            std::string(error.what()).find("Call instruction is missing a target operand") !=
            std::string::npos;
    }
    assert(malformedCallDetected);

    Program divProgram;
    Builder divBuilder(divProgram);
    auto& divFunction = divBuilder.createFunction("div_func", 0x80016000);
    auto& divBlock = divBuilder.createBlock(divFunction, "entry");
    Value divZeroLhs = divBuilder.createTemporary();
    Value divZeroRhs = divBuilder.createTemporary();
    Value divZeroHi = divBuilder.createTemporary();
    Value divZeroLo = divBuilder.createTemporary();
    Value divOverflowLhs = divBuilder.createTemporary();
    Value divOverflowRhs = divBuilder.createTemporary();
    Value divOverflowHi = divBuilder.createTemporary();
    Value divOverflowLo = divBuilder.createTemporary();
    Value divuLhs = divBuilder.createTemporary();
    Value divuRhs = divBuilder.createTemporary();
    Value divuHi = divBuilder.createTemporary();
    Value divuLo = divBuilder.createTemporary();
    divBlock.instructions.push_back(divBuilder.makeInstruction(
        Opcode::MOVE, {Value::makeImmediate(7)}, {divZeroLhs}, 0x80016000));
    divBlock.instructions.push_back(divBuilder.makeInstruction(
        Opcode::MOVE, {Value::makeImmediate(0)}, {divZeroRhs}, 0x80016004));
    divBlock.instructions.push_back(divBuilder.makeInstruction(
        Opcode::DIV, {divZeroLhs, divZeroRhs}, {divZeroHi, divZeroLo}, 0x80016008));
    divBlock.instructions.push_back(divBuilder.makeInstruction(
        Opcode::MOVE, {Value::makeImmediate(static_cast<psxrecomp::s32>(0x80000000u))},
        {divOverflowLhs}, 0x8001600C));
    divBlock.instructions.push_back(divBuilder.makeInstruction(
        Opcode::MOVE, {Value::makeImmediate(-1)}, {divOverflowRhs}, 0x80016010));
    divBlock.instructions.push_back(divBuilder.makeInstruction(
        Opcode::DIV, {divOverflowLhs, divOverflowRhs}, {divOverflowHi, divOverflowLo}, 0x80016014));
    divBlock.instructions.push_back(divBuilder.makeInstruction(
        Opcode::MOVE, {Value::makeImmediate(11)}, {divuLhs}, 0x80016018));
    divBlock.instructions.push_back(
        divBuilder.makeInstruction(Opcode::MOVE, {Value::makeImmediate(0)}, {divuRhs}, 0x8001601C));
    divBlock.instructions.push_back(
        divBuilder.makeInstruction(Opcode::DIVU, {divuLhs, divuRhs}, {divuHi, divuLo}, 0x80016020));
    divBlock.instructions.push_back(divBuilder.makeInstruction(Opcode::RETURN, {}, {}));
    const std::string divSource = generator.generateSource(divProgram, "div_module");
    assert(divSource.find("const s32 dividend = static_cast<s32>(") != std::string::npos);
    assert(divSource.find("= (dividend >= 0) ? 0xFFFFFFFFu : 1u;") != std::string::npos);
    assert(divSource.find("== 0x80000000u &&") != std::string::npos);
    assert(divSource.find("== 0xFFFFFFFFu)") != std::string::npos);
    assert(divSource.find("= 0x80000000u;") != std::string::npos);
    assert(divSource.find("= 0u;") != std::string::npos);

    Program zeroProgram;
    Builder zeroBuilder(zeroProgram);
    auto& zeroFunction = zeroBuilder.createFunction("zero_func", 0x80017000);
    auto& zeroBlock = zeroBuilder.createBlock(zeroFunction, "entry");
    zeroBlock.instructions.push_back(
        zeroBuilder.makeInstruction(Opcode::MOVE, {Value::makeImmediate(7)},
                                    {Value::makeRegister(psxrecomp::Registers::ZERO)}, 0x80017000));
    zeroBlock.instructions.push_back(
        zeroBuilder.makeInstruction(Opcode::LOAD, {Value::makeAddress(0x80010010)},
                                    {Value::makeRegister(psxrecomp::Registers::ZERO)}, 0x80017004));
    zeroBlock.instructions.push_back(zeroBuilder.makeInstruction(
        Opcode::SHR_LOGICAL,
        {Value::makeRegister(psxrecomp::Registers::T0), Value::makeImmediate(1)},
        {Value::makeRegister(psxrecomp::Registers::T1)}, 0x80017008));
    zeroBlock.instructions.push_back(zeroBuilder.makeInstruction(Opcode::RETURN, {}, {}));
    const std::string zeroSource = generator.generateSource(zeroProgram, "zero_module");
    assert(zeroSource.find("context.regs[Registers::ZERO] = 7;") == std::string::npos);
    assert(zeroSource.find("(void)(loadResult);") != std::string::npos);
    assert(zeroSource.find("static_cast<u32>(context.regs[Registers::T0]) >> (1 & 0x1F)") !=
           std::string::npos);

    Program jumpProgram;
    Builder jumpBuilder(jumpProgram);
    auto& jumpFunction = jumpBuilder.createFunction("jump_func", 0x80017100);
    auto& jumpBlock = jumpBuilder.createBlock(jumpFunction, "entry");
    jumpBlock.instructions.push_back(jumpBuilder.makeInstruction(
        Opcode::JUMP, {Value::makeAddress(0x80017120)}, {}, 0x80017100));
    jumpBlock.successors = {"block_external"};
    const std::string jumpSource = generator.generateSource(jumpProgram, "jump_module");
    assert(jumpSource.find("callRecompiledFunction(context, 0x80017120)") == std::string::npos);
    assert(jumpSource.find("failUnsupportedJump(0x80017120, 0x80017100);") != std::string::npos);

    runCodegenOverlapTest(generator);

#if defined(_MSC_VER)
    std::cerr << "Skipping compile-and-run check on MSVC toolchain.\n";
    return 0;
#endif

    const std::filesystem::path repoRoot = PSXRECOMP_SOURCE_DIR;
    assert(!repoRoot.empty());
    assert(std::filesystem::exists(repoRoot / "include/psxrecomp/types.h"));

    const auto stamp = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const std::filesystem::path outputDir =
        std::filesystem::temp_directory_path() / ("psxrecomp_codegen_test_" + stamp);
    std::filesystem::create_directories(outputDir);

    struct TempDirGuard
    {
        std::filesystem::path path;

        ~TempDirGuard()
        {
            std::filesystem::remove_all(path);
        }
    };
    TempDirGuard tempDirGuard{outputDir};

    std::string compiler = PSXRECOMP_TEST_CXX;
    if (compiler.empty())
    {
        compiler = "c++";
    }

    runCodegenCompileHarnessTest(generator, header, source, outputDir, repoRoot, compiler);
    return 0;
}
