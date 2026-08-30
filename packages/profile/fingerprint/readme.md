# Firefox Fingerprinting Surfaces → C++/Qt6 WebEngine Spoofing Guide

A complete walkthrough of all six fingerprinting surfaces, the exact origin of each value, what makes it unique per device, Firefox's RFP strategy (and its gaps), and how to *beat* Firefox in your Qt6 WebEngine implementation.

---

## Architectural Foundations — Read This First

### Firefox's tiered RFP system (you should mirror this)

Firefox has four protection levels (`nsRFPService.h:719-724`):

```
RFP       → privacy.resistFingerprinting           (Tor-equivalent; ALL ~80 targets active)
FPP       → privacy.fingerprintingProtection       (opt-in target list, ~8 targets)
Baseline  → privacy.baselineFingerprintingProtection (ship-by-default, 3 targets)
None      → no protection
```

The dispatch is centralized in **one function** (`nsRFPService::IsRFPEnabledFor`, `nsRFPService.cpp:335`):

```cpp
bool nsRFPService::IsRFPEnabledFor(bool isPrivate, RFPTarget target,
                                    Maybe<RFPTargetSet> overrides) {
  auto mode = GetFingerprintingProtectionType(isPrivate);
  if (mode == None) return false;
  if (auto r = HandleExceptionalRFPTargets(target, isPrivate, mode)) return *r;
  if (mode == RFP) return true;   // ← RFP mode: EVERY target is active
  if (overrides) return overrides->contains(target);
  return IsTargetActiveForMode(target, mode);
}
```

**For your Qt6 engine**: implement a single `enum class FPTarget : uint64_t` mirroring `RFPTarget`, plus a single dispatcher. Every getter in the engine calls `shouldResist(FPTarget::X)`. This is far cleaner than scattering `if (m_spoofMode)` checks.

### The X-macro target list

`RFPTargets.inc` is an X-macro that expands into:
- An enum (`enum class RFPTarget : uint64_t`)
- A string↔enum map for runtime overrides

The same file is reused by `RFPTargetsDefault.inc` and `RFPTargetsDefaultBaseline.inc` (which just contain `DESKTOP_DEFAULT(name)` / `ANDROID_DEFAULT(name)` macros). You can lift this pattern verbatim.

### Per-domain granular overrides

Firefox supports 5 scope patterns (`nsRFPService.h:698-707`):

```
{first-party domain}                          → only first-party
{first-party domain, *}                       → all under top-level
{*, third-party domain}                       → third-party under any top
{first-party domain, third-party domain}     → specific third-under-first
{*}                                           → global
```

These come from `privacy.fingerprintingProtection.overrides` pref as `+Target,-Target` strings. **Mirror this** — it's how Mozilla ships WebCompat fixes without disabling protection globally.

### Per-site randomization key (used by Canvas *and* WebGL noise)

```
sessionUUID  ──► HMAC_SHA256 ──► perSiteKey (32 bytes)
                  (topLevelSite)              │
                                                ▼
                                            HMAC_SHA256 ──► perCanvasKey (32 bytes)
                                            (canvasBytes)
```

- `sessionUUID` is generated lazily on first HTTP channel load, lives in parent process, persists until browser shuts down (`nsRFPService.h:683-693`).
- `perSiteKey = HMAC_SHA256(sessionUUID, topLevelSite)` (`nsRFPService.cpp:1427-1483`).
- The key travels with the document via `CookieJarSettings`.

**For Qt6**: generate one random 16-byte session UUID at startup. For each new top-level navigation, compute `perSiteKey = HMAC-SHA256(sessionUUID, origin)`. Cache it on the `QWebEnginePage`/profile. This is the single most important architectural decision — everything else depends on having a stable per-site random key.

---

## 1. Canvas Fingerprinting

### Where the value originates

When a page calls `ctx.fillText("Cwm fjordbank", 10, 50)`:
1. WebIDL binding → `CanvasRenderingContext2D::FillText` (`canvas/CanvasRenderingContext2D.cpp:4636`)
2. → `DrawOrMeasureText` (`:5093`)
3. → `CanvasBidiProcessor::DrawText` (`:4928`) which calls `mTextRun->Draw(...)` (`:5041`)
4. The `DrawTarget` is selected in `EnsureTarget()` (`:1741`) in priority order: WebGL-accelerated → shared GPU → software Skia

### What makes it unique per device

Five sources, in fingerprinting impact order:

1. **Font hinting + subpixel positioning** — FreeType (Linux), DirectWrite (Windows), CoreText (macOS) all snap glyph control points to the pixel grid differently. Fractional advance widths round differently per platform. This is the *dominant* source.
2. **Anti-aliasing gamma** — Skia's AA uses different gamma constants per platform.
3. **GPU driver rasterization** — when `DrawTargetWebgl` is selected, GPU `gl_FragCoord` precision and multisample patterns differ between NVIDIA/AMD/Intel.
4. **Floating-point path math** — `mozilla::gfx::Float` (32-bit) operations differ in ULP depending on SIMD path.
5. **UnpremultiplyData rounding** — when the canvas stores BGRA-premultiplied and JS reads RGBA-straight, the un-premultiply math rounds differently per backend.

Firefox explicitly does *not* normalize these. It intercepts the readback instead.

### Readback paths (where spoofing happens)

Three JS-callable readback entry points:
- `getImageData` → `GetImageDataArray` (`canvas/CanvasRenderingContext2D.cpp:6681`)
- `toDataURL` / `toBlob` → `CanvasRenderingContextHelper::ToBlob` (`canvas/CanvasRenderingContextHelper.cpp:46`)
- WebGL `readPixels` → `ClientWebGLContext::GetImageBuffer` (`canvas/ClientWebGLContext.cpp:1381`)

### Firefox's RFP canvas algorithm (the noise injection)

The complete algorithm, in `nsRFPService::RandomizeElements` (`nsRFPService.cpp:1868-1993`):

```
Step 1: Get perSiteKey (32 bytes) from CookieJarSettings

Step 2: Compute perCanvasKey:
   if (canvasSize < 2500 bytes):
       imageHash = HashBytes(canvasBytes)
       canvasKey[0..7] = SipHash(perSiteKey[0..15], imageHash)
       canvasKey[8..31] = XorShift128PlusRNG(imageHash || perSiteKey[16..23])
   else:
       canvasKey = HMAC_SHA256(perSiteKey, canvasBytes)   // 32 bytes

Step 3: Skip if canvas is uniform (all pixels identical)
   → returns NS_OK without noise

Step 4: Derive two RNGs from canvasKey:
   rng1 = XorShift128Plus(canvasKey[0..15])   // selects pixel + channel
   rnd3 = canvasKey[31]                        // number of noises
   canvasKey[31] = 0
   rng2 = XorShift128Plus(canvasKey[16..31])   // selects which bit to flip
   numNoises = clamp(rnd3, 20, 255)

Step 5: Flip numNoises bits:
   for each iteration:
     element = rng1.next() % moduloDivisor + aElementOffset   // skip alpha
     idx = groupSize * (rng1.next() % groupCount) + element * aBytesPerElement
     bit = rng2.next()
     aData[idx] ^= (0x2 >> (bit & 0x1))   // flips bit 1 OR bit 0
```

Key facts:
- **20-255 single-bit XORs** per canvas readback
- **Alpha channel excluded** (modulo divisor skips last element)
- **Uniform canvases skipped** (no noise added — prevents the "blank canvas has noise" tell)
- **Deterministic given content** — same canvas + same session + same site = same noise pattern
- **HMAC-SHA256 for ≥2500 bytes** (cryptographic), **SipHash+XorShift128 for smaller** (fast path)
- **Applied to BGRA-premultiplied buffer BEFORE `UnpremultiplyData`** — amplifies noise for low-alpha pixels

### Three modes (the RFPTarget dispatch)

```
ImageExtractionResult decision tree (canvas/CanvasUtils.cpp:373):
  if unrestricted principal → return real pixels
  if !IsImageExtractionAllowed → Placeholder (32 random bytes tiled, or all-white)
  if EfficientCanvasRandomization → encoder perturbs output bytes only
                                    (getImageData returns REAL pixels — known gap)
  if CanvasRandomization || WebGLRandomization → RandomizePixels (the noise algorithm)
  else → Unrestricted
```

### Firefox's gaps (what CreepJS catches)

