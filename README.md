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




# Session Handoff — Nothing Central

## Repo
- GitHub org: `Nothing-central`
- Repo: `git@github.com:Nothing-central/nothing.git`
- Local: `~/dev/nothing/`
- Dev environment: Deepin Linux (Debian-based), ThinkPad i5 8th gen / 8GB RAM, 200GB Linux partition
- Git identity: Ernest12287 / peaseernest8@gmail.com
- Uses GitHub Desktop over terminal git

## Monorepo structure
```
nothing/
├── apps/
│   ├── browser/        ← Sabre Browser (daily driver)
│   ├── scraper/        ← Nothing Browser
│   └── private/        ← Nothing Private Browser
├── packages/
│   ├── adblock/
│   ├── profile/        ← fingerprint / identity / incognito
│   ├── search/
│   ├── transport/      ← tor / proxy
│   ├── devtools/       ← NOT STARTED — next up
│   └── piggycpp/
├── piggy/               ← JS/TS library
└── docs/
```

---

## packages/adblock — COMPLETE
tokenizer, token_selector, flat_multimap, network_filter, request, blocker. C++17, EasyList/uBlock syntax parsing, domain/content-type filtering, third-party detection.

Known bug: exception result flag not returning correctly when no block rule fires first.

Not done: regex filters, cosmetic filtering (DOM element hiding), scriptlet injection, filter list auto-updater, Qt network interceptor integration.

---

## packages/profile — COMPLETE and PUSHED

### fingerprint/ (pure C++, no Qt)
- `NavigatorProfile` / `ScreenProfile` / `WebGLProfile` / `AudioProfile` — coherent identity generation, built from a deep research pass on Firefox's RFP (Resist Fingerprinting) defenses across canvas, WebGL, navigator, screen, audio, and fonts (including Firefox's own gaps, e.g. `EfficientCanvasRandomization` leaving `getImageData` unprotected, WebGL2 caps not clamped, no audio noise injection, `document.fonts.check()` unprotected).
- `SessionKeyStore` — HMAC-SHA256 + XorShift128Plus, one `sessionUuid` per identity, `PerSiteKey(origin)` derives a per-site key, `PerCallKey` derives a per-call key from that + content bytes. This is what makes canvas/audio noise vary per-origin instead of being a single fixed session-wide value (a fixed value would itself be a stable cross-site tracking ID — this was a real bug caught and fixed mid-session).
- Font visibility tiering (`FontVisibilityProvider`, Base/LangPack/User/Hidden levels) loaded from vendored **verbatim** Firefox `StandardFonts-{win10,macos,linux,android}.inc` files (copied from a cloned `mozilla-central` at `~/dev/mozilla-central`). Windows' `.inc` has THREE sections: `kBaseFonts[]`, `kLangPackFonts[]`, and `FONT_RULE(...)` locale-matching calls (only the first two are wired up so far). Linux's base array is named `kBaseFonts_Ubuntu_22_04`, not `kBaseFonts` — this cost real debugging time, don't assume symbol names match across platform `.inc` files.
- `fingerprint_report.cpp` — standalone test harness (builds with plain `g++ -std=c++17`, no Qt needed) that builds one full identity and dumps it as JSON. Confirmed working end-to-end: navigator, screen, WebGL, audio, fonts (94 base fonts loaded, Arial correctly visible, an unknown font correctly rejected), canvas/audio noise hashes, session/per-site key.

