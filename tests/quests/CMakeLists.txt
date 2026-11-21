cmake_minimum_required(VERSION 3.20)
project(FaxanaduQuestTests C)
set(CMAKE_C_STANDARD 11)
# fax_quest_table.c is plain data and pure functions: the test links it as it
# is, and reads the scripts from the ROM given as its argument.
add_executable(quests_test quests_test.c ../../src/fax_quest_table.c)
target_include_directories(quests_test PRIVATE ../../src)
if(NOT MSVC)
    target_compile_options(quests_test PRIVATE -Wall -Wextra)
endif()
enable_testing()
add_test(NAME quests_unit COMMAND quests_test)
