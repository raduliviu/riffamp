# RiffAmp landing site (Cloudflare Workers static assets)

Static site — hand-written HTML, no build step. Deployed as an assets-only
Cloudflare Worker (the 2026-recommended path for new static sites; Pages still
works but Workers is where Cloudflare's investment goes).

- `index.html` — landing page (SEO/marketing)
- `_headers` — response headers (COOP/COEP stub for the future `/app`)
- `robots.txt`

Config lives in `../wrangler.jsonc` (`name: riffamp`, `assets.directory: ./site`).

## Deploy — the safe way: `./deploy.sh`

From the repo root, run `./deploy.sh`. It builds the hosted `/app`, verifies
`site/app/index.html` exists, and only then runs `wrangler deploy` — so a failed
build (e.g. wrong Node version) can't publish an empty `/app`. Everything below
is the manual equivalent.

## Deploy — option A: direct upload (fastest)

    pnpm --dir web build:app    # rebuild site/app FIRST (or /app deploys empty)
    npx wrangler login          # one-time, opens a browser to authorize
    npx wrangler deploy         # run from the repo root; uploads ./site

Gives a `riffamp.workers.dev` URL. Re-run to redeploy.

## Deploy — option B: push-to-deploy (Git integration)

Dashboard → Workers & Pages → your `riffamp` Worker → Settings → Builds →
Connect → pick this (private) GitHub repo. Build command: none/empty;
deploy command: `npx wrangler deploy`. Every push to master auto-deploys.
(The Worker name in the dashboard must match `name` in wrangler.jsonc.)

## Custom domain

Workers & Pages → `riffamp` → Settings → Domains & Routes → Add custom domain
→ `riffamp.app`. Since the domain is registered in the same Cloudflare account,
the cert provisions automatically.

`/app` (the SPA + in-browser demo engine) gets added later — build the app,
copy into `site/app/`, and it deploys with the rest.
