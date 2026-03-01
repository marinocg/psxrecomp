cmake_minimum_required(VERSION 3.15)
project(psxrecomp VERSION 0.1.0 LANGUAGES CXX)

#Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

#Export compile commands for IDEs
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

#Build options
option(BUILD_TESTS "Build unit tests" ON)
option(BUILD_EXAMPLES "Build examples" ON)
option(BUILD_CLI "Build psxrecomp CLI" ON)
option(ENABLE_WARNINGS "Enable compiler warnings" ON)
option(ENABLE_CLANG_TIDY "Enable clang-tidy during builds" OFF)

#Compiler warnings
if(ENABLE_WARNINGS)
    if(MSVC)
        add_compile_options(/W4 /WX-)
    else()
        add_compile_options(-Wall -Wextra -Wpedantic)
    endif()
endif()

if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

if(WIN32 AND NOT MSVC)
    add_link_options(-static-libgcc -static-libstdc++)
endif()

if(ENABLE_CLANG_TIDY)
    find_program(CLANG_TIDY_EXE NAMES clang-tidy)
    if(CLANG_TIDY_EXE)
        set(CMAKE_CXX_CLANG_TIDY "${CLANG_TIDY_EXE}")
    else()
        message(WARNING "ENABLE_CLANG_TIDY is ON but clang-tidy was not found.")
    endif()
endif()

#Include directories
include_directories(${CMAKE_SOURCE_DIR}/include)

#Source files will be organized by component
set(ISO_SOURCES
    src/iso/cue_sheet.cpp
    src/iso/iso_boot.cpp
    src/iso/iso_parser_directory.cpp
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
    src/disasm/analysis_flow.cpp
    src/disasm/mips_disassembler.cpp
    src/disasm/instruction.cpp
    src/disasm/instruction_format.cpp
    src/disasm/instruction_format_helpers.cpp
)

set(IR_SOURCES
    src/ir/ir.cpp
    src/ir/control_flow.cpp
    src/ir/mips_ir_builder.cpp
    src/ir/mips_ir_translation.cpp
    src/ir/mips_ir_translation_no_delay.cpp
    src/ir/optimizations.cpp
    src/ir/ssa.cpp
    src/ir/verify.cpp
)

set(RECOMPILER_SOURCES
    src/recompiler/codegen.cpp
    src/recompiler/codegen_build.cpp
    src/recompiler/codegen_runner.cpp
    src/recompiler/codegen_runner_support_common.cpp
    src/recompiler/codegen_runner_support_presenter.cpp
    src/recompiler/codegen_source.cpp
    src/recompiler/codegen_source_body.cpp
    src/recompiler/codegen_runner_main.cpp
    src/recompiler/codegen_lowering.cpp
    src/recompiler/codegen_lowering_helpers.cpp
    src/recompiler/codegen_lowering_instructions.cpp
    src/recompiler/codegen_lowering_control_flow.cpp
    src/recompiler/codegen_runtime_helpers.cpp
    src/recompiler/cpp_emitter.cpp
    src/recompiler/pipeline_analysis_helpers.cpp
    src/recompiler/pipeline_helpers.cpp
    src/recompiler/pipeline_helpers_manifest.cpp
    src/recompiler/pipeline_helpers_output.cpp
    src/recompiler/pipeline_helpers_output_plan.cpp
    src/recompiler/pipeline_helpers_output_serialize.cpp
    src/recompiler/pipeline_helpers_output_support.cpp
    src/recompiler/pipeline_selection_helpers.cpp
    src/recompiler/pipeline.cpp
)

