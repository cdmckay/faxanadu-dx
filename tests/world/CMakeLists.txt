cmake_minimum_required(VERSION 3.20)
project(FaxanaduWorldPortableCompile C)
set(NESRECOMP_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../nesrecomp" CACHE PATH "Cycle engine checkout")
add_library(fax_world_no_sdl OBJECT ../../src/fax_world.c)
set_property(TARGET fax_world_no_sdl PROPERTY C_STANDARD 11)
target_include_directories(fax_world_no_sdl PRIVATE
    "${NESRECOMP_ROOT}/runner/cyc" "${NESRECOMP_ROOT}/runner/include")
# The host's fixed scale for Seamless's view (#107): the engine's own unit test
# of its fit and sharp-scale maths (cyc_present_fit.h), run by tools/test.sh.
add_executable(present_fit_test "${NESRECOMP_ROOT}/runner/cyc/present_fit_test.c")
set_property(TARGET present_fit_test PROPERTY C_STANDARD 11)
target_include_directories(present_fit_test PRIVATE "${NESRECOMP_ROOT}/runner/cyc")
if(NOT MSVC)
    target_link_libraries(present_fit_test PRIVATE m)
endif()
