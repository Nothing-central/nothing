# packages/profile — Fingerprint & Identity System

This package is the source of truth for "who does this browser instance
pretend to be" — one coherent identity (navigator strings, screen size,
WebGL vendor/renderer, audio params, font visibility) applied consistently
across every surface a website can query, plus per-site noise so canvas/audio
readbacks don't become a stable cross-site tracking key.

## Folder layout and what owns what

```
packages/profile/
├── fingerprint/          Pure C++ core. No Qt. Builds and tests standalone.
│   ├── include/          All struct/class declarations (the actual "what
│   │                     values does an identity have" definitions).
│   ├── src/               Implementations of everything in include/.
│   ├── vendor/            Verbatim Firefox StandardFonts-*.inc files.
│   ├── tools/gen_fonts.py Regenerates include/generated_fonts.h from Firefox
│   │                     source if you ever need the gfxFcPlatformFontList.cpp
│   │                     style arrays instead of the vendor/ .inc files.
│   ├── inject/bootstrap.js  Standalone reference JS (navigator/screen/Intl
│   │                     overrides) — kept for reference; the version that
│   │                     actually ships is regenerated inline by
│   │                     qt/FingerprintSpoofer.cpp, not loaded from this file.
│   ├── test/fingerprint_report.cpp  Builds a full identity + dumps it as
│   │                     JSON. Proves the C++ layer is correct in isolation,
│   │                     with no Qt/browser involved. Run this after any
│   │                     change to include/ or src/ before touching qt/.
│   └── qt/                Qt-dependent glue. Depends on fingerprint/include/
│                          for all real data — does not generate or own any
│                          identity values itself.
│
├── identity/             Owns "which identity is active for which context."
│   ├── include/identity_bundle.h    One struct: everything that makes up
│   │                     one identity (profiles + its SessionKeyStore).
│   └── include/identity_manager.h + src/identity_manager.cpp
│                          Map of contextId -> IdentityBundle. Creates,
│                          looks up, and destroys identities. Knows nothing
│                          about Qt, injection, or incognito policy — just
│                          bookkeeping.
│
└── incognito/            Policy layer on top of identity/.
    └── include/incognito_session.h  Start()/End() — tells IdentityManager
                          to create a fresh identity for a new incognito
                          session and destroy it when that session closes.
                          Does not touch or read the previous identity.
```

## Data flow — who calls what

```
App startup (normal browsing)
  └─► IdentityManager::CreateIdentity("default", family, os, w, h)
        └─► fingerprint/include: BuildNavigatorProfile, ScreenProfile::DeriveFrom,
            WebGLProfile{}, AudioProfile{}, GenerateRandomKey32()
        └─► stores one IdentityBundle under contextId "default"

New incognito window opened
  └─► IncognitoSession::Start(manager, family, os, w, h)
        └─► generates a fresh contextId ("incognito-<hex>")
        └─► calls manager.CreateIdentity(contextId, ...) — same path as
            above, brand-new SessionKeyStore, no link to "default" or any
            prior incognito session
        └─► returns contextId — caller (browser window code) stores this
            on the incognito window/profile object

Every tab in that incognito window
  └─► shares the SAME contextId → SAME IdentityBundle → SAME identity.
      One incognito session = one identity, regardless of tab count.
      This is deliberate: matches real Chrome/Firefox incognito behavior.

Incognito window closes
  └─► IncognitoSession::End(manager, contextId)
        └─► manager.DestroyIdentity(contextId) — bundle is gone, including
            its SessionKeyStore. Reopening incognito later starts from zero.

Every top-level navigation (normal or incognito tab)
  └─► browser code looks up: IdentityBundle* b = manager.GetIdentity(contextIdForThisWindow)
  └─► FingerprintSpoofer spoofer(b->nav, b->screen, b->webgl, b->audio, b->keys)
  └─► QString script = spoofer.injectionScript(currentOrigin)
        └─► qt/profile_to_json.cpp serializes b->nav/screen/webgl/audio to JSON
        └─► qt/FingerprintSpoofer.cpp derives the per-origin noise key via
            b->keys.PerSiteKey(origin) [fingerprint/include/site_key.h], embeds
            it + the JSON as constants in a JS string, appends the crypto +
            hook code, returns the full script
  └─► browser code injects `script` via QWebEngineScript,
      DocumentCreation, MainWorld, before page JS runs
```

## A side note can contributors please create file names with what is actually does and also keep the flow close to the original flow don't just change it i am human 
## What each file is actually responsible for

