cmake_minimum_required(VERSION 3.20)
project(FaxanaduSugataTests C)
set(CMAKE_C_STANDARD 11)
if(NOT NESRECOMP_ROOT)
    message(FATAL_ERROR "Set NESRECOMP_ROOT to the game's cycle engine checkout")
endif()
# fax_sugata.c is included by the test and fax_block.c by block_link.c (with
# fax_remember.c, where Block remembers its tutorial), against
# a model of the machine (../block/model.c).
add_executable(sugata_test sugata_test.c block_link.c ../block/model.c ../../src/fax_remember.c)
target_include_directories(sugata_test PRIVATE ../block ../../src ${NESRECOMP_ROOT}/runner/include ${NESRECOMP_ROOT}/runner/cyc)
if(NOT MSVC)
    target_compile_options(sugata_test PRIVATE -Wall -Wextra)
endif()
enable_testing()
add_test(NAME sugata_unit COMMAND sugata_test)
