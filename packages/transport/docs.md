# packages/transport — Tor & Proxy Routing

Two independent routing layers, both keyed by the same `contextId` pattern
as `packages/profile/identity` and `packages/search`: `tor/` handles Tor
SOCKS routing + circuit control, `proxy/` handles arbitrary HTTP/SOCKS
proxy lists from a file, a remote URL, or a single entry.

## Layout

```
packages/transport/
├── tor/
│   ├── include/tor_controller.h       Control-port protocol: connect,
│   │                                  authenticate (password or cookie),
│   │                                  NEWNYM (new circuit), bootstrap
│   │                                  status, circuit-status query.
│   ├── include/tor_config.h           SOCKS proxy endpoint (127.0.0.1:9050
│   │                                  by default) — what actually gets
│   │                                  handed to Qt for traffic routing.
│   ├── include/tor_context_manager.h  Per-context Tor on/off toggle.
│   └── src/tor_controller.cpp         Raw socket implementation of the
│                                      control protocol.
├── proxy/
│   ├── include/proxy_entry.h          One proxy: scheme, host, port,
│   │                                  optional auth. Parses both
│   │                                  "scheme://user:pass@host:port" and
│   │                                  bare "host:port".
│   ├── include/proxy_source.h         Loads a proxy LIST from one of three
│   │                                  input shapes: local file path,
│   │                                  remote .txt URL, or a single proxy
│   │                                  string treated as a one-entry list.
│   ├── include/proxy_pool.h           Holds parsed entries for one context,
│   │                                  hands one out per rotation mode
│   │                                  (Sequential, Random, StickyPerOrigin).
│   └── include/proxy_context_manager.h  Per-context proxy pool + mode,
│                                        same contextId pattern as everything
│                                        else in the ecosystem.
└── js/transport.js                    JS/TS surface: useTor(), useProxy(),
                                        newTorIdentity(), status().
```

## Tor: this is NOT the same thing as a normal proxy

Tor is enabled per-context via `TorContextManager::SetEnabled`, which lazily
connects to Tor's **control port** (9051) the first time it's turned on for
any context, then just flips a per-context boolean. The actual traffic
routing goes through Tor's **SOCKS port** (9050, from `TorSocksConfig`) —
this is a completely separate connection from the control port, and it's
what actually gets set as the SOCKS proxy on the `QWebEngineProfile`/network
stack for that context.

**Known limitation:** Tor's default configuration exposes one SOCKS port
for the whole daemon — there's no native "one circuit per contextId."
`RequestNewIdentity()` (`SIGNAL NEWNYM`) rotates the circuit for **every**
context currently routed through Tor, not just one. If per-context circuit
isolation is needed later, the fix is running multiple `SocksPort` lines
in `torrc` (each with its own `IsolateSOCKSAuth` or a distinct port) and
giving each context its own `TorSocksConfig` — not implemented yet.

## Proxy: the three input shapes

`ProxySource::Load()` auto-detects which of your three input kinds it got:

| Input | Detected as | Example |
|---|---|---|
| A path with no `://` | Local file, read line by line | `./myproxies.txt` |
| An `http(s)://` URL ending in `.txt` | Remote list, fetched then parsed line by line | `https://example.com/proxies.txt` |
| Anything else with a `host:port` shape | Single entry, wrapped as a one-line list | `socks5://user:pass@1.2.3.4:1080` or bare `1.2.3.4:1080` |

