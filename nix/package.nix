# Nix derivation for Ari IME. Called by the flake in the repository root.
# The version is read from CMakeLists.txt at evaluation time so it cannot
# drift from the project's single source of truth.
{
  lib,
  stdenv,
  cmake,
  pkg-config,
  kdePackages,
  fcitx5,
  libchewing,
  src,
}:

let
  # Matched line-by-line because builtins.match uses POSIX ERE where "\n"
  # has no meaning inside a bracket expression, and requires the whole
  # string to match.
  projectLines = builtins.filter
    (lib.hasPrefix "project(inputer ")
    (lib.splitString "\n" (builtins.readFile (src + "/CMakeLists.txt")));
  versionMatch =
    if projectLines == [ ] then null
    else builtins.match
      "project\\(inputer VERSION ([0-9]+\\.[0-9]+\\.[0-9]+).*"
      (builtins.head projectLines);
in

assert lib.assertMsg (versionMatch != null)
  "nix/package.nix: unable to read the project VERSION from CMakeLists.txt";

stdenv.mkDerivation {
  pname = "fcitx5-ari-ime";
  version = builtins.elemAt versionMatch 0;

  inherit src;

  nativeBuildInputs = [
    cmake
    # The top-level extra-cmake-modules alias was removed from nixpkgs when
    # KDE 5 reached end of life.
    kdePackages.extra-cmake-modules
    pkg-config
  ];

  buildInputs = [
    fcitx5
    libchewing
  ];

  # NOTE: no strictDeps here. With strictDeps, nativeBuildInputs are kept out
  # of the patched CMake's search path, so find_package(ECM REQUIRED) fails.
  # nixpkgs' own Fcitx5 addons build without it as well.

  meta = {
    description =
      "Fcitx5 input method for Traditional Chinese mixing Bopomofo and English without mode switching";
    longDescription = ''
      Ari IME is a Fcitx5 input method for Traditional Chinese that lets you
      type Bopomofo and English together without switching modes. Every key
      first shows as itself; keys only become Chinese once they form a
      complete, toned Bopomofo syllable. Built on libchewing for conversion,
      phrasing and per-user learning.
    '';
    homepage = "https://github.com/kaiyasi/Ari-IME";
    changelog = "https://github.com/kaiyasi/Ari-IME/blob/main/CHANGELOG.md";
    license = lib.licenses.gpl3Plus;
    platforms = lib.platforms.linux;
  };
}