1. **`EfficientCanvasRandomization` leaves `getImageData` untouched** — the default mode returns real pixels for `getImageData`. This is the #1 gap.
2. **Noise is content-derived** — fingerprinter changes 1 pixel → HMAC avalanche → completely different noise pattern → fingerprinter detects "noise is content-derived" via XOR of near-identical canvases.
3. **Alpha channel skipped** — fingerprinters hash on alpha-channel values.
4. **Uniform canvases get NO noise** — fingerprinter uses solid-color canvas as a "control" to detect noise budget.
5. **`captureStream()`/`MediaRecorder`** — captures the real canvas surface, never perturbed.
6. **`measureText()` width leak** — `TextMetrics` reflects real font rasterizer; not spoofed.

### Your Qt6 implementation strategy (beating Firefox)

```cpp
struct CanvasSpoofConfig {
    bool spoof_getImageData = true;        // ← close Firefox gap
    bool spoof_toDataURL = true;
    bool spoof_toBlob = true;
    bool spoof_readPixels = true;          // ← WebGL
    bool spoof_captureStream = true;        // ← close Firefox gap
    bool spoof_measureText = true;          // ← close Firefox gap (random LSB perturbation)
};

// In your canvas readback hook:
void CanvasReadbackInterceptor::perturb(uint8_t* rgba_buffer, 
                                         size_t width, size_t height,
                                         const Origin& origin) {
    auto perSiteKey = m_session.derivePerSiteKey(origin);
    auto perCanvasKey = HMAC_SHA256(perSiteKey, rgba_buffer, width*height*4);
    
    // Firefox's algorithm — proven
    XorShift128Plus rng1(perCanvasKey.first_16_bytes());
    XorShift128Plus rng2(perCanvasKey.last_16_bytes_sans_last());
    
    // IMPROVEMENT: also perturb alpha channel
    uint8_t numNoises = clamp(perCanvasKey.last_byte(), 20, 255);
    for (uint8_t i = 0; i < numNoises; i++) {
        uint32_t pixel = rng1.next() % (width * height);
        uint8_t channel = rng1.next() % 4;  // include alpha
        uint8_t bit = rng2.next() & 1;
        rgba_buffer[pixel*4 + channel] ^= (0x2 >> bit);
    }
    
    // IMPROVEMENT: also perturb measureText by ±1 LSB on width
}
```

For `toDataURL`/`toBlob`: perturb at the encoder byte level using the per-site key (this is what Firefox's `EfficientCanvasRandomization` does, but they leave `getImageData` unprotected — don't repeat that mistake).

For `captureStream`/`MediaRecorder`: intercept the frame-grab hook in Qt's WebEngine compositor; perturb each captured frame before it reaches the encoder.

---

## 2. WebGL Fingerprinting

### Where the value originates

JS-facing entry: `ClientWebGLContext::GetParameter` (`canvas/ClientWebGLContext.cpp:2169`). Three-tier dispatch:

| Tier | What | Source |
|---|---|---|
| 1 — cached | `MAX_TEXTURE_SIZE`, `MAX_VERTEX_ATTRIBS`, `MAX_VIEWPORT_DIMS`, etc. | Pre-computed `Limits` struct snapshot |
| 2 — hardcoded strings | `VENDOR`, `RENDERER`, `VERSION`, `SHADING_LANGUAGE_VERSION` | Compile-time constants |
| 3 — live driver | `STENCIL_VALUE_MASK`, `BLEND_COLOR`, `LINE_WIDTH`, etc. | IPC round-trip → `gl->fGetIntegerv`/`fGetString` |

The `Limits` snapshot is populated once at context creation by `MakeLimits` (`WebGLContextValidate.cpp:180-262`), shipped across IPC, and cached. **All `getParameter(MAX_*)` calls hit the cache, never the driver.**

### RENDERER/VENDOR unmasking

`WEBGL_debug_renderer_info` extension exposes `UNMASKED_VENDOR_WEBGL` (37445) and `UNMASKED_RENDERER_WEBGL` (37446). Default-disabled via `webgl_enable_debug_renderer_info` pref. Implementation in `ClientWebGLContext.cpp:2500-2557`:

```cpp
case UNMASKED_RENDERER_WEBGL:
    if (ShouldResistFingerprinting(WebGLRenderInfo) ||
        ShouldResistFingerprinting(WebGLRendererConstant))
        ret = "Mozilla";
    else
        ret = GetUnmaskedRenderer();  // → glGetString(GL_RENDERER)
        if (ret) ret = SanitizeRenderer(*ret);  // bucketize
```

Typical real strings (post-sanitization buckets):
- Intel iGPU → `"Intel(R) HD Graphics"`, `"Intel(R) Arc(TM) A750 Graphics"`
- NVIDIA → `"GeForce GTX 980, or similar"`, `"GeForce 8800 GTX, or similar"`
- AMD → `"Radeon HD 3200 Graphics"`, `"Radeon R9 200 Series"`
- Apple Silicon → `"Apple M1"`

### Firefox's RFP WebGL spoof

**Strings** (under `WebGLRenderInfo`):
- `UNMASKED_VENDOR_WEBGL` → `"Mozilla"`
- `UNMASKED_RENDERER_WEBGL` → `"Mozilla"`
- Bare `VENDOR` → `"Mozilla"`
- Bare `RENDERER` → `"Mozilla"`

**Caps clamped** (`WebGLContextValidate.cpp:435-478`) — the "common values" (from a 2013 Moto E and old MacBook):

| Cap | Common value |
|---|---|
| MAX_TEXTURE_SIZE | 2048 |
| MAX_CUBE_MAP_TEXTURE_SIZE | 2048 |
| MAX_RENDERBUFFER_SIZE | 2048 |
| MAX_VERTEX_TEXTURE_IMAGE_UNITS | 8 |
| MAX_TEXTURE_IMAGE_UNITS | 8 |
| MAX_COMBINED_TEXTURE_IMAGE_UNITS | 16 |
| MAX_VERTEX_ATTRIBS | 16 |
| MAX_VERTEX_UNIFORM_VECTORS | 256 |
| MAX_FRAGMENT_UNIFORM_VECTORS | 224 |
| MAX_VARYING_VECTORS | 8 |
| MAX_VIEWPORT_DIMS | 4096 |
| ALIASED_POINT_SIZE_RANGE | [1, 63] |
| ALIASED_LINE_WIDTH_RANGE | [1, 1] |

**Extensions hidden**:
- `WEBGL_debug_renderer_info` — gated by `webgl_enable_debug_renderer_info` (default false)
- `WEBGL_debug_shaders` — gated by `WebGLRenderInfo`

### Firefox's gaps (HUGE)

These caps/parameters are **NOT clamped under RFP**:

1. **WebGL2-only caps**: `MAX_3D_TEXTURE_SIZE`, `MAX_ARRAY_TEXTURE_LAYERS`, `MAX_UNIFORM_BUFFER_BINDINGS`, `UNIFORM_BUFFER_OFFSET_ALIGNMENT`, `MAX_SAMPLES`, `MAX_ELEMENT_INDEX`, `MAX_ELEMENTS_INDICES`, `MAX_ELEMENTS_VERTICES`, `MAX_TEXTURE_LOD_BIAS`, `MAX_VERTEX_OUTPUT_COMPONENTS`, `MAX_FRAGMENT_INPUT_COMPONENTS`, `MAX_VERTEX_UNIFORM_COMPONENTS`, `MAX_FRAGMENT_UNIFORM_COMPONENTS`, `MAX_VERTEX_UNIFORM_BLOCKS`, `MAX_FRAGMENT_UNIFORM_BLOCKS`, `MAX_COMBINED_UNIFORM_BLOCKS`, `MAX_COMBINED_FRAGMENT_UNIFORM_COMPONENTS`, `MAX_COMBINED_VERTEX_UNIFORM_COMPONENTS`, `MAX_UNIFORM_BLOCK_SIZE`, `MAX_SERVER_WAIT_TIMEOUT`, `MAX_VARYING_COMPONENTS`.

2. **Shader precision format table** — 12 entries (vertex/fragment × low/med/high float/int), all straight from driver. NOT clamped.

3. **`getSupportedExtensions()` list** — not filtered at all by RFP except for the 2 debug extensions. Driver-specific extensions like `WEBGL_compressed_texture_s3tc`, `EXT_texture_filter_anisotropic`, `EXT_disjoint_timer_query`, `WEBGL_compressed_texture_astc` all leak through.

4. **`COMPRESSED_TEXTURE_FORMATS` array** — driven by the extension list; leaks GPU codec support (S3TC=desktop Windows/Linux, ASTC=mobile, ETC=non-ANGLE).

5. **`getContextAttributes().antialias`** — flips to `false` if MSAA wasn't allocated (reveals WARP/SwiftShader/llvmpipe software renderer).

6. **`failIfMajorPerformanceCaveat: true`** — the *existence* of a context under this flag reveals hardware acceleration.

7. **`MAX_TEXTURE_MAX_ANISOTROPY_EXT`** — per-GPU (2/4/16).

