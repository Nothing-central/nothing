# packages/search — Search Engine Layer

Shared search-engine configuration and switching across every Nothing
ecosystem browser (Sabre, Nothing Browser scraper, Nothing Private Browser)
and the `piggy` JS/TS library. One list of engines, one place to add a
custom instance, switchable per-context (normal browsing, an incognito
session, or a scraper instance) independently.

## What's in here

```
packages/search/
├── include/
│   ├── search_engine.h            One engine's data: id, display name,
│   │                              query/suggest URL templates, home URL,
│   │                              whether it requires Tor (ahmia).
│   ├── search_engine_registry.h   All built-in engines (DuckDuckGo, Google,
│   │                              Bing, Brave Search, SearXNG, Ahmia) plus
│   │                              RegisterCustom() for user-added instances
│   │                              (e.g. a self-hosted SearXNG).
│   ├── search_engine_manager.h    contextId -> active engine id. Every
│   │                              context always resolves to *some* engine
│   │                              (falls back to DuckDuckGo if none set).
│   ├── search_ipc_handlers.h      Bridges manager calls to piggy's existing
│   │                              named-pipe IPC — same transport as
│   │                              PiggyFind/PiggyProvide, no new protocol.
│   └── search_suggest_cache.h     Caches autocomplete responses per
│                                  (engineId, query prefix) with a TTL.
├── src/                           Implementations of the above.
└── js/searchengine.js             The piggy-facing JS/TS wrapper.
```

## How contexts work

Same `contextId` pattern as `packages/profile/identity/`:
- `"default"` — normal browsing, one engine active across all normal tabs.
- An incognito session's contextId (from `IncognitoSession::Start()`) — its
  own independently-set engine, cleared when the session ends
  (`SearchEngineManager::ClearContext`, called alongside
  `IdentityManager::DestroyIdentity`).
- Any id the scraper/piggy side wants to use for a specific automation run.

Switching the engine in one context never affects another. A user can run
Google in their normal window and DuckDuckGo in incognito simultaneously.

## Using search from the JS/TS library

```ts
import { piggy, searchengine } from "nothing-browser"

const engine = searchengine("duckduckgo")
await engine.use("google")                 // switch anytime
await engine.registerCustom(
  "searxng-mine", "My SearXNG", "https://searx.example.com"
)
const { url } = await engine.query("weather in Nairobi")
```

Every call goes over the same IPC pipe as the rest of piggy — this is not
a separate connection or protocol, `searchengine()` is just another
namespace on the same client, the same way `piggy.site` is.

### Search APIs exposed

| JS call | IPC command | What it does |
|---|---|---|
| `engine.use(id)` | `search.setEngine` | Sets the active engine for this context. Throws if `id` isn't registered. |
| `engine.current()` | `search.getEngine` | Returns the active engine's `{id, displayName, homeUrl, requiresTor}` for this context. |
| `engine.list()` | `search.listEngines` | Returns every registered engine (built-in + custom), each with `{id, displayName, requiresTor, isCustom}`. |
| `engine.registerCustom(id, displayName, baseUrl)` | `search.registerCustom` | Adds a new SearXNG-style instance to the registry. Does not switch to it unless `{ switchTo: true }` (the default). |
| `engine.query(text)` | `search.buildQueryUrl` | Builds the full search URL for `text` using the context's active engine — does not navigate, just returns `{url}`. |

## Incognito, exposed to the JS library

Every browsing context — including a full incognito session — is reachable
from `piggy` by contextId, not just from the native browser UI. Incognito
is not a separate JS API; it's a flag on the calls you already use.

```ts
// Regular navigation — uses the "default" context's identity and search engine
await piggy.goto("https://example.com")

// Same call, routed through a fresh incognito identity for this one navigation
await piggy.goto("https://example.com", { useIncognito: true })
```

What happens under the hood when `useIncognito: true` is passed:
1. If no incognito session is currently open for this `piggy` client,
   `IncognitoSession::Start()` is called — a brand-new `IdentityBundle`
   is created (fresh navigator/screen/WebGL/audio profile, fresh
   `SessionKeyStore`), and `SearchEngineManager` gets a matching empty
   context (defaults to DuckDuckGo until you set one).
2. The navigation runs entirely under that incognito `contextId` — its own
   fingerprint, its own search engine choice, no correlation with the
   `"default"` context's identity or history.
3. The incognito session stays open across subsequent `{ useIncognito: true }`
   calls from the same `piggy` client — it is **one session**, matching
   `packages/profile/incognito`'s "one identity per session" rule, not a
   brand-new identity every call.
4. Calling `piggy.closeIncognito()` (or closing the underlying browser
   window/client) ends the session: `IncognitoSession::End()` destroys the
   identity, `SearchEngineManager::ClearContext()` drops its engine choice.
   Reopening incognito after that starts completely fresh.

Setting a search engine for the incognito context works exactly like the
default context, just pass the incognito contextId explicitly if you're
not going through `piggy.goto`'s shorthand:

```ts
const incognitoEngine = searchengine("duckduckgo", piggy.incognitoContextId)
await incognitoEngine.use("brave")
```

## For contributors

- Add a new built-in engine: edit `RegisterBuiltins()` in
  `search_engine_registry.h`. Don't add engine data anywhere else —
  `SearchEngineManager` and the IPC layer only ever read from the registry.
- Add a new IPC command: add a case in `SearchIpcHandlers::TryHandle` and a
  matching `js/searchengine.js` method. Keep the naming pattern
  `search.<verb><Noun>` for consistency with existing commands.
- `search_ipc_handlers.cpp`'s JSON parsing (`ExtractStringField`) is a
  placeholder — replace with piggycpp's existing JSON library once
  confirmed, per the CMakeLists.txt roadmap note.
- Onion-only engines (currently just Ahmia) are flagged `requiresTor = true`
  as a signal, not an enforcement — enforcing "block navigation unless Tor
  is active" belongs in `packages/transport/tor`, not here.
