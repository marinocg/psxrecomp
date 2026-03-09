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

# Shared build options (target-scoped)
add_library(psxrecomp_build_options INTERFACE)
target_compile_definitions(psxrecomp_build_options INTERFACE
    PSXRECOMP_PROJECT_VERSION="${PROJECT_VERSION}"
)

if(ENABLE_WARNINGS)
    target_compile_options(psxrecomp_build_options INTERFACE
        $<$<CXX_COMPILER_ID:MSVC>:/W4>
        $<$<CXX_COMPILER_ID:MSVC>:/WX->
        $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall>
        $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wextra>
        $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wpedantic>
    )
endif()

target_link_options(psxrecomp_build_options INTERFACE
    $<$<AND:$<PLATFORM_ID:Windows>,$<NOT:$<CXX_COMPILER_ID:MSVC>>>:-static-libgcc>
    $<$<AND:$<PLATFORM_ID:Windows>,$<NOT:$<CXX_COMPILER_ID:MSVC>>>:-static-libstdc++>
)

if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

if(ENABLE_CLANG_TIDY)
    find_program(CLANG_TIDY_EXE NAMES clang-tidy)
    if(CLANG_TIDY_EXE)
        set(CMAKE_CXX_CLANG_TIDY "${CLANG_TIDY_EXE}")
    else()
        message(WARNING "ENABLE_CLANG_TIDY is ON but clang-tidy was not found.")
    endif()
endif()

#Source files will be organized by component
set(ISO_SOURCES
    src/iso/cue_sheet.cpp
    src/iso/iso_boot.cpp
    src/iso/iso_parser_directory.cpp
    src/iso/iso_parser_filesystem.cpp
    src/iso/iso_parser_metadata.cpp
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
    src/ir/mips_ir_translation_no_delay_load.cpp
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
    src/recompiler/codegen_source_debug.cpp
    src/recompiler/codegen_source_body.cpp
    src/recompiler/codegen_runner_main.cpp
    src/recompiler/codegen_runner_exception_block.cpp
    src/recompiler/codegen_lowering.cpp
    src/recompiler/codegen_lowering_helpers.cpp
    src/recompiler/codegen_lowering_instructions.cpp
    src/recompiler/codegen_lowering_gte.cpp
    src/recompiler/codegen_lowering_control_flow.cpp
    src/recompiler/codegen_runtime_helpers.cpp
    src/recompiler/cpp_emitter.cpp
    src/recompiler/pipeline_analysis_helpers.cpp
    src/recompiler/pipeline_harvest_internal.cpp
    src/recompiler/pipeline_harvest_helpers.cpp
    src/recompiler/pipeline_indirect_call_helpers.cpp
    src/recompiler/pipeline_helpers.cpp
    src/recompiler/pipeline_helpers_manifest.cpp
    src/recompiler/pipeline_helpers_output.cpp
    src/recompiler/pipeline_helpers_output_plan.cpp
    src/recompiler/pipeline_helpers_output_serialize.cpp
    src/recompiler/pipeline_helpers_output_serialize_catalog.cpp
    src/recompiler/pipeline_helpers_output_support.cpp
    src/recompiler/pipeline_helpers_output_support_disc.cpp
    src/recompiler/pipeline_helpers_output_support_hash.cpp
    src/recompiler/pipeline_helpers_output_support_embedded.cpp
    src/recompiler/pipeline_input_loader.cpp
    src/recompiler/pipeline_selection_helpers.cpp
    src/recompiler/pipeline_workspace_helpers.cpp
    src/recompiler/pipeline_workspace_json.cpp
    src/recompiler/pipeline.cpp
)

