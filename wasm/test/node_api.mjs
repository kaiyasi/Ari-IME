// SPDX-License-Identifier: GPL-3.0-or-later

import assert from "node:assert/strict";

import {createAriIme} from "../src/index.mjs";

const ime = await createAriIme();
assert.equal(ime.state().engineReady, true);

for (const key of ["s", "u", "3"]) {
  ime.handleKey(key);
}
assert.equal(ime.state().preedit, "你");

const picking = ime.handleKey("ArrowDown");
assert.equal(picking.picking, true);
assert.ok(picking.candidates.length > 0);
assert.equal(ime.selectCandidate(0).preedit.length > 0, true);

ime.reset();
ime.setPunctuationShortcut("AltShift");
assert.equal(ime.handleKey("\\", {alt: true, shift: true}).preedit, "、");

ime.reset();
assert.equal(ime.paste("測試 abc").preedit, "測試 abc");
ime.reset();
assert.equal(ime.reconvert("你").editing, true);
assert.ok(ime.exportLearningState()["uhash.dat"] instanceof Uint8Array);

ime.dispose();
console.log("WASM Node API smoke test passed");
