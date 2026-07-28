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
option(
    RV32_ENABLE_SDL
    "Build the M7 SDL graphical frontend"
    ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_program(
    RV32_DTC
    NAMES dtc
    HINTS
        "C:/msys64/ucrt64/bin")
if(NOT RV32_DTC)
    message(
        FATAL_ERROR
        "M5 requires the Device Tree Compiler (dtc)")
endif()

set(
    RV32_GENERATED_IMAGE_DIR
    "${CMAKE_CURRENT_BINARY_DIR}/images")
set(
    RV32_VIRT_DTB
    "${RV32_GENERATED_IMAGE_DIR}/rv32-virt.dtb")

add_custom_command(
    OUTPUT
        "${RV32_VIRT_DTB}"
    COMMAND
        ${CMAKE_COMMAND} -E make_directory
        "${RV32_GENERATED_IMAGE_DIR}"
    COMMAND
        "${RV32_DTC}"
        -I dts
        -O dtb
        -o
        "${RV32_VIRT_DTB}"
        "${PROJECT_SOURCE_DIR}/platform/dts/rv32-virt.dts"
    DEPENDS
        platform/dts/rv32-virt.dts
    VERBATIM)

add_custom_target(
    rv32_device_tree
    ALL
    DEPENDS
        "${RV32_VIRT_DTB}")

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

if(RV32_ENABLE_SDL)
    find_path(
        RV32_SDL2_INCLUDE_DIR
        NAMES SDL.h
        HINTS
            "C:/msys64/ucrt64/include"
        PATH_SUFFIXES SDL2)
    find_library(
        RV32_SDL2_LIBRARY
        NAMES SDL2
        HINTS
            "C:/msys64/ucrt64/lib")
    if(NOT RV32_SDL2_INCLUDE_DIR OR NOT RV32_SDL2_LIBRARY)
        message(
            FATAL_ERROR
            "M7 requires the SDL2 development library")
    endif()

    add_library(
        rv32_app
        app/src/sdl_frontend.cpp
        app/src/terminal_console.cpp)

    target_include_directories(
        rv32_app
        PUBLIC
            ${PROJECT_SOURCE_DIR}/app/include
        PRIVATE
            ${RV32_SDL2_INCLUDE_DIR})
    target_compile_definitions(
        rv32_app
        PRIVATE
            SDL_MAIN_HANDLED)
    target_link_libraries(
        rv32_app
        PUBLIC
            rv32_platform
        PRIVATE
            ${RV32_SDL2_LIBRARY})
    rv32_enable_warnings(rv32_app)
endif()

add_executable(
    rv32_emulator
    app/main.cpp)

if(RV32_ENABLE_SDL)
    target_link_libraries(rv32_emulator PRIVATE rv32_app)
    target_compile_definitions(
        rv32_emulator
        PRIVATE
            RV32_ENABLE_SDL=1)
else()
    target_link_libraries(rv32_emulator PRIVATE rv32_platform)
endif()
rv32_enable_warnings(rv32_emulator)
add_dependencies(rv32_emulator rv32_device_tree)

if(WIN32 AND RV32_ENABLE_SDL)
    find_file(
        RV32_SDL2_RUNTIME
        NAMES SDL2.dll
        HINTS
            "C:/msys64/ucrt64/bin")
    if(NOT RV32_SDL2_RUNTIME)
        message(
            FATAL_ERROR
            "M7 requires SDL2.dll at runtime")
    endif()
    add_custom_command(
        TARGET rv32_emulator
        POST_BUILD
        COMMAND
            ${CMAKE_COMMAND} -E copy_if_different
            "${RV32_SDL2_RUNTIME}"
            "$<TARGET_FILE_DIR:rv32_emulator>"
        VERBATIM)
endif()

if(RV32_BUILD_TESTS)
    enable_testing()

    find_program(
        RV32_FDTGET
        NAMES fdtget
        HINTS
            "C:/msys64/ucrt64/bin")
    if(NOT RV32_FDTGET)
        message(
            FATAL_ERROR
            "M5 tests require fdtget")
    endif()

    add_test(
        NAME rv32_emulator_smoke
        COMMAND rv32_emulator)

    add_test(
        NAME rv32_device_tree_smoke
        COMMAND
            ${CMAKE_COMMAND}
            "-DDTB=${RV32_VIRT_DTB}"
            "-DFDTGET=${RV32_FDTGET}"
            -P
            "${PROJECT_SOURCE_DIR}/tests/cmake/verify_device_tree.cmake")

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
        rv32_compressed_extension_tests
        tests/compressed_extension_tests.cpp)

    target_link_libraries(
        rv32_compressed_extension_tests
        PRIVATE
            rv32_core)

    rv32_enable_warnings(rv32_compressed_extension_tests)

    add_test(
        NAME rv32_compressed_extension_tests
        COMMAND rv32_compressed_extension_tests)

    if(RV32_ENABLE_SDL)
        add_executable(
            rv32_terminal_console_tests
            tests/terminal_console_tests.cpp)

        target_link_libraries(
            rv32_terminal_console_tests
            PRIVATE
                rv32_app)
        rv32_enable_warnings(rv32_terminal_console_tests)

        add_test(
            NAME rv32_terminal_console_tests
            COMMAND rv32_terminal_console_tests)

        add_executable(
            rv32_sdl_frontend_tests
            tests/sdl_frontend_tests.cpp)

        target_include_directories(
            rv32_sdl_frontend_tests
            PRIVATE
                ${RV32_SDL2_INCLUDE_DIR})
        target_compile_definitions(
            rv32_sdl_frontend_tests
            PRIVATE
                SDL_MAIN_HANDLED)
        target_link_libraries(
            rv32_sdl_frontend_tests
            PRIVATE
                rv32_app
                ${RV32_SDL2_LIBRARY})
        rv32_enable_warnings(rv32_sdl_frontend_tests)

        add_test(
            NAME rv32_sdl_frontend_tests
            COMMAND rv32_sdl_frontend_tests)
    endif()

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
        set(
            RV32_COMPRESSED_ELF
            "${RV32_BAREMETAL_OUTPUT_DIR}/compressed-smoke.elf")
        set(
            RV32_COMPRESSED_BIN
            "${RV32_BAREMETAL_OUTPUT_DIR}/compressed-smoke.bin")
        set(
            RV32_BOOT_SMOKE_ELF
            "${RV32_BAREMETAL_OUTPUT_DIR}/boot_console.elf")
        set(
            RV32_BOOT_SMOKE_BIN
            "${RV32_BAREMETAL_OUTPUT_DIR}/boot_console.bin")

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

        add_custom_command(
            OUTPUT
                "${RV32_COMPRESSED_BIN}"
            BYPRODUCTS
                "${RV32_COMPRESSED_ELF}"
            COMMAND
                ${CMAKE_COMMAND} -E make_directory
                "${RV32_BAREMETAL_OUTPUT_DIR}"
            COMMAND
                "${RV32_BAREMETAL_GCC}"
                -march=rv32imac
                -mabi=ilp32
                -nostdlib
                -nostartfiles
                -mno-relax
                "-Wl,-T,${PROJECT_SOURCE_DIR}/tests/baremetal/link.ld"
                -Wl,--gc-sections
                -Wl,--no-relax
                -o
                "${RV32_COMPRESSED_ELF}"
                "${PROJECT_SOURCE_DIR}/tests/baremetal/compressed_smoke.S"
            COMMAND
                "${RV32_BAREMETAL_OBJCOPY}"
                -O
                binary
                "${RV32_COMPRESSED_ELF}"
                "${RV32_COMPRESSED_BIN}"
            DEPENDS
                tests/baremetal/link.ld
                tests/baremetal/compressed_smoke.S
            VERBATIM)

        add_custom_target(
            rv32_compressed_image
            ALL
            DEPENDS
                "${RV32_COMPRESSED_BIN}")

        add_custom_command(
            OUTPUT
                "${RV32_BOOT_SMOKE_BIN}"
            BYPRODUCTS
                "${RV32_BOOT_SMOKE_ELF}"
            COMMAND
                ${CMAKE_COMMAND} -E make_directory
                "${RV32_BAREMETAL_OUTPUT_DIR}"
            COMMAND
                "${RV32_BAREMETAL_GCC}"
                -march=rv32i
                -mabi=ilp32
                -nostdlib
                -nostartfiles
                "-Wl,-T,${PROJECT_SOURCE_DIR}/tests/baremetal/link.ld"
                -Wl,--no-relax
                -o
                "${RV32_BOOT_SMOKE_ELF}"
                "${PROJECT_SOURCE_DIR}/tests/boot/console_shutdown.S"
            COMMAND
                "${RV32_BAREMETAL_OBJCOPY}"
                -O
                binary
                "${RV32_BOOT_SMOKE_ELF}"
                "${RV32_BOOT_SMOKE_BIN}"
            DEPENDS
                tests/baremetal/link.ld
                tests/boot/console_shutdown.S
            VERBATIM)

        add_custom_target(
            rv32_boot_smoke_image
            ALL
            DEPENDS
                "${RV32_BOOT_SMOKE_BIN}")

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
            rv32_baremetal_image
            rv32_compressed_image)
        add_dependencies(
            rv32_emulator
            rv32_baremetal_image
            rv32_boot_smoke_image
            rv32_compressed_image)

        add_executable(
            rv32_architecture_runner
            tests/architecture/runner.cpp)
        target_link_libraries(
            rv32_architecture_runner
            PRIVATE
                rv32_platform)
        rv32_enable_warnings(rv32_architecture_runner)

        set(
            RV32_RISCV_TESTS_DIR
            "${PROJECT_SOURCE_DIR}/third_party/riscv-tests")
        set(
            RV32_ARCHITECTURE_OUTPUT_DIR
            "${CMAKE_CURRENT_BINARY_DIR}/architecture")
        file(
            GLOB_RECURSE
            RV32_ARCHITECTURE_SUPPORT_FILES
            CONFIGURE_DEPENDS
            "${RV32_RISCV_TESTS_DIR}/env/*"
            "${RV32_RISCV_TESTS_DIR}/isa/*")
        list(
            APPEND
            RV32_ARCHITECTURE_SUPPORT_FILES
            "${PROJECT_SOURCE_DIR}/tests/architecture/link.ld")
        if(CMAKE_GENERATOR MATCHES "Ninja")
            set_property(
                GLOBAL
                APPEND
                PROPERTY JOB_POOLS
                rv32_cross_compile_pool=4)
            set(
                RV32_ARCHITECTURE_JOB_POOL
                JOB_POOL
                rv32_cross_compile_pool)
        endif()

        set(
            RV32I_ARCHITECTURE_TESTS
            simple
            add addi
            and andi
            auipc
            beq bge bgeu blt bltu bne
            fence_i
            jal jalr
            lb lbu ld_st lh lhu lw
            lui
            or ori
            sb sh sw st_ld
            sll slli
            slt slti sltiu sltu
            sra srai
            srl srli
            sub
            xor xori)
        set(
            RV32M_ARCHITECTURE_TESTS
            div divu
            mul mulh mulhsu mulhu
            rem remu)
        set(
            RV32A_ARCHITECTURE_TESTS
            amoadd_w amoand_w
            amomax_w amomaxu_w
            amomin_w amominu_w
            amoor_w amoxor_w
            amoswap_w
            lrsc)
        set(
            RV32C_ARCHITECTURE_TESTS
            rvc)

        function(rv32_add_architecture_tests suite)
            foreach(test_name IN LISTS ARGN)
                set(
                    test_elf
                    "${RV32_ARCHITECTURE_OUTPUT_DIR}/${suite}-${test_name}.elf")
                set(
                    test_binary
                    "${RV32_ARCHITECTURE_OUTPUT_DIR}/${suite}-${test_name}.bin")
                set(
                    test_source
                    "${RV32_RISCV_TESTS_DIR}/isa/${suite}/${test_name}.S")

                add_custom_command(
                    OUTPUT
                        "${test_binary}"
                    BYPRODUCTS
                        "${test_elf}"
                    COMMAND
                        ${CMAKE_COMMAND} -E make_directory
                        "${RV32_ARCHITECTURE_OUTPUT_DIR}"
                    COMMAND
                        "${RV32_BAREMETAL_GCC}"
                        -march=rv32imac_zicsr_zifencei
                        -mabi=ilp32
                        -static
                        -mcmodel=medany
                        -fvisibility=hidden
                        -nostdlib
                        -nostartfiles
                        -mno-relax
                        "-I${RV32_RISCV_TESTS_DIR}/env/p"
                        "-I${RV32_RISCV_TESTS_DIR}/isa/macros/scalar"
                        "-T${PROJECT_SOURCE_DIR}/tests/architecture/link.ld"
                        -Wl,--no-relax
                        "${test_source}"
                        -o
                        "${test_elf}"
                    COMMAND
                        "${RV32_BAREMETAL_OBJCOPY}"
                        -O
                        binary
                        "${test_elf}"
                        "${test_binary}"
                    DEPENDS
                        "${test_source}"
                        ${RV32_ARCHITECTURE_SUPPORT_FILES}
                    ${RV32_ARCHITECTURE_JOB_POOL}
                    VERBATIM)

                list(
                    APPEND
                    RV32_ARCHITECTURE_BINARIES
                    "${test_binary}")
                add_test(
                    NAME "rv32_arch_${suite}_${test_name}"
                    COMMAND
                        rv32_architecture_runner
                        "${test_binary}")
                set_tests_properties(
                    "rv32_arch_${suite}_${test_name}"
                    PROPERTIES
                        LABELS "architecture;rv32imac"
                        TIMEOUT 20)
            endforeach()
            set(
                RV32_ARCHITECTURE_BINARIES
                "${RV32_ARCHITECTURE_BINARIES}"
                PARENT_SCOPE)
        endfunction()

        rv32_add_architecture_tests(
            rv32ui
            ${RV32I_ARCHITECTURE_TESTS})
        rv32_add_architecture_tests(
            rv32um
            ${RV32M_ARCHITECTURE_TESTS})
        rv32_add_architecture_tests(
            rv32ua
            ${RV32A_ARCHITECTURE_TESTS})
        rv32_add_architecture_tests(
            rv32uc
            ${RV32C_ARCHITECTURE_TESTS})

        add_custom_target(
            rv32_architecture_images
            ALL
            DEPENDS
                ${RV32_ARCHITECTURE_BINARIES})
        add_dependencies(
            rv32_architecture_runner
            rv32_architecture_images)

        find_package(
            Python3
            COMPONENTS Interpreter
            QUIET)
        if(Python3_Interpreter_FOUND)
            add_test(
                NAME rv32_spike_trace_parser_tests
                COMMAND
                    "${Python3_EXECUTABLE}"
                    "${PROJECT_SOURCE_DIR}/tests/differential/compare_spike.py"
                    --self-test)
            set_tests_properties(
                rv32_spike_trace_parser_tests
                PROPERTIES
                    LABELS "differential")

            find_program(
                RV32_SPIKE_EXECUTABLE
                NAMES spike)
            if(RV32_SPIKE_EXECUTABLE)
                function(
                    rv32_add_spike_differential_test
                    suite
                    test_name)
                    set(
                        differential_name
                        "rv32_spike_differential_${suite}_${test_name}")
                    set(
                        differential_stem
                        "${suite}-${test_name}")
                    add_test(
                        NAME "${differential_name}"
                        COMMAND
                            "${Python3_EXECUTABLE}"
                            "${PROJECT_SOURCE_DIR}/tests/differential/compare_spike.py"
                            --dut
                            "$<TARGET_FILE:rv32_architecture_runner>"
                            --binary
                            "${RV32_ARCHITECTURE_OUTPUT_DIR}/${differential_stem}.bin"
                            --elf
                            "${RV32_ARCHITECTURE_OUTPUT_DIR}/${differential_stem}.elf"
                            --spike
                            "${RV32_SPIKE_EXECUTABLE}"
                            --allow-spike-tail)
                    set_tests_properties(
                        "${differential_name}"
                        PROPERTIES
                            LABELS "differential"
                            TIMEOUT 45)
                endfunction()

                rv32_add_spike_differential_test(rv32ui simple)
                rv32_add_spike_differential_test(rv32ui add)
                rv32_add_spike_differential_test(rv32um mul)
                rv32_add_spike_differential_test(rv32ua amoadd_w)
                rv32_add_spike_differential_test(rv32uc rvc)
            else()
                message(
                    STATUS
                    "Spike not found; commit-trace parser is tested, "
                    "but live differential testing is disabled")
            endif()
        else()
            message(
                STATUS
                "Python 3 not found; optional Spike differential tests "
                "are disabled")
        endif()

        add_test(
            NAME rv32_baremetal_smoke
            COMMAND
                rv32_baremetal_runner
                "${RV32_BAREMETAL_BIN}")

        add_test(
            NAME rv32_compressed_baremetal_smoke
            COMMAND
                rv32_baremetal_runner
                "${RV32_COMPRESSED_BIN}")

        add_test(
            NAME rv32_boot_image_loader_smoke
            COMMAND
                rv32_emulator
                --load-images
                "${RV32_BAREMETAL_BIN}"
                "${RV32_BAREMETAL_BIN}"
                "${RV32_VIRT_DTB}")

        add_test(
            NAME rv32_boot_run_smoke
            COMMAND
                rv32_emulator
                --boot
                "${RV32_BOOT_SMOKE_BIN}"
                "${RV32_BAREMETAL_BIN}"
                "${RV32_VIRT_DTB}"
                100)
        set_tests_properties(
            rv32_boot_run_smoke
            PROPERTIES
                PASS_REGULAR_EXPRESSION "OK")

        if(RV32_ENABLE_SDL)
            add_test(
                NAME rv32_gui_boot_smoke
                COMMAND
                    rv32_emulator
                    --gui
                    --boot
                    "${RV32_BOOT_SMOKE_BIN}"
                    "${RV32_BAREMETAL_BIN}"
                    "${RV32_VIRT_DTB}"
                    100)
            set_tests_properties(
                rv32_gui_boot_smoke
                PROPERTIES
                    ENVIRONMENT
                        "SDL_VIDEODRIVER=dummy;SDL_RENDER_DRIVER=software"
                    PASS_REGULAR_EXPRESSION "OK")
        endif()

        set(
            RV32_TEST_DISK
            "${CMAKE_CURRENT_BINARY_DIR}/test-disk.img")
        string(REPEAT "0" 512 RV32_TEST_DISK_CONTENT)
        file(
            WRITE
            "${RV32_TEST_DISK}"
            "${RV32_TEST_DISK_CONTENT}")

        add_test(
            NAME rv32_boot_disk_smoke
            COMMAND
                rv32_emulator
                --boot-disk
                "${RV32_BOOT_SMOKE_BIN}"
                "${RV32_BAREMETAL_BIN}"
                "${RV32_VIRT_DTB}"
                "${RV32_TEST_DISK}"
                100)
        set_tests_properties(
            rv32_boot_disk_smoke
            PROPERTIES
                PASS_REGULAR_EXPRESSION
                    "Virtual disk loaded")
    endif()
endif()
