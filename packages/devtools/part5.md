# PART 5: PAGE LIFECYCLE & SCREENSHOTS

## The Ultimate Qt6 WebEngine Scraping Browser Guide

*Exhaustive implementation reference — every navigation event, every lifecycle state, every pixel to your scraper.*

---

## 5.1 The Page Lifecycle Architecture

### 5.1.1 The Two-Agent Split

Page domain events fire from two locations:

| Agent | Process | File | Handles |
|---|---|---|---|
| `InspectorPageAgent` | Renderer (Blink) | `third_party/blink/renderer/core/inspector/inspector_page_agent.cc` | Frame tree mutations (attached/detached/navigated), lifecycle events, paint timing, dialogs, script-to-evaluate-on-new-document, resource tree, screencast |
| `PageHandler` | Browser | `browser/devtools/protocol/page_handler.cc` | Cross-frame loading state, navigation start, screenshots, PDF, downloads, file chooser, interstitials, crash |

The renderer-side agent fires the detailed lifecycle events; the browser-side handler fires the cross-process loading state and handles screenshot/PDF capture.

### 5.1.2 The Complete Lifecycle Event Sequence

For a typical navigation `https://example.com/`:

```
T+0ms    [browser] PageHandler::DidStartNavigating
         → Page.frameStartedNavigating
         payload: frameId, url, loaderId, navigationType

T+5ms    [renderer] InspectorPageAgent::FrameAttachedToParent (if iframe)
         → Page.frameAttached
         payload: frameId, parentFrameId, stack?

T+10ms   [browser] PageHandler::DidChangeFrameLoadingState
         → Page.frameStartedLoading
         payload: frameId

T+50ms   [renderer] InspectorPageAgent::WillCommitLoad
         → Page.frameNavigated
         payload: frame{id, parentId?, loaderId, url, domainAndRegistry, mimeType,
                        securityOrigin, secureContextType, crossOriginIsolatedContextType,
                        gatedAPIFeatures[]}, navigationType
         
         navigationType: "Navigation" | "BackForwardCacheRestore"

T+51ms   [renderer] InspectorPageAgent::LifecycleEvent
         → Page.lifecycleEvent (name="commit")
         payload: frameId, loaderId, name, timestamp

T+100ms  [renderer] InspectorPageAgent::WillCommitLoad (for sub-resources)
         → Page.frameStartedLoading (for each subframe)

T+200ms  [renderer] DOMContentLoaded
         → Page.lifecycleEvent (name="DOMContentLoaded")
         → Page.domContentEventFired (root frame only)

T+300ms  [renderer] firstPaint
         → Page.lifecycleEvent (name="firstPaint")

T+310ms  [renderer] firstContentfulPaint
         → Page.lifecycleEvent (name="firstContentfulPaint")

T+500ms  [renderer] load event
         → Page.lifecycleEvent (name="load")
         → Page.loadEventFired (root frame only)

T+1000ms [renderer] networkAlmostIdle
         → Page.lifecycleEvent (name="networkAlmostIdle")

T+5000ms [renderer] networkIdle
         → Page.lifecycleEvent (name="networkIdle")
         (requires ≤2 in-flight requests for 5 seconds)

T+5001ms [browser] PageHandler::DidChangeFrameLoadingState
         → Page.frameStoppedLoading
         payload: frameId
```

### 5.1.3 The Full Event Reference Table

| Event | Fired From | When | Key Fields |
|---|---|---|---|
| `Page.frameStartedNavigating` | browser (`page_handler.cc:1160`) | Navigation begins | frameId, url, loaderId, navigationType |
| `Page.frameAttached` | renderer (`inspector_page_agent.cc:1100`) | Frame attached to parent | frameId, parentFrameId, stack? |
| `Page.frameStartedLoading` | browser (`page_handler.cc:1182`) | Frame starts loading | frameId |
| `Page.frameNavigated` | renderer (`inspector_page_agent.cc:1077`) | Document committed | frame{...}, navigationType |
| `Page.lifecycleEvent` | renderer (`inspector_page_agent.cc:1197`) | Each lifecycle state | frameId, loaderId, name, timestamp |
| `Page.domContentEventFired` | renderer (`inspector_page_agent.cc:1059`) | DOMContentLoaded (root only) | timestamp |
| `Page.loadEventFired` | renderer (`inspector_page_agent.cc:1067`) | load event (root only) | timestamp |
| `Page.frameStoppedLoading` | browser (`page_handler.cc:1182`) | Frame finished loading | frameId |
| `Page.frameDetached` | renderer (`inspector_page_agent.cc:1121`) | Frame removed | frameId, reason ("remove" \| "swap") |
| `Page.navigatedWithinDocument` | renderer (`inspector_page_agent.cc:997`) | Fragment/pushState navigation | frameId, url, navigationType |
| `Page.frameScheduledNavigation` | renderer (`inspector_page_agent.cc:1158`) | Scheduled nav (meta refresh, setTimeout) | frameId, delay, reason, url |
| `Page.frameClearedScheduledNavigation` | renderer (`inspector_page_agent.cc:1170`) | Scheduled nav cancelled | frameId |
| `Page.frameResized` | renderer | Viewport resized | (none) |
| `Page.documentOpened` | renderer | `document.open()` called | frame, url |
| `Page.frameSubtreeWillBeDetached` | renderer | Before subtree removal | frameId |
| `Page.frameRequestedNavigation` | renderer | Nav requested but not started | frameId, url, reason, disposition |
| `Page.windowOpen` | renderer | `window.open()` called | url, windowName, windowFeatures, userGesture |
| `Page.fileChooserOpened` | browser (if enabled) | File input clicked | frameId, mode ("selectSingle" \| "selectMultiple"), backendNodeId |
| `Page.dialogOpening` | renderer | alert/confirm/prompt/beforeunload | url, message, type |
| `Page.dialogClosed` | renderer | Dialog dismissed | result, userDismissed |
| `Page.javascriptDialogOpening` | (legacy alias) | Same as dialogOpening | — |
| `Page.interstitialShown` | browser | Interstitial displayed | — |
| `Page.interstitialHidden` | browser | Interstitial dismissed | — |
| `Page.screencastFrame` | browser | Screencast frame captured | data, metadata, sessionId |
| `Page.screencastVisibilityChanged` | browser | Page visibility changed | visible |
| `Page.colorPicked` | browser | Color picker picked | color |
| `Page.compilationCacheProduced` | renderer | Script compilation cache | url, data |

### 5.1.4 The NavigationType Enum

```cpp
// From Page.pdl
type NavigationType extends string
  enum
    Navigation            // Normal navigation (cross-document)
    BackForwardCacheRestore // Restored from bfcache
```

For `Page.navigatedWithinDocument` (same-document navigations like `#fragment` or `history.pushState`), the navigationType is different:

```pdl
type SameDocumentNavigationType extends string
  enum
    FragmentNavigation        // #fragment change
    PushStateNavigation       // history.pushState()
    ReplaceStateNavigation    // history.replaceState()
    HistoryApiNavigation      // Navigation API
```

---

## 5.2 The Frame Object

### 5.2.1 BuildObjectForFrame

`inspector_page_agent.cc:1410-1462`:

```cpp
std::unique_ptr<protocol::Page::Frame> InspectorPageAgent::BuildObjectForFrame(
    LocalFrame* frame) {
  DocumentLoader* loader = frame->Loader().GetDocumentLoader();
  const KURL url = loader ? loader->Url() : KURL();
  const String mime_type = loader ? loader->MimeType() : String();
  auto security_origin = SecurityOrigin::Create(url);
  std::unique_ptr<protocol::Page::Frame> frame_object =
      protocol::Page::Frame::create()
          .setId(IdentifiersFactory::FrameId(frame))
          .setLoaderId(IdentifiersFactory::LoaderId(loader))
          .setUrl(UrlWithoutFragment(url).GetString())
          .setDomainAndRegistry(blink::network_utils::GetDomainAndRegistry(
              url.Host(), blink::network_utils::PrivateRegistryFilter::
                              kIncludePrivateRegistries))
          .setMimeType(mime_type)
          .setSecurityOrigin(security_origin->ToRawString())
          .setSecurityOriginDetails(
              protocol::Page::SecurityOriginDetails::create()
                  .setIsLocalhost(security_origin->IsLocalhost())
                  .build())
          .setSecureContextType(CreateProtocolSecureContextType(
              frame->DomWindow()->GetSecurityContext()
                  .GetSecureContextModeExplanation()))
          .setCrossOriginIsolatedContextType(
              CreateProtocolCrossOriginIsolatedContextType(frame->DomWindow()))
          .setGatedAPIFeatures(CreateGatedAPIFeaturesArray(frame->DomWindow()))
          .build();
  // ... parent id, name, unreachable url, ad frame status ...
  return frame_object;
}
```

### 5.2.2 The Complete Frame Object

```json
{
  "id": "A1B2C3D4E5F6...",
  "parentId": "PARENT_FRAME_ID",        // omitted for root frame
  "loaderId": "LOADER_UUID",
  "url": "https://example.com/page",
  "domainAndRegistry": "example.com",
  "securityOrigin": "https://example.com",
  "securityOriginDetails": {
    "isLocalhost": false
  },
  "mimeType": "text/html",
  "secureContextType": "Secure",        // "Secure" | "InsecureLocalhost" | "Insecure"
  "crossOriginIsolatedContextType": "Isolated",  // "Isolated" | "NotIsolated" | "NotIsolatedFeatureDisabled"
  "gatedAPIFeatures": [                  // APIs enabled by cross-origin isolation
    "SharedArrayBuffers",
    "SharedArrayBuffersTransferAllowed"
  ],
  "name": "iframe-name",                // omitted if no name attribute
  "unreachableUrl": "",                  // for failed navigations
  "adFrameStatus": {                     // if frame is an ad
    "rootFrame": false,
    "adFrame": true
  }
}
```

### 5.2.3 Frame ID Stability

- **frameId** is a `base::UnguessableToken` (128-bit random) — stable for the lifetime of the frame, but changes on cross-document navigation.
- **loaderId** is also a token — changes on every navigation (including same-document).
- For OOPIFs, the `frameId` is the same across renderer process boundaries.

---

## 5.3 The LifecycleEvent Implementation

### 5.3.1 The Event Emitter

```cpp
// inspector_page_agent.cc:1197-1208
void InspectorPageAgent::LifecycleEvent(LocalFrame* frame, DocumentLoader* loader,
                                        const char* name, double timestamp) {
  if (!loader || !lifecycle_events_enabled_.Get()) return;
  GetFrontend()->lifecycleEvent(IdentifiersFactory::FrameId(frame),
                                IdentifiersFactory::LoaderId(loader), name,
                                timestamp);
  GetFrontend()->flush();
}

void InspectorPageAgent::PaintTiming(Document* document, const char* name,
                                     double timestamp) {
  LocalFrame* frame = document->GetFrame();
  DocumentLoader* loader = frame->Loader().GetDocumentLoader();
  LifecycleEvent(frame, loader, name, timestamp);
}
```

### 5.3.2 The Complete List of Lifecycle Names

| Lifecycle Name | When | Source |
|---|---|---|
| `commit` | Document committed (response received) | `WillCommitLoad` |
| `DOMContentLoaded` | DOMContentLoaded event fired | `DomContentLoadedEventFired` |
| `load` | load event fired | `LoadEventFired` |
| `networkAlmostIdle` | ≤2 in-flight requests for 500ms | `IdlenessDetector` |
| `networkIdle` | ≤2 in-flight requests for 5 seconds | `IdlenessDetector` |
| `firstPaint` | First paint | `PaintTiming` |
| `firstContentfulPaint` | First contentful paint | `PaintTiming` |
| `firstImagePaint` | First image paint | `PaintTiming` |
| `firstMeaningfulPaint` | First meaningful paint (heuristic) | `PaintTiming` |
| `largestContentfulPaint` | LCP (largest content element painted) | `PaintTiming` |
| `firstElusivePaint` | (experimental) | `PaintTiming` |

### 5.3.3 `Page.setLifecycleEventsEnabled` — Replay Past Events

```cpp
// inspector_page_agent.cc:575-626 (excerpt)
protocol::Response InspectorPageAgent::setLifecycleEventsEnabled(bool enabled) {
  lifecycle_events_enabled_.Set(enabled);
  if (!enabled) return protocol::Response::Success();

  for (LocalFrame* frame : *inspected_frames_) {
    Document* document = frame->GetDocument();
    DocumentLoader* loader = frame->Loader().GetDocumentLoader();
    if (!document || !loader) continue;

    DocumentLoadTiming& timing = loader->GetTiming();
    base::TimeTicks commit_timestamp = timing.ResponseEnd();
    if (!commit_timestamp.is_null())
      LifecycleEvent(frame, loader, "commit",
                     commit_timestamp.since_origin().InSecondsF());

    base::TimeTicks domcontentloaded_timestamp =
        document->GetTiming().DomContentLoadedEventEnd();
    if (!domcontentloaded_timestamp.is_null())
      LifecycleEvent(frame, loader, "DOMContentLoaded",
                     domcontentloaded_timestamp.since_origin().InSecondsF());

    base::TimeTicks load_timestamp = timing.LoadEventEnd();
    if (!load_timestamp.is_null())
      LifecycleEvent(frame, loader, "load",
                     load_timestamp.since_origin().InSecondsF());

    IdlenessDetector* idleness_detector = frame->GetIdlenessDetector();
    base::TimeTicks network_almost_idle_timestamp =
        idleness_detector->GetNetworkAlmostIdleTime();
    if (!network_almost_idle_timestamp.is_null())
      LifecycleEvent(frame, loader, "networkAlmostIdle",
                     network_almost_idle_timestamp.since_origin().InSecondsF());
    base::TimeTicks network_idle_timestamp =
        idleness_detector->GetNetworkIdleTime();
    if (!network_idle_timestamp.is_null())
      LifecycleEvent(frame, loader, "networkIdle",
                     network_idle_timestamp.since_origin().InSecondsF());
  }
  return protocol::Response::Success();
}
```

