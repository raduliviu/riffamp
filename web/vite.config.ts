import path from "path"
import tailwindcss from "@tailwindcss/vite"
import react from "@vitejs/plugin-react"
import { defineConfig } from "vite"
import { viteSingleFile } from "vite-plugin-singlefile"

// Two build targets from one app:
//
//  • HELPER build  — `vite build` (mode "production"). One self-contained
//    index.html: the native helper embeds it at compile time
//    (helper/cmake/embed_file.cmake) and serves it at http://127.0.0.1:43718.
//    No external chunks, no asset requests — so the in-browser WASM demo
//    engine is EXCLUDED here (a .wasm + AudioWorklet can't be inlined, and the
//    helper is itself the native engine, so it never needs the demo).
//
//  • HOSTED build  — `vite build --mode hosted`. A normal multi-file bundle
//    served at https://riffamp.app/app: emits the NAM wasm/worklet as real
//    assets and includes the demo engine. Output goes straight into ../site/app
//    so it deploys with the Cloudflare Workers static site.
//
// `__DEMO__` (false only in the helper build) gates a dynamic import of the
// demo engine so Rollup drops the WASM code from the single-file bundle.
export default defineConfig(({ mode }) => {
  const isHelper = mode === "production"
  const isHosted = mode === "hosted"
  return {
    base: isHosted ? "/app/" : "/",
    define: {
      __DEMO__: JSON.stringify(!isHelper),
    },
    plugins: [
      react(),
      tailwindcss(),
      // Single-file only for the helper build.
      ...(isHelper ? [viteSingleFile()] : []),
    ],
    resolve: {
      alias: {
        "@": path.resolve(__dirname, "./src"),
      },
    },
    build: isHosted
      ? {
          outDir: path.resolve(__dirname, "../site/app"),
          emptyOutDir: true,
        }
      : // Helper build embeds only index.html; it never serves public/ assets.
        { copyPublicDir: false },
  }
})
