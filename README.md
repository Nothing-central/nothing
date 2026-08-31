# Nothing Central

> **One foundation. Three browsers. Built in Kenya. 🇰🇪**

---

## What is this?

Nothing Central is the monorepo that powers the entire Nothing browser ecosystem. Instead of three separate codebases doing the same things differently, everything shares one foundation — one fingerprint engine, one storage layer, one adblock, one Tor service — and every browser just plugs in what it needs.

---

## The Ecosystem

| App | What it is |
|-----|-----------|
| `apps/browser` | **Sabre Browser** — full daily driver with Nothing aesthetic |
| `apps/scraper` | **Nothing Browser** — scraping, DevTools, network capture |
| `apps/private` | **Nothing Private Browser** — zero persistence, privacy first |

---

## Shared Packages

### `packages/profile`
The identity layer. Studies how Firefox generates its identity and does it better in C++.
- `fingerprint/` — fingerprint generator shared across all browsers
- `identity/` — browser ID generation
- `shared/` — shared generators and utilities

### `packages/storage`
Two storage strategies, one package.
- `persistent/` — used by the scraper. Data survives the session.
- `volatile/` — used by private browser and incognito. Cryptographically wiped on close.

### `packages/search`
Clean search engine configs for the whole ecosystem.
- DuckDuckGo
- Google
- Bing
- Brave Search
- SearXNG

### `packages/transport`
Network layer shared by all browsers.
- `tor/` — Tor routing, one toggle, available in every browser
- `incognito/` — RAM-disk sandbox, Sabre Browser only

### `packages/adblock`
Brave's adblock-rust engine, rewritten in C++ and built specifically for this ecosystem. Evaluated before the engine renders the page — zero overhead.

### `packages/devtools`
Deep browser instrumentation via the **Chrome DevTools Protocol (CDP) through Qt**. This is not the basic network tab you get for free — this is low level access to everything the engine exposes.

What it captures:
- Every network request and response with full headers and body
- WebSocket frames, direction tagged, JSON auto pretty-printed
- DOM snapshots and mutations in real time
- JavaScript runtime — console, exceptions, heap, call stacks
- Cookie and storage writes as they happen
- Page lifecycle events — load, paint, navigation, crash

Every app in the ecosystem that needs capture plugs into this package. The scraper uses it for network interception. The devtools panel in Nothing Browser is built on top of it. Sabre Browser uses it for the Glass View inspector.

### `packages/piggycpp`
The C++ interpreter that powers the Piggy scraping engine. Lives here so every app can use it.

---

## `piggy/`
The JavaScript library. Control any Nothing ecosystem browser from TypeScript/Bun. One import, no bloat.

```ts
import piggy from "nothing-browser"

await piggy.launch()
await piggy.register("site", "https://example.com")
await piggy.site.navigate()
```

---

## `docs/`
One documentation site for the entire ecosystem.
**[nothing-browser-docs.pages.dev](https://nothing-browser-docs.pages.dev)**

---

## Architecture

```
nothing-central/nothing/
├── apps/
│   ├── browser/        ← Sabre Browser
│   ├── scraper/        ← Nothing Browser  
│   └── private/        ← Nothing Private Browser
├── packages/
│   ├── profile/        ← fingerprint + identity
│   ├── storage/        ← persistent + volatile
│   ├── search/         ← search engine configs
│   ├── transport/      ← tor + incognito
│   ├── adblock/        ← C++ adblock engine
│   ├── devtools/       ← CDP through Qt, deep capture
│   └── piggycpp/       ← C++ scraping interpreter
├── piggy/              ← JS library
└── docs/               ← documentation
```

---

## Why a monorepo?

Three browsers sharing zero code means three times the bugs, three times the maintenance, and three fingerprint engines that drift apart over time. Nothing Central fixes that. One change to the fingerprint engine improves all three browsers at once. One Tor integration works everywhere. One adblock update ships to everything.

---

## Status

🚧 **Active Development** — foundation being laid.

---

## Links

- 📚 **Docs:** [nothing-browser-docs.pages.dev](https://nothing-browser-docs.pages.dev)
- 📦 **npm:** [nothing-browser](https://www.npmjs.com/package/nothing-browser)
- 💬 **Discord:** [Join](https://discord.gg/TUxBVQ7y)
- 📱 **WhatsApp:** [Ernest Tech House](https://whatsapp.com/channel/0029VbBzoXuCxoArtvaslR0U)

---

*Part of the Nothing Ecosystem · Built by [Ernest Tech House](https://github.com/ernest-tech-house-co-operation) · Kenya 🇰🇪*
so this are some addition ideas that has come to me their will be alot of assets released 
1. nothing private browser - the always incognito browser
2. sabre browser - the full browser with everhing
3. nothing browser - the full scrapper browser 
4. nothing browser headless - this is the headless verion that will manly talk with users in terminal 
5. nothing browser headfull - it is just the opposite of the headless
6. nothing browser mini - this is a small headfull browser that a user can use to test his site 
7. nothing browser (piggy) - the js wrapper that devs use  
8. nothing broser (linked) - this is like a diffrent build that will ned linux/arch users that will run sudo apt commands to download dependancies because tis will be tiny and will be built with links and not full build side note the rest of the others will have linked and unliked releases and we will produce windows arch arm and amd64 reases and windows releses boy o boy the github ymal will be big 