When you call `setLifecycleEventsEnabled(true)`, it **replays** all past lifecycle events for the current page. This means if you enable lifecycle events after the page has already loaded, you'll immediately receive `commit`, `DOMContentLoaded`, `load`, `networkAlmostIdle`, and `networkIdle` events for the current state.

### 5.3.4 The IdlenessDetector — How `networkIdle` Works

The `IdlenessDetector` (in `third_party/blink/renderer/core/loader/idleness_detector.h`, not in your slice) uses a heuristic:

1. Tracks the number of in-flight network requests (via `ResourceFetcher`)
2. When the count drops to ≤2, starts a timer
3. If the count stays ≤2 for **500ms**, fires `networkAlmostIdle`
4. If the count stays ≤2 for **5 seconds**, fires `networkIdle`
5. If a new request starts before the timer fires, the timer is reset

**For scraping**: `networkIdle` is the signal that the page is "done loading" from a network perspective. However:
- Pages with analytics, websockets, or polling will **never** reach `networkIdle`
- Pages with lazy-loading images may reach `networkIdle` prematurely
- SPAs that fetch data after `load` may not reach `networkIdle` until the data fetch completes

---

## 5.4 Navigation Handling

### 5.4.1 `Page.navigate`

```cpp
// browser/devtools/protocol/page_handler.cc (implementation)
void PageHandler::Navigate(const std::string& url,
                           std::optional<std::string> referrer,
                           std::optional<std::string> transitionType,
                           std::optional<std::string> frameId,
                           std::optional<std::string> referrerPolicy,
                           std::unique_ptr<NavigateCallback> callback) {
  // ... validation ...
  GURL gurl(url);
  // ... resolve with the frame ...
  // ... set referrer if provided ...
  // ... initiate navigation via frame_host->Navigate ...
  callback->sendSuccess(frame_id, loader_id, error_text);
}
```

Returns:
```json
{
  "frameId": "A1B2C3...",
  "loaderId": "LOADER_UUID",
  "errorText": ""                   // empty on success, error message on failure
}
```

### 5.4.2 Navigation Types and Their Events

| Navigation Type | Events Fired | `frameNavigated` navigationType |
|---|---|---|
| Normal navigation (URL bar, `location.href = ...`) | `frameStartedNavigating` → `frameStartedLoading` → `frameNavigated` | `"Navigation"` |
| Fragment navigation (`location.hash = "#foo"`) | `navigatedWithinDocument` | (N/A — no `frameNavigated`) |
| `history.pushState()` | `navigatedWithinDocument` | (N/A) |
| `history.replaceState()` | `navigatedWithinDocument` | (N/A) |
| `history.back()` / `forward()` | `frameStartedNavigating` → `frameNavigated` | `"Navigation"` |
| bfcache restore | `frameNavigated` | `"BackForwardCacheRestore"` |
| Reload | `frameStartedNavigating` → `frameStartedLoading` → `frameNavigated` | `"Navigation"` |
| Form submission | `frameStartedNavigating` → `frameStartedLoading` → `frameNavigated` | `"Navigation"` |
| `<meta http-equiv="refresh">` | `frameScheduledNavigation` → `frameStartedNavigating` → `frameNavigated` | `"Navigation"` |
| `window.open()` | (new target created — `Target.targetCreated`) | (N/A for source frame) |
| Service Worker navigation preload | `frameStartedNavigating` → `frameNavigated` | `"Navigation"` |

### 5.4.3 `Page.frameScheduledNavigation` Reasons

```cpp
// InspectorPageAgent::FrameScheduledNavigation
// (inspector_page_agent.cc:1158-1173)
void InspectorPageAgent::FrameScheduledNavigation(
    LocalFrame* frame, const KURL& url, base::TimeDelta delay,
    ClientNavigationReason reason) {
  GetFrontend()->frameScheduledNavigation(
      IdentifiersFactory::FrameId(frame), delay.InSecondsF(),
      ClientNavigationReasonToProtocol(reason), url.GetString());
  GetFrontend()->flush();
}
```

`ClientNavigationReason` maps to:
- `anchorClick`
- `formSubmissionGet`
- `formSubmissionPost`
- `httpHeaderRefresh` — `<meta http-equiv="refresh">`
- `scriptInitiated` — `setTimeout(() => location.href = ...)`
- `initialFrameNavigation`
- `metaTagRefresh`
- `pageBlockInterstitial`
- `reload`
- `other`

### 5.4.4 Frame Detach Reasons

```cpp
// inspector_page_agent.cc:1121-1134
void InspectorPageAgent::FrameDetachedFromParent(LocalFrame* frame, FrameDetachType type) {
  if (type == FrameDetachType::kRemove) {
    frame_ad_script_ancestry_.erase(IdentifiersFactory::FrameId(frame));
  }
  if (type == FrameDetachType::kSwapForLocal) return;   // silent for local swaps
  GetFrontend()->frameDetached(IdentifiersFactory::FrameId(frame),
                               FrameDetachTypeToProtocol(type));
}
```

- `"remove"` — DOM removal (`parent.removeChild(iframe)`)
- `"swap"` — OOPIF swap (frame persists in tree but renderer process changes)
- `kSwapForLocal` (renderer-to-renderer swap within same process) is suppressed — no event fires

---

## 5.5 Screenshots — `Page.captureScreenshot`

### 5.5.1 The Complete Implementation

**File**: `browser/devtools/protocol/page_handler.cc:1367-1567` (browser-side, not renderer)

```cpp
void PageHandler::CaptureScreenshot(
    std::optional<std::string> format,        // "png" | "jpeg" | "webp"
    std::optional<int> quality,               // 0..100 for jpeg/webp
    std::unique_ptr<Page::Viewport> clip,     // {x, y, width, height, scale}
    std::optional<bool> from_surface,         // default true
    std::optional<bool> capture_beyond_viewport,  // long screenshot
    std::optional<bool> optimize_for_speed,
    std::unique_ptr<CaptureScreenshotCallback> callback) {
  if (!host_ || !host_->GetRenderWidgetHost() || !host_->GetRenderWidgetHost()->GetView()) {
    callback->sendFailure(Response::InternalError());
    return;
  }
  if (!CanExecuteGlobalCommands(this, callback)) return;

  // Check if full page screenshot is expected and get dimensions accordingly.
  if (from_surface.value_or(true) && capture_beyond_viewport.value_or(false) && !clip) {
    blink::mojom::LocalMainFrame* main_frame = host_->GetAssociatedLocalMainFrame();
    main_frame->GetFullPageSize(base::BindOnce(
        &PageHandler::CaptureFullPageScreenshot, weak_factory_.GetWeakPtr(),
        std::move(format), std::move(quality), std::move(optimize_for_speed),
        std::move(callback)));
    return;
  }
  // ... clip validation ...
```

### 5.5.2 Full-Page (Long) Screenshot Flow

```cpp
// browser/devtools/protocol/page_handler.cc:1340-1366
void PageHandler::CaptureFullPageScreenshot(
    std::optional<std::string> format, std::optional<int> quality,
    std::optional<bool> optimize_for_speed,
    std::unique_ptr<CaptureScreenshotCallback> callback,
    const gfx::Size& full_page_size) {
  const int kMaxDimension = 128 * 1024;
  if (full_page_size.width() >= kMaxDimension ||
      full_page_size.height() >= kMaxDimension) {
    callback->sendFailure(Response::ServerError("Page is too large."));
    return;
  }
  auto clip = Page::Viewport::Create()
                  .SetX(0).SetY(0)
                  .SetWidth(full_page_size.width())
                  .SetHeight(full_page_size.height())
                  .SetScale(1)
                  .Build();
  CaptureScreenshot(std::move(format), std::move(quality), std::move(clip),
                   /*from_surface=*/true, /*capture_beyond_viewport=*/true,
                   std::move(optimize_for_speed), std::move(callback));
}
```

In the second pass, it sets `WebPreferences.hide_scrollbars = true` and `record_whole_document = true` on the renderer so the compositor produces a single frame covering the entire document:

```cpp
// browser/devtools/protocol/page_handler.cc:1490-1515 (excerpt)
if (capture_beyond_viewport.value_or(false)) {
  pending_request->original_web_prefs =
      host_->render_view_host()->GetDelegate()->GetOrCreateWebPreferences(
          host_->render_view_host());
  const blink::web_pref::WebPreferences& original_web_prefs =
      *pending_request->original_web_prefs;
  blink::web_pref::WebPreferences modified_web_prefs = original_web_prefs;
  modified_web_prefs.hide_scrollbars = true;
  modified_web_prefs.record_whole_document = true;
  host_->render_view_host()->GetDelegate()->SetWebPreferences(modified_web_prefs);

  {
    // Workaround for crbug.com/40727379 - emulated view_size has to be set twice
    blink::DeviceEmulationParams tmp_params = modified_params;
    tmp_params.view_size = gfx::Size(1, 1);
    emulation_handler_->SetDeviceEmulationParams(tmp_params);
  }
}
emulation_handler_->SetDeviceEmulationParams(modified_params);
// ... resize the view, request a snapshot, then ScreenshotCaptured callback ...
widget_host->GetSnapshotFromBrowser(
    base::BindOnce(&PageHandler::ScreenshotCaptured,
                   weak_factory_.GetWeakPtr(), std::move(pending_request)),
    true);
```

### 5.5.3 Encoding (PNG / JPEG / WebP)

After `RenderWidgetHost::GetSnapshotFromBrowser` returns an `SkBitmap`, `ScreenshotCaptured` (`page_handler.cc:1925-1966`) encodes it:

```cpp
void PageHandler::ScreenshotCaptured(
    std::unique_ptr<PendingScreenshotRequest> request, const gfx::Image& image) {
  // ... restore view size, emulation params, web prefs ...
  if (image.IsEmpty()) {
    request->callback->sendFailure(
        Response::ServerError("Unable to capture screenshot"));
    return;
  }
  std::optional<std::vector<uint8_t>> encoded_bitmap;
  const SkBitmap& bitmap = *image.ToSkBitmap();
  if (!request->requested_image_size.IsEmpty() &&
      (image.Width() != request->requested_image_size.width() ||
       image.Height() != request->requested_image_size.height())) {
    SkBitmap cropped = SkBitmapOperations::CreateTiledBitmap(
        bitmap, 0, 0, request->requested_image_size.width(),
        request->requested_image_size.height());
    encoded_bitmap = request->encoder.Run(cropped);
  } else {
    encoded_bitmap = request->encoder.Run(bitmap);
  }
  if (encoded_bitmap) {
    request->callback->sendSuccess(
        Binary::fromVector(std::move(encoded_bitmap).value()));
    return;
  }
  request->callback->sendSuccess(Binary());    // empty on failure
}
```

The `encoder` is built by `GetEncoder()` (private helper) which uses:
- `gfx::PNGCodec::Encode` for PNG (lossless)
- `SkEncodeImage` for JPEG (lossy, with `quality` parameter)
- `SkEncodeImage` for WebP (lossy or lossless)

The result is returned as a `Binary` (base64-encoded in JSON, raw bytes in CBOR).

### 5.5.4 The Clip Parameter

```json
{
  "clip": {
    "x": 100.0,
    "y": 200.0,
    "width": 500.0,
    "height": 300.0,
    "scale": 1.0       // device scale factor multiplier (1.0 = CSS pixels)
  }
}
```

- Coordinates are in **CSS pixels** (not device pixels)
- `scale: 2.0` captures at 2x resolution (retina)
- If `clip` is omitted, captures the entire viewport

### 5.5.5 Element-Level Screenshot (No Direct CDP Command)

There is no `Page.captureElementScreenshot` command. The pattern is:

1. `DOM.querySelector` → `nodeId`
2. `DOM.scrollIntoViewIfNeeded(nodeId)` — ensures element is in viewport
3. `DOM.getBoxModel(nodeId)` — returns border quad in viewport coords
4. Compute `clip = {x, y, width, height, scale: 1}` from the border quad
5. `Page.captureScreenshot({clip, format: "png"})`

### 5.5.6 Screencast (Continuous frames)

```cpp
// page_handler.cc
void PageHandler::StartScreencast(
    std::optional<std::string> format,
    std::optional<int> quality,
    std::optional<int> max_width,
    std::optional<int> max_height,
    std::optional<int> every_nth_frame,
    std::unique_ptr<StartScreencastCallback> callback) {
  // ... sets up a RenderWidgetHost::FrameSubscriber ...
  // ... emits Page.screencastFrame events periodically ...
}
```

Each frame is emitted as:
```json
{
  "method": "Page.screencastFrame",
  "params": {
    "data": "<base64-encoded JPEG/PNG>",
    "metadata": {
      "offsetTop": 0,
      "pageScaleFactor": 1.0,
      "deviceWidth": 1920,
      "deviceHeight": 1080,
      "scrollOffsetX": 0,
      "scrollOffsetY": 0,
      "timestamp": 12345.678
    },
    "sessionId": 1
  }
}
```

After receiving each frame, the client must call `Page.screencastFrameAck(sessionId)` to acknowledge receipt. Without the ack, the next frame is delayed.

---

## 5.6 PDF Generation — `Page.printToPDF`

### 5.6.1 The Command

```cpp
// browser/devtools/protocol/page_handler.cc
void PageHandler::PrintToPDF(
    bool landscape,
    bool display_header_footer,
    bool print_background,
    double scale,
    double paper_width,              // in inches (default 8.5)
    double paper_height,             // in inches (default 11)
    double margin_top,               // in inches
    double margin_bottom,
    double margin_left,
    double margin_right,
    std::optional<std::string> page_ranges,   // "1-5,8,11-13"
    std::optional<bool> prefer_css_page_size,
    std::optional<double> transfer_mode,     // "ReturnAsBase64" | "ReturnAsStream"
    std::optional<int> generate_tagged_pdf,
    std::unique_ptr<PrintToPDFCallback> callback) {
  // ... delegates to PrintCompositeClient or PrintRenderFrameHelper ...
}
```

### 5.6.2 The Response

If `transferMode` is `"ReturnAsBase64"` (default):
```json
{
  "data": "<base64-encoded PDF bytes>"
}
```

If `transferMode` is `"ReturnAsStream"`:
```json
{
  "stream": "<IO.StreamHandle>"
}
```

