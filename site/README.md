# RiffAmp landing site (Cloudflare Pages)

Static site served at the root domain. Hand-written HTML — no build step.

- `index.html` — landing page (SEO/marketing)
- `_headers` — Cloudflare Pages response headers (COOP/COEP stub for the future `/app`)
- `robots.txt`

## Deploy (Cloudflare Pages, direct upload via Wrangler)

One-time: `npx wrangler login` (opens a browser to authorize your Cloudflare account),
then create the Pages project once — the first deploy will prompt to create it.

    npx wrangler pages deploy site --project-name=riffamp

Redeploys: rerun the same command. Cloudflare gives a `*.pages.dev` URL;
attach a custom domain in the Pages dashboard when ready.

`/app` (the SPA) and the in-browser demo engine will be added here later
(build the app, copy into `site/app/`, then deploy).
