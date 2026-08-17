// SPDX-License-Identifier: GPL-3.0-or-later
// Minimal Fcitx key compatibility types used by the headless WASM build.
#ifndef ARI_WASM_FCITX_UTILS_KEY_H
#define ARI_WASM_FCITX_UTILS_KEY_H

#include <cstdint>
#include <initializer_list>

namespace fcitx {

using KeySym = std::uint32_t;

enum class KeyState : std::uint32_t {
    Shift = 1u << 0,
    Ctrl = 1u << 1,
    Alt = 1u << 2,
    Super = 1u << 3,
};

class KeyStates {
public:
    constexpr KeyStates() = default;
    explicit constexpr KeyStates(std::uint32_t bits) : bits_(bits) {}

    KeyStates(std::initializer_list<KeyState> states) {
        for (const KeyState state : states) {
            bits_ |= static_cast<std::uint32_t>(state);
        }
    }

    constexpr bool test(KeyState state) const {
        return (bits_ & static_cast<std::uint32_t>(state)) != 0;
    }

    constexpr bool testAny(const KeyStates &states) const {
        return (bits_ & states.bits_) != 0;
    }

    constexpr std::uint32_t bits() const { return bits_; }

private:
    std::uint32_t bits_ = 0;
};

class Key {
public:
    constexpr explicit Key(KeySym sym, KeyStates states = {})
        : sym_(sym), states_(states) {}

    constexpr KeySym sym() const { return sym_; }
    constexpr const KeyStates &states() const { return states_; }

private:
    KeySym sym_ = 0;
    KeyStates states_;
};

} // namespace fcitx

#endif // ARI_WASM_FCITX_UTILS_KEY_H
