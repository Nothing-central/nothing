# packages/devtools

Complete DevTools module for the Nothing Browser ecosystem. Provides full Chrome DevTools Protocol (CDP) access via Qt6 WebEngine's remote debugging port, exposing network capture, JS runtime, DOM inspection, page lifecycle, storage tracking, and multi-target management.

## Architecture

```
┌──────────────────────────────────────────────────────────────┐
│  Browser App (Sabre / Nothing / Private)                    │
│                                                              │
│  ┌─────────────────────────────────────────────────────┐    │
│  │  DevToolsServer                                      │    │
│  │  (packages/devtools/include/nothing/devtools/)      │    │
│  │                                                      │    │
│  │  ┌──────────┐  ┌──────────┐  ┌──────────┐         │    │
│  │  │ Network  │  │ Runtime  │  │   DOM    │         │    │
│  │  │ Tracker  │  │ Tracker  │  │ Tracker  │         │    │
│  │  └────┬─────┘  └────┬─────┘  └────┬─────┘         │    │
│  │       │              │              │                │    │
│  │  ┌────┴─────┐  ┌────┴─────┐  ┌────┴─────┐         │    │
│  │  │  Page    │  │ Storage  │  │ Target   │         │    │
│  │  │ Tracker  │  │ Tracker  │  │ Manager  │         │    │
│  │  └────┬─────┘  └────┬─────┘  └────┬─────┘         │    │
│  │       │              │              │                │    │
│  │       └──────────────┼──────────────┘                │    │
│  │                      │                                │    │
│  │               ┌──────┴──────┐                         │    │
│  │               │  CdpClient  │ ← WebSocket to           │    │
│  │               │             │   ws://127.0.0.1:PORT    │    │
│  │               └─────────────┘   /devtools/browser    │    │
│  └──────────────────────────────────────────────────────┘    │
│                            │                                 │
│  QWebEngineProfile         │ (Chrome DevTools Protocol       │
│  .setRemoteDebuggingPort(9222)  via WebSocket)               │
│                            │                                 │
│  ┌─────────────────────────┴───────────────────────────┐    │
│  │  Qt6 WebEngine (Chromium)                           │    │
│  │  --remote-debugging-port=9222                      │    │
│  │  Exposes full CDP on ws://127.0.0.1:9222           │    │
│  └────────────────────────────────────────────────────┘    │
│                                                              │
│  ┌──────────────────────────────────────────────────────┐   │
│  │  piggy (JS/TS IPC)                                  │   │
│  │  js/devtools.js → IPC → C++ DevToolsServer         │   │
│  └──────────────────────────────────────────────────────┘   │
└──────────────────────────────────────────────────────────────┘
```

### How It Works

1. **QWebEngineProfile** enables Chrome's remote debugging port (internally passes `--remote-debugging-port=N` to the Chromium engine)
2. **DevToolsServer** connects to `ws://127.0.0.1:N/devtools/browser` via WebSocket
3. **CdpClient** speaks the Chrome DevTools Protocol (CDP) — the same protocol Chrome's own DevTools uses
4. Each domain tracker (Network, Runtime, DOM, Page, Storage) subscribes to CDP events and exposes Qt signals
5. Browser UI connects to tracker signals to render the DevTools panels
6. The JS wrapper (`devtools.js`) bridges to the C++ side via piggy's named-pipe IPC

### Why CDP instead of Qt native APIs?

Qt6 WebEngine has limited DevTools APIs (`javaScriptConsoleMessage`, `loadFinished`, etc). CDP gives **full** Chrome DevTools power:
- Every network request with headers, bodies, timings
- WebSocket frame-level inspection
- Full DOM tree with mutations
- JS console + exceptions + stack traces
- JS evaluation with RemoteObject handles
- Cookies, localStorage, IndexedDB, CacheStorage
- Screenshots, PDF generation
- Multi-tab management with flattened sessions
- Emulation (device metrics, UA, locale, timezone, geolocation)

## Files

