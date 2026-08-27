cmake_minimum_required(VERSION 3.24)

project(EMU1820MNext VERSION 0.1.0 LANGUAGES CXX)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "EMU1820M-Next is an x64-only driver and host stack")
endif()

option(EMU_BUILD_TESTS "Build hardware-independent realtime tests" ON)
option(EMU_BUILD_ENGINE_PROBE "Build the CI-only engine probe" ON)

add_library(emu_realtime_core STATIC
    core/src/realtime_contract.cpp
    core/src/transport_contract.cpp
)

target_include_directories(emu_realtime_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/core/include
)

target_compile_features(emu_realtime_core PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(emu_realtime_core PRIVATE /W4 /WX /permissive- /EHsc)
else()
    target_compile_options(emu_realtime_core PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

if(EMU_BUILD_ENGINE_PROBE)
    add_executable(emu_engine_probe tools/engine_probe/main.cpp)
    target_link_libraries(emu_engine_probe PRIVATE emu_realtime_core)
    target_compile_features(emu_engine_probe PRIVATE cxx_std_20)

    if(MSVC)
        target_compile_options(emu_engine_probe PRIVATE /W4 /WX /permissive- /EHsc)
    else()
        target_compile_options(emu_engine_probe PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
endif()

if(EMU_BUILD_TESTS)
    enable_testing()
    find_package(Threads REQUIRED)

    add_executable(emu_core_tests tests/core_tests.cpp)
    target_link_libraries(emu_core_tests PRIVATE emu_realtime_core Threads::Threads)
    target_compile_features(emu_core_tests PRIVATE cxx_std_20)

    if(MSVC)
        target_compile_options(emu_core_tests PRIVATE /W4 /WX /permissive- /EHsc)
    else()
        target_compile_options(emu_core_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()

    add_test(NAME emu_core_tests COMMAND emu_core_tests)

    add_executable(emu_transport_tests tests/transport_tests.cpp)
    target_link_libraries(emu_transport_tests PRIVATE emu_realtime_core Threads::Threads)
    target_compile_features(emu_transport_tests PRIVATE cxx_std_20)

    if(MSVC)
        target_compile_options(emu_transport_tests PRIVATE /W4 /WX /permissive- /EHsc)
    else()
        target_compile_options(emu_transport_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()

    add_test(NAME emu_transport_tests COMMAND emu_transport_tests)
endif()
