cmake_minimum_required(VERSION 3.20)
project(FaxanaduRecomp C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
option(NESRECOMP_ENABLE_TRACE "Enable game diagnostics" OFF)
# compile_commands.json, for tools/lint.sh and editors.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# The bug-catching builds (docs/C_TOOLING.md). FAX_SANITIZE builds the whole
# game, the engine and the recompiled code with it, under AddressSanitizer and
# UndefinedBehaviorSanitizer: tools/test.sh --sanitize plays it, in build-asan.
# FAX_WERROR makes a warning in the repo's own C an error: tools/test.sh turns
# it on in its builds, so the gate fails on one, while a player's build, with
# whatever compiler, only warns.
option(FAX_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(FAX_WERROR "Treat warnings in the repo's own C (src/) as errors" OFF)
set(FAX_SANITIZE_RPATH "$ENV{FAX_SANITIZE_RPATH}" CACHE PATH "SDL3's folder, an rpath of the sanitized game")
if(FAX_SANITIZE)
    if(MSVC)
        message(FATAL_ERROR "FAX_SANITIZE needs clang or gcc")
    endif()
    # Under AddressSanitizer, sdl2-compat finds SDL3 only by the game's own
    # rpath, and on macOS a dialog says so when it does not (flake.nix).
    if(NOT EXISTS "${FAX_SANITIZE_RPATH}")
        message(FATAL_ERROR "FAX_SANITIZE needs FAX_SANITIZE_RPATH, SDL3's folder: configure in the Nix devShell")
    endif()
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
    list(APPEND CMAKE_BUILD_RPATH "${FAX_SANITIZE_RPATH}")
endif()

set(NESRECOMP_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/nesrecomp" CACHE PATH "NESRecomp engine checkout")

# nesrecomp/ and recomp-ui/ are git submodules (see .gitmodules); the pinned
# commit is the gitlink recorded in this repo. After cloning run setup.sh
# (git submodule update --init) to fetch them at the recorded commits.

set(NESRECOMP_BACKEND "cycle" CACHE STRING "CPU backend: cycle or legacy")
set_property(CACHE NESRECOMP_BACKEND PROPERTY STRINGS cycle legacy)
if(NESRECOMP_BACKEND STREQUAL "cycle")
    set(NESRECOMP_ROM "" CACHE FILEPATH "Original ROM image (not distributed)")
    set(NESRECOMP_CYCLE_SEEDS "${CMAKE_CURRENT_SOURCE_DIR}/cyc_seeds.txt" CACHE FILEPATH "Cycle coverage seeds")
    set(NESRECOMP_HOST_COMPILER "" CACHE FILEPATH "Optional prebuilt host compiler")
    if(NOT EXISTS "${NESRECOMP_ROM}")
        message(FATAL_ERROR "Set NESRECOMP_ROM to the original Faxanadu ROM")
    endif()
    find_package(Python3 3.11 REQUIRED COMPONENTS Interpreter)
    execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/verify_usa_rom.py"
        --rom "${NESRECOMP_ROM}" --crc 57DD23D1 --mapper 1 --title "Faxanadu"
        RESULT_VARIABLE _usa_rom_result)
    if(NOT _usa_rom_result EQUAL 0)
        message(FATAL_ERROR "The North American ROM profile is required for this build")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/verify_usa_rom.py" "${NESRECOMP_ROM}")
    include("${NESRECOMP_ROOT}/runner/cyc/project.cmake")
    nesrecomp_add_cycle_game(FaxanaduRecomp
        ROM "${NESRECOMP_ROM}"
        GAME_CONFIG "${CMAKE_CURRENT_SOURCE_DIR}/game.toml"
        RECOMPILER "${NESRECOMP_HOST_COMPILER}"
        SEED_FILE "${NESRECOMP_CYCLE_SEEDS}"
        HOST_EXTRAS src/cyc_extras.c src/fax_world.c src/fax_camera.c src/fax_travel.c src/fax_death.c
            src/fax_pendant.c
            src/fax_magic.c
            src/fax_zone.c src/fax_text.c src/fax_banner.c
            src/fax_speaker.c
            src/fax_keys.c
            src/fax_falling.c src/fax_jumps.c
            src/fax_ladders.c src/fax_ladder_hold.c
            src/fax_text_speed.c
            src/fax_save.c src/fax_files.c
            src/fax_title.c
            src/fax_dagger.c
            src/fax_block.c src/fax_remember.c src/fax_sugata.c
            src/fax_shops.c
            src/fax_respawn.c
            src/fax_recharge.c src/fax_charges.c
            src/fax_item_box.c
            src/fax_mana.c src/fax_lesson.c
            src/fax_gold.c
            src/fax_slain.c
            src/fax_indoor.c
            src/fax_damage.c
            src/fax_inventory.c
            src/fax_fixes.c
            src/fax_map_layout.c src/fax_map.c
            src/fax_quest_table.c src/fax_quests.c
            src/fax_graphics.c
            src/fax_flicker.c
            src/fax_finds.c
            src/fax_shaft.c
            src/fax_entrances.c
            src/fax_spell_cost.c
            src/fax_found.c
            src/fax_menu.c
            src/fax_controls.c src/fax_use_item.c
            src/fax_seams.c
            src/fax_punch.c
        MODS GAME_ID "faxanadu-usa"
        PLAYERS 1 REGION NTSC
        BOXART "${CMAKE_CURRENT_SOURCE_DIR}/assets/boxart.tga")
    target_sources(FaxanaduRecomp PRIVATE src/override_text.c
        "${NESRECOMP_ROOT}/runner/src/override_chr.c"
        "${NESRECOMP_ROOT}/runner/src/chr_codec.c")
    target_include_directories(FaxanaduRecomp PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${NESRECOMP_ROOT}/runner/include" "${NESRECOMP_ROOT}/runner/src")
    target_compile_definitions(FaxanaduRecomp PRIVATE CYC_CONTENT_TOOLS)
    # Warnings on the C this repo owns, every src/ file the game builds, not
    # the engine's or the recompiled code's (docs/C_TOOLING.md).
    if(NOT MSVC)
        get_target_property(FAX_OWN_SOURCES FaxanaduRecomp SOURCES)
        list(FILTER FAX_OWN_SOURCES INCLUDE REGEX "^src/")
        set(FAX_WARNINGS -Wall -Wextra -Wshadow -Wimplicit-fallthrough -Wmissing-prototypes
            -Wstrict-prototypes -Wformat=2 -Wundef -Wcast-qual -Wwrite-strings -Wpointer-arith)
        if(CMAKE_C_COMPILER_ID MATCHES "Clang")
            list(APPEND FAX_WARNINGS -Wconditional-uninitialized -Wmissing-variable-declarations
                -Wshorten-64-to-32 -Wunreachable-code -Wloop-analysis)
        endif()
        if(FAX_WERROR)
            list(APPEND FAX_WARNINGS -Werror)
        endif()
        set_source_files_properties(${FAX_OWN_SOURCES} PROPERTIES COMPILE_OPTIONS "${FAX_WARNINGS}")
        # gcc takes fax_world.c's statements after an if, on its line, for
        # ones it guards: until that file is formatted (.clang-format), not
        # there.
        if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
            set_property(SOURCE src/fax_world.c APPEND PROPERTY COMPILE_OPTIONS -Wno-misleading-indentation)
        endif()
    endif()
    # The remaster's features were packages of their own before they became
    # one package (#56), and so was Seamless until it joined it (#110). That
    # package was faxanadu.remastered until the project became Faxanadu DX
    # (#116). A build folder from then still holds them, and they would claim
    # its plugins a second time.
    set(FAX_OLD_PACKAGES "$<TARGET_FILE_DIR:FaxanaduRecomp>/mods/packages/faxanadu.remastered")
    foreach(old auto-keys block death-keeps-gold falling-pose forgiving-jumps guru-travel magic-button
            pendant-fix quick-shops recharging-items reusable-tools save-files slain-by speaker-names
            sugata-flash taller-dagger text-speed title-remastered zone-names zone-respawn indoor-equip
            seamless-world)
        list(APPEND FAX_OLD_PACKAGES "$<TARGET_FILE_DIR:FaxanaduRecomp>/mods/packages/faxanadu.enhancement.${old}")
    endforeach()
    add_custom_command(TARGET FaxanaduRecomp POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E rm -rf ${FAX_OLD_PACKAGES}
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/mods/preloaded" "$<TARGET_FILE_DIR:FaxanaduRecomp>/mods")
    return()
elseif(NOT NESRECOMP_BACKEND STREQUAL "legacy")
    message(FATAL_ERROR "NESRECOMP_BACKEND must be cycle or legacy")
endif()

include(${NESRECOMP_ROOT}/runner/runner.cmake)

list(APPEND CMAKE_PREFIX_PATH "${NESRECOMP_ROOT}/runner/external/SDL2/cmake")
find_package(SDL2 REQUIRED)

# ---- Main executable ----
add_executable(FaxanaduRecomp
    ${NESRECOMP_RUNNER_SOURCES}
    src/extras.c
    src/override_text.c   # optional text override plugin — game-specific strings in extras.c
    ${NESRECOMP_RUNNER_ROOT}/src/override_chr.c   # CHR asset dump + override plugin
    ${NESRECOMP_RUNNER_ROOT}/src/chr_codec.c       # NES 2bpp CHR <-> PNG codec
    src/nes_snapshot.c
    src/verify_mode.c
    src/watchdog.c
    generated/faxanadu_full.c
    generated/faxanadu_dispatch.c
)

target_compile_definitions(FaxanaduRecomp PRIVATE RECOMP_STACK_TRACKING WATCHDOG_ENABLED)

target_include_directories(FaxanaduRecomp PRIVATE
    ${NESRECOMP_RUNNER_INCLUDE_DIRS}
    ${NESRECOMP_RUNNER_ROOT}/src          # for chr_codec.h, stb_image.h
    ${CMAKE_SOURCE_DIR}/src
    ${SDL2_INCLUDE_DIRS}
)

target_link_libraries(FaxanaduRecomp PRIVATE ${SDL2_LIBRARIES})

if(WIN32)
    target_link_libraries(FaxanaduRecomp PRIVATE ws2_32)
endif()

# ---- Post-build ----
if(WIN32)
    add_custom_command(TARGET FaxanaduRecomp POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${NESRECOMP_ROOT}/runner/external/SDL2/lib/x64/SDL2.dll"
            "$<TARGET_FILE_DIR:FaxanaduRecomp>"
    )
endif()

if(MSVC)
    target_compile_options(FaxanaduRecomp PRIVATE /W3 /WX-)
    target_compile_options(FaxanaduRecomp PRIVATE /wd4102)
endif()

# ---- Pre-boot GUI launcher (recomp-ui / Dear ImGui) ----
# recomp-ui/ is a directory junction -> the shared recomp-ui checkout. Its
# module defines RECOMP_LAUNCHER on the target, un-gating the launcher block in
# launcher.c's main(), and stages the launcher assets next to the exe.
set(NESRECOMP_RECOMP_UI "${CMAKE_CURRENT_SOURCE_DIR}/recomp-ui" CACHE PATH "recomp-ui checkout")
include("${NESRECOMP_RECOMP_UI}/recomp_ui.cmake")
recomp_target_launcher_ui(FaxanaduRecomp CONSOLE nes BOXART ${CMAKE_SOURCE_DIR}/assets/boxart.tga)

# Faxanadu saves via a password (mantra), not battery SRAM. Drive the launcher's
# SAVES panel to show/edit that password (the faxanadu.srm sidecar next to the exe
# that the runtime auto-prefill reads/writes) instead of the binary SRAM UI.
target_compile_definitions(FaxanaduRecomp PRIVATE
    NESRECOMP_GAME_PASSWORD_SAVE="faxanadu.srm"
    NESRECOMP_GAME_PASSWORD_SAVE_LABEL="Mantra")

# Single-player (one controller) — hide the launcher's Player 2 card.
target_compile_definitions(FaxanaduRecomp PRIVATE NESRECOMP_GAME_PLAYERS=1)
