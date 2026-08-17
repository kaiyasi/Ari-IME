// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef ARI_WASM_FCITX_CONFIG_RAWCONFIG_H
#define ARI_WASM_FCITX_CONFIG_RAWCONFIG_H

namespace fcitx {
class RawConfig {
public:
    template <typename T>
    void setValueByPath(const char *, const T &) {}
};
}

#endif // ARI_WASM_FCITX_CONFIG_RAWCONFIG_H
