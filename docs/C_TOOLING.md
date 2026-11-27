# C tooling: what catches bugs in the repo's C

The C this repo owns, `src/` and `tests/`, goes through four checks: three
in the gate (`tools/test.sh`), the fourth a flag of it. Only the sanitized
game takes in the nesrecomp and recomp-ui submodules and the recompiled code
as well.

| Check | On | In the gate |
|---|---|---|
| Warnings, as errors | every `src/` file the game builds | yes: the game's build fails |
| clang-tidy (`tools/lint.sh`) | the same files | yes: `== lint` |
| AddressSanitizer and UBSan | every unit test | yes: the unit tests build that way |
| AddressSanitizer and UBSan | the whole game, in `build-asan` | opt-in: `tools/test.sh --sanitize` |

A `.clang-format` sets the style for the repo's C, but nothing is formatted
to it yet and the gate does not check it: a reformat of the whole tree comes
in a PR of its own. Until then, leave the formatting of the lines you do not
change as it is.

## Sanitizers

AddressSanitizer stops a program at a read or write out of bounds (heap,
stack or global), a use after free or a double free, with a report of where
it happened and where the memory came from. UndefinedBehaviorSanitizer
reports undefined behaviour as it happens: signed overflow, a shift too far,
an index out of an array's bounds, a misaligned or null pointer, a bad enum
or bool.

- **Unit tests** build with both, always (`unit()` in `tools/test.sh`), and
  stop at the first report: the test fails with it in the output. They cost
  nothing measurable, so they are the default.
- **The game** builds with both in `build-asan` (`-DFAX_SANITIZE=ON`, at
  `-O2 -g`), engine and recompiled code included. `tools/test.sh --sanitize`
  builds it and plays it instead of `build-cycle` in every end-to-end run and
  the headless smoke run. An AddressSanitizer report stops that game, and its
  test fails; a UBSan report lets it play on, so one suite finds every place.
  Every report goes to a file in `build-tests/sanitizer-reports/`, and the
  suite fails if there are any, listing each place and how many games hit
  it. The sanitized games keep fixtures of their own, in
  `build-tests/fixtures-asan` (`FAX_E2E_FIXTURES`): making a fixture with one
  build throws away the other's. The suite takes 1.4 to 1.7 times as long
  (the more when it makes its fixtures afresh, as after any change to the
  game), so it is opt-in: run it before a PR that changes how `src/` reads
  or writes memory (buffers, records, save states, anything indexed by what
  the game holds).

To build and play the sanitized game by hand:

```bash
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=RelWithDebInfo -DFAX_SANITIZE=ON \
  -DCMAKE_C_COMPILER="$FAX_SANITIZE_CC" -DCMAKE_CXX_COMPILER="$FAX_SANITIZE_CXX" \
  -DNESRECOMP_ROM="$PWD/Faxanadu (USA).nes"
cmake --build build-asan -j8
build-asan/FaxanaduRecomp "Faxanadu (USA).nes" --frames 600 --no-save
FAX_E2E_FIXTURES=build-tests/fixtures-asan python3 tests/punches/punches.py \
  --build build-asan --rom "Faxanadu (USA).nes" --out build-tests/punches-asan
```

The devShell sets up three things the sanitizers need (`flake.nix` says more):

- **clang 22** (`$FAX_SANITIZE_CC`, `$FAX_SANITIZE_CXX`), on every machine.
  The devShell's own clang 21 builds everything else, but its
  AddressSanitizer runtime hangs as any program starts on macOS 26
  ([llvm/llvm-project#200447](https://github.com/llvm/llvm-project/issues/200447)).
- **Two of Nix's hardening flags off** in that compiler: `fortify` stops a
  program at an overflow with a bare trap before AddressSanitizer can say
  where, and `strictoverflow` (`-fno-strict-overflow`) defines signed
  overflow, so UBSan never reports it.
- **SDL3's folder as an rpath** (`$FAX_SANITIZE_RPATH`). sdl2-compat loads
  SDL3 by its own rpath, but AddressSanitizer's `dlopen` takes its runtime
  for the caller, so the search finds nothing; on macOS sdl2-compat then
  shows an error dialog on screen and waits. Every sanitized program has
  SDL3's folder as an rpath of its own, and the build refuses to configure
  without it.

Leaks go unchecked (`detect_leaks=0`): LeakSanitizer finds none on macOS,
and the gate should pass or fail the same on every machine. Valgrind does not
run on Apple Silicon (nixpkgs marks it broken there), and AddressSanitizer
covers what it would.

If a sanitizer reports something in nesrecomp or the recompiled code, file
an issue, and suppress that one place narrowly so the gate passes meanwhile:
UBSan takes a suppressions file in `UBSAN_OPTIONS` for the game, whose UBSan
checks recover. None is needed today.

## Warnings

`CMakeLists.txt` compiles every `src/` file the game builds with
`FAX_WARNINGS`: `-Wall -Wextra -Wshadow -Wimplicit-fallthrough
-Wmissing-prototypes -Wstrict-prototypes -Wformat=2 -Wundef -Wcast-qual
-Wwrite-strings -Wpointer-arith`, and on clang `-Wconditional-uninitialized
-Wmissing-variable-declarations -Wshorten-64-to-32 -Wunreachable-code
-Wloop-analysis`. The engine and the recompiled code keep their own flags.
On gcc, `src/fax_world.c` goes without `-Wmisleading-indentation` until it
is formatted: its dense lines put a statement after an `if` on the `if`'s
line.

`tools/test.sh` configures its builds with `FAX_WERROR=ON`, so a warning in
`src/` fails the gate. A plain build only warns: a player's compiler, newer
or another, may warn where ours does not, and that must not stop the build.
The unit tests compile with `-Wall -Wextra` (each `tests/*/CMakeLists.txt`).

## clang-tidy

`tools/lint.sh` runs clang-tidy on every `src/` file the game builds, with
the compile commands of `build-cycle` (`compile_commands.json`, which the
build writes) and the checks in `.clang-tidy`: `bugprone-*`,
`clang-analyzer-*` (the clang static analyzer) and `misc-*`, less the ones
that only flag this code's style, each listed there with why. Any finding
fails it. `tools/lint.sh src/fax_map.c` lints one file.

For a false positive, put a `NOLINT` on its line naming the check, with a
comment saying why it is wrong, as `src/fax_controls.c` does. `scan-build`
runs the same analyzer as the `clang-analyzer-*` checks, so it is not run
separately. cppcheck 2.18 found nothing clang-tidy and the warnings do not,
so it is not used.

## Formatting

`.clang-format` (with `.clang-format-ignore` keeping clang-format out of the
submodules) is the newer files' style as far as clang-format can follow it:
4 spaces, 120 columns, braces on the line, short functions, ifs and loops on
one line, the address tables' values and comments in columns. clang-format
cannot keep a block of two or more statements on one line (`{ ++n; return; }`),
nor a one-line struct, so those become several lines, and a macro split over
lines gets a backslash at the end of each. Besides those backslashes it
changes only whitespace: nothing in it sorts includes, adds braces, or breaks
strings or comments.
