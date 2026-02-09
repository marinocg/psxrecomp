cmake_minimum_required(VERSION 3.15)
project(psxrecomp VERSION 0.1.0 LANGUAGES CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile commands for IDEs
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Build options
option(BUILD_TESTS "Build unit tests" ON)
option(BUILD_EXAMPLES "Build examples" ON)
option(BUILD_CLI "Build psxrecomp CLI" ON)
option(ENABLE_WARNINGS "Enable compiler warnings" ON)
option(ENABLE_CLANG_TIDY "Enable clang-tidy during builds" OFF)

# Compiler warnings
if(ENABLE_WARNINGS)
    if(MSVC)
        add_compile_options(/W4 /WX-)
    else()
        add_compile_options(-Wall -Wextra -Wpedantic)
    endif()
endif()

if(ENABLE_CLANG_TIDY)
    find_program(CLANG_TIDY_EXE NAMES clang-tidy)
    if(CLANG_TIDY_EXE)
        set(CMAKE_CXX_CLANG_TIDY "${CLANG_TIDY_EXE}")
    else()
        message(WARNING "ENABLE_CLANG_TIDY is ON but clang-tidy was not found.")
    endif()
endif()

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/include)

# Source files will be organized by component
set(ISO_SOURCES
    src/iso/cue_sheet.cpp
    src/iso/iso_boot.cpp
    src/iso/iso_parser_filesystem.cpp
    src/iso/iso_parser_pvd.cpp
    src/iso/iso_parser_resources.cpp
    src/iso/iso_parser_stream.cpp
    src/iso/iso_sector.cpp
    src/iso/iso_utils.cpp
    src/iso/iso_parser.cpp
    src/iso/multi_disc_set.cpp
    src/iso/path_table.cpp
    src/iso/psx_exe_loader.cpp
    src/iso/psx_exe_loader_helpers.cpp
)

set(DISASM_SOURCES
    src/disasm/analysis_helpers.cpp
    src/disasm/analysis.cpp
    src/disasm/mips_disassembler.cpp
    src/disasm/instruction.cpp
    src/disasm/instruction_format.cpp
    src/disasm/instruction_format_helpers.cpp
)

set(IR_SOURCES
    src/ir/ir.cpp
    src/ir/control_flow.cpp
    src/ir/mips_ir_builder.cpp
    src/ir/ssa.cpp
    src/ir/verify.cpp
)

set(RECOMPILER_SOURCES
    src/recompiler/codegen.cpp
    src/recompiler/codegen_lowering.cpp
    src/recompiler/cpp_emitter.cpp
    src/recompiler/pipeline_helpers.cpp
    src/recompiler/pipeline.cpp
)

set(RUNTIME_SOURCES
    src/runtime/cdrom.cpp
    src/runtime/dma.cpp
    src/runtime/gpu.cpp
    src/runtime/input.cpp
    src/runtime/interrupt_controller.cpp
    src/runtime/logger.cpp
    src/runtime/psx_system.cpp
    src/runtime/scheduler.cpp
    src/runtime/spu.cpp
)

