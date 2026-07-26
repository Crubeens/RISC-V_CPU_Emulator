cmake_minimum_required(VERSION 3.20)

project(
    rv32_cpu_emulator
    VERSION 0.1.0
    DESCRIPTION "Modular RV32 system emulator"
    LANGUAGES CXX)

option(RV32_BUILD_TESTS "Build framework tests" ON)
option(
    RV32_BUILD_BAREMETAL_TESTS
    "Build and run the RV32I bare-metal acceptance test"
    ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

function(rv32_enable_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(
            ${target}
            PRIVATE
                -Wall
                -Wextra
                -Wpedantic
                -Wconversion
                -Wsign-conversion)
    endif()
endfunction()

add_library(
    rv32_core
    core/src/core.cpp
    core/src/csr.cpp
    core/src/decode.cpp
    core/src/execute.cpp
    core/src/frontend.cpp
    core/src/interrupt.cpp
    core/src/mmu.cpp
    core/src/trap.cpp)

target_include_directories(
    rv32_core
    PUBLIC
        ${PROJECT_SOURCE_DIR}/core/include)

rv32_enable_warnings(rv32_core)

add_library(
    rv32_platform
    platform/src/machine.cpp
    platform/src/system_bus.cpp
    devices/src/clint.cpp
    devices/src/framebuffer.cpp
    devices/src/plic.cpp
    devices/src/ram.cpp
    devices/src/syscon.cpp
    devices/src/uart16550.cpp
    devices/src/virtio_block.cpp)

target_include_directories(
    rv32_platform
    PUBLIC
        ${PROJECT_SOURCE_DIR}/core/include
        ${PROJECT_SOURCE_DIR}/devices/include
        ${PROJECT_SOURCE_DIR}/platform/include)

target_link_libraries(rv32_platform PUBLIC rv32_core)
rv32_enable_warnings(rv32_platform)

add_executable(
    rv32_emulator
    app/main.cpp)

target_link_libraries(rv32_emulator PRIVATE rv32_platform)
rv32_enable_warnings(rv32_emulator)

if(RV32_BUILD_TESTS)
    enable_testing()

    add_test(
        NAME rv32_emulator_smoke
        COMMAND rv32_emulator)

    add_executable(
        rv32_framework_tests
        tests/framework_tests.cpp)

    target_link_libraries(rv32_framework_tests PRIVATE rv32_platform)
    rv32_enable_warnings(rv32_framework_tests)

    add_test(
        NAME rv32_framework_tests
        COMMAND rv32_framework_tests)

    add_executable(
        rv32_decode_tests
        tests/decode_tests.cpp)

    target_link_libraries(
        rv32_decode_tests
        PRIVATE
            rv32_core)

    rv32_enable_warnings(rv32_decode_tests)

    add_test(
        NAME rv32_decode_tests
        COMMAND rv32_decode_tests)

    add_executable(
        rv32_instruction_decode_tests
        tests/instruction_decode_tests.cpp)

    target_link_libraries(
        rv32_instruction_decode_tests
        PRIVATE
            rv32_core)

    rv32_enable_warnings(rv32_instruction_decode_tests)

    add_test(
        NAME rv32_instruction_decode_tests
        COMMAND rv32_instruction_decode_tests)

    add_executable(
        rv32_frontend_tests
        tests/frontend_tests.cpp)

    target_link_libraries(
        rv32_frontend_tests
        PRIVATE
            rv32_core)

    rv32_enable_warnings(rv32_frontend_tests)

    add_test(
        NAME rv32_frontend_tests
        COMMAND rv32_frontend_tests)

    add_executable(
        rv32_execute_tests
        tests/execute_tests.cpp)

    target_link_libraries(
        rv32_execute_tests
        PRIVATE
            rv32_core)

    rv32_enable_warnings(rv32_execute_tests)

    add_test(
        NAME rv32_execute_tests
        COMMAND rv32_execute_tests)

    add_executable(
        rv32_m_extension_tests
        tests/m_extension_tests.cpp)

    target_link_libraries(
        rv32_m_extension_tests
        PRIVATE
            rv32_core)

    rv32_enable_warnings(rv32_m_extension_tests)

    add_test(
        NAME rv32_m_extension_tests
        COMMAND rv32_m_extension_tests)

    add_executable(
        rv32_atomic_extension_tests
        tests/atomic_extension_tests.cpp)

    target_link_libraries(
        rv32_atomic_extension_tests
        PRIVATE
            rv32_platform)

    rv32_enable_warnings(rv32_atomic_extension_tests)

    add_test(
        NAME rv32_atomic_extension_tests
        COMMAND rv32_atomic_extension_tests)

    add_executable(
        rv32_csr_extension_tests
        tests/csr_extension_tests.cpp)

    target_link_libraries(
        rv32_csr_extension_tests
        PRIVATE
            rv32_platform)

    rv32_enable_warnings(rv32_csr_extension_tests)

    add_test(
        NAME rv32_csr_extension_tests
        COMMAND rv32_csr_extension_tests)

    add_executable(
        rv32_control_flow_tests
        tests/control_flow_tests.cpp)

    target_link_libraries(
        rv32_control_flow_tests
        PRIVATE
            rv32_core)

    rv32_enable_warnings(rv32_control_flow_tests)

    add_test(
        NAME rv32_control_flow_tests
        COMMAND rv32_control_flow_tests)

    add_executable(
        rv32_memory_tests
        tests/memory_tests.cpp)

    target_link_libraries(
        rv32_memory_tests
        PRIVATE
            rv32_core)

    rv32_enable_warnings(rv32_memory_tests)

    add_test(
        NAME rv32_memory_tests
        COMMAND rv32_memory_tests)

    add_executable(
        rv32_core_step_tests
        tests/core_step_tests.cpp)

    target_link_libraries(
        rv32_core_step_tests
        PRIVATE
            rv32_core)

    rv32_enable_warnings(rv32_core_step_tests)

    add_test(
        NAME rv32_core_step_tests
        COMMAND rv32_core_step_tests)

    add_executable(
        rv32_trap_tests
        tests/trap_tests.cpp)

    target_link_libraries(
        rv32_trap_tests
        PRIVATE
            rv32_core)

    rv32_enable_warnings(rv32_trap_tests)

    add_test(
        NAME rv32_trap_tests
        COMMAND rv32_trap_tests)

    add_executable(
        rv32_supervisor_trap_tests
        tests/supervisor_trap_tests.cpp)

    target_link_libraries(
        rv32_supervisor_trap_tests
        PRIVATE
            rv32_core)

    rv32_enable_warnings(rv32_supervisor_trap_tests)

    add_test(
        NAME rv32_supervisor_trap_tests
        COMMAND rv32_supervisor_trap_tests)

    add_executable(
        rv32_interrupt_tests
        tests/interrupt_tests.cpp)

    target_link_libraries(
        rv32_interrupt_tests
        PRIVATE
            rv32_platform)

    rv32_enable_warnings(rv32_interrupt_tests)

    add_test(
        NAME rv32_interrupt_tests
        COMMAND rv32_interrupt_tests)

    add_executable(
        rv32_mmu_tests
        tests/mmu_tests.cpp)

    target_link_libraries(
        rv32_mmu_tests
        PRIVATE
            rv32_core)

    rv32_enable_warnings(rv32_mmu_tests)

    add_test(
        NAME rv32_mmu_tests
        COMMAND rv32_mmu_tests)

    if(RV32_BUILD_BAREMETAL_TESTS)
        find_program(
            RV32_BAREMETAL_GCC
            NAMES riscv32-unknown-elf-gcc
            HINTS
                "C:/msys64/ucrt64/bin")
        find_program(
            RV32_BAREMETAL_OBJCOPY
            NAMES riscv32-unknown-elf-objcopy
            HINTS
                "C:/msys64/ucrt64/bin")

        if(NOT RV32_BAREMETAL_GCC OR NOT RV32_BAREMETAL_OBJCOPY)
            message(
                FATAL_ERROR
                "M1 acceptance requires riscv32-unknown-elf GCC and objcopy")
        endif()

        set(
            RV32_BAREMETAL_OUTPUT_DIR
            "${CMAKE_CURRENT_BINARY_DIR}/baremetal")
        set(
            RV32_BAREMETAL_ELF
            "${RV32_BAREMETAL_OUTPUT_DIR}/smoke.elf")
        set(
            RV32_BAREMETAL_BIN
            "${RV32_BAREMETAL_OUTPUT_DIR}/smoke.bin")

        add_custom_command(
            OUTPUT
                "${RV32_BAREMETAL_BIN}"
            BYPRODUCTS
                "${RV32_BAREMETAL_ELF}"
            COMMAND
                ${CMAKE_COMMAND} -E make_directory
                "${RV32_BAREMETAL_OUTPUT_DIR}"
            COMMAND
                "${RV32_BAREMETAL_GCC}"
                -march=rv32i
                -mabi=ilp32
                -O2
                -ffreestanding
                -fno-builtin
                -fno-stack-protector
                -mno-relax
                -msmall-data-limit=0
                -nostdlib
                -nostartfiles
                "-Wl,-T,${PROJECT_SOURCE_DIR}/tests/baremetal/link.ld"
                -Wl,--gc-sections
                -Wl,--no-relax
                -o
                "${RV32_BAREMETAL_ELF}"
                "${PROJECT_SOURCE_DIR}/tests/baremetal/start.S"
                "${PROJECT_SOURCE_DIR}/tests/baremetal/main.c"
                -lgcc
            COMMAND
                "${RV32_BAREMETAL_OBJCOPY}"
                -O
                binary
                "${RV32_BAREMETAL_ELF}"
                "${RV32_BAREMETAL_BIN}"
            DEPENDS
                tests/baremetal/link.ld
                tests/baremetal/start.S
                tests/baremetal/main.c
            VERBATIM)

        add_custom_target(
            rv32_baremetal_image
            ALL
            DEPENDS
                "${RV32_BAREMETAL_BIN}")

        add_executable(
            rv32_baremetal_runner
            tests/baremetal/runner.cpp)

        target_link_libraries(
            rv32_baremetal_runner
            PRIVATE
                rv32_platform)

        rv32_enable_warnings(rv32_baremetal_runner)
        add_dependencies(
            rv32_baremetal_runner
            rv32_baremetal_image)

        add_test(
            NAME rv32_baremetal_smoke
            COMMAND
                rv32_baremetal_runner
                "${RV32_BAREMETAL_BIN}")
    endif()
endif()
