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
            ] ++ pkgs.lib.optionals pkgs.stdenv.isLinux [
              pkgs.libGL
              # SDL_syswm.h, included by ImGui's SDL2 backend, includes Xlib.h.
              pkgs.libx11
            ] ++ pkgs.lib.optionals pkgs.stdenv.isDarwin [
              # tools/test.sh's end-to-end lock. Linux has util-linux's flock;
              # macOS has none.
              pkgs.flock
            ];
          };
        });
    };
}
