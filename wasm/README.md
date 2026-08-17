# `@ari-ime/wasm`

This directory contains Ari's headless WebAssembly package. It exposes the
input core only: the host application owns keyboard events, candidate rendering,
clipboard integration, and persistence. Fcitx5 is not a runtime dependency of
the package.

## Use

```js
import {createAriIme} from "@ari-ime/wasm";

const ime = await createAriIme();
ime.handleKey("s");
ime.handleKey("u");
const result = ime.handleKey("3");
console.log(result.preedit); // 你

ime.handleKey("ArrowDown");
console.log(ime.state().candidates);
ime.selectCandidate(0);
const committed = ime.commit();
console.log(committed.commitText);

ime.dispose();
```

`handleKey` accepts a DOM-style key name or a numeric X11/Fcitx-compatible
keysym. Modifiers can be a bit mask or `{shift, ctrl, alt, meta}`. `paste` and
`reconvert` are explicit host operations; no UI is created automatically.

## Build

The checked-in package build uses an Emscripten-built static libchewing. The
runtime package already contains the generated artifacts, so consumers do not
need Emscripten or libchewing; these steps are for maintainers who want to
rebuild them.

The reproducible dependency helper targets libchewing v0.8.5's portable C
backend. It also generates the legacy dictionary natively before cross-building
because the Emscripten build cannot execute its dictionary generator directly:

```sh
cd wasm
git clone --depth 1 --branch v0.8.5 https://github.com/chewing/libchewing.git /tmp/libchewing-v0.8.5
LIBCHEWING_SOURCE_DIR=/tmp/libchewing-v0.8.5 npm run build:libchewing
```

The helper prints the three `CHEWING_*` paths needed by the package build. Use
those values as environment variables:

```sh
CHEWING_WASM_INCLUDE_DIR=/path/to/libchewing/include \
CHEWING_WASM_LIBRARY=/path/to/libchewing/lib/libchewing.a \
CHEWING_DATA_DIR=/path/to/libchewing/share/libchewing \
npm run build:wasm
```

The output is `wasm/dist/ari-ime-wasm.js` plus its `.wasm` and dictionary data
files. Set `ARI_WASM_DIST_DIR` to write artifacts somewhere else while testing.
The repository's native headless harness can be run without Emscripten:

```sh
cmake -S wasm -B build-wasm-native \
  -DARI_WASM_BUILD_NATIVE_TEST=ON \
  -DCHEWING_INCLUDE_DIR=/usr/include/chewing \
  -DCHEWING_LIBRARY=/usr/lib/libchewing.so
cmake --build build-wasm-native
ctest --test-dir build-wasm-native --output-on-failure
```

The module keeps learning data in its MEMFS `/ari-ime` directory. Pass
`learningState` to `createAriIme` to restore a snapshot and call
`exportLearningState()` to let the host save it in IndexedDB, local storage,
or a server of its choice.
