// SPDX-License-Identifier: GPL-3.0-or-later

#include <cassert>
#include <string>

#include "ari-ime-wasm.h"
#include "user_data.h"

namespace {

void press(AriWasmEngine *engine, const std::string &keys) {
    for (unsigned char key : keys) {
        ari_engine_handle_key_json(engine, key, 0);
    }
}

} // namespace

int main() {
    std::error_code ec;
    inputer::resetUserDictionary(ec);
    assert(!ec);

    AriWasmEngine *engine = ari_engine_create();
    assert(engine != nullptr);

    std::string state = ari_engine_state_json(engine);
    assert(state.find("\"engineReady\":true") != std::string::npos);

    press(engine, "su3");
    state = ari_engine_state_json(engine);
    assert(state.find("\"preedit\":\"你\"") != std::string::npos);

    ari_engine_handle_key_json(engine, 0xff54, 0); // Down
    state = ari_engine_state_json(engine);
    assert(state.find("\"picking\":true") != std::string::npos);
    assert(state.find("\"candidates\":[") != std::string::npos);

    ari_engine_reset_json(engine);
    press(engine, "y");
    ari_engine_handle_key_json(engine, 0x20, 0); // tone one
    state = ari_engine_state_json(engine);
    assert(state.find("\"preedit\":\"資\"") != std::string::npos);

    ari_engine_destroy(engine);
    inputer::resetUserDictionary(ec);
    assert(!ec);
    return 0;
}
