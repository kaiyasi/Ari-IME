// SPDX-License-Identifier: GPL-3.0-or-later

export type KeyboardLayout =
  | "Default"
  | "Eten"
  | "Hsu"
  | "Ibm"
  | "GinYieh"
  | "Dvorak"
  | "Carpalx"
  | "ColemakDhAnsi"
  | "ColemakDhOrth"
  | "Workman"
  | "Colemak";

export type PunctuationShortcut =
  | "ControlShift"
  | "AltShift"
  | "Control"
  | "Alt"
  | "Disabled";

export interface KeyModifiers {
  shift?: boolean;
  ctrl?: boolean;
  control?: boolean;
  alt?: boolean;
  super?: boolean;
  meta?: boolean;
}

export type Key = string | number;
export type LearningFile =
  | "userdict.dat"
  | "uhash.dat"
  | "chewing.dat"
  | "chewing-deleted.dat"
  | "preferences.tsv";
export type LearningState = Partial<Record<LearningFile, Uint8Array | ArrayBuffer>>;

export interface AriState {
  handled: boolean;
  hasCommit: boolean;
  commitText: string;
  updateUI: boolean;
  notifyMode: boolean;
  notification: string;
  engineReady: boolean;
  preedit: string;
  candidates: string[];
  candidatePage: number;
  candidatePageCount: number;
  highlight: number;
  selectionChar: number;
  caretChar: number;
  editing: boolean;
  picking: boolean;
}

export interface WasmModule {
  _ari_engine_create(): number;
  _ari_engine_destroy(handle: number): void;
  _ari_engine_state_json(handle: number): number;
  _ari_engine_handle_key_json(handle: number, keySym: number, modifiers: number): number;
  _ari_engine_select_candidate_json(handle: number, pageIndex: number): number;
  _ari_engine_paste_utf8_json(handle: number, text: number): number;
  _ari_engine_reconvert_utf8_json(handle: number, text: number): number;
  _ari_engine_reset_json(handle: number): number;
  _ari_engine_set_learning_allowed(handle: number, allowed: number): void;
  _ari_engine_set_keyboard_layout(handle: number, layout: number): number;
  _ari_engine_set_full_width_punctuation(handle: number, enabled: number): void;
  _ari_engine_set_punctuation_shortcut(handle: number, shortcut: number): void;
  _ari_engine_set_space_candidate_mode(handle: number, enabled: number): void;
  _malloc(size: number): number;
  _free(pointer: number): void;
  UTF8ToString(pointer: number): string;
  lengthBytesUTF8(value: string): number;
  stringToUTF8(value: string, pointer: number, maxBytes: number): void;
  FS?: {
    mkdir(path: string): void;
    writeFile(path: string, data: Uint8Array): void;
    readFile(path: string): Uint8Array;
  };
}

export type ModuleFactory =
  (options?: Record<string, unknown>) => Promise<WasmModule> | WasmModule;

export interface CreateAriImeOptions {
  moduleFactory?: ModuleFactory;
  moduleOptions?: Record<string, unknown>;
  learningState?: LearningState;
  learningAllowed?: boolean;
}

export declare class AriIme {
  state(): AriState;
  handleKey(key: Key, modifiers?: number | KeyModifiers): AriState;
  selectCandidate(pageIndex: number): AriState;
  paste(text: string): AriState;
  reconvert(text: string): AriState;
  commit(): AriState;
  reset(): AriState;
  setLearningAllowed(allowed: boolean): void;
  setKeyboardLayout(layout: KeyboardLayout | number): boolean;
  setFullWidthPunctuation(enabled: boolean): void;
  setPunctuationShortcut(shortcut: PunctuationShortcut | number): void;
  setSpaceCandidateMode(enabled: boolean): void;
  exportLearningState(): Partial<Record<LearningFile, Uint8Array>>;
  dispose(): void;
}

export declare function createAriIme(options?: CreateAriImeOptions): Promise<AriIme>;
export declare const KEY_SYMS: Readonly<Record<string, number>>;
export declare const LAYOUTS: Readonly<Record<KeyboardLayout, number>>;
export declare const MODIFIERS: Readonly<Record<string, number>>;
export declare const PUNCTUATION_SHORTCUTS: Readonly<Record<PunctuationShortcut, number>>;