8. **`MAX_VIEWS_OVR`** — per-GPU (2, only on macOS/D3D11.3+).

9. **Disjoint timer query counter bits** — per-GPU (32/64).

### Your Qt6 implementation strategy (beating Firefox)

```cpp
struct WebGLProfile {
    // Strings
    const char* vendor = "Mozilla";              // or "Google Inc. (Intel)" if spoofing Chrome
    const char* renderer = "Mozilla";
    const char* version = "WebGL 2.0";
    const char* shading_language_version = "WebGL GLSL ES 3.00";
    
    // All clamped caps (mirror Firefox)
    uint32_t max_texture_size = 2048;
    uint32_t max_cube_map_texture_size = 2048;
    uint32_t max_renderbuffer_size = 2048;
    uint32_t max_vertex_texture_image_units = 8;
    uint32_t max_texture_image_units = 8;
    uint32_t max_combined_texture_image_units = 16;
    uint32_t max_vertex_attribs = 16;
    uint32_t max_vertex_uniform_vectors = 256;
    uint32_t max_fragment_uniform_vectors = 224;
    uint32_t max_varying_vectors = 8;
    uint32_t max_viewport_dims = 4096;
    float aliased_point_size_range[2] = {1.0f, 63.0f};
    float aliased_line_width_range[2] = {1.0f, 1.0f};
    
    // BEAT FIREFOX: WebGL2 caps
    uint32_t max_3d_texture_size = 256;
    uint32_t max_array_texture_layers = 256;
    uint32_t max_uniform_buffer_bindings = 24;
    uint32_t uniform_buffer_offset_alignment = 32;
    uint32_t max_samples = 4;
    uint64_t max_element_index = 0xFFFFFFFFu;
    uint32_t max_elements_indices = 1048576;
    uint32_t max_elements_vertices = 1048576;
    float max_texture_lod_bias = 2.0f;
    uint32_t max_vertex_output_components = 64;
    uint32_t max_fragment_input_components = 60;
    uint32_t max_vertex_uniform_components = 4096;
    uint32_t max_fragment_uniform_components = 4096;
    uint32_t max_vertex_uniform_blocks = 12;
    uint32_t max_fragment_uniform_blocks = 12;
    uint32_t max_combined_uniform_blocks = 24;
    uint64_t max_combined_fragment_uniform_components = 24 * 4096;
    uint64_t max_combined_vertex_uniform_components = 24 * 4096;
    uint64_t max_uniform_block_size = 16384;
    uint64_t max_server_wait_timeout = 0;
    uint32_t max_varying_components = 60;
    
    // BEAT FIREFOX: shader precision (canonical desktop GL signature)
    struct ShaderPrecision { int rangeMin, rangeMax, precision; };
    ShaderPrecision vertex_low_float  = {127, 127, 23};
    ShaderPrecision vertex_medium_float = {127, 127, 23};
    ShaderPrecision vertex_high_float = {127, 127, 23};
    ShaderPrecision fragment_low_float = {127, 127, 23};
    ShaderPrecision fragment_medium_float = {127, 127, 23};
    ShaderPrecision fragment_high_float = {127, 127, 23};  // BEAT FIREFOX: blanked
    ShaderPrecision vertex_low_int    = {1, 30, 0};
    ShaderPrecision vertex_medium_int  = {1, 30, 0};
    ShaderPrecision vertex_high_int    = {1, 30, 0};
    ShaderPrecision fragment_low_int   = {1, 30, 0};
    ShaderPrecision fragment_medium_int = {1, 30, 0};
    ShaderPrecision fragment_high_int   = {1, 30, 0};
    
    // BEAT FIREFOX: filtered extension list
    std::vector<const char*> supported_extensions = {
        "ANGLE_instanced_arrays",
        "EXT_blend_minmax",
        "EXT_color_buffer_half_float",
        "EXT_disjoint_timer_query_webgl2",  // optional — see notes
        "EXT_float_blend",
        "EXT_frag_depth",
        "EXT_shader_texture_lod",
        "EXT_sRGB",
        "EXT_texture_compression_bptc",      // BEAT FIREFOX: pick one canonical codec
        "EXT_texture_compression_rgtc",
        "EXT_texture_filter_anisotropic",
        "EXT_texture_norm16",
        "OES_element_index_uint",
        "OES_fbo_render_mipmap",
        "OES_standard_derivatives",
        "OES_texture_float",
        "OES_texture_float_linear",
        "OES_texture_half_float",
        "OES_texture_half_float_linear",
        "OES_vertex_array_object",
        "WEBGL_color_buffer_float",
        "WEBGL_compressed_texture_s3tc",     // pick desktop set
        "WEBGL_compressed_texture_s3tc_srgb",
        "WEBGL_debug_renderer_info",        // expose but return "Mozilla"
        "WEBGL_debug_shaders",
        "WEBGL_depth_texture",
        "WEBGL_draw_buffers",
        "WEBGL_lose_context",
        "WEBGL_multi_draw",
        "WEBGL_provoking_vertex",
    };
    
    // Context attributes — never leak software renderer
    bool antialias = true;          // always true (Firefox flips to false on WARP)
    bool failIfMajorPerformanceCaveat = false;  // don't expose this
};
```

In your Chromium WebGLRenderingContext override, return these cached values from `getParameter()`. **The critical insight**: build the entire `WebGLProfile` struct once at engine init from your chosen spoofed identity, never read live from the GPU.

---

## 3. Navigator Fingerprinting

### Where each value originates

