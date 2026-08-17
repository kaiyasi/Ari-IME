// SPDX-License-Identifier: GPL-3.0-or-later

const MODIFIERS = Object.freeze({
  shift: 1,
  ctrl: 2,
  alt: 4,
  super: 8,
});

const KEY_SYMS = Object.freeze({
  Tab: 0xff09,
  Enter: 0xff0d,
  NumpadEnter: 0xff8d,
  Backspace: 0xff08,
  Delete: 0xffff,
  Escape: 0xff1b,
  ArrowLeft: 0xff51,
  ArrowUp: 0xff52,
  ArrowRight: 0xff53,
  ArrowDown: 0xff54,
  PageUp: 0xff55,
  PageDown: 0xff56,
  End: 0xff57,
  Home: 0xff50,
  Insert: 0xff63,
  Numpad0: 0xffb0,
  Numpad1: 0xffb1,
  Numpad2: 0xffb2,
  Numpad3: 0xffb3,
  Numpad4: 0xffb4,
  Numpad5: 0xffb5,
  Numpad6: 0xffb6,
  Numpad7: 0xffb7,
  Numpad8: 0xffb8,
  Numpad9: 0xffb9,
  NumpadDecimal: 0xffae,
  NumpadDivide: 0xffaf,
  NumpadMultiply: 0xffaa,
  NumpadSubtract: 0xffad,
  NumpadAdd: 0xffab,
  NumpadEqual: 0xffbd,
});

const LAYOUTS = Object.freeze({
  Default: 0,
  Eten: 1,
  Hsu: 2,
  Ibm: 3,
  GinYieh: 4,
  Dvorak: 5,
  Carpalx: 6,
  ColemakDhAnsi: 7,
  ColemakDhOrth: 8,
  Workman: 9,
  Colemak: 10,
});

const PUNCTUATION_SHORTCUTS = Object.freeze({
  ControlShift: 0,
  AltShift: 1,
  Control: 2,
  Alt: 3,
  Disabled: 4,
});

const LEARNING_FILES = Object.freeze([
  "userdict.dat",
  "uhash.dat",
  "chewing.dat",
  "chewing-deleted.dat",
  "preferences.tsv",
]);

function keySym(key) {
  if (typeof key === "number") {
    return key >>> 0;
  }
  if (key.length === 1) {
    return key.charCodeAt(0);
  }
  if (Object.prototype.hasOwnProperty.call(KEY_SYMS, key)) {
    return KEY_SYMS[key];
  }
  throw new TypeError(`Unsupported key: ${key}`);
}

function modifierMask(modifiers = 0) {
  if (typeof modifiers === "number") {
    return modifiers >>> 0;
  }
  let mask = 0;
  if (modifiers.shift) mask |= MODIFIERS.shift;
  if (modifiers.ctrl || modifiers.control) mask |= MODIFIERS.ctrl;
  if (modifiers.alt) mask |= MODIFIERS.alt;
  if (modifiers.super || modifiers.meta) mask |= MODIFIERS.super;
  return mask;
}

function parseState(module, pointer) {
  return JSON.parse(module.UTF8ToString(pointer));
}

function withCString(module, value, callback) {
  const bytes = module.lengthBytesUTF8(value) + 1;
  const pointer = module._malloc(bytes);
  if (!pointer) {
    throw new Error("Ari WASM could not allocate a UTF-8 argument");
  }
  try {
    module.stringToUTF8(value, pointer, bytes);
    return callback(pointer);
  } finally {
    module._free(pointer);
  }
}

function ensureDirectory(module) {
  try {
    module.FS.mkdir("/ari-ime");
  } catch (error) {
    // EEXIST is expected when a module is reused; other errors are surfaced by
    // the subsequent writeFile call with a more useful path.
    if (!String(error).includes("exist")) {
      throw error;
    }
  }
}

function writeLearningState(module, state) {
  if (!state || !module.FS) return;
  ensureDirectory(module);
  for (const name of LEARNING_FILES) {
    const data = state[name];
    if (data === undefined) continue;
    const bytes = data instanceof Uint8Array ? data : new Uint8Array(data);
    module.FS.writeFile(`/ari-ime/${name}`, bytes);
  }
}