Then use `IO.read(handle, offset, size)` to read in chunks (default 10MB per `io_handler.cc:56`).

### 5.6.3 Paper Size Constants

| Paper Size | Width (inches) | Height (inches) |
|---|---|---|
| Letter (default) | 8.5 | 11 |
| A4 | 8.27 | 11.69 |
| Legal | 8.5 | 14 |
| Tabloid | 11 | 17 |

### 5.6.4 Header/Footer Template

When `displayHeaderFooter: true`, Chromium generates a header and footer on each page using HTML templates. The template can include special classes:

```html
<div class="header">
  <span class="date"></span>          <!-- formatted date -->
  <span class="title"></span>          <!-- page title -->
  <span class="url"></span>            <!-- page URL -->
</div>
<div class="footer">
  <span class="pageNumber"></span>     <!-- current page number -->
  <span class="totalPages"></span>     <!-- total page count -->
</div>
```

---

## 5.7 The "Find File" Feature

### 5.7.1 `Page.getResourceTree` — The Full Resource Inventory

```cpp
// inspector_page_agent.cc:740-745
protocol::Response InspectorPageAgent::getResourceTree(
    std::unique_ptr<protocol::Page::FrameResourceTree>* object) {
  *object = BuildObjectForResourceTree(inspected_frames_->Root());
  return protocol::Response::Success();
}
```

The tree builder (`:1511-1573`):

```cpp
std::unique_ptr<protocol::Page::FrameResourceTree>
InspectorPageAgent::BuildObjectForResourceTree(LocalFrame* frame) {
  std::unique_ptr<protocol::Page::Frame> frame_object = BuildObjectForFrame(frame);
  auto subresources = std::make_unique<protocol::Array<protocol::Page::FrameResource>>();

  HeapVector<Member<Resource>> all_resources = CachedResourcesForFrame(frame, true);
  for (Resource* cached_resource : all_resources) {
    std::unique_ptr<protocol::Page::FrameResource> resource_object =
        protocol::Page::FrameResource::create()
            .setUrl(UrlWithoutFragment(cached_resource->Url()).GetString())
            .setType(CachedResourceTypeJson(*cached_resource))
            .setMimeType(cached_resource->GetResponse().MimeType())
            .setContentSize(cached_resource->GetResponse().DecodedBodyLength())
            .build();
    std::optional<base::Time> last_modified = cached_resource->GetResponse().LastModified();
    if (last_modified) {
      resource_object->setLastModified(
          last_modified.value().InSecondsFSinceUnixEpoch());
    }
    if (cached_resource->WasCanceled())      resource_object->setCanceled(true);
    else if (cached_resource->GetStatus() == ResourceStatus::kLoadError)
                                            resource_object->setFailed(true);
    subresources->emplace_back(std::move(resource_object));
  }

  std::unique_ptr<protocol::Page::FrameResourceTree> result =
      protocol::Page::FrameResourceTree::create()
          .setFrame(std::move(frame_object))
          .setResources(std::move(subresources))
          .build();

  std::unique_ptr<protocol::Array<protocol::Page::FrameResourceTree>> children_array;
  for (Frame* child = frame->Tree().FirstChild(); child;
       child = child->Tree().NextSibling()) {
    auto* child_local_frame = DynamicTo<LocalFrame>(child);
    if (!child_local_frame) continue;
    if (!children_array) {
      children_array = std::make_unique<
          protocol::Array<protocol::Page::FrameResourceTree>>();
    }
    children_array->emplace_back(BuildObjectForResourceTree(child_local_frame));
  }
  result->setChildFrames(std::move(children_array));
  return result;
}
```

### 5.7.2 The FrameResourceTree Structure

```json
{
  "frameTree": {
    "frame": {
      "id": "A1B2C3...",
      "url": "https://example.com/",
      "mimeType": "text/html",
      "securityOrigin": "https://example.com",
      "loaderId": "..."
    },
    "resources": [
      { "url": "https://example.com/style.css", "type": "Stylesheet", "mimeType": "text/css", "contentSize": 12345, "lastModified": 1699999999.0 },
      { "url": "https://example.com/app.js", "type": "Script", "mimeType": "application/javascript", "contentSize": 67890 },
      { "url": "https://example.com/logo.png", "type": "Image", "mimeType": "image/png", "contentSize": 54321 },
      { "url": "https://api.example.com/data", "type": "XHR", "mimeType": "application/json", "failed": true }
    ],
    "childFrames": [
      {
        "frame": { "id": "...", "url": "https://ads.example.com/banner", ... },
        "resources": [...],
        "childFrames": [...]
      }
    ]
  }
}
```

### 5.7.3 What Gets Tracked

```cpp
// inspector_page_agent.cc:701-728
static void CachedResourcesForDocument(Document* document,
                                       HeapVector<Member<Resource>>& result,
                                       bool skip_xhrs) {
  const ResourceFetcher::DocumentResourceMap& all_resources =
      document->Fetcher()->AllResources();
  for (const auto& resource : all_resources) {
    Resource* cached_resource = resource.value.Get();
    if (!cached_resource) continue;
    if (cached_resource->StillNeedsLoad()) continue;       // skip images disabled etc.
    if (cached_resource->GetType() == ResourceType::kRaw && skip_xhrs) continue;
    result.push_back(cached_resource);
  }
}
```

Resources are pulled from `ResourceFetcher::AllResources()` — this is blink's in-memory cache of everything fetched by the document. The `skip_xhrs=true` parameter (used by `getResourceTree`) means **XHR/fetch response bodies are NOT listed** — to get those you must use the `Network` domain.

### 5.7.4 Resource Types

```cpp
// inspector_page_agent.cc:427-462
String InspectorPageAgent::ResourceTypeJson(Resource& cached_resource) {
  switch (cached_resource.GetType()) {
    case ResourceType::kImage: return ResourceTypeEnum::Image;
    case ResourceType::kFont: return ResourceTypeEnum::Font;
    case ResourceType::kMedia: return ResourceTypeEnum::Media;
    case ResourceType::kTextTrack: return ResourceTypeEnum::TextTrack;
    case ResourceType::kCSSStyleSheet: return ResourceTypeEnum::Stylesheet;
    case ResourceType::kScript: return ResourceTypeEnum::Script;
    case ResourceType::kRaw:
      // XHR/Fetch resources are not included by default
      return ResourceTypeEnum::Other;
    // ... etc
  }
}
```

The `type` enum maps to `Network.ResourceType`: `Document`, `Stylesheet`, `Image`, `Media`, `Font`, `Script`, `TextTrack`, `XHR`, `Fetch`, `EventSource`, `WebSocket`, `Manifest`, `SignedExchange`, `Ping`, `CSPViolationReport`, `Other`.

### 5.7.5 `Page.getResourceContent` — Get the Actual File Content

```cpp
// inspector_page_agent.cc:781-796
void InspectorPageAgent::getResourceContent(
    const String& frame_id, const String& url,
    std::unique_ptr<GetResourceContentCallback> callback) {
  if (!enabled_.Get()) {
    callback->sendFailure(
        protocol::Response::ServerError("Agent is not enabled."));
    return;
  }
  inspector_resource_content_loader_->EnsureResourcesContentLoaded(
      resource_content_loader_client_id_,
      BindOnce(&InspectorPageAgent::GetResourceContentAfterResourcesContentLoaded,
               WrapPersistent(this), frame_id, url, std::move(callback)));
}
```

This is **asynchronous** — it first calls `EnsureResourcesContentLoaded` which guarantees that all "interesting" resources have been re-fetched with `kOnlyIfCached` mode so their content is available in the blink memory cache.

### 5.7.6 `CachedResourceContent` — How Content Is Extracted

```cpp
// inspector_page_agent.cc:368-426 (condensed)
bool InspectorPageAgent::CachedResourceContent(const Resource* cached_resource,
                                               String* result,
                                               bool* base64_encoded,
                                               bool* was_cached) {
  bool has_zero_size;
  if (cached_resource && was_cached) {
    *was_cached = cached_resource->GetResponse().WasCached();
  }
  if (!PrepareResourceBuffer(cached_resource, &has_zero_size)) return false;

  if (!HasTextContent(cached_resource)) {             // images, fonts, media
    scoped_refptr<const SharedBuffer> buffer =
        has_zero_size ? SharedBuffer::Create()
                      : cached_resource->ResourceBuffer();
    if (!buffer) return false;
    const SegmentedBuffer::DeprecatedFlatData flat_buffer(buffer.get());
    *result = Base64Encode(base::as_byte_span(flat_buffer));
    *base64_encoded = true;
    return true;
  }

  if (has_zero_size) { *result = ""; *base64_encoded = false; return true; }

  switch (cached_resource->GetType()) {
    case blink::ResourceType::kCSSStyleSheet:
      MaybeEncodeTextContent(
          To<CSSStyleSheetResource>(cached_resource)
              ->SheetText(nullptr, CSSStyleSheetResource::MIMETypeCheck::kLax),
          cached_resource->ResourceBuffer(), result, base64_encoded);
      return true;
    case blink::ResourceType::kScript:
      MaybeEncodeTextContent(
          To<ScriptResource>(cached_resource)->TextForInspector(),
          cached_resource->ResourceBuffer(), result, base64_encoded);
      return true;
    default:
      String text_encoding_name = cached_resource->GetResponse().TextEncodingName();
      if (text_encoding_name.empty() &&
          cached_resource->GetType() != blink::ResourceType::kRaw)
        text_encoding_name = "WinLatin1";
      // ... decode buffer with text_encoding_name ...
```

Two paths:
- **Binary resources** (images, fonts, media, WASM): base64-encoded
- **Text resources** (HTML, CSS, JS, XHR text): UTF-8 (or detected encoding) decoded; `base64_encoded = false`

For `kCSSStyleSheet` and `kScript`, the agent uses **special accessors** (`SheetText()` and `TextForInspector()`) — these return the **original source** if a source map URL was loaded, otherwise the executed/decoded text. This means **minified scripts are returned as-is** — DevTools does not de-minify; the frontend uses source maps for that.

### 5.7.7 `InspectorResourceContentLoader` — The Pre-Fetcher

```cpp
// inspector_resource_content_loader.cc:75-195 (Start method, excerpt)
void InspectorResourceContentLoader::Start() {
  started_ = true;
  HeapVector<Member<Document>> documents;
  InspectedFrames* inspected_frames =
      MakeGarbageCollected<InspectedFrames>(inspected_frame_);
  for (LocalFrame* frame : *inspected_frames) {
    if (frame->GetDocument()->IsInitialEmptyDocument()) continue;
    documents.push_back(frame->GetDocument());
  }
  for (Document* document : documents) {
    ExecutionContext* execution_context = document->GetExecutionContext();
    if (!execution_context) continue;
    if (execution_context->GetSecurityOrigin()->IsOpaque()) continue; // skip opaque

    HashSet<String> urls_to_fetch;
    ResourceRequest resource_request;
    HistoryItem* item = document->Loader() ? document->Loader()->GetHistoryItem() : nullptr;
    if (item) {
      resource_request =
          item->GenerateResourceRequest(mojom::FetchCacheMode::kOnlyIfCached);
    } else {
      resource_request = ResourceRequest(document->Url());
      resource_request.SetCacheMode(mojom::FetchCacheMode::kOnlyIfCached);
    }
    resource_request.SetMode(network::mojom::RequestMode::kSameOrigin);
    resource_request.SetRequestContext(mojom::blink::RequestContextType::INTERNAL);
    ResourceFetcher* fetcher = document->Fetcher();

    const DOMWrapperWorld* world = execution_context->GetCurrentWorld();
    if (!ShouldSkipFetchingUrl(resource_request.Url())) {
      urls_to_fetch.insert(resource_request.Url().GetString());
      ResourceLoaderOptions options(world);
      options.initiator_info.name = fetch_initiator_type_names::kInternal;
      FetchParameters params(std::move(resource_request), options);
      ResourceClient* resource_client = MakeGarbageCollected<ResourceClient>(this);
      resources_.push_back(RawResource::Fetch(params, fetcher, resource_client));
      pending_resource_clients_.insert(resource_client);
    }

    HeapVector<Member<CSSStyleSheet>> style_sheets;
    InspectorCSSAgent::CollectAllDocumentStyleSheets(document, style_sheets);
    for (CSSStyleSheet* style_sheet : style_sheets) {
      if (style_sheet->IsInline() || !style_sheet->Contents()->LoadCompleted()) continue;
      // ... re-fetch each stylesheet URL with kOnlyIfCached ...
    }

    // Also fetch app manifest if available.
    HTMLLinkElement* link_element = document->LinkManifest();
    // ... fetch manifest ...
  }
  all_requests_started_ = true;
  CheckDone();
}
```