file(GLOB_RECURSE ALL_FORMAT_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_SOURCE_DIR}/src/*.cpp
    ${CMAKE_SOURCE_DIR}/include/*.h
    ${CMAKE_SOURCE_DIR}/tests/*.cpp
)

find_program(CLANG_FORMAT_EXE NAMES clang-format)
if(CLANG_FORMAT_EXE)
    add_custom_target(clang-format
        COMMAND ${CLANG_FORMAT_EXE} -i ${ALL_FORMAT_SOURCES}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Running clang-format on project sources"
    )
endif()

if(BUILD_CLI)
    add_executable(psxrecomp
        src/main.cpp
        ${ISO_SOURCES}
        ${DISASM_SOURCES}
        ${IR_SOURCES}
        ${RECOMPILER_SOURCES}
        ${RUNTIME_SOURCES}
    )

    target_include_directories(psxrecomp PRIVATE ${CMAKE_SOURCE_DIR}/include)
endif()

# Testing
if(BUILD_TESTS)
    enable_testing()
    add_executable(iso_parser_test
        tests/unit/iso_parser_test.cpp
        ${ISO_SOURCES}
    )
    target_include_directories(iso_parser_test PRIVATE ${CMAKE_SOURCE_DIR}/include)
    add_test(NAME iso_parser_test COMMAND iso_parser_test)

    add_executable(psx_exe_loader_test
        tests/unit/psx_exe_loader_test.cpp
        ${ISO_SOURCES}
    )
    target_include_directories(psx_exe_loader_test PRIVATE ${CMAKE_SOURCE_DIR}/include)
    add_test(NAME psx_exe_loader_test COMMAND psx_exe_loader_test)

    add_executable(disasm_test
        tests/unit/disasm_test.cpp
        ${DISASM_SOURCES}
    )
    target_include_directories(disasm_test PRIVATE ${CMAKE_SOURCE_DIR}/include)
    add_test(NAME disasm_test COMMAND disasm_test)

    add_executable(disasm_analysis_test
        tests/unit/disasm_analysis_test.cpp
        ${DISASM_SOURCES}
    )
    target_include_directories(disasm_analysis_test PRIVATE ${CMAKE_SOURCE_DIR}/include)
    add_test(NAME disasm_analysis_test COMMAND disasm_analysis_test)

    add_executable(ir_test
        tests/unit/ir_test.cpp
        ${IR_SOURCES}
        ${DISASM_SOURCES}
    )
    target_include_directories(ir_test PRIVATE ${CMAKE_SOURCE_DIR}/include)
    add_test(NAME ir_test COMMAND ir_test)

    add_executable(ir_passes_test
        tests/unit/ir_passes_test.cpp
        ${IR_SOURCES}
        ${DISASM_SOURCES}
    )
    target_include_directories(ir_passes_test PRIVATE ${CMAKE_SOURCE_DIR}/include)
    add_test(NAME ir_passes_test COMMAND ir_passes_test)

    add_executable(recompiler_codegen_test
        tests/unit/recompiler_codegen_test.cpp
        ${IR_SOURCES}
        ${DISASM_SOURCES}
        ${RECOMPILER_SOURCES}
        ${ISO_SOURCES}
    )
    target_include_directories(recompiler_codegen_test PRIVATE ${CMAKE_SOURCE_DIR}/include)
    target_compile_definitions(
        recompiler_codegen_test
        PRIVATE PSXRECOMP_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
                PSXRECOMP_TEST_CXX="${CMAKE_CXX_COMPILER}"
    )
    add_test(NAME recompiler_codegen_test COMMAND recompiler_codegen_test)

    add_executable(mips_ir_builder_test
        tests/unit/mips_ir_builder_test.cpp
        ${IR_SOURCES}
        ${DISASM_SOURCES}
    )
    target_include_directories(mips_ir_builder_test PRIVATE ${CMAKE_SOURCE_DIR}/include)
    add_test(NAME mips_ir_builder_test COMMAND mips_ir_builder_test)

    add_executable(runtime_test
        tests/unit/runtime_test.cpp
        ${RUNTIME_SOURCES}
    )
    target_include_directories(runtime_test PRIVATE ${CMAKE_SOURCE_DIR}/include)
    add_test(NAME runtime_test COMMAND runtime_test)

    add_executable(pipeline_test
        tests/unit/pipeline_test.cpp
        ${ISO_SOURCES}
        ${DISASM_SOURCES}
        ${IR_SOURCES}
        ${RECOMPILER_SOURCES}
        ${RUNTIME_SOURCES}
    )
    target_include_directories(pipeline_test PRIVATE ${CMAKE_SOURCE_DIR}/include)
    add_test(NAME pipeline_test COMMAND pipeline_test)

    add_executable(pipeline_candidate_test
        tests/unit/pipeline_candidate_test.cpp
        ${ISO_SOURCES}
        ${DISASM_SOURCES}
        ${IR_SOURCES}
        ${RECOMPILER_SOURCES}
        ${RUNTIME_SOURCES}
    )
    target_include_directories(pipeline_candidate_test PRIVATE ${CMAKE_SOURCE_DIR}/include)
    add_test(NAME pipeline_candidate_test COMMAND pipeline_candidate_test)
endif()

# Examples
if(BUILD_EXAMPLES)
    # add_subdirectory(examples)
endif()

# Installation rules
# install(TARGETS psxrecomp DESTINATION bin)
# install(DIRECTORY include/ DESTINATION include)

# Print configuration summary
message(STATUS "PSXRecomp Configuration:")
message(STATUS "  Version: ${PROJECT_VERSION}")
message(STATUS "  C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  Build Tests: ${BUILD_TESTS}")
message(STATUS "  Build Examples: ${BUILD_EXAMPLES}")
message(STATUS "  Compiler: ${CMAKE_CXX_COMPILER_ID}")
