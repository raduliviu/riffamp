import path from "path"
import tailwindcss from "@tailwindcss/vite"
import react from "@vitejs/plugin-react"
import { defineConfig } from "vite"
import { viteSingleFile } from "vite-plugin-singlefile"

// The production build must be ONE self-contained index.html: the helper
// embeds it at compile time (helper/cmake/embed_file.cmake) and serves it at
// http://127.0.0.1:43718 with no other routes, and the same file deploys to
// the hosted /app. No external chunks, no asset requests.
export default defineConfig({
  plugins: [react(), tailwindcss(), viteSingleFile()],
  resolve: {
    alias: {
      "@": path.resolve(__dirname, "./src"),
    },
  },
})