| File | Responsibility |
|---|---|
| `fingerprint/include/navigator_profile.h` + `src/navigator_profile.cpp` | Builds a coherent `NavigatorProfile` (UA, platform, vendor, hardwareConcurrency, etc.) from one `(family, os, seed)` decision. Nothing else should set navigator fields independently. |
| `fingerprint/include/screen_profile.h` + `src/screen_profile.cpp` | Screen/window size spoofing — the letterboxing-style stepped dimensions, devicePixelRatio, orientation. |
| `fingerprint/include/webgl_profile.h` | Static struct of WebGL vendor/renderer/caps/extensions for one identity. No per-call logic — Qt reads it as fixed data. |
| `fingerprint/include/audio_profile.h` | Static struct of AudioContext params (sampleRate, latency) for one identity. |
| `fingerprint/include/site_key.h` + `src/site_key.cpp` (uses `src/sha256.h`) | `SessionKeyStore` — one `sessionUuid` per identity, derives a per-origin key via HMAC-SHA256, and a per-call key from that + content bytes. This is what makes canvas/audio noise vary per-site instead of being a fixed session-wide seed. |
| `fingerprint/include/canvas_interceptor.h` / `audio_interceptor.h` + their `.cpp`s | Reference C++ implementations of the noise algorithm, used only by `test/fingerprint_report.cpp` to prove correctness offline. **Not used by the live browser** — the JS reimplementation inside `qt/FingerprintSpoofer.cpp` is what actually runs, because canvas/audio readback can only be intercepted from page-context JS in Qt6 WebEngine, not native C++. |
| `fingerprint/include/font_visibility.h` + `src/font_visibility.cpp` + `generated_fonts_direct.h` | Font visibility tiering (Base/LangPack/User/Hidden), loaded from vendored Firefox `StandardFonts-*.inc` files. **Known gap:** cannot currently be enforced from `qt/`'s JS injection layer — `document.fonts.check()` and `@font-face src: local()` resolve in Blink's native font code before any page JS runs. This logic is ready for a future native Blink/Chromium hook if one is ever added; until then it is not wired into the live browser. |
| `fingerprint/qt/profile_to_json.h/.cpp` | Pure struct → `QJsonObject` conversion. No logic, no identity generation — just makes `fingerprint/include` data consumable by Qt/JS. |
| `fingerprint/qt/FingerprintSpoofer.h/.cpp` | Builds the actual injection script for one navigation: serializes the identity to JSON, derives the per-origin key, embeds a JS reimplementation of HMAC-SHA256/XorShift128Plus (duplicated from `site_key.cpp`/`sha256.h` by necessity — it must run in the page's JS context), and appends all the override/hook code (navigator, screen, canvas, audio, WebGL, userAgentData, WebRTC, timezone). |
| `identity/include/identity_bundle.h` | One struct bundling everything that makes up an identity — what a context "is." |
| `identity/include/identity_manager.h` + `src/identity_manager.cpp` | `contextId -> IdentityBundle` map. Create/Get/Destroy. The only place identities are created or torn down. |
| `incognito/include/incognito_session.h` | Thin policy wrapper: `Start()` creates a fresh identity under a random contextId, `End()` destroys it. Contains no identity-generation logic of its own — delegates entirely to `IdentityManager`. |

## For contributors

- **Changing an identity value** (e.g. a new navigator field, a different WebGL cap): edit the relevant struct/builder in `fingerprint/include/` + `src/`. Do not add fields directly in `qt/` — `qt/` only serializes what already exists upstream.
- **Changing what gets spoofed in the live browser**: edit the JS block in `qt/FingerprintSpoofer.cpp`. Note the crypto duplication above — if you fix a bug in `sha256.h`/`site_key.cpp`, check whether the same bug exists in the JS copy inside `FingerprintSpoofer.cpp` and fix both.
- **Verifying a change**: rebuild and run `fingerprint/test/fingerprint_report.cpp` first (fast, no Qt, no browser needed) to confirm the underlying data is correct, before testing it live in the browser.
- **Incognito is not a separate identity system** — it's `IdentityManager` used with a throwaway `contextId`. Don't add incognito-specific fields to `IdentityBundle`; if incognito ever needs to differ from normal browsing (e.g. blocking a feature entirely), that's a flag passed into `CreateIdentity`, not a parallel code path.
- **One incognito session = one identity**, shared across all its tabs. Do not generate a new identity per-tab inside an incognito window — that would make tabs within the same session distinguishable from each other, which is worse than sharing one.
