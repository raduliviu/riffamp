#!/usr/bin/env bash
# Deploy the RiffAmp site (landing + hosted /app) to Cloudflare Workers.
#
# Guards against the empty-/app footgun: `build:app` empties site/app before
# rebuilding, so if the build fails (e.g. wrong Node version — Vite needs 20+),
# `set -e` + the index.html check stop us BEFORE wrangler uploads an empty /app.
set -euo pipefail

cd "$(dirname "$0")"

# Vite/wrangler need a recent Node; this shell often defaults to an old one.
if [ -x "$HOME/.nvm/versions/node/v24.15.0/bin/node" ]; then
  export PATH="$HOME/.nvm/versions/node/v24.15.0/bin:$PATH"
fi
node_major="$(node -p 'process.versions.node.split(".")[0]')"
if [ "$node_major" -lt 20 ]; then
  echo "Node $node_major is too old for the build (need >= 20). Fix your PATH/nvm." >&2
  exit 1
fi

echo "Building hosted /app…"
pnpm --dir web build:app

if [ ! -f site/app/index.html ]; then
  echo "site/app/index.html missing after build — aborting before deploy." >&2
  exit 1
fi

echo "Deploying to Cloudflare…"
npx wrangler deploy

echo "Done. Check https://riffamp.app/ and https://riffamp.app/app"