set(RUNTIME_SOURCES
    src/runtime/cdrom_shared.cpp
    src/runtime/cdrom_core.cpp
    src/runtime/cdrom_commands.cpp
    src/runtime/cdrom_data.cpp
    src/runtime/cdrom_state.cpp
    src/runtime/disc_image.cpp
    src/runtime/cop0.cpp
    src/runtime/debug_overlay.cpp
    src/runtime/dma.cpp
    src/runtime/gte.cpp
    src/runtime/gte_commands.cpp
    src/runtime/gte_lighting.cpp
    src/runtime/gte_math.cpp
    src/runtime/gte_ops.cpp
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
    src/runtime/mdec.cpp
    src/runtime/psx_system.cpp
    src/runtime/psx_system_accessors.cpp
    src/runtime/psx_system_callbacks.cpp
    src/runtime/psx_system_cd_callback_trace.cpp
    src/runtime/psx_system_copy_debug.cpp
    src/runtime/psx_system_display_trace.cpp
    src/runtime/psx_system_gpu_wait_trace.cpp
    src/runtime/psx_system_interrupts.cpp
    src/runtime/psx_system_irq_chains.cpp
    src/runtime/psx_system_bios.cpp
    src/runtime/psx_system_bios_a0.cpp
    src/runtime/psx_system_bios_b0.cpp
    src/runtime/psx_system_bios_c0.cpp
    src/runtime/bios_file_table.cpp
    src/runtime/psx_system_bios_cd.cpp
    src/runtime/psx_system_mmio.cpp
    src/runtime/psx_system_state.cpp
    src/runtime/psx_system_dma.cpp
    src/runtime/psx_system_wait_event.cpp
    src/runtime/resource_pack.cpp
    src/runtime/scheduler.cpp
    src/runtime/sio0.cpp
    src/runtime/spu.cpp
    src/runtime/spu_audio.cpp
    src/runtime/stall_classifier.cpp
    src/runtime/stall_heap_debug.cpp
    src/runtime/stall_write_watch.cpp
    src/runtime/timers.cpp
)

set(PSXRECOMP_BUILD_COMPONENTS OFF)
if(BUILD_CLI OR BUILD_TESTS)
    set(PSXRECOMP_BUILD_COMPONENTS ON)
endif()

if(PSXRECOMP_BUILD_COMPONENTS)
    add_library(psxrecomp_iso STATIC ${ISO_SOURCES})
    target_include_directories(psxrecomp_iso PUBLIC ${CMAKE_SOURCE_DIR}/include)
    target_link_libraries(psxrecomp_iso PUBLIC psxrecomp_build_options)

    add_library(psxrecomp_disasm STATIC ${DISASM_SOURCES})
    target_include_directories(psxrecomp_disasm PUBLIC ${CMAKE_SOURCE_DIR}/include)
    target_link_libraries(psxrecomp_disasm PUBLIC psxrecomp_build_options)

    add_library(psxrecomp_ir STATIC ${IR_SOURCES})
    target_include_directories(psxrecomp_ir PUBLIC ${CMAKE_SOURCE_DIR}/include)
    target_link_libraries(psxrecomp_ir PUBLIC psxrecomp_build_options psxrecomp_disasm)

    add_library(psxrecomp_recompiler STATIC ${RECOMPILER_SOURCES})
    target_include_directories(psxrecomp_recompiler PUBLIC ${CMAKE_SOURCE_DIR}/include)
    target_link_libraries(psxrecomp_recompiler
        PUBLIC
            psxrecomp_build_options
            psxrecomp_iso
            psxrecomp_disasm
            psxrecomp_ir
    )

    add_library(psxrecomp_runtime STATIC ${RUNTIME_SOURCES})
    target_include_directories(psxrecomp_runtime PUBLIC ${CMAKE_SOURCE_DIR}/include)
    target_link_libraries(psxrecomp_runtime PUBLIC psxrecomp_build_options)
endif()

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
        src/main_json_output.cpp
    )
    target_include_directories(psxrecomp PRIVATE ${CMAKE_SOURCE_DIR}/include)
    target_link_libraries(psxrecomp
        PRIVATE
            psxrecomp_build_options
            psxrecomp_iso
            psxrecomp_disasm
            psxrecomp_ir
            psxrecomp_recompiler
            psxrecomp_runtime
    )
endif()

