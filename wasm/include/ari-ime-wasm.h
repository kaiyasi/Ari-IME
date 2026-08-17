// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef ARI_IME_WASM_API_H
#define ARI_IME_WASM_API_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AriWasmEngine AriWasmEngine;

enum {
    ARI_MOD_SHIFT = 1u << 0,
    ARI_MOD_CTRL = 1u << 1,
    ARI_MOD_ALT = 1u << 2,
    ARI_MOD_SUPER = 1u << 3,
};

AriWasmEngine *ari_engine_create(void);
void ari_engine_destroy(AriWasmEngine *engine);

// The returned UTF-8 JSON pointer belongs to the engine and remains valid
// until the next operation on that engine.
const char *ari_engine_state_json(AriWasmEngine *engine);
const char *ari_engine_handle_key_json(AriWasmEngine *engine,
                                       uint32_t key_sym,
                                       uint32_t modifiers);
const char *ari_engine_select_candidate_json(AriWasmEngine *engine,
                                             int page_index);
const char *ari_engine_paste_utf8_json(AriWasmEngine *engine,
                                       const char *text);
const char *ari_engine_reconvert_utf8_json(AriWasmEngine *engine,
                                           const char *text);
const char *ari_engine_reset_json(AriWasmEngine *engine);

void ari_engine_set_learning_allowed(AriWasmEngine *engine, int allowed);
int ari_engine_set_keyboard_layout(AriWasmEngine *engine, int layout);
void ari_engine_set_full_width_punctuation(AriWasmEngine *engine, int enabled);
void ari_engine_set_punctuation_shortcut(AriWasmEngine *engine, int shortcut);
void ari_engine_set_space_candidate_mode(AriWasmEngine *engine, int enabled);

#ifdef __cplusplus
}
#endif

#endif // ARI_IME_WASM_API_H