| Property | Origin | Spoofed value (Firefox RFP) |
|---|---|---|
| `userAgent` | `Navigator::GetUserAgent` (`base/Navigator.cpp:2113`) → `nsHttpHandler::BuildUserAgent` | `"Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:121.0) Gecko/20100101 Firefox/121.0"` |
| `platform` | `Navigator::GetPlatform` (`:2031`) — compile-time constant | NOT spoofed (returns host OS literal) |
| `oscpu` | `Navigator::GetOscpu` (`:458`) → `nsHttpHandler::GetOscpu` | `"Windows NT 10.0; Win64; x64"` (or platform-specific) |
| `appVersion` | `Navigator::GetAppVersion` (`:2065`) → HTTP handler | `"5.0 (Windows)"` |
| `appName` | `Navigator::GetAppName` (`:330`) | Hardcoded `"Netscape"` |
| `appCodeName` | `Navigator::GetAppCodeName` (`:298`) → HTTP handler | Always `"Mozilla"` |
| `product` | `Navigator::GetProduct` (`:499`) | Hardcoded `"Gecko"` |
| `productSub` | `Navigator::GetProductSub` (`:503`) | Hardcoded `"20100101"` (frozen since Bug 776376) |
| `vendor` | `Navigator::GetVendor` (`:495`) | Empty string `""` (Chrome returns `"Google Inc."`) |
| `vendorSub` | `Navigator::GetVendorSub` (`:497`) | Empty string `""` |
| `buildID` | `Navigator::GetBuildID` (`:621`) → `nsIXULAppInfo::GetAppBuildID` | `"20181001000000"` (frozen, even without RFP except on `*.mozilla.org`) |
| `hardwareConcurrency` | `Navigator::HardwareConcurrency` (`:698`) → `RuntimeService::ClampedHardwareConcurrency` | macOS→8, else→4 |
| `deviceMemory` | NOT EXPOSED | `undefined` (Firefox doesn't ship this) |
| `language`/`languages` | `Navigator::GetLanguage` (`:406`), `GetLanguages` (`:413`) → `intl.accept_languages` pref | Only spoofed if user opts into `privacy.spoof_english == 2` |
| `userAgentData` | NOT EXPOSED | `undefined` (Firefox doesn't ship UA-CH) |
| `maxTouchPoints` | `Navigator::MaxTouchPoints` (`:898`) → `WidgetUtils::GetMaxTouchPoints` | Win→10, Mac→0, Android/Linux→5 |
| `webdriver` | `Navigator::Webdriver` (`:2353`) | Not spoofed (returns true only during automation) |
| `cookieEnabled` | `Navigator::CookieEnabled` (`:558`) | Not spoofed |
| `onLine` | `Navigator::OnLine` (`:604`) | Always `true` under RFP |
| `pdfViewerEnabled` | `Navigator::PdfViewerEnabled` (`:529`) | `true` under RFP |
| `doNotTrack` | `Navigator::GetDoNotTrack` (`:679`) | Not spoofed (returns user pref) |
| `globalPrivacyControl` | `Navigator::GlobalPrivacyControl` (`:687`) | Not spoofed (returns user pref) |

### The spoofed OS tokens (`nsRFPService.h:36-60`)

Firefox keeps the real OS family (deliberate — comment at `nsRFPService.h:26-31` says spoofing OS as different would break keyboard shortcuts):

```cpp
#ifdef XP_WIN
#  define SPOOFED_UA_OS          "Windows NT 10.0; Win64; x64"
#  define SPOOFED_APPVERSION     "5.0 (Windows)"
#  define SPOOFED_OSCPU          "Windows NT 10.0; Win64; x64"
#  define SPOOFED_MAX_TOUCH_POINTS 10
#elif defined(XP_MACOSX)
#  define SPOOFED_UA_OS          "Macintosh; Intel Mac OS X 10.15"
#  define SPOOFED_APPVERSION     "5.0 (Macintosh)"
#  define SPOOFED_OSCPU          "Intel Mac OS X 10.15"
#  define SPOOFED_MAX_TOUCH_POINTS 0
#elif defined(MOZ_WIDGET_ANDROID)
#  define SPOOFED_UA_OS          "Android 10; Mobile"
#  define SPOOFED_APPVERSION     "5.0 (Android 10)"
#  define SPOOFED_OSCPU          "Linux armv81"
#  define SPOOFED_MAX_TOUCH_POINTS 5
#else
#  define SPOOFED_UA_OS          "X11; Linux x86_64"
#  define SPOOFED_APPVERSION     "5.0 (X11)"
#  define SPOOFED_OSCPU          "Linux x86_64"
#  define SPOOFED_MAX_TOUCH_POINTS 5
#endif
```

### Firefox's gaps

1. **`platform` NOT spoofed** — `RFPTarget::NavigatorPlatform` exists but is in no default overrides list. So under FPP/Baseline the real platform leaks; even under RFP the comment is explicit about not spoofing.
2. **`navigator.userAgent` version tracks real Firefox version** — a Firefox 130 user under RFP says `Firefox/130.0` while Tor users say `Firefox/115.0`. **Less anonymous than Tor.**
3. **Locale mismatch** — under RFP without `spoof_english == 2`, `navigator.language` returns real locale but `Intl.DateTimeFormat().resolvedOptions().locale` returns `en-US`. CreepJS specifically probes this.
4. **`hardwareConcurrency` always 4 (or 8 on Mac)** — distinguishable from broader Firefox population.
5. **`vendor === ""` and `userAgentData === undefined`** — these are Firefox-family tells.
6. **`productSub === "20100101"`** — Firefox-specific constant. Chrome returns `"20030107"`.
7. **`buildID === "20181001000000"`** — frozen since Firefox 63 (Oct 2018). Firefox-family tell.
8. **FPP mode doesn't spoof any navigator strings** — `RFPTargetsDefault.inc` has no Navigator* targets. Only under full RFP.

### Your Qt6 implementation strategy (beating Firefox)

Decide on ONE spoofed identity and apply it consistently everywhere:

```cpp
struct NavigatorProfile {
    // Pick a coherent identity
    enum class BrowserFamily { Firefox, Chrome, Safari } family;
    enum class OSFamily { Windows, macOS, Linux, Android } os;
    
    // Strings - all derived from above
    std::string user_agent;
    std::string platform;
    std::string oscpu;             // Firefox-only
    std::string app_version;
    std::string app_name = "Netscape";
    std::string app_code_name = "Mozilla";
    std::string product;           // "Gecko" or "Gecko"
    std::string product_sub;       // "20100101" (Firefox) or "20030107" (Chrome)
    std::string vendor;            // "" (Firefox) or "Google Inc." (Chrome)
    std::string vendor_sub = "";
    std::string build_id;          // Firefox-only, "20181001000000"
    
    // Numeric
    uint32_t hardware_concurrency;
    std::optional<double> device_memory;  // Chrome only
    uint32_t max_touch_points;
    
    // Locales (FORCE consistency)
    std::vector<std::string> languages;
    std::string intl_locale;       // for Intl.DateTimeFormat
    
    // Boolean
    bool pdf_viewer_enabled;
    bool webdriver = false;
    bool cookie_enabled = true;
    bool online = true;
    std::string do_not_track = "unspecified";
    bool global_privacy_control = false;
    
    // UA-CH (Chrome only)
    std::optional<UserAgentData> user_agent_data;
};
```

**Critical rules** for a consistent identity:

1. **Pick a target browser+OS combo and derive everything from it**. Don't mix Firefox strings with Chrome strings.

2. **Force locale coherence**: if spoofing as Firefox RFP, set `navigator.language = "en-US"`, `navigator.languages = ["en-US", "en"]`, AND patch the V8 Intl object's default locale to `"en-US"` AND set the Accept-Language HTTP header to `"en-US,en;q=0.5"`. Firefox only spoofs the JS engine locale (not navigator.language) unless `spoof_english == 2` — fix this.

3. **Force timezone coherence**: spoof `Intl.DateTimeFormat().resolvedOptions().timeZone = "Atlantic/Reykjavik"` (Firefox's choice — UTC year-round, no DST). Patch `Date.prototype.getTimezoneOffset()` to return 0.

4. **If spoofing Chrome**: must expose `navigator.userAgentData = {brands: [...], mobile: false, platform: "Windows"}` AND emit `sec-ch-ua`, `sec-ch-ua-mobile`, `sec-ch-ua-platform` HTTP request headers. The headers and the JS object must agree.

5. **Pin `hardwareConcurrency` to a tiered value**: round real count down to {2, 4, 8, 16}. Better yet, pin to 4 always for strongest anonymity (matches Firefox RFP).

6. **Pin `deviceMemory`**: if spoofing Chrome, round to nearest of {0.25, 0.5, 1, 2, 4, 8}.

7. **Patch `navigator.webdriver` to `false`** in strict mode (Firefox leaves it true during automation — fix this if you're not running automation).

8. **Force `doNotTrack` and `globalPrivacyControl` to common values** in strict mode (e.g., `doNotTrack = "unspecified"`, `globalPrivacyControl = false` — matches the median user).

9. **HTTP `User-Agent` header and JS `navigator.userAgent` MUST agree**. Firefox decouples them via separate `RFPTarget::NavigatorUserAgent` vs `RFPTarget::HttpUserAgent`. Don't do this — keep them in lockstep.

10. **Per-domain overrides**: support `+Target,-Target` syntax with the 5 scope patterns (Firefox's `nsRFPService.h:698-707`). This lets you ship WebCompat fixes for sites that break.

---

## 4. Screen Fingerprinting

### Modern Firefox approach (this is critical to understand)

Firefox **does NOT** round `screen.width` to multiples of 200 anymore. That was the pre-2020 design. The current approach is **two-layer**:

1. **Letterboxing** — physically shrinks the `<browser>` element to a stepped dimension via injected CSS, so the *actual CSS pixels* the page renders into are quantized.
2. **API spoofing** — every screen/window API returns the *letterboxed top-window inner size* (`BrowsingContext::TopInnerSizeSpoofedForRFP()`).

### Where each value originates

| Property | C++ getter | RFP behavior |
|---|---|---|
| `screen.width/height/left/top` | `nsScreen::GetRect` (`base/nsScreen.cpp:70`) | Stepped top-inner rect |
| `screen.availWidth/Height/Left/Top` | `nsScreen::GetAvailRect` (`:102`) | Same stepped top-inner rect |
| `screen.colorDepth/pixelDepth` | `nsScreen::PixelDepth` (`:47`) | Hardcoded **24** |
| `screen.colorGamut` | `nsScreen::ColorGamut` | Hardcoded `"srgb"` (always) |
| `screen.orientation.{type,angle}` | `ScreenOrientation` | Derived from spoofed WxH |
| `window.innerWidth/Height` | `nsGlobalWindowOuter::GetInnerSize` (`:3448`) | Real (via letterboxing) |
| `window.outerWidth/Height` | `nsGlobalWindowOuter::GetOuterSize` (`:3518`) | Spoofed top-inner size |
| `window.screenX/Y` | `nsGlobalWindowOuter::GetScreenXY` (`:3575`) | **(0, 0)** |
| `window.mozInnerScreenX/Y` | (`:3662`) | **0.0** |
| `window.devicePixelRatio` | `nsGlobalWindowInner::GetDevicePixelRatio` (`:3798`) | **2.0** (not 1.0!) |
| `window.getDesktopToDeviceScale()` | (`:3824`) | **NOT spoofed** (Firefox gap) |

### The letterboxing stepper (`RFPHelper.sys.mjs:391-404`)

```js
steppedSize(aDimension, aIsWidth = false) {
  if (aDimension <= 50) return aDimension;          // tiny: don't round
  let stepping;
  if (aDimension <= 500)       stepping = 50;       // phones
  else if (aDimension <= 1600) stepping = aIsWidth ? 200 : 100;  // laptop
  else                          stepping = 200;       // desktop
  return aDimension - (aDimension % stepping);
}
```

The CSS that enforces this (`resistfingerprinting/content/letterboxing.css`):

```css
.letterboxing .browserContainer { overflow: hidden; background: var(--letterboxing-bgcolor); }
.letterboxing .browserContainer:not(.responsive-mode) >
    .browserStack:not(.exclude-letterboxing) > browser {
  width:  var(--letterboxing-width)  !important;
  height: var(--letterboxing-height) !important;
}
```

### devicePixelRatio spoofing formula (`nsRFPService.cpp:3156-3176`)

```cpp
float nsRFPService::GetDefaultPixelDensity() { return 2.0f; }

double nsRFPService::GetDevicePixelRatioAtZoom(float aZoom) {
  aZoom /= LookAndFeel::SystemZoomSettings().mFullZoom;
  int32_t unzoomedAppUnits = NS_lround(AppUnitsPerCSSPixel() / GetDefaultPixelDensity());  // 60/2 = 30
  int32_t appUnitsPerDevPixel = (aZoom == 1.0f) ? unzoomedAppUnits
                                                 : std::max(1, NSToIntRound(float(unzoomedAppUnits) / aZoom));
  return double(AppUnitsPerCSSPixel()) / double(appUnitsPerDevPixel);  // 60/30 = 2.0
}
```

**Why 2.0 not 1.0**: spoofing to 1.0 breaks HiDPI layouts (everything renders at 50% size on Retina/4K). 2.0 is the most common DPR for modern Macs/HiDPI laptops, so it both preserves visual fidelity AND puts the user in the largest fingerprint bucket. Bug 1954493 is the bug reference.

### Firefox's gaps

1. **`window.getDesktopToDeviceScale()` is NOT spoofed** (`nsGlobalWindowInner.cpp:3824`). Returns real `DeviceContext()->GetDesktopToDeviceScale().scale`. A script calling this sees real DPR while `devicePixelRatio` returns 2.0.
2. **Legacy `GetSpoofedScreenAvailSize` path still uses platform-specific taskbar offsets** (Win=48, Mac=76+25, Linux=0). This is enabled under FPP (`ScreenAvailToResolution` target) and leaks OS via `availTop` (mac=25, others=0).
3. **`screen.colorGamut` always returns `"srgb"` even without RFP** — already a constant, but inconsistent on HDR/P3 panels.
4. **Resize events fire at unstepped sizes during letterboxing reflow** — fingerprinter can time them.
5. **Multi-monitor `screenchange` events still fire** — value is spoofed but the *event* leaks that the user moved monitors.
6. **`dom_innerSize_rounding` pref is independent of RFP** — user-set rounding mode can create off-by-one mismatches.

### Your Qt6 implementation strategy (beating Firefox)

```cpp
struct ScreenProfile {
    // Single source of truth
    int32_t stepped_width;
    int32_t stepped_height;
    int32_t color_depth = 24;          // always 24, never 30/48
    int32_t pixel_depth = 24;          // == color_depth
    double device_pixel_ratio = 2.0;   // not 1.0
    int32_t screen_x = 0;
    int32_t screen_y = 0;
    int32_t avail_left = 0;
    int32_t avail_top = 0;
    std::string color_gamut = "srgb";
    std::string orientation_type = "landscape-primary";
    uint16_t orientation_angle = 0;
    
    // Derive everything from stepped_width × stepped_height
    void deriveFrom(int width, int height) {
        stepped_width = width;
        stepped_height = height;
        // screen.width == screen.availWidth == window.outerWidth == window.innerWidth
        // screen.height == screen.availHeight == window.outerHeight == window.innerHeight
        if (width > height) {
            orientation_type = "landscape-primary";
            orientation_angle = 0;
        } else {
            orientation_type = "portrait-primary";
            orientation_angle = 90;
        }
    }
};

// Letterboxing stepper (Firefox's algorithm)
int32_t stepDimension(int32_t dim, bool is_width) {
    if (dim <= 50) return dim;
    int step;
    if (dim <= 500) step = 50;
    else if (dim <= 1600) step = is_width ? 200 : 100;
    else step = 200;
    return dim - (dim % step);
}
```

**Key Qt6 hooks**:

1. **Implement real letterboxing** in Qt6 WebEngine — wrap the WebEngineView in a `QQuickItem` with clipped+centered inner view. The outer area is painted with the toolbar background color (looks like a black border). This makes `window.innerWidth` actually equal the stepped value (Firefox's approach).

2. **Spoof every screen property** in your QWebEngineUrlRequestInterceptor + custom WebChannel:
   - `screen.{width,height,left,top,availWidth,availHeight,availLeft,availTop}` = stepped top-inner rect
   - `screen.{colorDepth,pixelDepth}` = 24
   - `window.{outerWidth,outerHeight}` = stepped top-inner size
   - `window.{screenX,screenY}` = (0, 0)
   - `window.devicePixelRatio` = 2.0

3. **Patch `window.getDesktopToDeviceScale()`-equivalent** if Qt6 exposes it. Audit the full `window` object.

4. **Patch CSS media features**: `resolution`, `device-width`, `device-height`, `color`, `color-gamut` must all return spoofed values. The most important: `matchMedia("(min-resolution: 2dppx)").matches` must be `true` exactly when `devicePixelRatio === 2`. This coherence is verified by Firefox's `browser_dpr_media_queries.js` test.

5. **Don't recompute on monitor change** — lock the spoofed value to the initial value or to a per-session random stepped dimension. Firefox has the issue that cross-monitor moves still trigger resize events.

6. **Debounce resize events** to only fire when the stepped size actually changes.

7. **Force `screen.orientation` to be derived from spoofed WxH**, not real monitor orientation.

---

## 5. Audio Fingerprinting

### Where the value originates

JS-facing: `AnalyserNode::GetFloatFrequencyData` (`webaudio/AnalyserNode.cpp:211`). Internally calls `FFTAnalysis()` (`:281`):

```cpp
bool AnalyserNode::FFTAnalysis() {
    GetTimeDomainData(inputBuffer, fftSize);          // mix down chunks → mono float
    ApplyBlackmanWindow(inputBuffer, fftSize);         // fdlibm_cos windowing
    mAnalysisBlock.PerformFFT(inputBuffer);            // FFVPX av_tx RDFT
    for (each bin) {
        scalarMagnitude = fdlibm_hypot(Real, Imag) / fftSize;
        mOutputBuffer[i] = smoothing * old + (1 - smoothing) * scalarMagnitude;
    }
}
```

The FFT library is **FFVPX** (FFmpeg's `av_tx` RDFT), not KissFFT. Loaded at startup via `FFVPXRuntimeLinker::Init()` (`FFTBlock.h:29-34`).

### What makes audio fingerprints unique per device

The standard fingerprinter JS:
```js
const ctx = new AudioContext();
const osc = ctx.createOscillator();
osc.type = 'triangle'; osc.frequency.value = 10000;
const compressor = ctx.createDynamicsCompressor();
const analyser = ctx.createAnalyser();
osc.connect(compressor); compressor.connect(analyser); analyser.connect(ctx.destination);
osc.start();
const buffer = new Float32Array(analyser.frequencyBinCount);
analyser.getFloatFrequencyData(buffer);  // ← THE FINGERPRINT
```

Five sources of per-device variation, **in order of impact**:

1. **SIMD backend selected at runtime** (`AudioNodeEngine.cpp:67-101`) — **DOMINANT**. The downmix in `GetTimeDomainData` uses `AudioBufferCopyWithScale`/`AudioBufferAddWithScale` which dispatch to NEON/SSE2/SSE4.2/scalar paths. Different summation orders produce LSB-level differences that compound through the compressor's IIR filters and the FFT.

2. **OS sample rate** (44100 vs 48000) — affects bin index of every peak. `10000 Hz * fftSize / sampleRate / 2` differs between 44100 and 48000.

3. **FFT library implementation** — different across browsers (Chrome's Blink FFT vs Firefox's FFVPX vs Safari's vDSP). Within Firefox builds, deterministic.

4. **Compiler FP optimizations** — eliminated by Firefox's use of `fdlibm_*` math (correctly-rounded, deterministic per build).

5. **Audio output hardware** — NOT relevant; AnalyserNode taps before destination.

### Firefox's RFP audio strategy

**No noise injection.** Firefox uses **uniformity**, not randomization:

| Surface | RFP behavior |
|---|---|
| `AudioContext.sampleRate` | Clamped to **44100** via `CubebUtils::PreferredSampleRate(true)` (`AudioContext.cpp:151`) |
| `AudioContext.maxChannelCount` | Clamped to **2** (`AudioContext.cpp:716-723`) |
| `AudioContext.currentTime` | Rounded via `ReduceTimePrecisionAsSecs` with per-context random seed — BUT short-circuits to raw time when `128/sampleRate > timer_resolution`, which is always true at 44100Hz with default 16.667ms resolution |
| `AudioContext.baseLatency` | Hardcoded **0.0** (always, even without RFP) — Gecko does no buffering |
| `AudioContext.outputLatency` | Fixed platform constant: Mac=512/sampleRate, Linux=0.025, Win=0.04, Android=0.020 |
| AnalyserNode FFT output | **NO NOISE** — raw FFVPX + smoothing + dB conversion |
| `OfflineAudioContext` sampleRate | **NOT clamped** — honors user-supplied rate |
| `OfflineAudioContext.currentTime` | Still RFP-reduced, but fingerprinters don't read it |
| `AudioListener` position/orientation | Not spoofed, but defaults are universal |

### The DynamicsCompressorNode — why fingerprinters always include it

The compressor is a stateful, nonlinear, IIR-laden signal processor:

1. **4-pole pre-emphasis + 4-pole de-emphasis filter cascade** — coefficients derived from `nyquist() = sampleRate / 2`. Different sample rates → different pole positions.
2. **Knee-curve computation** with Newton-style binary search (15 iterations).
3. **Adaptive release polynomial** with hand-tuned 4th-order coefficients.
4. **Pre-delay buffers** whose length = `preDelayTime * sampleRate` (6ms = 264 samples at 44100, 288 at 48000).
5. **`fdlibm_asinf` / `fdlibm_sinf` soft-clip** — deterministic per build.

It takes the deterministic oscillator signal and stamps it with sample-rate-specific filter state AND SIMD-specific FP summation, amplifying per-device divergence by orders of magnitude.

### Firefox's gaps (massive)

1. **No noise injection on `getFloatFrequencyData` output** — different SIMD backends still produce different fingerprints. This is the biggest gap.
2. **`OfflineAudioContext` bypasses sample-rate clamping** — fingerprinter can force 48000 Hz rendering.
3. **`OfflineAudioContext.startRendering()` returns raw float buffer with no perturbation** — the strongest audio fingerprint vector.
4. **`baseLatency === 0` is a Firefox-family tell** — Chrome returns real values.
5. **SIMD backend varies by CPU** — not normalized.

### Your Qt6 implementation strategy (beating Firefox)

```cpp
struct AudioProfile {
    // Mirror Firefox's uniformity approach
    double sample_rate = 44100.0;          // always 44100
    uint32_t max_channel_count = 2;        // always stereo
    double base_latency = 0.0;            // matches Firefox
    double output_latency = 0.025;         // pick one platform's value
    double current_time_resolution = 16.667e-3;  // 60Hz frame quantization
    
    // BEAT FIREFOX: noise injection on analyser output
    bool inject_fft_noise = true;
    bool inject_offline_render_noise = true;
    
    // BEAT FIREFOX: pin SIMD backend
    bool force_scalar_audio_path = true;  // or pin to SSE2 always
};
```

**The noise injection algorithm** (modeled on Firefox's canvas noise pattern):

```cpp
void AudioReadbackInterceptor::perturbFFT(float* fft_output, 
                                          size_t bin_count,
                                          const Origin& origin) {
    auto perSiteKey = m_session.derivePerSiteKey(origin);
    
    // Hash the current FFT output to derive a per-call key
    auto perCallKey = HMAC_SHA256(perSiteKey, 
                                  reinterpret_cast<uint8_t*>(fft_output),
                                  bin_count * sizeof(float));
    
    XorShift128Plus rng1(perCallKey.first_16_bytes());
    XorShift128Plus rng2(perCallKey.last_16_bytes_sans_last());
    
    // Perturb 5-20 bins (out of typically 1024)
    uint8_t numNoises = clamp(perCallKey.last_byte() % 16 + 5, 5, 20);
    for (uint8_t i = 0; i < numNoises; i++) {
        uint32_t bin = rng1.next() % bin_count;
        uint8_t bit_to_flip = rng2.next() % 23;  // mantissa bits (1-23)
        
        // Flip one bit in the float's mantissa
        uint32_t* p = reinterpret_cast<uint32_t*>(&fft_output[bin]);
        *p ^= (1u << bit_to_flip);
    }
}
```

Key rules:
- **Per-site key** (stable across refreshes within a session)
- **Per-call key** derived from the actual FFT output via HMAC (deterministic per content)
- **Flip mantissa bits** of float values (not exponent — flipping exponent creates huge visible differences)
- **5-20 bins perturbed** out of 1024 — small enough to be invisible, large enough to break XOR-of-renders attacks
- **Apply to BOTH `getFloatFrequencyData` AND `OfflineAudioContext.startRendering()` output** — close Firefox's offline gap

**For `OfflineAudioContext`**: intercept `startRendering()`. After the render promise resolves, perturb the returned `AudioBuffer`'s channel data using the same algorithm. Fingerprinters specifically use offline contexts because they're "deterministic" — break that assumption.

**For SIMD determinism**: either (a) force the scalar fallback in the audio mix-down paths (highest portability, eliminates SSE/NEON FP variation), or (b) link a single FFT implementation (e.g., a vendored KissFFT) instead of relying on whatever the platform provides.

**Use `fdlibm`-equivalent math everywhere** — port Mozilla's `fdlibm` to C++ and use it instead of `<math.h>` for `cos`, `sin`, `log10`, `hypot`, `exp`, `asinf`, `sinf`. This eliminates compiler-flag FP variation.

---

## 6. Font Fingerprinting

### How websites detect installed fonts

The classic technique: measure string width with `font: "TargetFont, FallbackFont"`. If TargetFont is installed, width matches TargetFont's metrics; if not, browser falls back.

Call chain in Firefox:
```
canvas.measureText()
  → CanvasRenderingContext2D::MeasureText  (canvas/CanvasRenderingContext2D.cpp:4723)
    → DrawOrMeasureText(MEASURE)            (:5093)
      → GetCurrentFontStyle()               (:5477)  → builds gfxFontGroup with FontVisibilityProvider
      → CanvasBidiProcessor::SetText()       (:4825)  → mFontgrp->MakeTextRun()
      → CanvasBidiProcessor::GetWidth()      (:4851)  → mTextRun->MeasureText() sums glyph advances
```

The substitution point is `WhichSystemFontSupportsChar` (`thebes/gfxTextRun.cpp:4095`):

```cpp
already_AddRefed<gfxFont> gfxFontGroup::WhichSystemFontSupportsChar(...) {
    FontVisibility visibility;
    return gfxPlatformFontList::PlatformFontList()->SystemFindFontForChar(
        mFontVisibilityProvider, ...);
}
```

### Where uniqueness comes from

1. **Different OS default fonts** — `kBaseFonts` differs per platform (Arial/Baskerville on Mac, Arial/Calibri on Windows, DejaVu/Liberation on Linux).
2. **User-installed fonts** (Office, Adobe, etc.) — `Visibility == User`.
3. **Different versions of same font** — different glyph widths.
4. **Different rendering backends** — DirectWrite vs FreeType vs CoreText produce different `measureText` widths even for the same font.
5. **fontconfig user config** — `~/.fonts.conf` hinting tweaks would normally be baked into the font descriptor.

### Firefox's FontVisibility tier system

Defined in `thebes/gfxTypes.h:133`:

```cpp
enum class FontVisibility : uint8_t {
    Unknown = 0,   // unclassified (or safety-net disabled)
    Base = 1,      // standard OS installation
    LangPack = 2,  // optional OS language component
    User = 3,      // user-installed
    Hidden = 4,    // internal system font
    Webfont = 5,   // @font-face defined
};
```

The single gatekeeping predicate (`gfxPlatformFontList.cpp:1174`):

```cpp
bool gfxPlatformFontList::IsVisibleToCSS(const gfxFontFamily& aFamily,
                                         FontVisibility aVisibility) const {
    return aFamily.Visibility() <= aVisibility || IsFontFamilyWhitelistActive();
}
```

### ComputeFontVisibility decision logic (`FontVisibilityProvider.cpp:49-114`)

```cpp
FontVisibility FontVisibilityProvider::ComputeFontVisibility() const {
    if (Maybe<FontVisibility> inherited = MaybeInheritFontVisibility()) 
        return *inherited;
    if (IsChrome()) return FontVisibility::User;
    
    bool isPrivate = IsPrivateBrowsing();
    int32_t level;
    
    if (ShouldResistFingerprinting(FontVisibilityBaseSystem)) {
        if (nsRFPService::IsRFPPrefEnabled(isPrivate))
            return FontVisibility::Base;       // ← strictest: RFP + BaseSystem
        level = int32_t(FontVisibility::Base);
    } else if (ShouldResistFingerprinting(FontVisibilityLangPack)) {
        level = int32_t(FontVisibility::LangPack);
    } else {
        level = StaticPrefs::layout_css_font_visibility();
    }
    
    // ETP-allow-listed domains get default level
    if (level != StaticPrefs::layout_css_font_visibility() &&
        ContentBlockingAllowList::Check(GetCookieJarSettings()))
        level = StaticPrefs::layout_css_font_visibility();
    
    return FontVisibility(clamp(level, Base, User));
}
```

### The two-pronged defense

**Prong A — Per-context visibility tier** (the fine-grained mechanism):
- Every font lookup checks `family.Visibility() <= context_visibility_level`
- Tied to RFP via `RFPTarget::FontVisibilityBaseSystem` (43) and `FontVisibilityLangPack` (44)
- Per-document-context, not per-origin

**Prong B — Hard whitelist** (`font.system.whitelist` pref, the blunt instrument):
- When set, physically removes non-whitelisted families from the font list (`gfxPlatformFontList.cpp:624-651`)
- Wins over Prong A

### Per-platform font classification

**macOS** (`CoreTextFontList.cpp:1316`):
- `CTFontManagerCopyAvailableFontFamilyNames()` enumerates
- `GetVisibilityForFamily()` checks name against `kBaseFonts` (StandardFonts-macos.inc)
- Names starting with `.` → `Hidden`

**Windows** (`gfxDWriteFontList.cpp:1616`):
- `IDWriteFactory::GetSystemFonts()` enumerates
- `GetVisibilityForFamily()` checks `kBaseFonts`, then `kLangPackFonts`, then `FontIsAllowedByLocale()`

**Linux** (`gfxFcPlatformFontList.cpp:2173`):
- `FcConfigGetFonts(nullptr, FcSetSystem)` enumerates
- `GetVisibilityForFamily()` dispatches on distro version
- **Important**: strips `FC_HINT_STYLE` and `FC_HINTING` from patterns (`:2141`) — prevents user `~/.fonts.conf` tweaks from leaking
- Strips `FC_CHARSET` from TrueType/OpenType (`:2151`) — memory optimization
- **Safety net**: if fewer than 3 known-Base fonts found, ALL fonts become `Unknown` (protection disabled, fails OPEN not closed)

**Android** (`gfxFT2FontList.cpp:1569`):
- `ASystemFontIterator_open()` / `ASystemFontIterator_next()` (API 29+)

### FingerprintedFonts.inc — what is it really?

**Not a list Firefox pretends to have.** It's the list of font names that **real-world fingerprinters probe**, collected from web crawl data. Used by Mozilla's `UserCharacteristics` telemetry to measure its own fingerprintability.

Variants (`fpjs`, `variantA`-`variantE`, `variantF_FontList`, `variantI_FontList`) are probe sets. `ProcessFingerprintedFonts` (`nsUserCharacteristics.cpp:376-424`) iterates each list, asks `gfxPlatformFontList::GetFontVisibility(name)`, hashes the allowlisted (Base+LangPack) ones separately from non-allowlisted, ships both hashes as telemetry.

### Firefox's gaps

1. **`document.fonts.check('1px SomeFont')` is NOT spoofed** — `GetStandardFamilyName` passes `nullptr` as visibility provider (`gfxPlatformFontList.cpp:2086`), so it uses `FontVisibility::User` (most permissive). Returns true for any installed font regardless of RFP.
2. **Timing side-channel on `src: local()` vs `src: url()`** — `DoLoadNextSrc` (`gfxUserFontSet.cpp:448`) resolves local synchronously (microseconds), URL async (milliseconds). Page measures `document.fonts.load()` time to detect local installation.
3. **Glyph-width fingerprinting via `measureText`** — even with RFP restricting visible set to Base-tier, different OSes/backends produce different widths for the same fallback.
4. **Per-locale variance on Windows** — `CalculateFontLocaleAllowlist()` reads `intl.accept_languages` and promotes locale-specific fonts to LangPack tier. Same machine with different locale → different visible set.
5. **`gfxFontEntry::HasCharacter` probing** — pages can indirectly probe character support.
6. **User-installed fonts that match Base-tier names** — if user installs a custom "Arial" with different glyph widths, it's tagged `Base` (Firefox only checks family name, not font identity).

### Your Qt6 implementation strategy (beating Firefox)

For Qt6 WebEngine (Chromium + Skia + FreeType + fontconfig on Linux):

```cpp
enum class FontVisibility : uint8_t {
    Unknown = 0, Base = 1, LangPack = 2, User = 3, Hidden = 4, Webfont = 5
};

class FontVisibilityProvider {
public:
    virtual FontVisibility GetFontVisibility() const = 0;
    virtual bool ShouldResistFingerprinting(FPTarget) const = 0;
    virtual bool IsChrome() const = 0;
    virtual bool IsPrivateBrowsing() const = 0;
    FontVisibility ComputeFontVisibility() const;  // mirror Firefox's algorithm
};

class QFontVisibilityProviderImpl : public FontVisibilityProvider {
    // Implement on QWebEnginePage
};
```

**Step-by-step implementation**:

1. **Lift Mozilla's `StandardFonts-{platform}.inc` files verbatim**. They're already researched and exhaustive. These define which families get tagged `Base`.

2. **Implement per-family classification at font enumeration time**:
   - On Linux: port `gfxFcPlatformFontList::GetVisibilityForFamily` (`:2204-2252`) — dispatch on distro version, look up family name in sorted hard-coded list.
   - **CRITICAL**: strip `FC_HINT_STYLE` and `FC_HINTING` from fontconfig patterns before adding to your font list — prevents user `~/.fonts.conf` from leaking.
   - Strip `FC_CHARSET` from TrueType/OpenType patterns — memory optimization.
   - **Safety net**: if fewer than 3 known-Base families found, disable protection entirely (fail OPEN).

3. **Hook Blink's `FontCache::GetFontPlatformData` and `FontSelector::GetFontData`** with the `IsVisibleToCSS(family, level)` predicate. This is the single highest-leverage change.

4. **Hook `@font-face src: local()` resolution** — intercept the equivalent of `gfxUserFontEntry::DoLoadNextSrc`. Apply `IsVisibleToCSS` so `src: local("Adobe Caslon Pro")` returns null under RFP, forcing download.

5. **Fix `document.fonts.check()` gap** (Firefox hasn't) — pass the document's `FontVisibilityProvider` (not nullptr) into the family-name lookup. This makes `check('1px SomeUserFont')` return `false` under RFP.

6. **Accept the timing side-channel on `src: local()`** — there's no clean fix without also restricting webfont downloads (which would break pages). Add artificial delay to `src: url()` downloads to mask the difference, if you want to go further than Firefox.

7. **Implement the hard-whitelist equivalent** of `font.system.whitelist` — physically drop non-whitelisted families from the in-memory font list before the renderer sees them.

8. **Per-WebEnginePage visibility level** — driven by a `setResistFingerprinting(bool)` API on `QWebEngineProfile`. Compute the level once per navigation, cache it on the page.

9. **Locale-based promotion on Windows** — port `nsRFPService::CalculateFontLocaleAllowlist` (`nsRFPService.cpp:3364-3472`). Reads `navigator.languages` and promotes locale-specific fonts (MS Mincho for ja, SimHei for zh-hans) from User to LangPack.

10. **Reuse `FingerprintedFonts.inc` as a test corpus** — validate that your spoofing layer blocks the same set of fonts Firefox does. Run the variants A-I from `nsUserCharacteristics.cpp` and verify your hashes match Firefox's under equivalent config.

---

## Cross-Surface Consistency Checklist

The key insight for beating Firefox: **CreepJS detects inconsistencies between surfaces, not individual surface spoofing**. Here's the consistency matrix you must enforce:

| Surface A | Surface B | Consistency rule |
|---|---|---|
| `navigator.userAgent` OS token | `navigator.platform` | If UA says Windows, platform must be `Win32` |
| `navigator.userAgent` OS token | `navigator.oscpu` | If UA says Win10, oscpu says `Windows NT 10.0; Win64; x64` |
| `navigator.userAgent` OS token | `navigator.maxTouchPoints` | Windows→10, Mac→0, Android→5 |
| `navigator.userAgent` OS token | WebGL RENDERER | If spoofing as Mac, RENDERER should be Mac-shaped |
| `navigator.userAgent` OS token | Font list | Mac fonts (Arial, Baskerville) vs Windows (Arial, Calibri) |
| `navigator.language` | `Intl.DateTimeFormat().resolvedOptions().locale` | MUST be equal (Firefox's gap) |
| `navigator.language` | Accept-Language HTTP header | MUST be equal |
| `Intl.DateTimeFormat().resolvedOptions().timeZone` | `Date.prototype.getTimezoneOffset()` | TimeZone=Atlantic/Reykjavik → offset=0 |
| `screen.width × screen.height` | `window.outerWidth × outerHeight` | MUST be equal (Firefox enforces) |
| `screen.colorDepth` | `matchMedia("(color: 8)").matches` | 24-bit depth → color=8 |
| `window.devicePixelRatio` | `matchMedia("(min-resolution: 2dppx)").matches` | DPR=2 → resolution query true |
| `window.devicePixelRatio` | `window.getDesktopToDeviceScale()` | MUST be equal (Firefox's gap) |
| `screen.width > screen.height` | `screen.orientation.type` | landscape if W>H, portrait if H>W |
| `navigator.hardwareConcurrency` | Real CPU count (probed via timing) | Tiered rounding (Firefox clamps to 4) |
| `AudioContext.sampleRate` | Compressor filter coefficients | Both must be 44100 |
| `WebGL UNMASKED_RENDERER_WEBGL` | `getSupportedExtensions()` codec set | If RENDERER says mobile GPU, don't expose S3TC |
| Canvas noise pattern | WebGL readPixels noise | Same per-site key derivation |
| `navigator.vendor` | `navigator.productSub` | Firefox: `""` + `"20100101"`; Chrome: `"Google Inc."` + `"20030107"` |
| `navigator.buildID` | `navigator.productSub` | Firefox: `"20181001000000"` + `"20100101"` |
| `navigator.userAgentData` presence | `navigator.vendor` | If spoofing Chrome: expose userAgentData, vendor="Google Inc."; if Firefox: omit userAgentData, vendor="" |

---

## The Per-Site Randomization Key Architecture

This is the single most important architectural decision. Without it, you can't beat CreepJS:

```
Browser startup
  └─► generate sessionUUID (16-32 random bytes, in-memory only)
        │
        ▼
Per top-level navigation
  └─► topLevelSite = scheme://host:port
  └─► perSiteKey = HMAC_SHA256(sessionUUID, topLevelSite)  // 32 bytes
  └─► Store perSiteKey on the QWebEnginePage
        │
        ▼
Per canvas/audio readback
  └─► perCallKey = HMAC_SHA256(perSiteKey, content_bytes)
  └─► Seed XorShift128Plus RNGs from perCallKey
  └─► Flip 5-255 bits in the output buffer
```

**Properties this gives you**:
- Same site + same content + same session → same noise (script sees consistent values)
- Different site → completely different noise (no cross-site correlation)
- Browser restart → different noise (no long-term tracking)
- Private browsing → separate sessionUUID (cleared on exit)
- Content-derived → fingerprinter can't predict noise without knowing sessionUUID

---

## Recommended Implementation Order

For a Qt6 WebEngine spoofing engine, build in this order:

1. **Foundation (week 1-2)**:
   - `enum class FPTarget : uint64_t` with all surface IDs
   - Single `shouldResist(FPTarget)` dispatcher
   - Per-domain override system (the `+Target,-Target` syntax with 5 scope patterns)
   - Per-site session key + HMAC derivation
   - Tiered protection levels (Off / Baseline / FPP / RFP / Strict)

2. **Navigator + Screen (week 3)**:
   - All `navigator.*` getters (UA, platform, oscpu, hardwareConcurrency, etc.)
   - All `screen.*` and `window.*` size getters
   - Letterboxing CSS injection (real viewport shrink)
   - devicePixelRatio spoofing + CSS media query coherence
   - Force locale coherence (navigator.language + Intl + Accept-Language header)
   - Force timezone coherence

3. **Canvas + WebGL (week 4-5)**:
   - Canvas readback interceptor (`getImageData`, `toDataURL`, `toBlob`, `captureStream`)
   - WebGL `getParameter` cache + spoofed strings + clamped caps
   - WebGL extension list filter
   - WebGL shader precision table spoofing
   - Per-call HMAC-SHA256 noise injection

4. **Audio (week 6)**:
   - `AudioContext.sampleRate` clamp to 44100
   - `OfflineAudioContext.startRendering()` output perturbation
   - `getFloatFrequencyData` / `getByteFrequencyData` noise injection
   - Pin SIMD backend to scalar (or one canonical SIMD level)

5. **Fonts (week 7-8)**:
   - Port `StandardFonts-{platform}.inc` lists
   - Per-family classification at font enumeration time
   - `IsVisibleToCSS` predicate in Blink's FontCache
   - `@font-face src: local()` interception
   - Fix `document.fonts.check()` gap (Firefox hasn't)
   - fontconfig pattern sanitization on Linux

6. **Validation (ongoing)**:
   - Run CreepJS, FingerprintJS Pro, BrowserLeaks, CoverYourTracks
   - Cross-reference your fingerprint hash with Firefox RFP's hash
   - Audit for cross-surface inconsistencies using the matrix above

---

## Summary: How to Beat Firefox

Firefox's RFP has structural gaps that fingerprinting sites exploit:

1. **`EfficientCanvasRandomization` leaves `getImageData` untouched** — fix by spoofing all readback paths.
2. **WebGL2 caps not clamped** — clamp ALL `MAX_*` parameters, not just WebGL1.
3. **Shader precision table leaks driver** — blank to canonical desktop GL signature.
4. **WebGL extensions list not filtered** — pick a canonical set.
5. **`navigator.platform` not spoofed** — uniform it (or accept Firefox's tradeoff).
6. **Locale mismatch** — force `navigator.language === Intl.DateTimeFormat().resolvedOptions().locale`.
7. **`window.getDesktopToDeviceScale()` not spoofed** — patch it.
8. **Audio has NO noise injection** — add per-call HMAC-derived LSB perturbation to `getFloatFrequencyData` and `OfflineAudioContext.startRendering()`.
9. **`OfflineAudioContext` bypasses sample-rate clamp** — perturb its output too.
10. **`document.fonts.check()` not spoofed** — pass non-null visibility provider into the lookup.
11. **`src: local()` timing side-channel** — add artificial delay to `src: url()` to mask the difference.
12. **`baseLatency === 0`** — Firefox-family tell. If spoofing as Chrome, return a real value.
13. **`navigator.vendor === ""` and `userAgentData === undefined`** — Firefox-family tells. Pick a coherent identity (Firefox or Chrome) and apply consistently.

The single most important principle: **pick ONE coherent spoofed identity** (browser family + OS + version + locale + timezone) and derive EVERY surface from it. Never compute surfaces independently — they will diverge and CreepJS will catch the mismatches.
I've delivered a comprehensive analysis of all six fingerprinting surfaces — Canvas, WebGL, Navigator, Screen, Audio, and Fonts — with the exact origin of each value, what makes it unique per device, Firefox's RFP strategy (including its specific gaps), and concrete C++/Qt6 implementation strategies to beat Firefox.

**Key architectural insights for your C++ port**:

1. **Single dispatcher pattern** — Mirror Firefox's `RFPTarget` X-macro enum + `shouldResist(FPTarget)` dispatcher. Every getter calls it. Far cleaner than scattered `if (m_spoofMode)` checks.

2. **Per-site randomization key** — `sessionUUID → HMAC(perSiteKey, topLevelSite) → HMAC(perCallKey, content_bytes)`. This is the most important architectural decision; without it, you can't beat CreepJS.

3. **Coherent identity, not independent spoofing** — Pick ONE browser+OS+locale+timezone identity and derive every surface from it. Firefox's gaps are mostly cross-surface inconsistencies (e.g. `navigator.language` ≠ `Intl.DateTimeFormat().resolvedOptions().locale`).

4. **Where Firefox is weak**:
   - Canvas: `EfficientCanvasRandomization` leaves `getImageData` unprotected
   - WebGL: WebGL2 caps, shader precision, extension list, compressed texture formats all unspoofed
   - Audio: **zero noise injection** — relies on uniformity only; `OfflineAudioContext` bypasses sample-rate clamp
   - Fonts: `document.fonts.check()` not spoofed; `src: local()` timing side-channel
   - Screen: `getDesktopToDeviceScale()` not spoofed; legacy availTop leaks OS

5. **Where you can beat Firefox**: apply HMAC-derived LSB noise to audio output, clamp ALL WebGL caps (not just WebGL1), force locale coherence across navigator + Intl + Accept-Language header, fix `document.fonts.check()` to consult visibility provider.

analayzed by glm 5.2  in z.ai website