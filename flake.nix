{
  description = "faxanadu-remaster - Faxanadu (NES) statically recompiled for PC";

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
            # (harfbuzz.pc requires glib-2.0).
            packages = [
              pkgs.cmake
              pkgs.pkg-config
              pkgs.python3
              pkgs.SDL2
              pkgs.freetype
              pkgs.harfbuzz
              pkgs.glib
            ] ++ pkgs.lib.optionals pkgs.stdenv.isLinux [
              pkgs.libGL
              # SDL_syswm.h, included by ImGui's SDL2 backend, includes Xlib.h.
              pkgs.libx11
            ];
          };
        });
    };
}