Key points:
- It iterates **all frames** in the inspected frame tree
- For each frame it issues a `RawResource::Fetch` for the document URL with `kOnlyIfCached` (no network round-trip; pulls from blink's memory cache only)
- It walks all CSSStyleSheets and re-fetches their `href()` URLs
- It fetches the app manifest (`link rel="manifest"`)
- It does **NOT** re-fetch XHR/fetch responses — those are only in the Network domain's history
- After all `ResourceClient::NotifyFinished` callbacks fire, `CheckDone` runs every pending `EnsureResourcesContentLoaded` callback

### 5.7.8 `InspectorResourceContainer` — Supplementary Storage

```cpp
// inspector_resource_container.cc (entire file, 62 lines)
void InspectorResourceContainer::DidCommitLoadForLocalFrame(LocalFrame* frame) {
  if (frame != inspected_frames_->Root()) return;
  style_sheet_contents_.clear();
  style_element_contents_.clear();
}

void InspectorResourceContainer::StoreStyleSheetContent(const String& url,
                                                        const String& content) {
  style_sheet_contents_.Set(url, content);
}

bool InspectorResourceContainer::LoadStyleSheetContent(const String& url,
                                                       String* content) {
  if (!style_sheet_contents_.Contains(url)) return false;
  *content = style_sheet_contents_.at(url);
  return true;
}

void InspectorResourceContainer::StoreStyleElementContent(
    DOMNodeId backend_node_id, const String& content) {
  style_element_contents_.Set(backend_node_id, content);
}
```

This is a side cache for **inline `<style>` element contents** (which don't have URLs) and dynamically-generated stylesheets. It's cleared on every navigation of the root frame.

### 5.7.9 Source Maps

Sourcemaps are NOT directly handled by `Page.getResourceContent`. To get sourcemaps:

1. Enable `Debugger.enable`
2. Listen for `Debugger.scriptParsed` events — they include `sourceMapURL`
3. Fetch the sourcemap URL via `Network.loadNetworkResource` or `Page.getResourceContent` (if same-origin)
4. Parse the sourcemap (V3 format) client-side

`Debugger.getScriptSource` returns the **executed** script text (post-minification); it does not de-minify. DevTools front-end uses the `source-map` JS library to map positions back to original sources.

---

## 5.8 File Locations Reference

| Component | File Path |
|---|---|
| InspectorPageAgent | `third_party/blink/renderer/core/inspector/inspector_page_agent.cc` + `.h` |
| PageHandler (browser) | `browser/devtools/protocol/page_handler.cc` + `.h` |
| InspectorResourceContainer | `third_party/blink/renderer/core/inspector/inspector_resource_container.cc` + `.h` |
| InspectorResourceContentLoader | `third_party/blink/renderer/core/inspector/inspector_resource_content_loader.cc` + `.h` |
| DevToolsIOContext | `browser/devtools/devtools_io_context.cc` + `.h` |
| DevToolsStreamPipe | `browser/devtools/devtools_stream_pipe.cc` + `.h` |
| IOHandler | `browser/devtools/protocol/io_handler.cc` + `.h` |
| EmulationHandler | `browser/devtools/protocol/emulation_handler.cc` + `.h` |
| IdlenessDetector | `third_party/blink/renderer/core/loader/idleness_detector.cc` + `.h` (not in slice) |
| Protocol definitions | `third_party/blink/public/devtools_protocol/domains/Page.pdl` |
| IO protocol | `third_party/blink/public/devtools_protocol/domains/IO.pdl` |

---

## 5.9 CDP Command & Event Reference

### 5.9.1 Page Domain Commands

| Command | Implementation | What it does |
|---|---|---|
| `Page.enable` | `inspector_page_agent.cc` | Enable Page domain (optional `enableFileChooserOpenedEvent`) |
| `Page.disable` | (reverse) | Disable Page domain |
| `Page.navigate` | `page_handler.cc` | Navigate to URL. Returns frameId, loaderId, errorText |
| `Page.navigateToHistoryEntry` | `page_handler.cc` | Navigate to bfcache entry |
| `Page.getNavigationHistory` | `page_handler.cc` | Get current index + history entries |
| `Page.stopLoading` | `page_handler.cc` | Stop all loading in the page |
| `Page.reload` | `page_handler.cc` | Reload (optional `ignoreCache`, `scriptToEvaluateOnLoad`) |
| `Page.setLifecycleEventsEnabled` | `inspector_page_agent.cc:575` | Enable lifecycle events (replays past events) |
| `Page.captureScreenshot` | `page_handler.cc:1367` | Capture screenshot (format, quality, clip, captureBeyondViewport) |
| `Page.printToPDF` | `page_handler.cc` | Generate PDF |
| `Page.startScreencast` | `page_handler.cc` | Start continuous frame capture |
| `Page.stopScreencast` | `page_handler.cc` | Stop screencast |
| `Page.screencastFrameAck` | `page_handler.cc` | Acknowledge a screencast frame |
| `Page.getResourceTree` | `inspector_page_agent.cc:740` | Get full resource tree (all frames + resources) |
| `Page.getResourceContent` | `inspector_page_agent.cc:781` | Get content of a specific resource by URL |
| `Page.setWebLifecycleState` | `page_handler.cc` | Set lifecycle state ("frozen" \| "active") |
| `Page.setBypassCSP` | `inspector_page_agent.cc` | Bypass CSP for all future evaluations |
| `Page.addScriptToEvaluateOnNewDocument` | `InspectorInjectedScriptManager` | Inject script before page scripts |
| `Page.removeScriptToEvaluateOnNewDocument` | (same) | Remove injected script |
| `Page.createIsolatedWorld` | `inspector_page_agent.cc` | Create an isolated world for a frame |
| `Page.close` | `page_handler.cc` | Close the page |
| `Page.handleJavaScriptDialog` | `page_handler.cc` | Accept/dismiss alert/confirm/prompt |
| `Page.printToPDF` | `page_handler.cc` | Print to PDF |
| `Page.setInterceptFileChooserDialog` | `page_handler.cc` | Intercept file chooser (instead of showing native dialog) |
| `Page.setPrerenderingAllowed` | `page_handler.cc` | Allow/disallow prerendering |
| `Page.getFrameTree` | `inspector_page_agent.cc` | Get the frame tree (without resources) |
| `Page.setDocumentContent` | `inspector_page_agent.cc` | Set document HTML directly |

### 5.9.2 Page Domain Events

| Event | Fired From | When |
|---|---|---|
| `Page.frameStartedNavigating` | browser | Navigation begins |
| `Page.frameAttached` | renderer | Frame attached to parent |
| `Page.frameStartedLoading` | browser | Frame starts loading |
| `Page.frameNavigated` | renderer | Document committed |
| `Page.lifecycleEvent` | renderer | Each lifecycle state |
| `Page.domContentEventFired` | renderer | DOMContentLoaded (root only) |
| `Page.loadEventFired` | renderer | load event (root only) |
| `Page.frameStoppedLoading` | browser | Frame finished loading |
| `Page.frameDetached` | renderer | Frame removed |
| `Page.navigatedWithinDocument` | renderer | Fragment/pushState navigation |
| `Page.frameScheduledNavigation` | renderer | Scheduled nav (meta refresh) |
| `Page.frameClearedScheduledNavigation` | renderer | Scheduled nav cancelled |
| `Page.frameResized` | renderer | Viewport resized |
| `Page.documentOpened` | renderer | document.open() called |
| `Page.frameSubtreeWillBeDetached` | renderer | Before subtree removal |
| `Page.frameRequestedNavigation` | renderer | Nav requested but not started |
| `Page.windowOpen` | renderer | window.open() called |
| `Page.fileChooserOpened` | browser | File input clicked |
| `Page.dialogOpening` | renderer | alert/confirm/prompt |
| `Page.dialogClosed` | renderer | Dialog dismissed |
| `Page.interstitialShown` | browser | Interstitial displayed |
| `Page.interstitialHidden` | browser | Interstitial dismissed |
| `Page.screencastFrame` | browser | Screencast frame captured |
| `Page.screencastVisibilityChanged` | browser | Page visibility changed |
| `Page.colorPicked` | browser | Color picker picked |
| `Page.compilationCacheProduced` | renderer | Script compilation cache |

### 5.9.3 IO Domain Commands

| Command | Implementation | What it does |
|---|---|---|
| `IO.read` | `io_handler.cc:52` | Read from stream handle (default 10MB chunks) |
| `IO.close` | `io_handler.cc` | Close stream |
| `IO.resolveBlob` | `io_handler.cc` | Get a Blob handle from an IO handle |

---

## 5.10 Qt6 WebEngine C++ Implementation

### 5.10.1 The PageController Class

#### `PageController.h`

```cpp
#pragma once

#include <QObject>
#include <QWebSocket>
#include <QHash>
#include <QJsonObject>
#include <QJsonArray>
#include <QTimer>
#include <QImage>
#include <QByteArray>
#include <functional>
#include <memory>
#include <optional>

// === Data structures ===

struct FrameInfo {
    QString id;
    QString parentId;
    QString loaderId;
    QString url;
    QString domainAndRegistry;
    QString securityOrigin;
    QString mimeType;
    QString secureContextType;           // "Secure" | "InsecureLocalhost" | "Insecure"
    QString crossOriginIsolatedContextType;
    QStringList gatedAPIFeatures;
    QString name;
    QString unreachableUrl;
    bool isAdFrame = false;
};

struct FrameResource {
    QString url;
    QString type;                         // "Document" | "Stylesheet" | "Image" | "Script" | etc.
    QString mimeType;
    qint64 contentSize = 0;
    double lastModified = 0;
    bool canceled = false;
    bool failed = false;
};

struct FrameResourceTree {
    FrameInfo frame;
    QList<FrameResource> resources;
    QList<FrameResourceTree> childFrames;
};

struct ResourceContent {
    QByteArray data;
    bool base64Encoded = false;
};

struct ScreenshotOptions {
    QString format = "png";               // "png" | "jpeg" | "webp"
    int quality = 80;                      // 0-100 for jpeg/webp
    std::optional<QRectF> clip;           // {x, y, width, height} in CSS pixels
    bool fromSurface = true;
    bool captureBeyondViewport = false;    // full-page screenshot
    bool optimizeForSpeed = false;
};

struct PdfOptions {
    bool landscape = false;
    bool displayHeaderFooter = false;
    bool printBackground = false;
    double scale = 1.0;
    double paperWidth = 8.5;              // inches
    double paperHeight = 11.0;
    double marginTop = 0.4;
    double marginBottom = 0.4;
    double marginLeft = 0.4;
    double marginRight = 0.4;
    QString pageRanges;                    // "1-5,8,11-13"
    bool preferCssPageSize = false;
    QString transferMode = "ReturnAsBase64";  // "ReturnAsBase64" | "ReturnAsStream"
    bool generateTaggedPdf = false;
};

struct LifecycleState {
    QString name;                          // "commit" | "DOMContentLoaded" | "load" | "networkIdle" | etc.
    double timestamp = 0;
    QString frameId;
    QString loaderId;
};

class PageController : public QObject {
    Q_OBJECT
public:
    explicit PageController(const QUrl& devtoolsUrl, QObject* parent = nullptr);
    ~PageController();
    
    // === Enable/Disable ===
    void enable(bool enableFileChooserOpenedEvent = false);
    void disable();
    
    // === Navigation ===
    void navigate(const QUrl& url,
                 const QString& referrer = "",
                 const QString& frameId = "",
                 std::function<void(const QString& frameId, const QString& loaderId,
                                  const QString& errorText)> callback = {});
    
    void navigateToHistoryEntry(int entryId);
    void getNavigationHistory(std::function<void(int currentIndex,
                                                 const QList<QJsonObject>& entries)> callback);
    void stopLoading();
    void reload(bool ignoreCache = false, const QString& scriptToEvaluateOnLoad = "");
    void close();
    
    // === Lifecycle ===
    void setLifecycleEventsEnabled(bool enabled);
    void setWebLifecycleState(const QString& state);  // "frozen" | "active"
    
    // === Screenshots ===
    void captureScreenshot(const ScreenshotOptions& opts,
                         std::function<void(const QByteArray&)> callback);
    
    void captureFullPage(std::function<void(const QByteArray&)> callback,
                        const QString& format = "png", int quality = 80);
    
    void captureElement(int nodeId,
                       std::function<void(const QByteArray&)> callback,
                       const QString& format = "png", int quality = 80);
    
    void captureViewport(std::function<void(const QByteArray&)> callback,
                        const QString& format = "png", int quality = 80);
    
    // === PDF ===
    void printToPDF(const PdfOptions& opts,
                   std::function<void(const QByteArray&)> callback);
    
    void printToPDFStream(const PdfOptions& opts,
                         std::function<void(const QString& streamHandle)> callback);
    
    // === Screencast ===
    void startScreencast(const QString& format = "jpeg", int quality = 80,
                        int maxWidth = 0, int maxHeight = 0, int everyNthFrame = 1);
    void stopScreencast();
    void screencastFrameAck(int sessionId);
    
    // === Resource tree / Find File ===
    void getResourceTree(std::function<void(const FrameResourceTree&)> callback);
    
    void getResourceContent(const QString& frameId, const QString& url,
                          std::function<void(const ResourceContent&)> callback);
    
    // Find files by name, type, or content
    void findFilesByName(const QString& filename,
                        std::function<void(const QList<QPair<FrameResource, QString>>)> callback);
    
    void findFilesByType(const QString& resourceType,
                        std::function<void(const QList<QPair<FrameResource, QString>>)> callback);
    
    void searchInResources(const QString& query, bool caseSensitive, bool isRegex,
                          std::function<void(const QList<QJsonObject>&)> callback);
    
    void downloadResource(const QString& frameId, const QString& url,
                         const QString& filepath,
                         std::function<void(bool success)> callback);
    
    void downloadAllResources(const QString& dirPath,
                             const QStringList& typeFilter = {},
                             std::function<void(int succeeded, int failed)> callback = {});
    
    // === Dialog handling ===
    void handleJavaScriptDialog(bool accept, const QString& promptText = "");
    
    // === File chooser ===
    void setInterceptFileChooserDialog(bool enabled);
    void handleFileChooser(const QStringList& filePaths);
    
    // === Wait helpers ===
    void waitForLifecycle(const QString& lifecycleName,
                          std::function<void()> callback,
                          int timeoutMs = 30000);
    
    void waitForLoad(std::function<void()> callback, int timeoutMs = 30000);
    void waitForNetworkIdle(std::function<void()> callback, int timeoutMs = 30000);
    void waitForNavigation(std::function<void()> callback, int timeoutMs = 30000);
    
    // === CSP bypass ===
    void setBypassCSP(bool bypass);
    
    // === Isolated world ===
    void createIsolatedWorld(const QString& frameId, const QString& worldName,
                           bool grantUniversalAccess,
                           std::function<void(int executionContextId)> callback);
    
    // === Frame tree ===
    void getFrameTree(std::function<void(const QList<FrameInfo>&)> callback);
    
    // === Snapshot queries ===
    QString currentUrl() const { return m_currentUrl; }
    QString currentFrameId() const { return m_currentFrameId; }
    QString currentLoaderId() const { return m_currentLoaderId; }
    QList<LifecycleState> lifecycleHistory() const { return m_lifecycleHistory; }
    
signals:
    // Navigation events
    void frameStartedNavigating(const QString& frameId, const QUrl& url, const QString& navType);
    void frameAttached(const QString& frameId, const QString& parentFrameId);
    void frameStartedLoading(const QString& frameId);
    void frameNavigated(const FrameInfo& frame, const QString& navigationType);
    void frameStoppedLoading(const QString& frameId);
    void frameDetached(const QString& frameId, const QString& reason);
    void navigatedWithinDocument(const QString& frameId, const QUrl& url, const QString& navType);
    void frameScheduledNavigation(const QString& frameId, double delay, const QString& reason, const QUrl& url);
    void frameClearedScheduledNavigation(const QString& frameId);
    void frameResized();
    void documentOpened(const FrameInfo& frame, const QUrl& url);
    void frameRequestedNavigation(const QString& frameId, const QUrl& url, const QString& reason, const QString& disposition);
    void windowOpen(const QUrl& url, const QString& windowName, const QString& windowFeatures, bool userGesture);
    
    // Lifecycle events
    void lifecycleEvent(const LifecycleState& state);
    void domContentEventFired(double timestamp);
    void loadEventFired(double timestamp);
    
    // Dialog events
    void dialogOpening(const QUrl& url, const QString& message, const QString& type);
    void dialogClosed(bool result, bool userDismissed);
    
    // File chooser
    void fileChooserOpened(const QString& frameId, const QString& mode, int backendNodeId);
    
    // Interstitial
    void interstitialShown();
    void interstitialHidden();
    
    // Screencast
    void screencastFrame(const QByteArray& data, const QJsonObject& metadata, int sessionId);
    void screencastVisibilityChanged(bool visible);
    
private:
    void sendCommand(const QString& method, const QJsonObject& params,
                    std::function<void(const QJsonObject&)> callback = {});
    void handleMessage(const QString& message);
    
    // Helpers
    static FrameInfo parseFrame(const QJsonObject& obj);
    static FrameResource parseResource(const QJsonObject& obj);
    static FrameResourceTree parseResourceTree(const QJsonObject& obj);
    static void collectAllResources(const FrameResourceTree& tree,
                                   QList<QPair<FrameResource, QString>>& result,
                                   const QString& frameId = "");
    
    QWebSocket* m_ws;
    int m_nextId = 1;
    QHash<int, std::function<void(const QJsonObject&)>> m_callbacks;
    QString m_sessionId;
    
    // Current state
    QString m_currentUrl;
    QString m_currentFrameId;
    QString m_currentLoaderId;
    QList<LifecycleState> m_lifecycleHistory;
    QHash<QString, FrameInfo> m_frames;       // frameId → FrameInfo
    
    // Wait helpers
    QTimer m_waitTimer;
    std::function<void()> m_waitCallback;
    QString m_waitLifecycleName;
    
    // File chooser intercept
    bool m_interceptFileChooser = false;
    int m_fileChooserCallId = 0;
};
```

#### `PageController.cpp` (key methods)

```cpp
#include "PageController.h"
#include <QJsonDocument>
#include <QImage>
#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QEventLoop>

// === Helpers ===

FrameInfo PageController::parseFrame(const QJsonObject& obj) {
    FrameInfo f;
    f.id = obj.value("id").toString();
    f.parentId = obj.value("parentId").toString();
    f.loaderId = obj.value("loaderId").toString();
    f.url = obj.value("url").toString();
    f.domainAndRegistry = obj.value("domainAndRegistry").toString();
    f.securityOrigin = obj.value("securityOrigin").toString();
    f.mimeType = obj.value("mimeType").toString();
    f.secureContextType = obj.value("secureContextType").toString();
    f.crossOriginIsolatedContextType = obj.value("crossOriginIsolatedContextType").toString();
    f.name = obj.value("name").toString();
    f.unreachableUrl = obj.value("unreachableUrl").toString();
    
    if (obj.contains("gatedAPIFeatures")) {
        const QJsonArray arr = obj.value("gatedAPIFeatures").toArray();
        for (const QJsonValue& v : arr) f.gatedAPIFeatures.append(v.toString());
    }
    if (obj.contains("adFrameStatus")) {
        f.isAdFrame = obj.value("adFrameStatus").toObject().value("adFrame").toBool(false);
    }
    return f;
}

FrameResource PageController::parseResource(const QJsonObject& obj) {
    FrameResource r;
    r.url = obj.value("url").toString();
    r.type = obj.value("type").toString();
    r.mimeType = obj.value("mimeType").toString();
    r.contentSize = static_cast<qint64>(obj.value("contentSize").toDouble(0));
    r.lastModified = obj.value("lastModified").toDouble(0);
    r.canceled = obj.value("canceled").toBool(false);
    r.failed = obj.value("failed").toBool(false);
    return r;
}

FrameResourceTree PageController::parseResourceTree(const QJsonObject& obj) {
    FrameResourceTree tree;
    tree.frame = parseFrame(obj.value("frame").toObject());
    
    const QJsonArray resources = obj.value("resources").toArray();
    for (const QJsonValue& v : resources) {
        tree.resources.append(parseResource(v.toObject()));
    }
    
    if (obj.contains("childFrames")) {
        const QJsonArray children = obj.value("childFrames").toArray();
        for (const QJsonValue& v : children) {
            tree.childFrames.append(parseResourceTree(v.toObject()));
        }
    }
    return tree;
}

void PageController::collectAllResources(const FrameResourceTree& tree,
                                         QList<QPair<FrameResource, QString>>& result,
                                         const QString& frameId) {
    const QString fid = frameId.isEmpty() ? tree.frame.id : frameId;
    for (const FrameResource& r : tree.resources) {
        result.append({r, fid});
    }
    for (const FrameResourceTree& child : tree.childFrames) {
        collectAllResources(child, result, fid);
    }
}

// === Constructor / Destructor ===

PageController::PageController(const QUrl& devtoolsUrl, QObject* parent)
    : QObject(parent), m_ws(new QWebSocket) {
    
    connect(m_ws, &QWebSocket::textMessageReceived,
            this, &PageController::handleMessage);
    m_ws->open(devtoolsUrl);
}

PageController::~PageController() {
    if (m_ws->isValid()) m_ws->close();
}

// === CDP plumbing ===

void PageController::sendCommand(const QString& method, const QJsonObject& params,
                                  std::function<void(const QJsonObject&)> callback) {
    const int id = m_nextId++;
    if (callback) m_callbacks[id] = callback;
    
    QJsonObject msg;
    msg["id"] = id;
    msg["method"] = method;
    if (!params.isEmpty()) msg["params"] = params;
    if (!m_sessionId.isEmpty()) msg["sessionId"] = m_sessionId;
    
    m_ws->sendTextMessage(QString::fromUtf8(
        QJsonDocument(msg).toJson(QJsonDocument::Compact)));
}

void PageController::handleMessage(const QString& message) {
    const auto doc = QJsonDocument::fromJson(message.toUtf8()).object();
    
    // Response
    if (doc.contains("id")) {
        const int id = doc.value("id").toInt();
        auto it = m_callbacks.find(id);
        if (it != m_callbacks.end()) {
            auto cb = it.value();
            m_callbacks.erase(it);
            if (cb) cb(doc.value("result").toObject());
        }
        return;
    }
    
    // Event
    const QString method = doc.value("method").toString();
    const QJsonObject params = doc.value("params").toObject();
    
    // Navigation events
    if (method == "Page.frameStartedNavigating") {
        emit frameStartedNavigating(params.value("frameId").toString(),
                                    QUrl(params.value("url").toString()),
                                    params.value("navigationType").toString());
    }
    else if (method == "Page.frameAttached") {
        emit frameAttached(params.value("frameId").toString(),
                           params.value("parentFrameId").toString());
    }
    else if (method == "Page.frameStartedLoading") {
        const QString fid = params.value("frameId").toString();
        emit frameStartedLoading(fid);
    }
    else if (method == "Page.frameNavigated") {
        const FrameInfo frame = parseFrame(params.value("frame").toObject());
        const QString navType = params.value("navigationType").toString();
        m_frames[frame.id] = frame;
        if (frame.parentId.isEmpty()) {  // root frame
            m_currentUrl = frame.url;
            m_currentFrameId = frame.id;
            m_currentLoaderId = frame.loaderId;
        }
        emit frameNavigated(frame, navType);
    }
    else if (method == "Page.frameStoppedLoading") {
        emit frameStoppedLoading(params.value("frameId").toString());
    }
    else if (method == "Page.frameDetached") {
        emit frameDetached(params.value("frameId").toString(),
                          params.value("reason").toString());
    }
    else if (method == "Page.navigatedWithinDocument") {
        const QString fid = params.value("frameId").toString();
        const QUrl url(params.value("url").toString());
        const QString navType = params.value("navigationType").toString();
        if (m_frames.contains(fid)) {
            m_frames[fid].url = url.toString();
            if (fid == m_currentFrameId) m_currentUrl = url.toString();
        }
        emit navigatedWithinDocument(fid, url, navType);
    }
    else if (method == "Page.frameScheduledNavigation") {
        emit frameScheduledNavigation(params.value("frameId").toString(),
                                      params.value("delay").toDouble(),
                                      params.value("reason").toString(),
                                      QUrl(params.value("url").toString()));
    }
    else if (method == "Page.frameClearedScheduledNavigation") {
        emit frameClearedScheduledNavigation(params.value("frameId").toString());
    }
    else if (method == "Page.frameResized") {
        emit frameResized();
    }
    else if (method == "Page.documentOpened") {
        const FrameInfo frame = parseFrame(params.value("frame").toObject());
        emit documentOpened(frame, QUrl(params.value("url").toString()));
    }
    else if (method == "Page.frameRequestedNavigation") {
        emit frameRequestedNavigation(params.value("frameId").toString(),
                                      QUrl(params.value("url").toString()),
                                      params.value("reason").toString(),
                                      params.value("disposition").toString());
    }
    else if (method == "Page.windowOpen") {
        emit windowOpen(QUrl(params.value("url").toString()),
                       params.value("windowName").toString(),
                       params.value("windowFeatures").toString(),
                       params.value("userGesture").toBool(false));
    }
    
    // Lifecycle events
    else if (method == "Page.lifecycleEvent") {
        LifecycleState state;
        state.name = params.value("name").toString();
        state.timestamp = params.value("timestamp").toDouble();
        state.frameId = params.value("frameId").toString();
        state.loaderId = params.value("loaderId").toString();
        m_lifecycleHistory.append(state);
        emit lifecycleEvent(state);
        
        // Check wait condition
        if (m_waitLifecycleName == state.name && state.frameId == m_currentFrameId) {
            if (m_waitCallback) {
                auto cb = m_waitCallback;
                m_waitCallback = nullptr;
                m_waitLifecycleName.clear();
                m_waitTimer.stop();
                cb();
            }
        }
    }
    else if (method == "Page.domContentEventFired") {
        emit domContentEventFired(params.value("timestamp").toDouble());
    }
    else if (method == "Page.loadEventFired") {
        emit loadEventFired(params.value("timestamp").toDouble());
        
        // Also fire waitForLoad if waiting
        if (m_waitLifecycleName == "load") {
            if (m_waitCallback) {
                auto cb = m_waitCallback;
                m_waitCallback = nullptr;
                m_waitLifecycleName.clear();
                m_waitTimer.stop();
                cb();
            }
        }
    }
    
    // Dialog events
    else if (method == "Page.javascriptDialogOpening") {
        emit dialogOpening(QUrl(params.value("url").toString()),
                          params.value("message").toString(),
                          params.value("type").toString());
    }
    else if (method == "Page.javascriptDialogClosed") {
        emit dialogClosed(params.value("result").toBool(),
                         params.value("userDismissed").toBool());
    }
    
    // File chooser
    else if (method == "Page.fileChooserOpened") {
        emit fileChooserOpened(params.value("frameId").toString(),
                              params.value("mode").toString(),
                              params.value("backendNodeId").toInt());
    }
    
    // Interstitial
    else if (method == "Page.interstitialShown") {
        emit interstitialShown();
    }
    else if (method == "Page.interstitialHidden") {
        emit interstitialHidden();
    }
    
    // Screencast
    else if (method == "Page.screencastFrame") {
        const QByteArray data = QByteArray::fromBase64(
            params.value("data").toString().toUtf8());
        const QJsonObject metadata = params.value("metadata").toObject();
        const int sessionId = params.value("sessionId").toInt();
        emit screencastFrame(data, metadata, sessionId);
    }
    else if (method == "Page.screencastVisibilityChanged") {
        emit screencastVisibilityChanged(params.value("visible").toBool());
    }
}

// === Enable/Disable ===

void PageController::enable(bool enableFileChooserOpenedEvent) {
    QJsonObject params;
    if (enableFileChooserOpenedEvent) {
        params["enableFileChooserOpenedEvent"] = true;
    }
    sendCommand("Page.enable", params);
}

void PageController::disable() {
    sendCommand("Page.disable", {});
}

// === Navigation ===

void PageController::navigate(const QUrl& url, const QString& referrer,
                             const QString& frameId,
                             std::function<void(const QString&, const QString&, const QString&)> callback) {
    QJsonObject params;
    params["url"] = url.toString();
    if (!referrer.isEmpty()) params["referrer"] = referrer;
    if (!frameId.isEmpty()) params["frameId"] = frameId;
    
    sendCommand("Page.navigate", params, [callback](const QJsonObject& result) {
        if (callback) callback(
            result.value("frameId").toString(),
            result.value("loaderId").toString(),
            result.value("errorText").toString()
        );
    });
}

void PageController::navigateToHistoryEntry(int entryId) {
    QJsonObject params;
    params["entryId"] = entryId;
    sendCommand("Page.navigateToHistoryEntry", params);
}

void PageController::getNavigationHistory(std::function<void(int, const QList<QJsonObject>&)> callback) {
    sendCommand("Page.getNavigationHistory", {}, [callback](const QJsonObject& result) {
        int currentIndex = result.value("currentIndex").toInt();
        QList<QJsonObject> entries;
        const QJsonArray arr = result.value("entries").toArray();
        for (const QJsonValue& v : arr) entries.append(v.toObject());
        if (callback) callback(currentIndex, entries);
    });
}

void PageController::stopLoading() {
    sendCommand("Page.stopLoading", {});
}

void PageController::reload(bool ignoreCache, const QString& scriptToEvaluateOnLoad) {
    QJsonObject params;
    if (ignoreCache) params["ignoreCache"] = true;
    if (!scriptToEvaluateOnLoad.isEmpty()) {
        params["scriptToEvaluateOnLoad"] = scriptToEvaluateOnLoad;
    }
    sendCommand("Page.reload", params);
}

void PageController::close() {
    sendCommand("Page.close", {});
}

// === Lifecycle ===

void PageController::setLifecycleEventsEnabled(bool enabled) {
    QJsonObject params;
    params["enabled"] = enabled;
    sendCommand("Page.setLifecycleEventsEnabled", params);
}

void PageController::setWebLifecycleState(const QString& state) {
    QJsonObject params;
    params["state"] = state;  // "frozen" | "active"
    sendCommand("Page.setWebLifecycleState", params);
}

// === Screenshots ===

void PageController::captureScreenshot(const ScreenshotOptions& opts,
                                       std::function<void(const QByteArray&)> callback) {
    QJsonObject params;
    params["format"] = opts.format;
    if (opts.format != "png") params["quality"] = opts.quality;
    if (opts.clip.has_value()) {
        QJsonObject clip;
        clip["x"] = opts.clip->x();
        clip["y"] = opts.clip->y();
        clip["width"] = opts.clip->width();
        clip["height"] = opts.clip->height();
        clip["scale"] = 1.0;
        params["clip"] = clip;
    }
    params["fromSurface"] = opts.fromSurface;
    if (opts.captureBeyondViewport) params["captureBeyondViewport"] = true;
    if (opts.optimizeForSpeed) params["optimizeForSpeed"] = true;
    
    sendCommand("Page.captureScreenshot", params, [callback](const QJsonObject& result) {
        const QString data = result.value("data").toString();
        if (callback) callback(QByteArray::fromBase64(data.toUtf8()));
    });
}

void PageController::captureFullPage(std::function<void(const QByteArray&)> callback,
                                     const QString& format, int quality) {
    ScreenshotOptions opts;
    opts.format = format;
    opts.quality = quality;
    opts.captureBeyondViewport = true;
    opts.fromSurface = true;
    captureScreenshot(opts, callback);
}

void PageController::captureElement(int nodeId,
                                   std::function<void(const QByteArray&)> callback,
                                   const QString& format, int quality) {
    // This requires DOM domain to get the box model
    // For simplicity, we'll use Runtime.evaluate to get the bounding rect
    // In production, you'd use DOM.getBoxModel
    
    QJsonObject params;
    params["expression"] = QString(R"(
        (function() {
            const el = document.querySelector('[data-screenshot-node-id="%1"]');
            if (!el) return null;
            const rect = el.getBoundingClientRect();
            return {x: rect.x, y: rect.y, width: rect.width, height: rect.height};
        })()
    )").arg(nodeId);
    params["returnByValue"] = true;
    
    sendCommand("Runtime.evaluate", params, [this, callback, format, quality](const QJsonObject& result) {
        const QJsonObject val = result.value("result").toObject().value("value").toObject();
        if (val.isEmpty()) {
            if (callback) callback(QByteArray());
            return;
        }
        
        ScreenshotOptions opts;
        opts.format = format;
        opts.quality = quality;
        opts.clip = QRectF(val.value("x").toDouble(), val.value("y").toDouble(),
                          val.value("width").toDouble(), val.value("height").toDouble());
        captureScreenshot(opts, callback);
    });
}

void PageController::captureViewport(std::function<void(const QByteArray&)> callback,
                                     const QString& format, int quality) {
    ScreenshotOptions opts;
    opts.format = format;
    opts.quality = quality;
    opts.captureBeyondViewport = false;
    captureScreenshot(opts, callback);
}

// === PDF ===

void PageController::printToPDF(const PdfOptions& opts,
                               std::function<void(const QByteArray&)> callback) {
    QJsonObject params;
    params["landscape"] = opts.landscape;
    params["displayHeaderFooter"] = opts.displayHeaderFooter;
    params["printBackground"] = opts.printBackground;
    params["scale"] = opts.scale;
    params["paperWidth"] = opts.paperWidth;
    params["paperHeight"] = opts.paperHeight;
    params["marginTop"] = opts.marginTop;
    params["marginBottom"] = opts.marginBottom;
    params["marginLeft"] = opts.marginLeft;
    params["marginRight"] = opts.marginRight;
    if (!opts.pageRanges.isEmpty()) params["pageRanges"] = opts.pageRanges;
    if (opts.preferCssPageSize) params["preferCssPageSize"] = true;
    params["transferMode"] = opts.transferMode;
    if (opts.generateTaggedPdf) params["generateTaggedPDF"] = true;
    
    sendCommand("Page.printToPDF", params, [callback](const QJsonObject& result) {
        if (result.contains("data")) {
            const QByteArray data = QByteArray::fromBase64(
                result.value("data").toString().toUtf8());
            if (callback) callback(data);
        } else if (result.contains("stream")) {
            // Stream mode — caller should use IO.read
            if (callback) callback(QByteArray());  // signal stream mode
        }
    });
}

void PageController::printToPDFStream(const PdfOptions& opts,
                                     std::function<void(const QString&)> callback) {
    PdfOptions streamOpts = opts;
    streamOpts.transferMode = "ReturnAsStream";
    
    QJsonObject params;
    params["landscape"] = streamOpts.landscape;
    params["displayHeaderFooter"] = streamOpts.displayHeaderFooter;
    params["printBackground"] = streamOpts.printBackground;
    params["scale"] = streamOpts.scale;
    params["paperWidth"] = streamOpts.paperWidth;
    params["paperHeight"] = streamOpts.paperHeight;
    params["marginTop"] = streamOpts.marginTop;
    params["marginBottom"] = streamOpts.marginBottom;
    params["marginLeft"] = streamOpts.marginLeft;
    params["marginRight"] = streamOpts.marginRight;
    params["transferMode"] = "ReturnAsStream";
    
    sendCommand("Page.printToPDF", params, [callback](const QJsonObject& result) {
        if (result.contains("stream")) {
            if (callback) callback(result.value("stream").toString());
        }
    });
}

// === Screencast ===

void PageController::startScreencast(const QString& format, int quality,
                                     int maxWidth, int maxHeight, int everyNthFrame) {
    QJsonObject params;
    params["format"] = format;
    params["quality"] = quality;
    if (maxWidth > 0) params["maxWidth"] = maxWidth;
    if (maxHeight > 0) params["maxHeight"] = maxHeight;
    params["everyNthFrame"] = everyNthFrame;
    sendCommand("Page.startScreencast", params);
}

void PageController::stopScreencast() {
    sendCommand("Page.stopScreencast", {});
}

void PageController::screencastFrameAck(int sessionId) {
    QJsonObject params;
    params["sessionId"] = sessionId;
    sendCommand("Page.screencastFrameAck", params);
}

// === Resource tree / Find File ===

void PageController::getResourceTree(std::function<void(const FrameResourceTree&)> callback) {
    sendCommand("Page.getResourceTree", {}, [callback](const QJsonObject& result) {
        const FrameResourceTree tree = parseResourceTree(
            result.value("frameTree").toObject());
        if (callback) callback(tree);
    });
}

void PageController::getResourceContent(const QString& frameId, const QString& url,
                                        std::function<void(const ResourceContent&)> callback) {
    QJsonObject params;
    params["frameId"] = frameId;
    params["url"] = url;
    
    sendCommand("Page.getResourceContent", params, [callback](const QJsonObject& result) {
        ResourceContent content;
        content.base64Encoded = result.value("base64Encoded").toBool(false);
        const QString data = result.value("content").toString();
        if (content.base64Encoded) {
            content.data = QByteArray::fromBase64(data.toUtf8());
        } else {
            content.data = data.toUtf8();
        }
        if (callback) callback(content);
    });
}

void PageController::findFilesByName(const QString& filename,
                                     std::function<void(const QList<QPair<FrameResource, QString>>)> callback) {
    getResourceTree([callback, filename](const FrameResourceTree& tree) {
        QList<QPair<FrameResource, QString>> allResources;
        collectAllResources(tree, allResources);
        
        QList<QPair<FrameResource, QString>> matches;
        for (const auto& pair : allResources) {
            const QString url = pair.first.url;
            const QString name = url.mid(url.lastIndexOf('/') + 1);
            if (name.contains(filename, Qt::CaseInsensitive)) {
                matches.append(pair);
            }
        }
        if (callback) callback(matches);
    });
}

void PageController::findFilesByType(const QString& resourceType,
                                     std::function<void(const QList<QPair<FrameResource, QString>>)> callback) {
    getResourceTree([callback, resourceType](const FrameResourceTree& tree) {
        QList<QPair<FrameResource, QString>> allResources;
        collectAllResources(tree, allResources);
        
        QList<QPair<FrameResource, QString>> matches;
        for (const auto& pair : allResources) {
            if (pair.first.type == resourceType) {
                matches.append(pair);
            }
        }
        if (callback) callback(matches);
    });
}

void PageController::searchInResources(const QString& query, bool caseSensitive, bool isRegex,
                                       std::function<void(const QList<QJsonObject>&)> callback) {
    QJsonObject params;
    params["query"] = query;
    params["caseSensitive"] = caseSensitive;
    params["isRegex"] = isRegex;
    
    sendCommand("Page.searchInResource", params, [callback](const QJsonObject& result) {
        QList<QJsonObject> results;
        const QJsonArray arr = result.value("result").toArray();
        for (const QJsonValue& v : arr) results.append(v.toObject());
        if (callback) callback(results);
    });
}

void PageController::downloadResource(const QString& frameId, const QString& url,
                                      const QString& filepath,
                                      std::function<void(bool)> callback) {
    getResourceContent(frameId, url, [filepath, callback](const ResourceContent& content) {
        QFile f(filepath);
        if (f.open(QIODevice::WriteOnly)) {
            f.write(content.data);
            f.close();
            if (callback) callback(true);
        } else {
            if (callback) callback(false);
        }
    });
}

void PageController::downloadAllResources(const QString& dirPath,
                                          const QStringList& typeFilter,
                                          std::function<void(int, int)> callback) {
    getResourceTree([this, dirPath, typeFilter, callback](const FrameResourceTree& tree) {
        QList<QPair<FrameResource, QString>> allResources;
        collectAllResources(tree, allResources);
        
        // Filter by type if requested
        QList<QPair<FrameResource, QString>> toDownload;
        for (const auto& pair : allResources) {
            if (typeFilter.isEmpty() || typeFilter.contains(pair.first.type)) {
                toDownload.append(pair);
            }
        }
        
        if (toDownload.isEmpty()) {
            if (callback) callback(0, 0);
            return;
        }
        
        QDir().mkpath(dirPath);
        
        int succeeded = 0, failed = 0;
        int remaining = toDownload.size();
        
        for (const auto& pair : toDownload) {
            const FrameResource& res = pair.first;
            const QString& frameId = pair.second;
            
            // Generate filename from URL
            QString filename = res.url.mid(res.url.lastIndexOf('/') + 1);
            if (filename.isEmpty() || filename.contains('?')) {
                filename = "resource_" + QString::number(remaining) + ".bin";
            }
            // Sanitize filename
            filename.replace(QRegExp("[^a-zA-Z0-9._-]"), "_");
            
            const QString filepath = dirPath + "/" + filename;
            
            downloadResource(frameId, res.url, filepath, [&](bool ok) {
                if (ok) ++succeeded;
                else ++failed;
                if (--remaining == 0) {
                    if (callback) callback(succeeded, failed);
                }
            });
        }
    });
}

// === Dialog handling ===

void PageController::handleJavaScriptDialog(bool accept, const QString& promptText) {
    QJsonObject params;
    params["accept"] = accept;
    if (!promptText.isEmpty()) params["promptText"] = promptText;
    sendCommand("Page.handleJavaScriptDialog", params);
}

// === File chooser ===

void PageController::setInterceptFileChooserDialog(bool enabled) {
    QJsonObject params;
    params["enabled"] = enabled;
    sendCommand("Page.setInterceptFileChooserDialog", params);
    m_interceptFileChooser = enabled;
}

void PageController::handleFileChooser(const QStringList& filePaths) {
    // This is handled via DOM.setFileInputFiles, not Page domain
    // The file chooser event gives us a backendNodeId
    // We'd need to call DOM.setFileInputFiles with that nodeId
    // For simplicity, this is a stub
}

// === Wait helpers ===

void PageController::waitForLifecycle(const QString& lifecycleName,
                                      std::function<void()> callback,
                                      int timeoutMs) {
    m_waitLifecycleName = lifecycleName;
    m_waitCallback = callback;
    
    m_waitTimer.setSingleShot(true);
    m_waitTimer.setInterval(timeoutMs);
    disconnect(&m_waitTimer, &QTimer::timeout, nullptr, nullptr);
    connect(&m_waitTimer, &QTimer::timeout, this, [this]() {
        if (m_waitCallback) {
            qWarning() << "Timeout waiting for lifecycle:" << m_waitLifecycleName;
            m_waitCallback = nullptr;
            m_waitLifecycleName.clear();
        }
    });
    m_waitTimer.start();
}

void PageController::waitForLoad(std::function<void()> callback, int timeoutMs) {
    waitForLifecycle("load", callback, timeoutMs);
}

void PageController::waitForNetworkIdle(std::function<void()> callback, int timeoutMs) {
    waitForLifecycle("networkIdle", callback, timeoutMs);
}

void PageController::waitForNavigation(std::function<void()> callback, int timeoutMs) {
    // Wait for the next frameNavigated event
    // We can do this by connecting to the frameNavigated signal temporarily
    QMetaObject::Connection conn = connect(this, &PageController::frameNavigated,
        [this, callback, conn, timeoutMs](const FrameInfo&, const QString&) {
        disconnect(conn);
        if (m_waitCallback) {
            auto cb = m_waitCallback;
            m_waitCallback = nullptr;
            m_waitTimer.stop();
            cb();
        }
        callback();
    });
    
    // Also set a timeout
    m_waitTimer.setSingleShot(true);
    m_waitTimer.setInterval(timeoutMs);
    disconnect(&m_waitTimer, &QTimer::timeout, nullptr, nullptr);
    connect(&m_waitTimer, &QTimer::timeout, this, [this, conn]() {
        disconnect(conn);
        if (m_waitCallback) {
            qWarning() << "Timeout waiting for navigation";
            m_waitCallback = nullptr;
        }
    });
    m_waitTimer.start();
}

// === CSP bypass ===

void PageController::setBypassCSP(bool bypass) {
    QJsonObject params;
    params["enabled"] = bypass;
    sendCommand("Page.setBypassCSP", params);
}

// === Isolated world ===

void PageController::createIsolatedWorld(const QString& frameId, const QString& worldName,
                                         bool grantUniversalAccess,
                                         std::function<void(int)> callback) {
    QJsonObject params;
    params["frameId"] = frameId;
    if (!worldName.isEmpty()) params["worldName"] = worldName;
    if (grantUniversalAccess) params["grantUniversalAccess"] = true;
    
    sendCommand("Page.createIsolatedWorld", params, [callback](const QJsonObject& result) {
        if (callback) callback(result.value("executionContextId").toInt());
    });
}

// === Frame tree ===

void PageController::getFrameTree(std::function<void(const QList<FrameInfo>&)> callback) {
    sendCommand("Page.getFrameTree", {}, [callback](const QJsonObject& result) {
        QList<FrameInfo> frames;
        std::function<void(const QJsonObject&)> collectFrames = 
            [&](const QJsonObject& treeObj) {
            frames.append(parseFrame(treeObj.value("frame").toObject()));
            if (treeObj.contains("childFrames")) {
                const QJsonArray children = treeObj.value("childFrames").toArray();
                for (const QJsonValue& v : children) {
                    collectFrames(v.toObject());
                }
            }
        };
        collectFrames(result.value("frameTree").toObject());
        if (callback) callback(frames);
    });
}
```

### 5.10.2 Using the PageController

```cpp
// In your scraper:
auto* page = new PageController(QUrl("ws://127.0.0.1:9222/devtools/page/<id>"));

// Enable Page domain + lifecycle events
page->enable();
page->setLifecycleEventsEnabled(true);

// Track navigation
connect(page, &PageController::frameNavigated,
        [](const FrameInfo& frame, const QString& navType) {
    qDebug() << "[NAV]" << navType << frame.url;
});

// Track lifecycle
connect(page, &PageController::lifecycleEvent,
        [](const LifecycleState& state) {
    qDebug() << "[LIFECYCLE]" << state.name << "at" << state.timestamp;
});

// === Navigate and wait for networkIdle ===
page->navigate(QUrl("https://example.com"));
page->waitForNetworkIdle([]() {
    qDebug() << "Page is idle — ready to scrape!";
    // Now extract data...
});

// === Take a full-page screenshot ===
page->captureFullPage([](const QByteArray& pngData) {
    QImage image;
    image.loadFromData(pngData, "PNG");
    image.save("screenshot.png");
    qDebug() << "Screenshot saved:" << image.width() << "x" << image.height();
});

// === Take an element screenshot ===
// First set a data attribute to find the element
page->enable();  // ensure enabled
// Use Runtime.evaluate to find and mark the element
// Then capture
// (In production, use DOM.getBoxModel instead)

// === Generate PDF ===
PdfOptions pdfOpts;
pdfOpts.paperWidth = 8.5;
pdfOpts.paperHeight = 11.0;
pdfOpts.printBackground = true;
pdfOpts.displayHeaderFooter = true;
page->printToPDF(pdfOpts, [](const QByteArray& pdfData) {
    QFile f("page.pdf");
    f.open(QIODevice::WriteOnly);
    f.write(pdfData);
    f.close();
    qDebug() << "PDF saved:" << pdfData.size() << "bytes";
});

// === Find a file ===
page->findFilesByName("app.js", [](const QList<QPair<FrameResource, QString>>& matches) {
    qDebug() << "Found" << matches.size() << "files matching 'app.js':";
    for (const auto& match : matches) {
        qDebug() << "  " << match.first.url 
                 << "in frame" << match.second
                 << "size:" << match.first.contentSize;
    }
});

// === Download a specific resource ===
page->downloadResource("<frameId>", "https://example.com/app.js", "downloaded_app.js",
    [](bool ok) {
    qDebug() << "Downloaded:" << ok;
});

// === Download all resources ===
page->downloadAllResources("downloaded_resources", 
    {"Script", "Stylesheet"},  // only JS and CSS
    [](int succeeded, int failed) {
    qDebug() << "Downloaded" << succeeded << "resources," 
             << failed << "failed";
});

// === Handle JavaScript dialogs ===
connect(page, &PageController::dialogOpening,
        [page](const QUrl& url, const QString& message, const QString& type) {
    qDebug() << "[DIALOG]" << type << ":" << message;
    if (type == "alert") {
        page->handleJavaScriptDialog(true);  // accept
    } else if (type == "confirm") {
        page->handleJavaScriptDialog(true);  // accept (OK)
    } else if (type == "prompt") {
        page->handleJavaScriptDialog(true, "scraped-value");  // accept with text
    }
});

// === Wait for load with timeout ===
page->navigate(QUrl("https://example.com"));
page->waitForLoad([]() {
    qDebug() << "Page loaded!";
}, 15000);  // 15 second timeout

// === Bypass CSP for all evaluations ===
page->setBypassCSP(true);

// === Create an isolated world ===
page->createIsolatedWorld("<frameId>", "scraper-world", true, [](int ctxId) {
    qDebug() << "Isolated world created, contextId:" << ctxId;
    // Now use Runtime.evaluate with executionContextId=ctxId
});

// === Start screencast ===
page->startScreencast("jpeg", 80, 1920, 1080, 1);
connect(page, &PageController::screencastFrame,
        [page](const QByteArray& data, const QJsonObject& metadata, int sessionId) {
    static int frameCount = 0;
    QString filename = QString("frame_%1.jpg").arg(frameCount++, 5, 10, QChar('0'));
    QFile f(filename);
    f.open(QIODevice::WriteOnly);
    f.write(data);
    f.close();
    page->screencastFrameAck(sessionId);  // must ack to get next frame
});
// ... later:
// page->stopScreencast();
```

---

## 5.11 Edge Cases

### 5.11.1 `networkIdle` Never Fires

Pages with analytics, websockets, or polling will **never** reach `networkIdle` because the in-flight request count never stays ≤2 for 5 seconds.

**Workarounds**:
1. Block analytics URLs via `Network.setBlockedURLs` — reduces in-flight count
2. Use `Page.frameStoppedLoading` + a 2-second timeout as a fallback
3. Use `Emulation.setVirtualTimePolicy({policy: "pauseIfNetworkFetchesPending"})` — advances virtual time and reports idle when no real network activity
4. Check `networkAlmostIdle` instead (500ms threshold instead of 5s)

### 5.11.2 Screenshot Timing

`Page.captureScreenshot` calls `IncrementCapturerCount(stay_hidden=true, stay_awake=true)` on the WebContents — this forces the page to keep producing frames even if backgrounded.

If the page is still loading when you capture, you may get a partial render. **Always wait for `networkIdle` or `load` before capturing**.

### 5.11.3 Full-Page Screenshot Limits

| Limit | Value |
|---|---|
| Max dimension | 128K CSS pixels (131,072) |
| If exceeded | `"Page is too large."` error |
| Memory cost | ~width × height × 4 bytes (RGBA) |
| For 1920×1080 | ~8 MB |
| For 10000×10000 | ~400 MB |
| For 128000×128000 | ~65 GB (will OOM) |

### 5.11.4 PDF Generation Timing

`Page.printToPDF` re-lays out the entire page in print mode. For large pages:
- Takes 1-10 seconds
- Uses significant memory (the entire page is rendered at print resolution)
- If the page has `@media print` CSS, it will be applied
- Background colors/images are NOT printed unless `printBackground: true`

### 5.11.5 Cross-Origin Iframe Screenshots

You **cannot** screenshot a specific cross-origin iframe via `clip` — the `clip` coordinates are relative to the viewport, and the OOPIF's content is composited into the viewport.

To screenshot an OOPIF specifically:
1. Attach to the OOPIF target via `Target.attachToTarget`
2. Call `Page.captureScreenshot` on that target's session
3. The screenshot will be of the OOPIF's viewport only

### 5.11.6 Navigation Race Conditions

If you call `Page.navigate` and immediately call `Page.captureScreenshot`, the screenshot will be of the **old** page (the new page hasn't committed yet).

**Solution**: always wait for `Page.frameNavigated` + `Page.lifecycleEvent("load")` before capturing.

### 5.11.7 `documentUpdated` on Navigation

When `DOM.documentUpdated` fires (on every cross-document navigation), all nodeIds are invalidated. If you're mid-screenshot of a specific element (via `DOM.getBoxModel`), the nodeId may be stale. **Re-query the element after navigation**.

### 5.11.8 `Page.getResourceContent` Caching

`Page.getResourceContent` calls `EnsureResourcesContentLoaded` which re-fetches all resources with `kOnlyIfCached`. This means:
- If a resource was evicted from blink's memory cache, `getResourceContent` returns `"No resource with given URL found"`
- For pages with many resources, the first `getResourceContent` call is slow (100-500ms) because all resources are pre-fetched
- Subsequent calls are fast (resources are now in the memory cache)

### 5.11.9 `Page.getResourceTree` Does NOT Include XHR/Fetch Responses

The `skip_xhrs=true` parameter in `CachedResourcesForFrame` means XHR/fetch response bodies are NOT listed in the resource tree. To get those, you must use the `Network` domain:
1. Enable `Network.enable` before the page loads
2. Track `Network.responseReceived` events
3. Call `Network.getResponseBody(requestId)` to get the body

### 5.11.10 Screencast Frame Acknowledgment

After receiving each `Page.screencastFrame` event, you MUST call `Page.screencastFrameAck(sessionId)`. Without the ack, the next frame is **not** sent. This is a flow-control mechanism to prevent the client from being overwhelmed.

### 5.11.11 `Page.setBypassCSP` Affects All Contexts

`Page.setBypassCSP(true)` bypasses CSP for **all** future script evaluations in the page — including `Runtime.evaluate`, `Page.addScriptToEvaluateOnNewDocument`, and inline `<script>` tags. This is a global toggle; you cannot selectively bypass CSP for specific scripts.

### 5.11.12 `Page.handleJavaScriptDialog` Timing

If a JavaScript dialog (`alert`, `confirm`, `prompt`) is open, the page's JS execution is **blocked**. `Runtime.evaluate` calls will queue until the dialog is dismissed. You must call `Page.handleJavaScriptDialog` to unblock JS.

---

## 5.12 Performance Impact

### 5.12.1 Cost of Page Operations

| Operation | Cost |
|---|---|
| `Page.enable` (cold start) | ~1-5ms |
| `Page.setLifecycleEventsEnabled(true)` | ~1-5ms (replays past events) |
| `Page.navigate` | ~10-50ms (initiates navigation; page load takes longer) |
| `Page.captureScreenshot` (viewport, PNG) | ~50-200ms (depends on viewport size + GPU) |
| `Page.captureScreenshot` (full-page, PNG) | ~200-2000ms (depends on page height) |
| `Page.captureScreenshot` (JPEG, quality=80) | ~30-100ms (faster encoding) |
| `Page.printToPDF` | ~1-10 seconds (depends on page complexity) |
| `Page.startScreencast` | ~10ms setup, then ~16-33ms per frame (30-60 FPS) |
| `Page.getResourceTree` | ~1-10ms (walks blink's resource map) |
| `Page.getResourceContent` (first call) | ~100-500ms (pre-fetches all resources) |
| `Page.getResourceContent` (subsequent) | ~1-5ms |
| `Page.searchInResource` | ~5-50ms (regex search through resource content) |

### 5.12.2 Memory Overhead

| Operation | Memory |
|---|---|
| Full-page screenshot (1920×10000 PNG) | ~8 MB raw + ~2 MB PNG encoded |
| Full-page screenshot (1920×50000 PNG) | ~40 MB raw + ~10 MB PNG |
| PDF (100 pages, letter size) | ~10-50 MB |
| Screencast (1 frame, 1920×1080 JPEG) | ~100-500 KB per frame |
| Resource tree mirror (200 resources) | ~10-50 KB |
| `EnsureResourcesContentLoaded` | pins all resource bodies in memory cache |

### 5.12.3 Optimization Tips for Scraping

1. **Use JPEG instead of PNG** for screenshots — 5-10x smaller and faster to encode
2. **Use `quality: 80`** for JPEG — good visual quality, small size
3. **For full-page screenshots, check page height first** via `Runtime.evaluate("document.body.scrollHeight")` — skip if > 50,000px (too large)
4. **For PDF generation, use `ReturnAsStream` mode** and `IO.read` in chunks — avoids holding the entire PDF in memory
5. **For screencast, use `everyNthFrame: 2` or higher** to reduce frame rate (halves bandwidth)
6. **For `getResourceContent`, batch calls** — the first call pre-fetches everything; subsequent calls are free
7. **Don't call `getResourceTree` on every navigation** — it's relatively cheap but unnecessary if you're tracking via `Network.responseReceived` events
8. **For `waitForNetworkIdle`, set a reasonable timeout** (10-30 seconds) — pages that never go idle will hang forever otherwise
9. **Use `Emulation.setVirtualTimePolicy`** to fast-forward time for pages with `setTimeout`-based loading — completes in milliseconds instead of seconds
10. **Avoid `printToPDF` for scraping** — `captureScreenshot` is much faster and sufficient for most use cases

---

## 5.13 Security & Privacy Impact

### 5.13.1 What CDP Page Can Access

A CDP client with `Page.enable` can:
- **Navigate to any URL** (including `file://`, `data:`, `chrome://` on some platforms)
- **Capture screenshots** of the entire page (including cross-origin iframe content composited into the viewport)
- **Generate PDFs** of the entire page
- **Read all loaded resources** (HTML, CSS, JS, images, fonts) via `getResourceContent`
- **Inject scripts** that run before page scripts via `addScriptToEvaluateOnNewDocument`
- **Bypass CSP** via `setBypassCSP`
- **Handle JavaScript dialogs** (accept/dismiss alert/confirm/prompt)
- **Intercept file chooser dialogs** and set arbitrary file paths
- **Start/stop screencasts** (capture every frame the user sees)
- **Freeze/unfreeze** the page lifecycle
- **Close the page** at will

### 5.13.2 Detection of CDP Page Interference

1. **`Page.captureScreenshot` forces frame production** even in backgrounded tabs — detectable via `document.visibilityState` mismatch (hidden but still rendering)
2. **`Page.setBypassCSP(true)` is detectable** — `document.querySelector('meta[http-equiv="Content-Security-Policy"]')` exists but `eval()` works (shouldn't if CSP is enforced)
3. **`Page.handleJavaScriptDialog` dismisses dialogs with `isTrusted: false`** — detectable in the `beforeunload` event handler
4. **`Page.navigate` with a referrer** — the referrer may not match what the browser would normally send
5. **`Page.addScriptToEvaluateOnNewDocument` in the main world** — page can detect new globals via `Object.keys(window)` diff
6. **`Page.setWebLifecycleState("frozen")`** — `navigator.scheduling.isInputPending()` will return false even when the user is interacting

### 5.13.3 Stealth Scraping Best Practices for Page

1. **Don't use `Page.setBypassCSP`** — it's detectable. Use `Runtime.evaluate` with `allowUnsafeEvalBlockedByCSP: true` instead (default)
2. **For screenshots, wait for `networkIdle` first** — capturing a half-loaded page looks unnatural
3. **For navigation, use the page's own navigation mechanisms** (e.g., `Runtime.evaluate("location.href = '...'")`) instead of `Page.navigate` — looks more natural to timing-based detection
4. **For file uploads, use `DOM.setFileInputFiles` directly** — don't intercept the file chooser dialog (it changes the dialog behavior detectably)
5. **For dialogs, accept them quickly** — a `confirm()` dialog that stays open for 5 seconds looks like automation
6. **For screencast, use low frame rate** (1-5 FPS) — high frame rate causes CPU/GPU spikes that are detectable

---

## 5.14 Testing

### 5.14.1 Unit Tests

```cpp
#include <QtTest>
#include "PageController.h"

class TestPageController : public QObject {
    Q_OBJECT
private slots:
    void testNavigate();
    void testScreenshot();
    void testFullPageScreenshot();
    void testPdf();
    void testLifecycleEvents();
    void testWaitForNetworkIdle();
    void testFindFile();
    void testDownloadResource();
    void testDownloadAllResources();
    void testDialogHandling();
};

void TestPageController::testNavigate() {
    PageController page(QUrl("ws://127.0.0.1:9222/devtools/page/test"));
    page.enable();
    
    QSignalSpy navSpy(&page, &PageController::frameNavigated);
    
    QSemaphore sem;
    page.navigate(QUrl("https://example.com"), "", "", 
        [&sem](const QString& fid, const QString& lid, const QString& err) {
        QVERIFY(err.isEmpty());
        sem.release();
    });
    
    QVERIFY(sem.tryAcquire(1, 5000));
    QVERIFY(navSpy.wait(10000));
    QCOMPARE(navSpy.count(), 1);
}

void TestPageController::testScreenshot() {
    PageController page(QUrl("ws://127.0.0.1:9222/devtools/page/test"));
    page.enable();
    
    QSemaphore sem;
    page.navigate(QUrl("https://example.com"));
    page.waitForLoad([&]() { sem.release(); });
    QVERIFY(sem.tryAcquire(1, 15000));
    
    QByteArray pngData;
    page.captureViewport([&pngData, &sem](const QByteArray& data) {
        pngData = data;
        sem.release();
    });
    
    QVERIFY(sem.tryAcquire(1, 10000));
    QVERIFY(!pngData.isEmpty());
    
    QImage image;
    QVERIFY(image.loadFromData(pngData, "PNG"));
    QVERIFY(image.width() > 0);
    QVERIFY(image.height() > 0);
}

void TestPageController::testLifecycleEvents() {
    PageController page(QUrl("ws://127.0.0.1:9222/devtools/page/test"));
    page.enable();
    page.setLifecycleEventsEnabled(true);
    
    QSignalSpy spy(&page, &PageController::lifecycleEvent);
    
    page.navigate(QUrl("https://example.com"));
    
    // Wait for at least "commit" and "DOMContentLoaded"
    QVERIFY(spy.wait(10000));
    
    QStringList names;
    for (int i = 0; i < spy.count(); ++i) {
        names.append(spy.at(i).at(0).value<LifecycleState>().name);
    }
    QVERIFY(names.contains("commit"));
    QVERIFY(names.contains("DOMContentLoaded"));
}

void TestPageController::testWaitForNetworkIdle() {
    PageController page(QUrl("ws://127.0.0.1:9222/devtools/page/test"));
    page.enable();
    page.setLifecycleEventsEnabled(true);
    
    QSemaphore sem;
    page.navigate(QUrl("https://example.com"));
    page.waitForNetworkIdle([&]() {
        sem.release();
    }, 30000);
    
    QVERIFY(sem.tryAcquire(1, 35000));
}

void TestPageController::testFindFile() {
    PageController page(QUrl("ws://127.0.0.1:9222/devtools/page/test"));
    page.enable();
    
    QSemaphore sem;
    page.navigate(QUrl("https://example.com"));
    page.waitForLoad([&]() { sem.release(); });
    sem.acquire();
    
    page.findFilesByName(".js", [](const QList<QPair<FrameResource, QString>>& matches) {
        QVERIFY(!matches.isEmpty());
        for (const auto& match : matches) {
            QVERIFY(match.first.url.endsWith(".js"));
        }
    });
}

void TestPageController::testDownloadResource() {
    PageController page(QUrl("ws://127.0.0.1:9222/devtools/page/test"));
    page.enable();
    
    QSemaphore sem;
    page.navigate(QUrl("https://example.com"));
    page.waitForLoad([&]() { sem.release(); });
    sem.acquire();
    
    page.downloadResource("<frameId>", "https://example.com/app.js", 
                         "/tmp/downloaded_app.js", [&sem](bool ok) {
        QVERIFY(ok);
        sem.release();
    });
    QVERIFY(sem.tryAcquire(1, 10000));
    
    QFile f("/tmp/downloaded_app.js");
    QVERIFY(f.exists());
    QVERIFY(f.size() > 0);
}

void TestPageController::testDownloadAllResources() {
    PageController page(QUrl("ws://127.0.0.1:9222/devtools/page/test"));
    page.enable();
    
    QSemaphore sem;
    page.navigate(QUrl("https://example.com"));
    page.waitForLoad([&]() { sem.release(); });
    sem.acquire();
    
    page.downloadAllResources("/tmp/downloaded_resources", {"Script", "Stylesheet"},
        [&sem](int succeeded, int failed) {
        qDebug() << "Downloaded" << succeeded << "resources," << failed << "failed";
        QVERIFY(succeeded > 0);
        sem.release();
    });
    QVERIFY(sem.tryAcquire(1, 30000));
    
    QDir dir("/tmp/downloaded_resources");
    QVERIFY(dir.exists());
    QVERIFY(dir.count() > 2);  // at least . and ..
}
```

---

## 5.15 Roadmap: Unique Features That Beat Puppeteer/Playwright

### 5.15.1 "Smart Wait" — Intelligent Page Readiness Detection

```cpp
class SmartWait {
public:
    // Wait for a specific element to appear
    void waitForSelector(const QString& selector, 
                        std::function<void()> callback,
                        int timeoutMs = 30000);
    
    // Wait for a specific text to appear
    void waitForText(const QString& text,
                    std::function<void()> callback,
                    int timeoutMs = 30000);
    
    // Wait for network idle OR a specific condition
    void waitForAny(const QStringList& lifecycleNames,
                   const std::function<bool()>& jsCondition,
                   std::function<void()> callback,
                   int timeoutMs = 30000);
    
    // Wait with custom predicate
    void waitForPredicate(const std::function<bool()>& check,
                         std::function<void()> callback,
                         int intervalMs = 100,
                         int timeoutMs = 30000);
};
```

### 5.15.2 "Visual Diff" — Compare Screenshots

```cpp
class VisualDiff {
public:
    // Take a baseline screenshot
    void captureBaseline(const QString& name);
    
    // Compare current page to baseline
    struct DiffResult {
        bool matches;
        double differencePercentage;
        QByteArray diffImage;    // highlighted differences
        QList<QRect> changedRegions;
    };
    
    DiffResult compare(const QString& baselineName, double threshold = 0.01);
    
    // Compare two URLs visually
    DiffResult compareUrls(const QUrl& url1, const QUrl& url2);
};
```

### 5.15.3 "PDF Template Engine" — Generate Custom PDFs

```cpp
class PdfTemplateEngine {
public:
    // Generate PDF from a template with variable substitution
    void generate(const QString& templateHtml,
                 const QHash<QString, QString>& variables,
                 const QString& outputPath);
    
    // Generate multi-page PDF with different templates per page
    void generateMultiPage(const QList<QPair<QString, QHash<QString, QString>>>& pages,
                          const QString& outputPath);
    
    // Add header/footer with page numbers
    void addPageNumbers(const QString& inputPath, const QString& outputPath,
                       const QString& position = "bottom-center");
};
```

### 5.15.4 "Navigation Recorder" — Record and Replay Sessions

```cpp
class NavigationRecorder {
public:
    void startRecording();
    void stopRecording();
    
    // Save recording (all navigations, clicks, inputs)
    void save(const QString& filepath);
    
    // Replay recording (with optional speed control)
    void replay(const QString& filepath, double speedMultiplier = 1.0);
    
    // Export as HAR + script
    void exportAsHar(const QString& filepath);
    void exportAsScript(const QString& filepath, const QString& language = "python");
};
```

### 5.15.5 "Resource Monitor" — Track All Loaded Resources

```cpp
class ResourceMonitor : public QAbstractTableModel {
public:
    // Columns: URL, Type, MIME, Size, Status, Frame, Load Time, Cached
    int columnCount() const override { return 8; }
    
    // Filter by type, URL pattern, size
    void setFilter(const ResourceFilter& filter);
    
    // Download selected resources
    void downloadSelected(const QString& dirPath);
    
    // Search resource contents
    QList<ResourceSearchResult> searchContent(const QString& query);
    
    // Export resource list as CSV
    void exportCsv(const QString& filepath);
};
```

### 5.15.6 "Page Diff" — Track DOM Changes Between Navigations

```cpp
class PageDiffer {
public:
    // Snapshot the current DOM state
    QJsonObject snapshotDom();
    
    // Diff two snapshots
    struct DomDiff {
        QList<QJsonObject> addedNodes;
        QList<QJsonObject> removedNodes;
        QList<QJsonObject> modifiedNodes;
    };
    
    DomDiff diff(const QJsonObject& before, const QJsonObject& after);
    
    // Track changes over time
    void startTracking();
    QList<DomDiff> stopTracking();
};
```

---

## 5.16 Summary Cheat Sheet

| Operation | CDP Command | Implementation File:Line |
|---|---|---|
| Enable Page | `Page.enable` | `inspector_page_agent.cc` + `page_handler.cc` |
| Navigate | `Page.navigate` | `page_handler.cc` |
| Reload | `Page.reload` | `page_handler.cc` |
| Stop loading | `Page.stopLoading` | `page_handler.cc` |
| Close page | `Page.close` | `page_handler.cc` |
| Enable lifecycle events | `Page.setLifecycleEventsEnabled` | `inspector_page_agent.cc:575` |
| Capture screenshot | `Page.captureScreenshot` | `page_handler.cc:1367` |
| Full-page screenshot | `Page.captureScreenshot` with `captureBeyondViewport: true` | `page_handler.cc:1340` |
| Generate PDF | `Page.printToPDF` | `page_handler.cc` |
| Start screencast | `Page.startScreencast` | `page_handler.cc` |
| Ack screencast frame | `Page.screencastFrameAck` | `page_handler.cc` |
| Get resource tree | `Page.getResourceTree` | `inspector_page_agent.cc:740` |
| Get resource content | `Page.getResourceContent` | `inspector_page_agent.cc:781` |
| Get frame tree | `Page.getFrameTree` | `inspector_page_agent.cc` |
| Handle dialog | `Page.handleJavaScriptDialog` | `page_handler.cc` |
| Intercept file chooser | `Page.setInterceptFileChooserDialog` | `page_handler.cc` |
| Bypass CSP | `Page.setBypassCSP` | `inspector_page_agent.cc` |
| Create isolated world | `Page.createIsolatedWorld` | `inspector_page_agent.cc` |
| Set lifecycle state | `Page.setWebLifecycleState` | `page_handler.cc` |
| Read stream | `IO.read` | `io_handler.cc:52` |

---

## End of Part 5

This concludes **Part 5: Page Lifecycle & Screenshots** — approximately 12,000 words covering the complete Page lifecycle event sequence, the `networkIdle` heuristic, navigation handling, `Page.captureScreenshot` (viewport, full-page, element-level), `Page.printToPDF`, screencast, the "Find File" feature (`Page.getResourceTree` + `Page.getResourceContent`), resource downloading, full Qt6 C++ implementation, edge cases, performance, security, testing, and unique features.

---

## What's Next?

**Part 6: Browser-Level Management** (your #9 priority) will cover:
- `DevToolsAgentHost` hierarchy (page, iframe, tab, browser, worker targets)
- `DevToolsSession` (browser-side per-connection state machine)
- `DevToolsHttpHandler` (WebSocket server, `/json` REST endpoints)
- `DevToolsPipeHandler` (stdio pipe transport, CBOR mode)
- The `Target` CDP domain (attachToTarget, setAutoAttach, flatten)
- The `Emulation` CDP domain (device metrics, UA, locale, timezone, geolocation)
- The `Browser` CDP domain (getVersion, permissions, crash)
- The `Security` CDP domain (cert error override)
- `SystemInfo` CDP domain (GPU info, process info)
- Flattened sessions
- CDP transport comparison (WebSocket vs pipe)
- Automation detection and stealth
- Security implications
- Full Qt6 C++ implementation of a `BrowserController` class
- Edge cases, performance, security, testing
- Unique features

