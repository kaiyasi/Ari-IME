# Ari IME 2.5.5

This release adds NixOS support. The repository now ships a Nix flake, so
NixOS users install Ari IME through declarative configuration instead of an
install script.

The flake exposes `packages`, `overlays.default` (adds `pkgs.fcitx5-ari-ime`),
plus `nixosModules.default` and `homeManagerModules.default`. Importing the
module appends the package to `i18n.inputMethod.fcitx5.addons` whenever Fcitx5
is the selected input method framework, and changes nothing under ibus or
other frameworks. Learned personal data stays in `~/.config/inputer/` exactly
as on other distributions, so rebuilds never remove it.

The package derivation reads its version from `CMakeLists.txt` at evaluation
time, so it cannot drift from the project's single source of truth. A new
GitHub Actions job runs `nix flake check` on every push and pull request to
keep the flake buildable.

Validation covers a full `nix build -L .#default` on nixpkgs unstable with
libchewing 0.13 and Fcitx 5.1.21, module evaluation for both the fcitx5 and
ibus cases, dynamic-link inspection of the installed addon, and the ordinary
release checks.