### fingerprint/qt/ (Qt-dependent glue)
- `profile_to_json.h/.cpp` — pure struct → `QJsonObject` conversion, no logic.
- `FingerprintSpoofer.h/.cpp` — builds ONE injection script per navigation: serializes the identity to JSON, derives the per-origin key via `SessionKeyStore`, embeds a **JS reimplementation** of HMAC-SHA256 + XorShift128Plus (unavoidable duplication — this JS runs inside the actual page, C++ can't reach in), then appends override code for navigator/screen/canvas/audio/WebGL/`userAgentData`/WebRTC/timezone.
- Injected via `QWebEngineScript`, `DocumentCreation` injection point, `MainWorld` (not `IsolatedWorld` — isolated world can't override page-visible prototypes).
- **Known unfixable-from-JS gap:** `document.fonts.check()` and `@font-face { src: local() }` resolve in Blink's native font matching before any page JS runs. A MainWorld script cannot intercept this. Font visibility logic (`font_visibility.h/.cpp`) is fully built and tested in isolation but **not currently enforced in the live browser** — would need a native Blink/Chromium hook, not found yet.

### identity/
- `IdentityBundle` — one struct bundling `NavigatorProfile` + `ScreenProfile` + `WebGLProfile` + `AudioProfile` + `SessionKeyStore` for one identity.
- `IdentityManager` — `contextId -> IdentityBundle` map. `CreateIdentity` / `GetIdentity` / `DestroyIdentity`. This `contextId` pattern (`"default"` for normal browsing, a generated id per incognito session) is reused identically by `packages/search` and `packages/transport`.

### incognito/
- `IncognitoSession::Start()` — generates a random contextId, calls `IdentityManager::CreateIdentity`, returns the contextId for the caller to store on the window/profile object.
- `IncognitoSession::End()` — calls `IdentityManager::DestroyIdentity`.
- **Confirmed design: ONE identity per incognito session, shared across all its tabs** — not a new identity per tab. Matches real Chrome/Firefox incognito behavior.

### packages/profile/docs.md
Full architecture + data flow + per-file responsibility table + contributor notes already written and pushed. Read this first before touching anything in `packages/profile`.

### Real bugs caught and fixed during this build (worth remembering the pattern, not just the fix)
1. Canvas/audio noise seeded once per browser launch (from a prior draft) → same fingerprint on every site AND every site sees the same fingerprint as every other site → worse than no spoofing at all, a stable cross-site correlation key. Fixed by deriving noise from `HMAC(sessionSecret, origin)` per navigation.
2. `userAgentData.platform` hardcoded to `'Linux'` regardless of actual spoofed platform (from a prior draft) → breaks coherence on Windows/Mac identities, an easy CreepJS-style tell.
3. Missing `#include <unordered_map>` and a missing `#include "canvas_interceptor.h"` — plain compile errors, but took a few rounds because the fix wasn't actually applied/saved before rebuilding.
4. `generated_fonts_direct.h` assumed all four platform `.inc` files used array name `kBaseFonts` — wrong for Linux (`kBaseFonts_Ubuntu_22_04`). Also missed that the `.inc` files are gated behind `#ifdef StandardFonts` — needed `#define StandardFonts 1` before the `#include`s or the arrays compile out to nothing (silently — `baseFontCount` was 0, not a compile error).

---

## packages/search — COMPLETE and PUSHED

- `SearchEngine` — one engine's data (id, display name, query/suggest URL templates, home URL, `requiresTor` flag for onion-only engines).
- `SearchEngineRegistry` — built-ins: DuckDuckGo, Google, Bing, Brave Search, SearXNG (default instance searx.be), Ahmia (onion, `requiresTor = true`). Plus `RegisterCustom()` for user-added SearXNG instances.
- `SearchEngineManager` — same `contextId -> active engine id` pattern as identity. Falls back to DuckDuckGo if a context never set one explicitly (never returns null).
- `search_ipc_handlers.h/.cpp` — bridges manager calls to piggy's existing named-pipe IPC (`search.setEngine`, `search.getEngine`, `search.listEngines`, `search.registerCustom`, `search.buildQueryUrl`). **PLACEHOLDER:** the actual dispatcher registration call and JSON parsing (`ExtractStringField`) are stubs — need piggycpp's real command-registration API and JSON library, which is explicitly deferred (piggy integration is its own future session, not mixed into this work).
- `search_suggest_cache.h` — TTL cache for autocomplete responses per `(engineId, queryPrefix)`.
- `js/searchengine.js` — the JS/TS wrapper matching the planned `import { piggy, searchengine } from "nothing-browser"` shape.
- `packages/search/docs.md` — written and pushed, covers the context model and documents `useIncognito` routing through `packages/profile/incognito` for BOTH fingerprint identity and search engine simultaneously.

---

## packages/transport — BUILT, NOT YET PUSHED (push this first, next session)

### tor/
- `TorController` — raw socket implementation of Tor's **control port** protocol (default 9051): connect, authenticate (password OR cookie file), `SIGNAL NEWNYM` (new circuit), `GETINFO circuit-status`, `GETINFO status/bootstrap-phase`.
- `TorSocksConfig` — the separate **SOCKS port** (default 9050) that actual traffic routes through — this is what gets handed to Qt's proxy/network setup, NOT the control port.
- `TorContextManager` — per-`contextId` Tor on/off toggle. Lazily connects to the control port on first `SetEnabled(true)` call.
- **Known limitation, not yet solved:** Tor's default config has ONE SOCKS port for the whole daemon. `RequestNewIdentity()` (NEWNYM) rotates the circuit for **every** Tor-enabled context at once — there's no native per-context circuit isolation without running multiple `SocksPort` lines in `torrc`, each with its own `IsolateSOCKSAuth` or distinct port. Not implemented.

### proxy/
- `ProxyEntry` — parses `scheme://user:pass@host:port` or bare `host:port` (defaults to `http` scheme if none given).
- `ProxySource` — the three input shapes requested: local file path (`./myproxies.txt`), remote URL serving a plain-text list (`https://.../proxies.txt`), or a single proxy string treated as a one-entry list. `DetectKind()` auto-detects which one you passed. **`FetchFromUrl` is a stub** — needs wiring to whatever HTTP client the browser already uses (Qt's `QNetworkAccessManager` or a piggycpp utility) for the remote-URL case; local file and single-entry both work today.
- `ProxyPool` — holds parsed entries, hands one out per rotation mode: `Sequential`, `Random`, `StickyPerOrigin` (same origin always gets the same proxy, avoiding a fingerprint that jumps IPs mid-session on one site).
- `ProxyContextManager` — per-context pool + mode, same `contextId` pattern as everything else.
- `js/transport.js` — `useTor(bool, default false)`, `newTorIdentity()`, `useProxy(source, {rotation})`, `clearProxy()`, `status()`.
- `packages/transport/docs.md` — written, NOT yet pushed. Explicitly flags the unresolved question: **what happens if both Tor and proxy are enabled on the same context?** Recommendation written into the doc (Tor takes precedence, proxy ignored with a warning) but **not enforced in any code yet** — whoever wires the actual `QWebEngineProfile` network layer needs to implement that precedence decision.

### Action item: push packages/transport before doing anything else next session.
Suggested commit message pattern (matches the style used for profile/search):
```
Add Tor + proxy transport layer: control-port client, SOCKS routing,
proxy list loading (file/URL/single), per-context rotation, docs

- tor/: TorController (control-port protocol, NEWNYM, bootstrap status),
  TorSocksConfig, TorContextManager (per-context on/off)
- proxy/: ProxyEntry (URI parsing), ProxySource (file/URL/single-entry
  auto-detect, FetchFromUrl still a stub), ProxyPool (Sequential/Random/
  StickyPerOrigin rotation), ProxyContextManager
- js/transport.js: useTor/useProxy/newTorIdentity/status
- docs.md: known limitation (no per-context circuit isolation without
  multiple torrc SocksPort lines), unresolved Tor+proxy precedence

Known gaps: ProxySource::FetchFromUrl unimplemented (needs HTTP client
wiring), Tor/proxy precedence when both enabled on one context not
enforced in code, IPC handlers for both (transport.setTorEnabled,
transport.setProxy, etc.) not yet written
```

---

## JS/TS library (piggy) — design decisions so far, NOT yet implemented
- `import { piggy, searchengine, transport } from "nothing-browser"`
- Incognito is NOT a separate API — it's a flag: `piggy.goto(url, { useIncognito: true })`. First call with this flag auto-starts one incognito session (via `IncognitoSession::Start()`), subsequent calls reuse the SAME session (one identity, per the confirmed design above), `piggy.closeIncognito()` ends it.
- `searchengine(engineId, contextId)` and `transport(contextId)` both namespace onto the SAME underlying IPC client as the rest of piggy — no separate connection, no new protocol, same named-pipe transport `PiggyFind`/`PiggyProvide`/etc already use.
- Actual piggycpp wiring (dispatcher registration API, JSON parsing library) is explicitly deferred to a dedicated future session — do not mix it into profile/search/transport work.

---

## NEXT SESSION — two things queued

### 1. Tor service setup how-to (requested, separate doc)
A practical guide: installing/running the `tor` daemon, `torrc` control-port config (password vs cookie auth), to actually pair with the already-built `packages/transport/tor` code. This is infra/ops, not more C++ — should be a standalone reference doc, not mixed into `packages/transport/docs.md`.

### 2. packages/devtools — the heavy one, NOT STARTED
Goal: recreate what happens when a user clicks "Inspect" — Network tab (every request/response, full headers/body), WebSocket frames (direction-tagged, JSON pretty-printed), DOM snapshots + live mutations, JS runtime (console, exceptions, call stacks, heap), cookie/storage writes as they happen, page lifecycle events (load, paint, navigation, crash) — via the Chrome DevTools Protocol (CDP), through Qt.

**Confirmed hard constraint:** Qt6 WebEngine does not natively expose CDP the way a real Chromium build does.

**Research needed before writing any code:**
1. How Chromium itself implements the DevTools protocol internally — which process/Mojo interfaces actually back Network/Page/Runtime/DOM domains under the hood.
2. What Qt6 WebEngine APIs could expose equivalent data: `QWebEngineDevToolsResourceHandler`, the Chromium remote-debugging port that `QWebEngineProfile` can enable (`--remote-debugging-port`), `QWebChannel`, page/webview Qt signals (load progress, JS console messages, etc.) — inventory what's actually available before assuming anything is or isn't possible.
3. Combine (1) and (2): given what Qt actually exposes, what's the closest achievable equivalent to real CDP-driven DevTools, and where are the hard walls (things Chromium's internal implementation does that Qt's public API genuinely cannot reach)?

This is expected to be the heaviest package in the project so far — budget real research time before any code gets written, same as the fingerprinting research phase before `packages/profile`.