set(RUNTIME_SOURCES
    src/runtime/cdrom.cpp
    src/runtime/cop0.cpp
    src/runtime/debug_overlay.cpp
    src/runtime/dma.cpp
    src/runtime/gpu.cpp
    src/runtime/gpu_transfer.cpp
    src/runtime/gpu_commands.cpp
    src/runtime/gpu_renderer.cpp
    src/runtime/gpu_renderer_primitives.cpp
    src/runtime/input.cpp
    src/runtime/interrupt_controller.cpp
    src/runtime/interrupt_dispatcher.cpp
    src/runtime/kernel_events.cpp
    src/runtime/logger.cpp
    src/runtime/psx_system.cpp
    src/runtime/psx_system_interrupts.cpp
    src/runtime/psx_system_bios.cpp
    src/runtime/psx_system_bios_a0.cpp
    src/runtime/psx_system_bios_b0.cpp
    src/runtime/psx_system_bios_c0.cpp
    src/runtime/psx_system_mmio.cpp
    src/runtime/psx_system_state.cpp
    src/runtime/psx_system_dma.cpp
    src/runtime/resource_pack.cpp
    src/runtime/scheduler.cpp
    src/runtime/spu.cpp
    src/runtime/timers.cpp
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
        tests/unit/iso_parser_test_helpers_extra.cpp
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

    add_executable(mips_ir_translation_diagnostics_test
        tests/unit/mips_ir_translation_diagnostics_test.cpp
        ${IR_SOURCES}
        ${DISASM_SOURCES}
    )
    target_include_directories(mips_ir_translation_diagnostics_test PRIVATE ${CMAKE_SOURCE_DIR}/include)
    add_test(NAME mips_ir_translation_diagnostics_test COMMAND mips_ir_translation_diagnostics_test)

    add_executable(mips_ir_control_flow_test
        tests/unit/mips_ir_control_flow_test.cpp
        ${IR_SOURCES}
        ${DISASM_SOURCES}
    )
    target_include_directories(mips_ir_control_flow_test PRIVATE ${CMAKE_SOURCE_DIR}/include)
    add_test(NAME mips_ir_control_flow_test COMMAND mips_ir_control_flow_test)

    add_executable(gpu_test
        tests/unit/gpu_test.cpp
        src/runtime/gpu.cpp
        src/runtime/gpu_transfer.cpp
        src/runtime/gpu_commands.cpp
        src/runtime/gpu_renderer.cpp
        src/runtime/gpu_renderer_primitives.cpp
    )
    target_include_directories(gpu_test PRIVATE ${CMAKE_SOURCE_DIR}/include)
    add_test(NAME gpu_test COMMAND gpu_test)
    add_executable(runtime_test
        tests/unit/runtime_test.cpp
        tests/unit/runtime_test_sections.cpp
        ${RUNTIME_SOURCES}
    )
    target_include_directories(runtime_test PRIVATE ${CMAKE_SOURCE_DIR}/include)
    add_test(NAME runtime_test COMMAND runtime_test)

    add_executable(cop0_test
        tests/unit/cop0_test.cpp
        src/runtime/cop0.cpp
    )
    target_include_directories(cop0_test PRIVATE ${CMAKE_SOURCE_DIR}/include)
    add_test(NAME cop0_test COMMAND cop0_test)

    add_executable(spu_test
        tests/unit/spu_test.cpp
        src/runtime/spu.cpp
    )
    target_include_directories(spu_test PRIVATE ${CMAKE_SOURCE_DIR}/include)
    add_test(NAME spu_test COMMAND spu_test)

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

    add_executable(codegen_lowering_test
        tests/unit/codegen_lowering_test.cpp
        tests/unit/codegen_lowering_source_comments_case.cpp
        ${IR_SOURCES}
        ${DISASM_SOURCES}
        ${RECOMPILER_SOURCES}
        ${ISO_SOURCES}
    )
    target_include_directories(codegen_lowering_test PRIVATE ${CMAKE_SOURCE_DIR}/include)
    add_test(NAME codegen_lowering_test COMMAND codegen_lowering_test)

    add_executable(codegen_metadata_test
        tests/unit/codegen_metadata_test.cpp
        ${IR_SOURCES}
        ${DISASM_SOURCES}
        ${RECOMPILER_SOURCES}
        ${ISO_SOURCES}
    )
    target_include_directories(codegen_metadata_test PRIVATE ${CMAKE_SOURCE_DIR}/include)
    add_test(NAME codegen_metadata_test COMMAND codegen_metadata_test)

    add_executable(bios_vector_test
        tests/unit/bios_vector_test.cpp
        tests/unit/bios_vector_test_sections.cpp
        tests/unit/bios_vector_test_interrupts.cpp
        ${RUNTIME_SOURCES}
    )
    target_include_directories(bios_vector_test PRIVATE ${CMAKE_SOURCE_DIR}/include)
    add_test(NAME bios_vector_test COMMAND bios_vector_test)

    add_executable(kernel_events_test
        tests/unit/kernel_events_test.cpp
        src/runtime/kernel_events.cpp
        src/runtime/interrupt_controller.cpp
    )
    target_include_directories(kernel_events_test PRIVATE ${CMAKE_SOURCE_DIR}/include)
    add_test(NAME kernel_events_test COMMAND kernel_events_test)

    add_executable(interrupt_dispatcher_test
        tests/unit/interrupt_dispatcher_test.cpp
        tests/unit/interrupt_dispatcher_test_sections.cpp
        src/runtime/interrupt_dispatcher.cpp
        src/runtime/kernel_events.cpp
        src/runtime/interrupt_controller.cpp
        src/runtime/logger.cpp
    )
    target_include_directories(interrupt_dispatcher_test PRIVATE ${CMAKE_SOURCE_DIR}/include)
    add_test(NAME interrupt_dispatcher_test COMMAND interrupt_dispatcher_test)

    add_executable(codegen_debug_test
        tests/unit/codegen_debug_test.cpp
        ${IR_SOURCES}
        ${DISASM_SOURCES}
        ${RECOMPILER_SOURCES}
        ${ISO_SOURCES}
    )
    target_include_directories(codegen_debug_test PRIVATE ${CMAKE_SOURCE_DIR}/include)
    add_test(NAME codegen_debug_test COMMAND codegen_debug_test)
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