| File | Responsibility |
|---|---|
| `include/nothing/devtools/DevToolsModels.h` | All data structures (NetworkRequest, ConsoleMessage, DomNode, CookieInfo, etc.) |
| `include/nothing/devtools/CdpClient.h` | Low-level CDP WebSocket client — sends commands, receives events |
| `include/nothing/devtools/DevToolsServer.h` | Main orchestrator — creates and manages all domain trackers |
| `include/nothing/devtools/NetworkTracker.h` | Network tab — captures every HTTP request, response, WebSocket frame, SSE event |
| `include/nothing/devtools/RuntimeTracker.h` | JS console, exceptions, evaluate, callFunctionOn, bindings, script injection |
| `include/nothing/devtools/DomTracker.h` | DOM inspection — getDocument, querySelector, mutations, CSS, box model, file upload |
| `include/nothing/devtools/PageTracker.h` | Page lifecycle — navigation, lifecycle events, screenshots, PDF, Find File |
| `include/nothing/devtools/StorageTracker.h` | Cookies, localStorage, sessionStorage, IndexedDB, CacheStorage |
| `include/nothing/devtools/TargetManager.h` | Multi-tab management — attachToTarget, setAutoAttach, browser contexts |
| `src/*.cpp` | Implementations for all headers |
| `js/devtools.js` | JS/TS API wrapper for piggy |
| `CMakeLists.txt` | Build configuration (Qt6, C++17) |

## Setup

### 1. Enable Remote Debugging on QWebEngineProfile

```cpp
// In your browser's main.cpp or profile setup:
auto* profile = QWebEngineProfile::defaultProfile();
// Qt 6.2+ doesn't expose setRemoteDebuggingPort directly,
// so use the environment variable approach:
qputenv("QTWEBENGINE_CHROMIUM_FLAGS", "--remote-debugging-port=9222");
// OR set it before QCoreApplication is created:
// QGuiApplication::setAttribute(Qt::AA_EnableRemoteDebugging);
```

### 2. Create DevToolsServer

```cpp
#include <nothing/devtools/DevToolsServer.h>

auto* devtools = new nothing::devtools::DevToolsServer(9222, this);

// Connect to the browser-level CDP endpoint
devtools->connectToBrowser();

// Wait for connection
connect(devtools, &nothing::devtools::DevToolsServer::connected, [=]() {
    qDebug() << "DevTools connected!";

    // Enable auto-attach to all pages
    devtools->enableAutoAttach();

    // When a new page is attached, enable all domain trackers for it
    connect(devtools, &nothing::devtools::DevToolsServer::targetAttached,
        [=](const QString& sessionId, const nothing::devtools::TargetInfo& info) {
        qDebug() << "Target attached:" << info.type << info.url;
        devtools->enableAll(sessionId);
    });
});
```

### 3. Use Domain Trackers

```cpp
// Network tab — listen for every request
connect(devtools->network(), &nothing::devtools::NetworkTracker::requestWillBeSent,
    [](const nothing::devtools::NetworkRequest& req) {
    qDebug() << "[REQ]" << req.request.method << req.request.url.toString();
});

connect(devtools->network(), &nothing::devtools::NetworkTracker::responseReceived,
    [](const nothing::devtools::NetworkRequest& req) {
    qDebug() << "[RESP]" << req.response.status << req.response.protocol
             << req.response.remoteIPAddress << ":" << req.response.remotePort;
});

// Console — listen for every console.log
connect(devtools->runtime(), &nothing::devtools::RuntimeTracker::consoleApiCalled,
    [](const nothing::devtools::ConsoleMessage& msg) {
    qDebug() << "[CONSOLE]" << msg.args.first().description;
});

// Exceptions
connect(devtools->runtime(), &nothing::devtools::RuntimeTracker::exceptionThrown,
    [](const nothing::devtools::ExceptionDetails& d) {
    qDebug() << "[EXCEPTION]" << d.text << "at" << d.url << ":" << d.lineNumber;
});

// DOM mutations
connect(devtools->dom(), &nothing::devtools::DomTracker::childNodeInserted,
    [](int parent, int prev, const nothing::devtools::DomNode& node) {
    qDebug() << "[DOM+]" << node.nodeName << "parent:" << parent;
});

// Page lifecycle
connect(devtools->page(), &nothing::devtools::PageTracker::lifecycleEvent,
    [](const nothing::devtools::LifecycleEvent& event) {
    qDebug() << "[LIFECYCLE]" << event.name;
});

// Cookie changes
connect(devtools->storage(), &nothing::devtools::StorageTracker::cookieChanged,
    [](const nothing::devtools::CookieInfo& cookie, bool deleted, const QString& cause) {
    qDebug() << "[COOKIE]" << (deleted ? "deleted" : "added") << cookie.name
             << "domain:" << cookie.domain << "cause:" << cause;
});
```

## How Browsers Use This

### Sabre Browser (Daily Driver)

Sabre uses DevToolsServer for its built-in DevTools panel:

```cpp
// In Sabre's main window:
class SabreMainWindow : public QMainWindow {
    nothing::devtools::DevToolsServer* m_devtools;

    void setupDevTools() {
        m_devtools = new nothing::devtools::DevToolsServer(9222, this);
        m_devtools->connectToBrowser();

        connect(m_devtools, &nothing::devtools::DevToolsServer::connected, [=]() {
            m_devtools->enableAutoAttach();
        });

        connect(m_devtools, &nothing::devtools::DevToolsServer::targetAttached,
            [=](const QString& sessionId, const auto& info) {
            if (info.type == "page") {
                m_devtools->enableAll(sessionId);
                m_devToolsPanel->setActiveSession(sessionId);
            }
        });

        // Network panel
        connect(m_devtools->network(),
                &nothing::devtools::NetworkTracker::requestWillBeSent,
            m_networkPanel, &NetworkPanel::addRequest);
        connect(m_devtools->network(),
                &nothing::devtools::NetworkTracker::responseReceived,
            m_networkPanel, &NetworkPanel::updateRequest);
        connect(m_devtools->network(),
                &nothing::devtools::NetworkTracker::loadingFinished,
            m_networkPanel, &NetworkPanel::finishRequest);
        connect(m_devtools->network(),
                &nothing::devtools::NetworkTracker::loadingFailed,
            m_networkPanel, &NetworkPanel::failRequest);

        // Console panel
        connect(m_devtools->runtime(),
                &nothing::devtools::RuntimeTracker::consoleApiCalled,
            m_consolePanel, &ConsolePanel::addMessage);
        connect(m_devtools->runtime(),
                &nothing::devtools::RuntimeTracker::exceptionThrown,
            m_consolePanel, &ConsolePanel::addException);

        // DOM panel
        connect(m_devtools->dom(),
                &nothing::devtools::DomTracker::documentUpdated,
            m_domPanel, &DomPanel::refreshTree);
        connect(m_devtools->dom(),
                &nothing::devtools::DomTracker::childNodeInserted,
            m_domPanel, &DomPanel::insertNode);
        connect(m_devtools->dom(),
                &nothing::devtools::DomTracker::childNodeRemoved,
            m_domPanel, &DomPanel::removeNode);

        // Storage panel
        connect(m_devtools->storage(),
                &nothing::devtools::StorageTracker::cookieChanged,
            m_storagePanel, &StoragePanel::updateCookie);
        connect(m_devtools->storage(),
                &nothing::devtools::StorageTracker::domStorageItemUpdated,
            m_storagePanel, &StoragePanel::updateStorageItem);
    }
};
```

### Nothing Browser (Scraper)

Nothing Browser uses DevToolsServer for programmatic scraping:

```cpp
// Navigate, wait for idle, extract data
m_devtools->page()->navigate(QUrl("https://example.com"));
m_devtools->page()->waitForNetworkIdle([=]() {
    // Page is ready — extract data via JS
    nothing::devtools::EvaluateOptions opts;
    opts.returnByValue = true;
    m_devtools->runtime()->evaluate(
        "Array.from(document.querySelectorAll('.product')).map(el => ({"
        "  name: el.querySelector('.name').textContent,"
        "  price: el.querySelector('.price').textContent"
        "}))",
        opts,
        [](const nothing::devtools::EvaluateResult& result) {
            qDebug() << "Scraped data:" << result.result.value;
        });
});

// Capture all network requests for HAR export
// ... (listen to network signals, build a list, export as JSON)

// Download all scripts
m_devtools->page()->findFilesByName(".js", [=](const QList<auto>& matches) {
    for (const auto& match : matches) {
        m_devtools->page()->downloadResource(match.second, match.first.url.toString(),
            "scripts/" + match.first.url.fileName());
    }
});

// Take a full-page screenshot
m_devtools->page()->captureFullPage([](const QByteArray& png) {
    QFile f("screenshot.png");
    f.open(QIODevice::WriteOnly);
    f.write(png);
});

// Override UA + locale for stealth
m_devtools->cdp()->sendCommand("Emulation.setUserAgentOverride", QJsonObject{
    {"userAgent", "Mozilla/5.0 (iPhone; CPU iPhone OS 17_0...)"},
    {"acceptLanguage", "en-US,en;q=0.9"},
    {"platform", "iPhone"}
}, sessionId);
```

### Nothing Private Browser

Private browser uses DevToolsServer with additional privacy settings:

