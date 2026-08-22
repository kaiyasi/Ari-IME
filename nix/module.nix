# Shared NixOS / home-manager module. When Fcitx5 is the configured input
# method framework, Ari IME is appended to its addon list automatically:
#
#   imports = [ inputs.ari-ime.nixosModules.default ];
#
# Nothing is changed when the user selected another framework such as ibus.
self:
{
  config,
  lib,
  pkgs,
  ...
}:
{
  i18n.inputMethod.fcitx5.addons = lib.mkIf
    ((config.i18n.inputMethod.type or null) == "fcitx5")
    [ self.packages.${pkgs.system}.default ];
}