**Known gap:** `ProxySource::FetchFromUrl` is a stub — it needs to be wired
to whatever HTTP client the browser already uses for non-page requests
(Qt's `QNetworkAccessManager` or a piggycpp utility, whichever exists).
Local file and single-entry loading both work today; remote list URLs do
not until that's wired in.

## Precedence: Tor + proxy enabled on the same context

Not currently resolved in code — if a context has both `TorContextManager`
and `ProxyContextManager` enabled simultaneously, whichever code wires the
actual network layer needs to pick one (chaining proxy-over-Tor is possible
but adds real complexity and is not assumed here). Recommendation once
that wiring happens: Tor takes precedence, proxy is ignored with a warning,
since Tor already anonymizes the exit IP and proxy-over-Tor is an advanced/
niche case, not a default behavior.

## Using it from the JS/TS library

```ts
import { piggy, transport } from "nothing-browser"

// Tor — default is false, opt-in per context
const t = transport() // "default" context
await t.useTor(true)
await t.newTorIdentity()          // rotate circuit

// Proxy — any of the three input shapes work the same way
await t.useProxy("./myproxies.txt")
await t.useProxy("https://example.com/proxies.txt", { rotation: "random" })
await t.useProxy("socks5://user:pass@1.2.3.4:1080")

await t.status()   // { torEnabled, proxyEnabled, proxyCount, ... }
```

Same `useIncognito`-style shorthand as search — a context created via
`IncognitoSession::Start()` gets its own independent Tor/proxy state, off
by default, cleared on `IncognitoSession::End()` (both `TorContextManager`
and `ProxyContextManager` expose `ClearContext()` for this, called
alongside `IdentityManager::DestroyIdentity` and
`SearchEngineManager::ClearContext`).

## For contributors

- Tor control-port parsing (`SendCommand`'s response handling) is minimal —
  it checks for `"250 OK"` substrings rather than fully parsing Tor's
  reply-line grammar. Fine for the commands used today (AUTHENTICATE,
  SIGNAL NEWNYM, two GETINFO queries); extend `SendCommand`/add real
  reply parsing before adding commands with multi-line or structured replies.
- `proxy_entry.cpp`'s parser is deliberately permissive (defaults to `http`
  scheme, tolerates missing auth) — if you tighten validation, keep bare
  `host:port` (no scheme) working, since that's the most common format in
  scraped/free proxy lists.
- Both `tor_context_manager.h` and `proxy_context_manager.h` are header-only
  except for `tor_controller.cpp`'s socket code — keep new per-context
  logic header-only where possible, consistent with the rest of the
  ecosystem's manager classes.
```

`packages/transport/CMakeLists.txt`
```cmake
cmake_minimum_required(VERSION 3.20)
project(nothing-transport VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ─── tor ─────────────────────────────────────────────────────────
set(TOR_SOURCES
    tor/src/tor_controller.cpp
)

set(TOR_HEADERS
    tor/include/tor_controller.h
    tor/include/tor_config.h            # header only — no .cpp needed
    tor/include/tor_context_manager.h   # header only — no .cpp needed
)

add_library(nothing-tor STATIC ${TOR_SOURCES})

target_include_directories(nothing-tor PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/tor/include
)

if(WIN32)
    target_link_libraries(nothing-tor PUBLIC ws2_32)
endif()

# ─── proxy ───────────────────────────────────────────────────────
set(PROXY_SOURCES
    proxy/src/proxy_entry.cpp
    proxy/src/proxy_source.cpp
)

set(PROXY_HEADERS
    proxy/include/proxy_entry.h
    proxy/include/proxy_source.h
    proxy/include/proxy_pool.h              # header only — no .cpp needed
    proxy/include/proxy_context_manager.h   # header only — no .cpp needed
)

add_library(nothing-proxy STATIC ${PROXY_SOURCES})

target_include_directories(nothing-proxy PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/proxy/include
)

# ─── Test binaries ───────────────────────────────────────────────
option(TRANSPORT_BUILD_TESTS "Build transport tests" OFF)

if(TRANSPORT_BUILD_TESTS)
    add_executable(test_tor tor/src/test_tor_controller.cpp)
    target_link_libraries(test_tor PRIVATE nothing-tor)

    add_executable(test_proxy proxy/src/test_proxy.cpp)
    target_link_libraries(test_proxy PRIVATE nothing-proxy)
endif()

# ─── Roadmap ─────────────────────────────────────────────────────
# Files to add in future sessions:
#   proxy/src/proxy_http_fetch.cpp — wires ProxySource::FetchFromUrl to
#     Qt's QNetworkAccessManager or piggycpp's HTTP utility.
#   proxy/include/proxy_http_fetch.h
#   tor/src/tor_ipc_handlers.cpp — bridges TorContextManager to piggy's
#     IPC (transport.setTorEnabled, transport.newTorIdentity), same
#     pattern as packages/search/src/search_ipc_handlers.cpp.
#   tor/include/tor_ipc_handlers.h
#   proxy/src/proxy_ipc_handlers.cpp — bridges ProxyContextManager
#     (transport.setProxy, transport.clearProxy, transport.status).
#   proxy/include/proxy_ipc_handlers.h
#   Decide + implement Tor+proxy precedence (see docs.md) wherever the
#     actual QWebEngineProfile network setup happens.
