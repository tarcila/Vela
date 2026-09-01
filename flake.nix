{
  description = ''
    VisRTX / ANARI local dev shell
  '';

  nixConfig = {
    extra-substituters = [
      "https://dldt.cachix.org/"
    ];
    extra-trusted-public-keys = [
      "dldt.cachix.org-1:lF3I8Yijsqk+5+ZjH3QCLYrPvKadXpL41fsdIpM5Rss="
    ];
  };

  inputs = {
    anari-nix.url = "github:dldt/anari-nix";
    nixpkgs.follows = "anari-nix/nixpkgs";
    flake-utils = {
      url = "github:numtide/flake-utils";
      inputs.systems.follows = "systems";
    };
    systems.url = "github:nix-systems/default";
    treefmt-nix = {
      url = "github:numtide/treefmt-nix";
      inputs.nixpkgs.follows = "nixpkgs";
    };
  };

  outputs =
    {
      self,
      nixpkgs,
      anari-nix,
      flake-utils,
      systems,
      treefmt-nix,
    }:
    flake-utils.lib.eachDefaultSystem (
      system:
      let
        inherit (nixpkgs) lib;

        # Let's bring nixpkgs in
        pkgs = import nixpkgs {
          inherit system;
          config = {
            allowUnfree = true;
            cudaSupport = system == "x86_64-linux" || system == "aarch64-linux";
          };
          overlays = [ anari-nix.overlays.default ];
        };
        # ANARI specific
        anariDevices =
          with pkgs;
          # The first in the list is the default

          (lib.optionals pkgs.config.cudaSupport [
            barney
            visrtx
          ])
          ++ [
            anari-helide
            anari-helide-gpu
            anari-cycles
            anari-ospray
            anari-visionaray
          ];

        # Global cudaSupport would force CUDA rebuilds of these tools, missing the binary cache
        pkgsNoCuda = import nixpkgs {
          inherit system;
          config.allowUnfree = true;
        };
        assetTools = with pkgsNoCuda; [
          (python3.withPackages (
            ps: with ps; [
              numpy
              scikit-image
              imageio
              openimageio
              trimesh
              pymeshlab
              manifold3d
              cgal
            ]
          ))
          openimageio
          opencolorio
          flip
          f3d
          cgal
        ];
        treefmtEval = treefmt-nix.lib.evalModule pkgs ./treefmt.nix;
      in
      {
        # The devShell for each system type.
        devShells.default = pkgs.mkShell {
          hardeningDisable = [ "all" ];
          packages =
            with pkgs;
            [
              anari-cts
              jq
            ]
            ++ assetTools;
          nativeBuildInputs = with pkgs; [
            gdb
            lldb
            ninja
            cmakeCurses
            valgrind
            watchexec
          ];
          buildInputs = with pkgs; [
            lua54Packages.luafilesystem
            openssl
            cacert
            nlohmann_json
            openusd
          ];

          inputsFrom = with pkgs; [
            vela
          ];

          LD_LIBRARY_PATH = "/run/opengl-driver/lib:${pkgs.mdl-sdk}/lib:${lib.makeLibraryPath anariDevices}";
          ANARI_LIBRARY = "visrtx";
          VSR_ANARI_LIBRARIES = lib.concatStringsSep "," (
            # ANARI library names use underscores (e.g. libanari_library_helide_gpu.so)
            map (p: lib.replaceStrings [ "-" ] [ "_" ] (lib.removePrefix "anari-" p.pname)) anariDevices
          );
          VSR_LUA_PACKAGE_PATHS = "/data/VSR/scripts";

          shellHook = ''
            if test -n "''${VSCODE_IPC_HOOK_CLI}"
            then
              export DISPLAY=:10
            fi
            export CMAKE_TLS_VERIFY=0
          '';

        };

        # Treefmt
        formatter = treefmtEval.config.build.wrapper;
      }
    );
}