```cpp
// Ignore cert errors for dev (use carefully)
m_devtools->cdp()->sendCommand("Security.setIgnoreCertificateErrors",
    QJsonObject{{"ignore", true}}, sessionId);

// Block tracking domains
m_devtools->network()->setBlockedUrls({
    "*://*.doubleclick.net/*",
    "*://*.google-analytics.com/*",
    "*://*.facebook.net/*"
});

// Disable cache for fresh state
m_devtools->network()->setCacheDisabled(true);

// Hide automation
m_devtools->cdp()->sendCommand("Emulation.setAutomationOverride",
    QJsonObject{{"enabled", false}}, sessionId);
```

## JS/TS API (piggy)

The `devtools.js` file provides a clean API for piggy users:

```typescript
import { devtools } from "nothing-browser";

// Enable DevTools
await devtools.enable();

// Network
const requests = await devtools.network.getAllRequests();
const body = await devtools.network.getResponseBody(requests[0].requestId);
devtools.network.on("requestWillBeSent", (req) => {
    console.log(`[REQ] ${req.method} ${req.url}`);
});

// Runtime — evaluate JS
const result = await devtools.runtime.evaluate("document.title", {
    returnByValue: true,
});
console.log(result.value); // → "Page Title"

// Runtime — inject script before page scripts
const scriptId = await devtools.runtime.injectScript(
    "window.__scraper = { extract: () => document.querySelector('h1').textContent };",
    { worldName: "scraper-world", grantUniversalAccess: true }
);

// Runtime — listen for console messages
devtools.runtime.on("consoleApiCalled", (msg) => {
    console.log(`[${msg.type}]`, ...msg.args);
});

// DOM — query elements
const nodeId = await devtools.dom.querySelector("h1");
const html = await devtools.dom.getOuterHTML(nodeId);
const styles = await devtools.dom.getComputedStyle(nodeId);

// Page — navigate and wait
await devtools.page.navigate("https://example.com");
await devtools.page.waitForLoad();
await devtools.page.waitForNetworkIdle();

// Page — screenshot
const base64Png = await devtools.page.screenshot({ format: "png" });

// Page — find files
const files = await devtools.page.findFiles("app.js");
for (const file of files) {
    console.log(file.url, file.size, file.type);
}

// Page — download a resource
await devtools.page.downloadResource(file.frameId, file.url, "app.js");

// Storage — cookies
const cookies = await devtools.storage.getAllCookies();
await devtools.storage.setCookie({
    name: "session", value: "abc123", domain: "example.com"
});
await devtools.storage.deleteCookiesForDomain("tracking.com");

// Storage — localStorage
const items = await devtools.storage.getLocalStorage("https://example.com");
await devtools.storage.setLocalStorageItem("https://example.com", "key", "value");

// Storage — IndexedDB
const dbs = await devtools.storage.listDatabases("https://example.com");
const data = await devtools.storage.readObjectStore(
    "https://example.com", "mydb", "users", 0, 100
);

// Targets — multi-tab management
const targets = await devtools.targets.list();
const newTabId = await devtools.targets.create("https://example.com");

// Emulation — stealth
await devtools.emulation.setUserAgent("Mozilla/5.0 ...");
await devtools.emulation.setDeviceMetrics({
    width: 390, height: 844, deviceScaleFactor: 3, mobile: true
});
await devtools.emulation.setLocale("en-US");
await devtools.emulation.setTimezone("America/New_York");
await devtools.emulation.hideAutomation();
await devtools.emulation.ignoreCertErrors(true);
```

## CDP Domain Reference

