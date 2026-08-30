# Session Handoff — Nothing Central

## What we built today

### 1. Nothing Central monorepo
- Org: `Nothing-central` on GitHub
- Repo: `git@github.com:Nothing-central/nothing.git`
- Local: `~/dev/nothing/`

### Structure
nothing/
├── apps/
│   ├── browser/        ← Sabre Browser (Qt6, daily driver)
│   ├── scraper/        ← Nothing Browser (scraping)
│   └── private/        ← Nothing Private Browser
├── packages/
│   ├── adblock/        ← DONE — working C++ engine
│   ├── profile/        ← NEXT — fingerprint spoofing
│   │   ├── fingerprint/
│   │   ├── identity/
│   │   └── shared/
│   ├── storage/
│   │   ├── persistent/
│   │   └── volatile/
│   ├── search/
│   ├── transport/
│   │   ├── tor/
│   │   └── incognito/
│   ├── devtools/       ← CDP through Qt
│   └── piggycpp/
├── piggy/              ← JS scraping library
└── docs/

---

## packages/adblock — COMPLETE ✅

### Files built
- `include/tokenizer.h` + `src/tokenizer.cpp`
- `include/token_selector.h` + `src/token_selector.cpp`
- `include/flat_multimap.h` (header only)
- `include/network_filter.h` + `src/network_filter.cpp`
- `include/request.h` + `src/request.cpp`
- `include/blocker.h` + `src/blocker.cpp`
- `src/test_tokenizer.cpp`
- `CMakeLists.txt`
- `DOCS.md`

### How to compile and test
```bash
cd ~/dev/nothing/packages/adblock
g++ -std=c++17 src/tokenizer.cpp src/token_selector.cpp \
    src/network_filter.cpp src/request.cpp src/blocker.cpp \
    src/test_tokenizer.cpp -o test_adblock && ./test_adblock
```

### What works
- EasyList/uBlock filter syntax parsing
- Tokenizer + rarest token selection
- FlatMultiMap binary search lookup
- Block/allow/exception/important decisions
- Domain filtering ($domain=)
- Content type filtering ($script, $image etc)
- Third party detection

### Known small bug
- Exception result flag not returning correctly
  when no block rule fires first (low priority)

### What is NOT done yet
- Regex filter support (IS_REGEX patterns with *)
- Cosmetic filtering (DOM element hiding)
- Scriptlet injection
- Filter list auto-updater
- Qt network interceptor integration

---

## packages/profile — NEXT SESSION 🔨

### What to build
Fingerprint spoofing engine. Study Firefox source first.

### Firefox folders to analyze
```
toolkit/components/resistfingerprinting/  ← GOLDMINE
dom/base/Navigator.cpp
dom/base/Navigator.h
dom/base/Screen.cpp
dom/base/Screen.h
dom/canvas/
dom/media/webaudio/
gfx/thebes/gfxPlatformFontList.cpp
dom/battery/BatteryManager.cpp           ← already studied
```

### Surfaces to spoof
- Canvas — pixel noise injection
- WebGL — fake VENDOR + RENDERER strings
- Navigator — UA, hardwareConcurrency, deviceMemory, languages
- Screen — fake resolution, colorDepth
- Audio — AudioContext sampleRate noise
- Font — block font enumeration
- Battery — round level to 10%, time to 15min intervals
- UA-CH — navigator.userAgentData spoofing

### Spoofing strategy
- All values generated from a SINGLE seed per session
- Seed changes every session (volatile) or every N days (persistent)
- All surfaces must be CONSISTENT with each other
  (fake Chrome UA must match fake Chrome WebGL strings etc)
- Use xorshift PRNG (same as Nothing Private Browser already uses)
- Battery — level rounded to 10% + randomized within bucket,
            charging state randomized per session,
            remaining time rounded to 15min + noise
            SOURCE: dom/battery/BatteryManager.cpp

### AI prompt for next session
Feed these Firefox folders to an AI and ask:
> I am building a browser fingerprint spoofing engine in C++
> for a Qt6 WebEngine browser. Analyze these Firefox source files
> and explain for each fingerprinting surface:
> 1. Where the value originates in the browser
> 2. What makes it unique per device
> 3. Firefox's exact spoofing strategy
> 4. How to implement the spoof at Qt6 WebEngine level
> Focus on toolkit/components/resistfingerprinting/ first.
> I want consistent cross-surface spoofing that passes CreepJS.

---

## Other context

### Ernest's existing browsers
- Nothing Browser (scraper) — `github.com/BunElysiaReact/nothing-browser`
- Nothing Private Browser — `github.com/ernest-tech-house-co-operation/nothing-private-browser`
- Both are Qt6 + Chromium WebEngine
- Both already have fingerprint spoofing (xorshift canvas noise)
- Goal is to unify fingerprint engine into `packages/profile`

### Mozilla-central
- Cloned at `~/dev/mozilla-central` (474k files, 60GB)
- Built successfully (2 hour build)
- Sabre branding applied in `browser/branding/sabre/`
- Decision: NOT using Gecko, staying with Qt6
- Mozilla-central kept for reference/research only

### Dev environment
- OS: Deepin Linux (Debian based)
- Machine: ThinkPad, i5 8th gen, 8GB RAM
- Storage: 200GB Linux partition (freed after deleting Windows)
- Git: Ernest12287 / peaseernest8@gmail.com
- GitHub org: Nothing-central
- Local dev: ~/dev/

---

## Priority order for next sessions

1. `packages/profile/fingerprint/` — spoof all surfaces
2. `packages/profile/identity/` — browser ID generation
3. `packages/devtools/` — CDP through Qt
4. `packages/transport/tor/` — Tor integration
5. `apps/scraper/` — new scraper browser using all packages
6. `packages/adblock/` — finish regex + cosmetic filtering
7. `apps/browser/` — Sabre Browser
8. `apps/private/` — Nothing Private Browser
```
