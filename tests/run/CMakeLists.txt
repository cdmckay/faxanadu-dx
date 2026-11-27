cmake_minimum_required(VERSION 3.20)
project(FaxanaduRunTests C)
set(CMAKE_C_STANDARD 11)
if(NOT NESRECOMP_ROOT)
    message(FATAL_ERROR "Set NESRECOMP_ROOT to the game's cycle engine checkout")
endif()
# fax_run.c is included by the test, against tests/movement/model.h, a model
# of the machine and the hero's update. The engine's cyc_input.c and SDL name
# its keys and buttons, and give the window events it reads.
find_package(SDL2 REQUIRED CONFIG)
add_executable(run_test run_test.c ${NESRECOMP_ROOT}/runner/cyc/cyc_input.c)
target_include_directories(run_test PRIVATE ../../src ${NESRECOMP_ROOT}/runner/include ${NESRECOMP_ROOT}/runner/cyc)
target_link_libraries(run_test PRIVATE SDL2::SDL2)
target_compile_definitions(run_test PRIVATE SDL_MAIN_HANDLED)
if(NOT MSVC)
    target_compile_options(run_test PRIVATE -Wall -Wextra)
endif()
enable_testing()
add_test(NAME run_unit COMMAND run_test)
