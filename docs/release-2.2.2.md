# Ari IME 2.2.2

This patch release improves the first-run installation path without adding a
custom UI or changing the mixed-input behavior.

## Recommended first run

After installing the Arch, Debian/Ubuntu, or source-built package:

```sh
ari-ime-enable --make-default
```

The command explicitly adds Ari to the first Fcitx5 input-method group,
creates a timestamped backup before changing an existing profile, and leaves
other input methods in place. Omit `--make-default` to add Ari without
changing the current default. `fcitx5-configtool` remains available as the
graphical alternative.

The package also installs `ari-ime-reset-data` so learned data can be reset
without keeping a source checkout.