| Domain | Tracker | Key Capabilities |
|---|---|---|
| Network | `NetworkTracker` | requestWillBeSent, responseReceived, dataReceived, loadingFinished, loadingFailed, webSocketFrameSent/Received, eventSourceMessageReceived, getResponseBody, getRequestPostData, setExtraHTTPHeaders, setBlockedURLs, emulateNetworkConditions, setCacheDisabled |
| Runtime | `RuntimeTracker` | evaluate, callFunctionOn, getProperties, releaseObject, consoleAPICalled, exceptionThrown, executionContextCreated/Destroyed, addBinding, addScriptToEvaluateOnNewDocument, createIsolatedWorld |
| DOM | `DomTracker` | getDocument, querySelector, querySelectorAll, setAttributeValue, removeAttribute, removeNode, getOuterHTML, setOuterHTML, focus, scrollIntoViewIfNeeded, resolveNode, getBoxModel, setFileInputFiles, getComputedStyle, getMatchedStyles, createStyleSheet, addRule |
| Page | `PageTracker` | navigate, reload, close, captureScreenshot, printToPDF, handleJavaScriptDialog, setLifecycleEventsEnabled, getResourceTree, getResourceContent, findFilesByName, downloadResource, waitForLoad, waitForNetworkIdle, setBypassCSP |
| Storage | `StorageTracker` | getCookies, setCookie, deleteCookie, clearAllCookies, exportCookiesToJson, importCookiesFromJson, getDomStorageItems, setDomStorageItem, removeDomStorageItem, requestDatabaseNames, requestDatabase, requestData, requestCacheNames, requestCacheEntries, requestCachedResponse, trackCookies, trackIndexedDB, trackCacheStorage |
| Target | `TargetManager` | getTargets, attachToTarget, setAutoAttach, createTarget, closeTarget, activateTarget, createBrowserContext, disposeBrowserContext |
| Emulation | via `cdp()->sendCommand()` | setDeviceMetricsOverride, setUserAgentOverride, setLocaleOverride, setTimezoneOverride, setGeolocationOverride, setIdleOverride, setScriptExecutionDisabled, setTouchEmulationEnabled, setCPUThrottlingRate, setAutomationOverride |
| Security | via `cdp()->sendCommand()` | setIgnoreCertificateErrors, setOverrideCertificateErrors, handleCertificateError |
| Fetch | via `cdp()->sendCommand()` | enable, continueRequest, fulfillRequest, failRequest, getResponseBody, takeResponseBodyAsStream |
| Browser | via `cdp()->sendCommand()` | getVersion, getBrowserCommandLine, grantPermissions, crash |
| SystemInfo | via `cdp()->sendCommand()` | getInfo, getProcessInfo |

## Dependencies

- **Qt6 Core** — QObject, signals/slots, JSON
- **Qt6 WebSockets** — QWebSocket for CDP transport
- **Qt6 Network** — QNetworkAccessManager for /json/version discovery
- **Qt6 WebEngineCore** — QWebEngineProfile (for remote debugging port)
- **C++17** — std::optional, structured bindings, if constexpr

## Known Limitations

1. **Remote debugging port only**: Qt6 WebEngine exposes CDP via `--remote-debugging-port`. The pipe transport (`--remote-debugging-pipe`) is not directly accessible from Qt's public API. For production scraping, consider setting `QTWEBENGINE_CHROMIUM_FLAGS=--remote-debugging-pipe=cbor` via `qputenv`.

2. **Port discovery**: The browser GUID is discovered via HTTP `/json/version`. This requires a brief HTTP request before the WebSocket can connect. For faster startup, cache the GUID.

3. **Per-session routing**: Domain trackers currently listen to ALL CDP events regardless of sessionId. For multi-tab scenarios, you need to filter by sessionId in your signal handlers. A future improvement would be to add per-session tracker instances.

4. **No direct V8 inspector access**: Qt6 WebEngine doesn't expose the V8 inspector directly. All V8 interaction goes through CDP's Runtime domain, which is sufficient but adds WebSocket latency (~0.5-2ms per round-trip).

5. **IPC handler stubs**: The JS API (`devtools.js`) uses piggy's IPC (named pipes) to call C++ methods. The actual IPC handler registration (`devtools.enable`, `devtools.network.getAllRequests`, etc.) needs to be wired up when piggycpp's command-registration API is available. The JS API signatures are ready; the C++ IPC bridge is the missing piece.

6. **No pipe transport**: `CdpClient` only supports WebSocket transport. A `PipeTransport` class (for `--remote-debugging-pipe=cbor`) is planned but not implemented. Pipe transport would eliminate the port and provide CBOR binary protocol for lower latency.

## Future Improvements

- [ ] Per-session domain tracker instances (for multi-tab isolation)
- [ ] Pipe transport (`--remote-debugging-pipe=cbor`) for lower latency
- [ ] Fetch domain tracker (request interception, response mocking)
- [ ] Debugger domain tracker (breakpoints, stepping, call frame evaluation)
- [ ] Profiler domain tracker (CPU profiling, heap snapshots)
- [ ] HAR export from NetworkTracker
- [ ] Network throttling presets (2G, 3G, 4G, WiFi)
- [ ] Visual element picker (hover to highlight, click to select)
- [ ] DOM snapshot diff (track changes between navigations)
- [ ] Cookie vault (encrypted cross-session persistence)
- [ ] IndexedDB full dump (paginated, with blob support)
