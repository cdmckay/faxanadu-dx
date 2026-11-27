{
  description = "Faxanadu DX - Faxanadu (NES) statically recompiled for PC";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-26.05";
  };

  outputs = { self, nixpkgs }:
    let
      systems = [ "x86_64-linux" "aarch64-linux" "aarch64-darwin" ];
      forAllSystems = nixpkgs.lib.genAttrs systems;
    in
    {
      devShells = forAllSystems (system:
        let
          pkgs = import nixpkgs { inherit system; };
          llvm = pkgs.llvmPackages_22;
          # clang 22, without two of Nix's hardening flags: fortify's checks
          # stop a program at an overflow with a bare trap, before
          # AddressSanitizer can say where, and strictoverflow's
          # -fno-strict-overflow defines signed overflow, so UBSan never
          # reports it.
          sanitizing = name: pkgs.writeShellScript "fax-sanitize-${name}" ''
            hardening=" ''${NIX_HARDENING_ENABLE-} "
            for flag in fortify fortify3 strictoverflow; do hardening="''${hardening// $flag / }"; done
            NIX_HARDENING_ENABLE="$hardening" exec ${llvm.clang}/bin/${name} "$@"
          '';
        in
        {
          default = pkgs.mkShell {
            # The cycle build runs the recompiler and its Python generators at
            # configure time, then links the system SDL2. recomp-ui needs GL,
            # and draws color emoji with FreeType and HarfBuzz when it finds them
            # (harfbuzz.pc requires glib-2.0, and glib-2.0.pc sysprof-capture-4,
            # which glib doesn't bring: without it pkg-config complains).
            packages = [
              pkgs.cmake
              pkgs.pkg-config
              pkgs.python3
              pkgs.SDL2
              pkgs.freetype
              pkgs.harfbuzz
              pkgs.glib
              pkgs.libsysprof-capture
              # tools/disassembly.sh assembles chipx86's disassembly.
              pkgs.cc65
              # clang-tidy for tools/lint.sh, and clang-format, the same
              # version on every machine (docs/C_TOOLING.md).
              llvm.clang-tools
            ] ++ pkgs.lib.optionals pkgs.stdenv.isLinux [
              pkgs.libGL
              # SDL_syswm.h, included by ImGui's SDL2 backend, includes Xlib.h.
              pkgs.libx11
            ] ++ pkgs.lib.optionals pkgs.stdenv.isDarwin [
              # tools/test.sh's end-to-end lock. Linux has util-linux's flock;
              # macOS has none.
              pkgs.flock
            ];
            # The sanitizer builds' compiler (tools/test.sh: the unit tests,
            # and the game with --sanitize), on every machine, so the checks
            # are the same everywhere. The devShell's own compiler (clang 21;
            # on Linux, gcc) builds everything else. clang 21's
            # AddressSanitizer runtime hangs as any program starts on macOS
            # 26 (llvm/llvm-project#200447); 22's does not.
            FAX_SANITIZE_CC = sanitizing "clang";
            FAX_SANITIZE_CXX = sanitizing "clang++";
            # SDL3's folder, an rpath of every sanitized program. sdl2-compat
            # loads SDL3 by its own rpath, but under AddressSanitizer the
            # loader takes the sanitizer's runtime for the caller and finds
            # nothing, and on macOS sdl2-compat says so in a dialog on screen.
            # This is the SDL3 sdl2-compat is built with (its package.nix).
            FAX_SANITIZE_RPATH = "${pkgs.lib.getLib (pkgs.sdl3.override { traySupport = false; })}/lib";
            # The clang-format tools/format.sh and the gate run, by path: on
            # macOS the stdenv's clang 21 has one too, later on PATH, and two
            # versions can format the same code differently.
            FAX_CLANG_FORMAT = "${llvm.clang-tools}/bin/clang-format";
          };
        });
    };
}
