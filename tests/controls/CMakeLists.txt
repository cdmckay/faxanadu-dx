cmake_minimum_required(VERSION 3.20)
project(FaxanaduControlsTests C)
set(CMAKE_C_STANDARD 11)
if(NOT NESRECOMP_ROOT)
    message(FATAL_ERROR "Set NESRECOMP_ROOT to the game's cycle engine checkout")
endif()
# fax_controls.c is included by the test, against a model of the machine. The
# engine's config.ini code (cyc_settings.c, cyc_input.c) reads an old file,
# and SDL names keys and buttons and makes a virtual controller.
find_package(SDL2 REQUIRED CONFIG)
add_executable(controls_test controls_test.c
    ${NESRECOMP_ROOT}/runner/cyc/cyc_settings.c ${NESRECOMP_ROOT}/runner/cyc/cyc_input.c)
target_include_directories(controls_test PRIVATE ../../src ${NESRECOMP_ROOT}/runner/include ${NESRECOMP_ROOT}/runner/cyc)
target_link_libraries(controls_test PRIVATE SDL2::SDL2)
target_compile_definitions(controls_test PRIVATE SDL_MAIN_HANDLED)
if(NOT MSVC)
    target_compile_options(controls_test PRIVATE -Wall -Wextra)
endif()
enable_testing()
add_test(NAME controls_unit COMMAND controls_test ${CMAKE_CURRENT_BINARY_DIR})