# Testing
if(BUILD_TESTS)
    enable_testing()

    function(add_psxrecomp_unit_test target_name)
        add_executable(${target_name} ${ARGN})
        target_include_directories(${target_name} PRIVATE ${CMAKE_SOURCE_DIR}/include)
        target_link_libraries(${target_name} PRIVATE psxrecomp_build_options)
        add_test(NAME ${target_name} COMMAND ${target_name})
    endfunction()

    add_psxrecomp_unit_test(iso_parser_test
        tests/unit/iso_parser_test.cpp
        tests/unit/iso_parser_test_helpers_extra.cpp
    )
    target_link_libraries(iso_parser_test PRIVATE psxrecomp_iso)

    add_psxrecomp_unit_test(psx_exe_loader_test
        tests/unit/psx_exe_loader_test.cpp
    )
    target_link_libraries(psx_exe_loader_test PRIVATE psxrecomp_iso)

    add_psxrecomp_unit_test(disasm_test
        tests/unit/disasm_test.cpp
    )
    target_link_libraries(disasm_test PRIVATE psxrecomp_disasm)

    add_psxrecomp_unit_test(disasm_analysis_test
        tests/unit/disasm_analysis_test.cpp
    )
    target_link_libraries(disasm_analysis_test PRIVATE psxrecomp_disasm)

    add_psxrecomp_unit_test(ir_test
        tests/unit/ir_test.cpp
    )
    target_link_libraries(ir_test PRIVATE psxrecomp_ir)

    add_psxrecomp_unit_test(ir_passes_test
        tests/unit/ir_passes_test.cpp
    )
    target_link_libraries(ir_passes_test PRIVATE psxrecomp_ir)

    add_psxrecomp_unit_test(recompiler_codegen_test
        tests/unit/recompiler_codegen_test.cpp
        tests/unit/recompiler_codegen_test_sections.cpp
    )
    target_link_libraries(recompiler_codegen_test PRIVATE psxrecomp_recompiler)
    target_compile_definitions(
        recompiler_codegen_test
        PRIVATE PSXRECOMP_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
                PSXRECOMP_TEST_CXX="${CMAKE_CXX_COMPILER}"
    )

    add_psxrecomp_unit_test(mips_ir_builder_test
        tests/unit/mips_ir_builder_test.cpp
    )
    target_link_libraries(mips_ir_builder_test PRIVATE psxrecomp_ir)

    add_psxrecomp_unit_test(mips_ir_translation_diagnostics_test
        tests/unit/mips_ir_translation_diagnostics_test.cpp
    )
    target_link_libraries(mips_ir_translation_diagnostics_test PRIVATE psxrecomp_ir)

    add_psxrecomp_unit_test(mips_ir_control_flow_test
        tests/unit/mips_ir_control_flow_test.cpp
    )
    target_link_libraries(mips_ir_control_flow_test PRIVATE psxrecomp_ir)

    add_psxrecomp_unit_test(gpu_test
        tests/unit/gpu_test.cpp
    )
    target_link_libraries(gpu_test PRIVATE psxrecomp_runtime)

    add_psxrecomp_unit_test(runtime_test
        tests/unit/runtime_test.cpp
        tests/unit/runtime_test_sections.cpp
        tests/unit/runtime_test_timer_sections.cpp
    )
    target_link_libraries(runtime_test PRIVATE psxrecomp_runtime)

    add_psxrecomp_unit_test(callback_bridge_test
        tests/unit/callback_bridge_test.cpp
    )
    target_link_libraries(callback_bridge_test PRIVATE psxrecomp_runtime)

    add_psxrecomp_unit_test(cop0_test
        tests/unit/cop0_test.cpp
    )
    target_link_libraries(cop0_test PRIVATE psxrecomp_runtime)

    add_psxrecomp_unit_test(gte_test
        tests/unit/gte_test.cpp
    )
    target_link_libraries(gte_test PRIVATE psxrecomp_runtime)

    add_psxrecomp_unit_test(spu_test
        tests/unit/spu_test.cpp
    )
    target_link_libraries(spu_test PRIVATE psxrecomp_runtime)

    add_psxrecomp_unit_test(disc_image_test
        tests/unit/disc_image_test.cpp
    )
    target_link_libraries(disc_image_test PRIVATE psxrecomp_runtime)

    add_psxrecomp_unit_test(cdrom_command_test
        tests/unit/cdrom_command_test.cpp
    )
    target_link_libraries(cdrom_command_test PRIVATE psxrecomp_runtime)

    add_psxrecomp_unit_test(cdrom_read_pipeline_test
        tests/unit/cdrom_read_pipeline_test.cpp
    )
    target_link_libraries(cdrom_read_pipeline_test PRIVATE psxrecomp_runtime)

    add_psxrecomp_unit_test(cdrom_xa_audio_test
        tests/unit/cdrom_xa_audio_test.cpp
    )
    target_link_libraries(cdrom_xa_audio_test PRIVATE psxrecomp_runtime)

    add_psxrecomp_unit_test(cdrom_disc_swap_test
        tests/unit/cdrom_disc_swap_test.cpp
    )
    target_link_libraries(cdrom_disc_swap_test PRIVATE psxrecomp_runtime)

    add_psxrecomp_unit_test(cdrom_state_test
        tests/unit/cdrom_state_test.cpp
    )
    target_link_libraries(cdrom_state_test PRIVATE psxrecomp_runtime)

    add_psxrecomp_unit_test(sio0_test
        tests/unit/sio0_test.cpp
        tests/unit/sio0_irq_bus_test_sections.cpp
    )
    target_link_libraries(sio0_test PRIVATE psxrecomp_runtime)

    add_psxrecomp_unit_test(stall_classifier_test
        tests/unit/stall_classifier_test.cpp
    )
    target_link_libraries(stall_classifier_test PRIVATE psxrecomp_runtime)

    add_psxrecomp_unit_test(wait_event_test
        tests/unit/wait_event_test.cpp
    )
    target_link_libraries(wait_event_test PRIVATE psxrecomp_runtime)

    add_psxrecomp_unit_test(bios_cd_test
        tests/unit/bios_cd_test.cpp
    )
    target_link_libraries(bios_cd_test PRIVATE psxrecomp_runtime)

    add_psxrecomp_unit_test(pipeline_test
        tests/unit/pipeline_test.cpp
        tests/unit/pipeline_test_builders.cpp
    )
    target_link_libraries(pipeline_test
        PRIVATE
            psxrecomp_iso
            psxrecomp_disasm
            psxrecomp_ir
            psxrecomp_recompiler
            psxrecomp_runtime
    )

    add_psxrecomp_unit_test(pipeline_candidate_test
        tests/unit/pipeline_candidate_test.cpp
        tests/unit/pipeline_candidate_test_helpers.cpp
        tests/unit/pipeline_candidate_test_iso_builders.cpp
        tests/unit/pipeline_candidate_test_sections.cpp
    )
    target_link_libraries(pipeline_candidate_test
        PRIVATE
            psxrecomp_iso
            psxrecomp_disasm
            psxrecomp_ir
            psxrecomp_recompiler
            psxrecomp_runtime
    )

    add_psxrecomp_unit_test(codegen_lowering_test
        tests/unit/codegen_lowering_test.cpp
        tests/unit/codegen_lowering_source_comments_case.cpp
        tests/unit/codegen_lowering_cop_test_sections.cpp
    )
    target_link_libraries(codegen_lowering_test PRIVATE psxrecomp_recompiler)

    add_psxrecomp_unit_test(codegen_metadata_test
        tests/unit/codegen_metadata_test.cpp
    )
    target_link_libraries(codegen_metadata_test PRIVATE psxrecomp_recompiler)

    add_psxrecomp_unit_test(bios_vector_test
        tests/unit/bios_vector_test.cpp
        tests/unit/bios_vector_test_sections.cpp
        tests/unit/bios_vector_test_interrupts.cpp
        tests/unit/bios_vector_test_output_sections.cpp
    )
    target_link_libraries(bios_vector_test PRIVATE psxrecomp_runtime)

    add_psxrecomp_unit_test(kernel_events_test
        tests/unit/kernel_events_test.cpp
    )
    target_link_libraries(kernel_events_test PRIVATE psxrecomp_runtime)

    add_psxrecomp_unit_test(interrupt_dispatcher_test
        tests/unit/interrupt_dispatcher_test.cpp
        tests/unit/interrupt_dispatcher_test_sections.cpp
    )
    target_link_libraries(interrupt_dispatcher_test PRIVATE psxrecomp_runtime)

    add_psxrecomp_unit_test(bios_file_test
        tests/unit/bios_file_test.cpp
    )
    target_link_libraries(bios_file_test PRIVATE psxrecomp_runtime)

    add_psxrecomp_unit_test(codegen_debug_test
        tests/unit/codegen_debug_test.cpp
    )
    target_link_libraries(codegen_debug_test PRIVATE psxrecomp_recompiler)
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
