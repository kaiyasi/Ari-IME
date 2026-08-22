{
  description =
    "Ari IME: Fcitx5 mixed Bopomofo/English input without mode switching";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs =
    { self, nixpkgs }:
    let
      inherit (nixpkgs) lib;

      systems = [
        "x86_64-linux"
        "aarch64-linux"
      ];

      forAllSystems = lib.genAttrs systems;

      # Copy the repository into the Nix store without local build trees,
      # binary package artifacts and other development-only paths.
      sourceTree = lib.cleanSourceWith {
        src = self;
        filter =
          path: type:
          let
            name = baseNameOf (toString path);
          in
          !(
            name == ".git"
            || name == "result"
            || name == "pkg"
            || (type == "directory" && (name == "build" || lib.hasPrefix "build-" name))
            || lib.hasSuffix ".pkg.tar.zst" name
            || lib.hasSuffix ".tar.gz" name
          );
      };
    in
    {
      overlays.default = final: _prev: {
        fcitx5-ari-ime = final.callPackage ./nix/package.nix {
          src = sourceTree;
        };
      };

      packages = forAllSystems (
        system:
        let
          pkgs = import nixpkgs {
            inherit system;
            overlays = [ self.overlays.default ];
          };
        in
        {
          fcitx5-ari-ime = pkgs.fcitx5-ari-ime;
          default = pkgs.fcitx5-ari-ime;
        }
      );

      nixosModules.default = import ./nix/module.nix self;
      homeManagerModules.default = import ./nix/module.nix self;

      checks = forAllSystems (system: {
        inherit (self.packages.${system}) default;
      });

      devShells = forAllSystems (
        system:
        let
          pkgs = import nixpkgs { inherit system; };
        in
        {
          default = pkgs.mkShell {
            nativeBuildInputs = with pkgs; [
              cmake
              kdePackages.extra-cmake-modules
              pkg-config
            ];
            buildInputs = with pkgs; [
              fcitx5
              libchewing
            ];
          };
        }
      );
    };
}