function readLearningState(module) {
  const state = {};
  if (!module.FS) return state;
  for (const name of LEARNING_FILES) {
    try {
      state[name] = new Uint8Array(module.FS.readFile(`/ari-ime/${name}`));
    } catch {
      // A new engine normally has no personal dictionary files yet.
    }
  }
  return state;
}

async function defaultModuleFactory(moduleOptions) {
  const moduleUrl = new URL("../dist/ari-ime-wasm.js", import.meta.url);
  const imported = await import(moduleUrl.href);
  const factory = imported.default ?? imported;
  const options = {...moduleOptions};
  if (!options.locateFile) {
    options.locateFile = (file) => {
      const asset = new URL(`../dist/${file}`, import.meta.url);
      // Emscripten's Node data-file loader passes this value directly to
      // fs.readFileSync, while its WASM loader and browsers accept a URL.
      if (globalThis.process?.versions?.node) {
        return decodeURIComponent(asset.pathname);
      }
      return asset.href;
    };
  }
  return factory(options);
}

export class AriIme {
  #module;
  #handle;

  constructor(module, handle) {
    this.#module = module;
    this.#handle = handle;
  }

  #state(pointer) {
    return parseState(this.#module, pointer);
  }

  state() {
    return this.#state(this.#module._ari_engine_state_json(this.#handle));
  }

  handleKey(key, modifiers = 0) {
    return this.#state(this.#module._ari_engine_handle_key_json(
      this.#handle,
      keySym(key),
      modifierMask(modifiers),
    ));
  }

  selectCandidate(pageIndex) {
    return this.#state(this.#module._ari_engine_select_candidate_json(
      this.#handle,
      pageIndex,
    ));
  }

  paste(text) {
    if (typeof text !== "string") throw new TypeError("text must be a string");
    return this.#state(withCString(this.#module, text, (pointer) =>
      this.#module._ari_engine_paste_utf8_json(this.#handle, pointer)));
  }

  reconvert(text) {
    if (typeof text !== "string") throw new TypeError("text must be a string");
    return this.#state(withCString(this.#module, text, (pointer) =>
      this.#module._ari_engine_reconvert_utf8_json(this.#handle, pointer)));
  }

  commit() {
    return this.handleKey("Enter");
  }

  reset() {
    return this.#state(this.#module._ari_engine_reset_json(this.#handle));
  }

  setLearningAllowed(allowed) {
    this.#module._ari_engine_set_learning_allowed(this.#handle, allowed ? 1 : 0);
  }

  setKeyboardLayout(layout) {
    const value = typeof layout === "string" ? LAYOUTS[layout] : layout;
    if (!Number.isInteger(value) || value < 0 || value > 10) {
      throw new TypeError(`Unsupported keyboard layout: ${layout}`);
    }
    return this.#module._ari_engine_set_keyboard_layout(this.#handle, value) !== 0;
  }

  setFullWidthPunctuation(enabled) {
    this.#module._ari_engine_set_full_width_punctuation(this.#handle, enabled ? 1 : 0);
  }

  setPunctuationShortcut(shortcut) {
    const value = typeof shortcut === "string"
      ? PUNCTUATION_SHORTCUTS[shortcut]
      : shortcut;
    if (!Number.isInteger(value) || value < 0 || value > 4) {
      throw new TypeError(`Unsupported punctuation shortcut: ${shortcut}`);
    }
    this.#module._ari_engine_set_punctuation_shortcut(this.#handle, value);
  }

  setSpaceCandidateMode(enabled) {
    this.#module._ari_engine_set_space_candidate_mode(this.#handle, enabled ? 1 : 0);
  }

  exportLearningState() {
    return readLearningState(this.#module);
  }

  dispose() {
    if (this.#handle !== 0) {
      this.#module._ari_engine_destroy(this.#handle);
      this.#handle = 0;
    }
  }
}

export async function createAriIme(options = {}) {
  const factory = options.moduleFactory ?? defaultModuleFactory;
  const moduleOptions = {...(options.moduleOptions ?? {})};
  const module = await factory(moduleOptions);
  writeLearningState(module, options.learningState);
  const handle = module._ari_engine_create();
  if (!handle) throw new Error("Ari WASM engine could not be created");
  const engine = new AriIme(module, handle);
  if (options.learningAllowed !== undefined) {
    engine.setLearningAllowed(options.learningAllowed);
  }
  return engine;
}

export {KEY_SYMS, LAYOUTS, MODIFIERS, PUNCTUATION_SHORTCUTS};
