cmake_minimum_required(VERSION 3.16)

project(rv32_cpu_base LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_library(rv32_core
    src/cpu.c
    src/decode.c
    src/dmem.c
    src/execute.c
    src/imem.c
)
target_include_directories(rv32_core PUBLIC include)

if(MSVC)
    target_compile_options(rv32_core PRIVATE /W4)
else()
    target_compile_options(rv32_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(rv32_cpu src/main.c)
target_link_libraries(rv32_cpu PRIVATE rv32_core)

enable_testing()

add_executable(test_decode tests/test_decode.c)
target_link_libraries(test_decode PRIVATE rv32_core)
add_test(NAME decode COMMAND test_decode)

add_executable(test_memory tests/test_memory.c)
target_link_libraries(test_memory PRIVATE rv32_core)
add_test(NAME memory COMMAND test_memory)

add_executable(test_step tests/test_step.c)
target_link_libraries(test_step PRIVATE rv32_core)
add_test(NAME step COMMAND test_step)
