/// <reference types="vite/client" />

// Build-time flag: true in dev and the hosted build, false in the helper's
// single-file build (so the WASM demo engine is tree-shaken out there).
declare const __DEMO__: boolean
