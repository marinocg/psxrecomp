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
    src/iso/iso_sector.cpp
    src/iso/iso_utils.cpp
    src/iso/iso_parser.cpp
    src/iso/path_table.cpp
    src/iso/psx_exe_loader.cpp
)

set(DISASM_SOURCES
    src/disasm/mips_disassembler.cpp
    src/disasm/instruction.cpp
)

set(IR_SOURCES
    src/ir/ir.cpp
    src/ir/control_flow.cpp
    src/ir/ssa.cpp
    src/ir/verify.cpp
)

set(RECOMPILER_SOURCES
    # src/recompiler/codegen.cpp
    # src/recompiler/optimizer.cpp
)

set(RUNTIME_SOURCES
    # src/runtime/psx_system.cpp
    # src/runtime/gpu.cpp
    # src/runtime/spu.cpp
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

# Main executable (commented out until we have source files)
# add_executable(psxrecomp
#     src/main.cpp
#     ${ISO_SOURCES}
#     ${DISASM_SOURCES}
#     ${IR_SOURCES}
#     ${RECOMPILER_SOURCES}
#     ${RUNTIME_SOURCES}
# )

# target_include_directories(psxrecomp PRIVATE ${CMAKE_SOURCE_DIR}/include)

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

    add_executable(ir_test
        tests/unit/ir_test.cpp
        ${IR_SOURCES}
    )
    target_include_directories(ir_test PRIVATE ${CMAKE_SOURCE_DIR}/include)
    add_test(NAME ir_test COMMAND ir_test)

    add_executable(ir_passes_test
        tests/unit/ir_passes_test.cpp
        ${IR_SOURCES}
    )
    target_include_directories(ir_passes_test PRIVATE ${CMAKE_SOURCE_DIR}/include)
    add_test(NAME ir_passes_test COMMAND ir_passes_test)
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
