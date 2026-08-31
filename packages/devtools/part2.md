# PART 2: NETWORK TAB — FULL REQUEST/RESPONSE CAPTURE

## The Ultimate Qt6 WebEngine Scraping Browser Guide

*Exhaustive implementation reference — every byte from URL bar to response body to your scraper.*

---

## 2.1 The Dual-Agent Architecture

Chromium's `Network` CDP domain is implemented by **two cooperating agents** that fire **the same events** for different request categories. Understanding this split is essential — without it, you'll be confused why some requests show up in your scraper and others don't.

### 2.1.1 The Two Agents

| Agent | Process | File | Handles |
|---|---|---|---|
| `InspectorNetworkAgent` | Renderer (Blink) | `third_party/blink/renderer/core/inspector/inspector_network_agent.cc` | Subresource requests the *renderer* issues: `ResourceFetcher`, `ThreadableLoader`, `XMLHttpRequest`, `EventSource`, `WebSocket`, `fetch()`. Also Worker-thread requests. |
| `NetworkHandler` | Browser | `browser/devtools/protocol/network_handler.cc` | Navigation requests (`NavigationRequest`), fetch keepalive, prefetch, fenced-frame beacons, FedCM, signed-exchange cert fetches, CORS preflights, all browser-side URLLoader traffic. |

**Both agents own a `Network::Frontend` channel** and emit the same `requestWillBeSent`, `responseReceived`, `dataReceived`, `loadingFinished`, `loadingFailed` events. The renderer side is more granular for body capture; the browser side is more accurate for header/timing info because the network service is in the browser process.

### 2.1.2 The Bridge: NetworkServiceDevToolsObserver

The browser-process bridge between the network service and the browser-side `NetworkHandler` is `NetworkServiceDevToolsObserver` (`browser/devtools/network_service_devtools_observer.cc`). It implements `network::mojom::DevToolsObserver` (a Mojo interface) and delivers raw header/cookie/extra-info events to `NetworkHandler`:

```cpp
// browser/devtools/network_service_devtools_observer.cc:71
void NetworkServiceDevToolsObserver::OnRawRequest(
    const std::string& devtools_request_id,
    const net::CookieAccessResultList& request_cookie_list,
    std::vector<network::mojom::HttpRawHeaderPairPtr> request_headers,
    base::TimeTicks timestamp,
    /*...*/) {
  auto* host = GetDevToolsAgentHost();
  if (!host) return;
  DispatchToAgents(host,
                   &protocol::NetworkHandler::OnRequestWillBeSentExtraInfo,
                   devtools_request_id, request_cookie_list, request_headers,
                   timestamp, device_bound_session_usages, security_state,
                   other_partition_info, applied_network_conditions_id);
}
```

### 2.1.3 How Requests Get Their DevTools IDs

`InspectorNetworkAgent::SetDevToolsIds` (`inspector_network_agent.cc:1518`) stamps each outgoing request with two identifiers before it leaves the renderer:

```cpp
void InspectorNetworkAgent::SetDevToolsIds(
    ResourceRequest& request, const FetchInitiatorInfo& initiator_info) {
  if (initiator_info.name == fetch_initiator_type_names::kInternal) {
    return;  // Don't report internal requests.
  }
  request.SetDevToolsThrottlingToken(devtools_throttling_token_);
  request.SetDevToolsId(
      IdentifiersFactory::SubresourceRequestId(request.InspectorId()));
}
```

- **`DevToolsThrottlingToken`**: a per-target `base::UnguessableToken` so the network service can apply throttling to this target's requests only.
- **`DevToolsId`**: the renderer-side `requestId` string (process-id-prefixed identifier) — this becomes the `requestId` you see in CDP events.

### 2.1.4 How Browser-Side Handlers Are Wired

`DevToolsSession::Append(InspectorAgent*)` registers agents with the browser's `UberDispatcher`. The browser-side `NetworkHandler` is registered for the browser target and each frame target:

```
DevToolsSession
├── Browser-side handlers (in browser process):
│   ├── NetworkHandler (Network domain, browser side)
│   ├── PageHandler (Page domain)
│   ├── EmulationHandler (Emulation domain, browser side)
│   ├── TargetHandler (Target domain)
│   ├── SecurityHandler (Security domain)
│   ├── StorageHandler (Storage domain)
│   └── FetchHandler (Fetch domain)
│
└── Renderer-side agents (forwarded via Mojo):
    ├── InspectorNetworkAgent (Network domain, renderer side)
    ├── InspectorPageAgent (Page domain)
    ├── InspectorEmulationAgent (Emulation domain, renderer side)
    ├── InspectorDOMAgent (DOM domain)
    └── ... etc.
```

When you call `Network.enable`, the dispatcher routes it to **both** `NetworkHandler::Enable` (browser) AND `InspectorNetworkAgent::Enable` (renderer, via the `Response::FallThrough()` mechanism). Both agents start emitting events for the requests they see.

---

## 2.2 The Complete Request Lifecycle (As DevTools Sees It)

### 2.2.1 The Event Sequence

For a renderer-initiated GET (e.g. `<img src>` or `fetch()`), the events fire in this order:

```
T+0ms    [renderer] InspectorNetworkAgent::PrepareRequest
         - merges extra_request_headers_ into outgoing request
         - if cache_disabled_, sets kBypassCache
         - if bypass_service_worker_, sets SkipServiceWorker
         - attaches stack trace for initiator

T+0.5ms  [renderer] Network.requestWillBeSent
         payload: requestId, loaderId, documentURL, Request{url,method,headers,postData?},
                  timestamp (monotonic), wallTime (epoch), Initiator{type,stack,url,lineNumber},
                  redirectHasExtraInfo, redirectResponse?, type, frameId, hasUserGesture

T+1ms    [browser, network service] NetworkServiceDevToolsObserver::OnRawRequest
         → NetworkHandler::OnRequestWillBeSentExtraInfo
         → Network.requestWillBeSentExtraInfo
         payload: requestId, cookies (associated list), headers (raw), 
                  connectTiming, clientSecurityState, isCookieless, appliedNetworkConditionsId?

         NOTE: requestWillBeSent and requestWillBeSentExtraInfo are OUT OF ORDER
         with respect to each other. The protocol explicitly documents this.
         CDP clients must be prepared for either order; they correlate by requestId.

T+10ms   [renderer] InspectorNetworkAgent::DidReceiveResourceResponse
         - builds Response object via BuildObjectForResourceResponse
         - populates: status, statusText, headers, mimeType, remoteIPAddress, remotePort,
           protocol ("h2"/"h3"/"http/1.1"), securityState, securityDetails (TLS),
           timing, fromDiskCache, fromServiceWorker, fromPrefetchCache

         → Network.responseReceived
         payload: requestId, loaderId, timestamp, type, Response{...},
                  responseHasExtraInfo, frameId

T+11ms   [browser] NetworkServiceDevToolsObserver::OnRawResponse
         → NetworkHandler::OnResponseReceivedExtraInfo
         → Network.responseReceivedExtraInfo
         payload: requestId, blockedCookies, headers (raw), resourceIPAddressSpace,
                  statusCode, headersText, cookiePartitionKey, exemptedCookies?

T+12-100ms [renderer] InspectorNetworkAgent::DidReceiveData (× N chunks)
         - accumulates bytes in NetworkResourcesData::ResourceData::data_buffer_
         - emits Network.dataReceived per chunk
         payload: requestId, timestamp, dataLength, encodedDataLength, data? (only if streaming)

T+100ms  [renderer] InspectorNetworkAgent::DidFinishLoading
         - decodes accumulated buffer to content via MaybeDecodeDataToContent
         → Network.loadingFinished
         payload: requestId, timestamp, encodedDataLength, shouldReportCorbBlocking?

OR on error:
T+100ms  [renderer] InspectorNetworkAgent::DidFailLoading
         → Network.loadingFailed
         payload: requestId, timestamp, type, errorText, canceled, blockedReason?, corsErrorStatus?
```

### 2.2.2 The Full Happy-Path Sequence Diagram

```
Page JS:  fetch("https://api.example.com/data")
              │
              ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│ RENDERER PROCESS (Blink)                                                   │
│                                                                            │
│  ResourceFetcher::RequestResource()                                       │
│       │                                                                    │
│       ▼                                                                    │
│  InspectorNetworkAgent::PrepareRequest()          [inspector_network_agent.cc:1537]
│       - Apply extra_request_headers_                                       │
│       - Apply cache_disabled_, bypass_service_worker_                      │
│       - Capture stack trace                                                │
│       │                                                                    │
│       ▼                                                                    │
│  InspectorNetworkAgent::WillSendRequestInternal() [inspector_network_agent.cc:1370]
│       - Build Request object                                               │
│       - Build Initiator object (stack trace or parser position)            │
│       - Build redirectResponse if this is a redirect                      │
│       │                                                                    │
│       ▼                                                                    │
│  GetFrontend()->requestWillBeSent(...)  [inspector_network_agent.cc:1461] │
│       │                                                                    │
│       ▼                                                                    │
│  NetworkResourcesData::ResourceCreated()   [network_resources_data.cc]    │
│       - Create ResourceData entry keyed by requestId                       │
│       - Store post_data_ if request has HttpBody                           │
└───────┼────────────────────────────────────────────────────────────────────┘
        │ (CDP event sent over Mojo to browser process)
        ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│ BROWSER PROCESS                                                            │
│                                                                            │
│  DevToolsSession::DispatchProtocolMessageToClient()                       │
│       │                                                                    │
│       ▼                                                                    │
│  WebSocket/pipe transport → your Qt6 CDP client                           │
└────────────────────────────────────────────────────────────────────────────┘

        Meanwhile, in parallel:
┌─────────────────────────────────────────────────────────────────────────────┐
│ NETWORK SERVICE PROCESS                                                    │
│                                                                            │
│  URLLoader::OnReceiveResponse()                                           │
│       │                                                                    │
│       ▼                                                                    │
│  NetworkServiceDevToolsObserver::OnRawRequest()                           │
│       │ (Mojo)                                                            │
│       ▼                                                                    │
│  NetworkHandler::OnRequestWillBeSentExtraInfo()  [network_handler.cc:3987]│
│       │                                                                    │
│       ▼                                                                    │
│  Network.requestWillBeSentExtraInfo  (CDP event)                          │
└────────────────────────────────────────────────────────────────────────────┘
```

### 2.2.3 The Network Event Reference Table

| Event | Fired From | When | Key Fields |
|---|---|---|---|
| `Network.requestWillBeSent` | renderer OR browser | Request initiated | requestId, request{url,method,headers,postData?}, timestamp, wallTime, initiator, redirectResponse?, type, frameId, hasUserGesture |
| `Network.requestWillBeSentExtraInfo` | network service (browser) | Raw headers + cookies available | requestId, cookies, headers, connectTiming, clientSecurityState, isCookieless?, appliedNetworkConditionsId? |
| `Network.responseReceived` | renderer OR browser | Response headers received | requestId, response{status,headers,mimeType,remoteIPAddress,remotePort,protocol,securityState,securityDetails,timing,fromDiskCache,fromServiceWorker}, responseHasExtraInfo, frameId |
| `Network.responseReceivedExtraInfo` | network service (browser) | Raw response headers + Set-Cookie blocked list | requestId, blockedCookies, headers, resourceIPAddressSpace, statusCode, headersText, cookiePartitionKey? |
| `Network.responseReceivedEarlyHints` | network service | HTTP 103 Early Hints | requestId, headers, code |
| `Network.dataReceived` | renderer | Each chunk of response body | requestId, timestamp, dataLength, encodedDataLength, data? (only if streaming enabled) |
| `Network.requestServedFromCache` | renderer | Response from HTTP cache | requestId |
| `Network.loadingFinished` | renderer OR browser | Request completed successfully | requestId, timestamp, encodedDataLength, shouldReportCorbBlocking? |
| `Network.loadingFailed` | renderer OR browser | Request failed | requestId, timestamp, type, errorText, canceled, blockedReason?, corsErrorStatus? |
| `Network.resourceChangedPriority` | renderer | Priority changed mid-flight | requestId, newPriority, timestamp |
| `Network.signedExchangeReceived` | browser | Signed exchange received | requestId, info |

### 2.2.4 WebSocket Event Reference Table

| Event | When | Key Fields |
|---|---|---|
| `Network.webSocketCreated` | `new WebSocket(url)` called | requestId, url, initiator? |
| `Network.webSocketWillSendHandshakeRequest` | Before sending HTTP Upgrade | requestId, timestamp, wallTime, request{headers} |
| `Network.webSocketHandshakeResponseReceived` | HTTP 101 received | requestId, timestamp, response{status,headers,requestHeaders} |
| `Network.webSocketFrameSent` | `ws.send()` called | requestId, timestamp, response{opcode,mask,payloadData} |
| `Network.webSocketFrameReceived` | Frame decoded | requestId, timestamp, response{opcode,mask,payloadData} |
| `Network.webSocketFrameError` | Frame parse error | requestId, timestamp, errorMessage |
| `Network.webSocketClosed` | Channel torn down | requestId, timestamp |

**Note on payloadData encoding**:
- Opcode 1 (text): UTF-8 string
- Opcode 2 (binary): base64
- Opcode 8 (close), 9 (ping), 10 (pong): base64

### 2.2.5 Server-Sent Events (SSE) Events

| Event | When | Key Fields |
|---|---|---|
| `Network.eventSourceMessageReceived` | Each SSE event parsed | requestId, timestamp, eventName, eventId, data |

The matching `Network.requestWillBeSent` for the SSE GET will have `type: "EventSource"`.

---

## 2.3 The Data Structures

### 2.3.1 NetworkResourcesData (Renderer-Side Per-Request State)

**File**: `third_party/blink/renderer/core/inspector/network_resources_data.h:84`

```cpp
class CORE_EXPORT ResourceData final
    : public GarbageCollected<ResourceData>,
      public FontResourceClearDataObserver {
 public:
  String RequestId() const;
  String LoaderId() const;
  String FrameId() const;
  KURL RequestedURL() const;
  size_t ContentSize() const;
  bool HasContent() const { return !content_.IsNull(); }
  String Content() const { return content_; }
  void SetContent(const String&, bool base64_encoded);
  bool Base64Encoded() const;
  bool IsContentEvicted() const;
  InspectorPageAgent::ResourceType GetType() const;
  int HttpStatusCode() const;
  String MimeType() const;
  String TextEncodingName() const;
  const Resource* CachedResource() const;
  BlobDataHandle* DownloadedFileBlob() const;        // For streamed/large bodies
  net::X509Certificate* Certificate();                // TLS cert chain
  EncodedFormData* PostData() const;                  // POST body for getRequestPostData
  const std::optional<SegmentedBuffer>& Data() const { return data_buffer_; }

 private:
  Member<NetworkResourcesData> network_resources_data_;
  String request_id_, loader_id_, frame_id_;
  KURL requested_url_;
  String content_;                                    // Decoded text (or null)
  bool base64_encoded_;
  std::optional<SegmentedBuffer> data_buffer_;       // Raw appended bytes
  bool is_content_evicted_;
  Member<XHRReplayData> xhr_replay_data_;
  UntracedMember<const Resource> cached_resource_;   // Weak ref to Blink's Resource
  scoped_refptr<BlobDataHandle> downloaded_file_blob_;
  scoped_refptr<net::X509Certificate> certificate_;
  scoped_refptr<EncodedFormData> post_data_;
};
```

**The outer container** (`network_resources_data.h:254`):

```cpp
class CORE_EXPORT NetworkResourcesData
    : public GarbageCollected<NetworkResourcesData> {
 private:
  Deque<String> request_ids_deque_;                            // LRU order
  typedef HeapHashMap<String, Member<ResourceData>> ResourceDataMap;
  ResourceDataMap request_id_to_resource_data_map_;
  size_t content_size_;
  size_t maximum_resources_content_size_;            // default 200 MB desktop, 10 MB Android
  size_t maximum_single_resource_content_size_;      // default 20 MB desktop, 5 MB Android
};
```

**Buffer limits** (`inspector_network_agent.cc:131`):

```cpp
#if BUILDFLAG(IS_ANDROID)
constexpr int kDefaultTotalBufferSize = 10 * 1000 * 1000;    // 10 MB
constexpr int kDefaultResourceBufferSize = 5 * 1000 * 1000;  // 5 MB
#else
constexpr int kDefaultTotalBufferSize = 200 * 1000 * 1000;    // 200 MB
constexpr int kDefaultResourceBufferSize = 20 * 1000 * 1000;  // 20 MB
#endif
```

### 2.3.2 The CDP Request Object

```json
{
  "url": "https://api.example.com/data",
  "method": "GET",
  "headers": {"Accept": "application/json", "User-Agent": "..."},
  "postData": "key=value&foo=bar",          // present only if small + maxPostDataSize set
  "postDataEntries": [                       // preferred binary form
    {"bytes": "a2V5PXZhbHVlJmZvbz1iYXI="}   // base64
  ],
  "hasPostData": true,                       // indicates postData was truncated
  "mixedContentType": "none",                // "blockable" | "optionally-blockable" | "none"
  "initialPriority": "High",                 // "VeryLow"|"Low"|"Medium"|"High"|"VeryHigh"
  "referrerPolicy": "strict-origin-when-cross-origin",
  "isLinkPreload": false,
  "isSameSite": true,
  "trustTokenParams": { /* optional */ },
  "signedExchangeInfo": { /* optional */ }
}
```

### 2.3.3 The CDP Response Object

```json
{
  "url": "https://api.example.com/data",
  "status": 200,
  "statusText": "OK",
  "headers": {"Content-Type": "application/json", "Content-Length": "1234"},
  "headersText": "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n...",
  "mimeType": "application/json",
  "charset": "utf-8",
  "requestHeaders": {"Accept": "application/json", ...},
  "requestHeadersText": "GET /data HTTP/1.1\r\n...",
  "connectionReused": false,
  "connectionId": 42,
  "remoteIPAddress": "93.184.216.34",
  "remotePort": 443,
  "fromDiskCache": false,
  "fromServiceWorker": false,
  "fromPrefetchCache": false,
  "encodedDataLength": 1500,
  "timing": {
    "requestTime": 1234567890.123,
    "proxyStart": -1,
    "proxyEnd": -1,
    "dnsStart": 0.5,
    "dnsEnd": 5.2,
    "connectStart": 5.2,
    "connectEnd": 25.1,
    "sslStart": 10.0,
    "sslEnd": 25.0,
    "workerStart": -1,
    "workerReady": -1,
    "workerFetchStart": -1,
    "workerRespondWithSettled": -1,
    "sendStart": 25.2,
    "sendEnd": 25.3,
    "receiveHeadersStart": 50.0,
    "receiveHeadersEnd": 55.0,
    "pushStart": 0.0,
    "pushEnd": 0.0,
    "workerRouterStart": -1,
    "workerRouterEnd": -1
  },
  "serviceWorkerResponseSource": "network",  // or "http-cache" | "cache-storage" | "fallback-code"
  "responseTime": 1234567890.123,
  "cacheStorageCacheName": "",
  "protocol": "h2",
  "alternateProtocolUsage": "unspecifiedReason",
  "securityState": "secure",
  "securityDetails": {
    "protocol": "TLSv1.3",
    "keyExchange": "",
    "keyExchangeGroup": "X25519",
    "cipher": "AES_128_GCM",
    "mac": "",
    "subjectName": "example.com",
    "sanList": ["example.com", "www.example.com"],
    "issuer": "Let's Encrypt R3",
    "validFrom": 1690000000.0,
    "validTo": 1720000000.0,
    "signedCertificateTimestampList": [...],
    "certificateTransparencyCompliance": "compliant",
    "encryptedClientHello": false
  }
}
```

### 2.3.4 The CDP Initiator Object

```json
{
  "type": "script",                   // "parser" | "script" | "preload" | "SignedExchange" | "Other"
  "stack": {                          // only for "script" type
    "callFrames": [
      {"functionName": "fetch", "scriptId": "42", "url": "https://example.com/app.js",
       "lineNumber": 10, "columnNumber": 5}
    ],
    "parent": { /* async parent stack */ },
    "parentId": {"id": "1"}          // for async chains
  },
  "url": "https://example.com/",     // only for "parser"
  "lineNumber": 0,                   // only for "parser"
  "columnNumber": 0
}
```

The `Initiator` is built by `BuildInitiatorObject` (`inspector_network_agent.cc:1909`). It tries in order:
1. Imported module referrer
2. Stylesheet referrer
3. Current JS async stack trace
4. Parser position
5. "Other"

### 2.3.5 The CDP ResourceTiming Object

```json
{
  "requestTime": 1234567890.123,   // base::TimeTicks since origin (monotonic)
  "proxyStart": -1, "proxyEnd": -1,    // -1 = no proxy
  "dnsStart": 0.5, "dnsEnd": 5.2,
  "connectStart": 5.2, "connectEnd": 25.1,
  "sslStart": 10.0, "sslEnd": 25.0,
  "workerStart": -1,                   // -1 = no SW
  "workerReady": -1,
  "workerFetchStart": -1,
  "workerRespondWithSettled": -1,
  "sendStart": 25.2, "sendEnd": 25.3,
  "receiveHeadersStart": 50.0,
  "receiveHeadersEnd": 55.0,
  "pushStart": 0.0, "pushEnd": 0.0,
  "workerRouterStart": -1, "workerRouterEnd": -1
}
```

All values are **milliseconds relative to `requestTime`**. A value of `-1` means that phase didn't occur (e.g. `dnsStart=-1` for cached connection).

---

## 2.4 File Locations Reference

| Component | File Path |
|---|---|
| InspectorNetworkAgent | `third_party/blink/renderer/core/inspector/inspector_network_agent.cc` + `.h` |
| NetworkResourcesData | `third_party/blink/renderer/core/inspector/network_resources_data.cc` + `.h` |
| NetworkHandler (browser) | `browser/devtools/protocol/network_handler.cc` + `.h` |
| FetchHandler | `browser/devtools/protocol/fetch_handler.cc` + `.h` |
| DevToolsURLLoaderInterceptor | `browser/devtools/devtools_url_loader_interceptor.cc` + `.h` |
| NetworkServiceDevToolsObserver | `browser/devtools/network_service_devtools_observer.cc` + `.h` |
| DevToolsIOContext | `browser/devtools/devtools_io_context.cc` + `.h` |
| DevToolsStreamPipe | `browser/devtools/devtools_stream_pipe.cc` + `.h` |
| DevToolsInstrumentation | `browser/devtools/devtools_instrumentation.cc` + `.h` |
| RequestBodyCollector | `browser/devtools/request_body_collector.cc` + `.h` |
| Protocol definitions | `third_party/blink/public/devtools_protocol/domains/Network.pdl` |
| Fetch protocol | `third_party/blink/public/devtools_protocol/domains/Fetch.pdl` |
| IO protocol | `third_party/blink/public/devtools_protocol/domains/IO.pdl` |
| URLLoader (low-level) | `services/network/url_loader.cc` (not in your slice) |
| WebSocketChannel | `net/websockets/websocket_channel.cc` (not in your slice) |
| HTTP request headers | `net/http/http_request_headers.cc` (not in your slice) |
| HTTP response headers | `net/http/http_response_headers.cc` (not in your slice) |

---

## 2.5 Body Capture Mechanisms

### 2.5.1 Response Body Capture — Three Mechanisms

Chromium uses **three different mechanisms** to capture response bodies, depending on the size and type:

#### Mechanism 1: In-Memory Buffer (default, <20MB)

Response bytes are accumulated in a `SegmentedBuffer` via `ResourceData::AppendData`:

```cpp
// network_resources_data.cc:186
void NetworkResourcesData::ResourceData::AppendData(base::span<const char> data) {
  DCHECK(!HasContent());
  if (!data_buffer_)
    data_buffer_ = SegmentedBuffer();
  data_buffer_->Append(data);
}
```

When `DidFinishLoading` runs, the buffer is decoded to a `String content_` via `MaybeDecodeDataToContent`:

```cpp
size_t NetworkResourcesData::ResourceData::DecodeDataToContent() {
  DCHECK(!HasContent());
  DCHECK(HasData());
  size_t data_length = data_buffer_->size();
  bool success = InspectorPageAgent::SegmentedBufferContent(
      data_buffer_ ? &*data_buffer_ : nullptr, mime_type_, text_encoding_name_,
      &content_, &base64_encoded_);
  DCHECK(success);
  data_buffer_ = std::nullopt;       // free the raw buffer
  return content_.CharactersSizeInBytes() - data_length;
}
```

`InspectorPageAgent::SegmentedBufferContent` (`inspector_page_agent.cc:331`) builds a `TextResourceDecoder` from mime+charset, decodes the bytes; if the decoder saw invalid byte sequences (lossy UTF-8), it falls back to base64.

#### Mechanism 2: File-Backed Blob (large streamed downloads)

When a body is delivered as a blob handle (large streamed downloads), `ResourceData::SetDownloadedFileBlob` stores it. The `getResponseBody` then uses `InspectorFileReaderLoaderClient` to read it asynchronously via `FileReaderLoader`:

```cpp
// inspector_network_agent.cc:2471
void InspectorNetworkAgent::GetResponseBodyBlob(...) {
  InspectorFileReaderLoaderClient* client =
      MakeGarbageCollected<InspectorFileReaderLoaderClient>(
          blob, context->GetTaskRunner(TaskType::kFileReading),
          BindOnce(ResponseBodyFileReaderLoaderDone, resource_data->MimeType(),
                   resource_data->TextEncodingName(), std::move(callback)));
  client->Start();
}
```

When the blob finishes loading, `ResponseBodyFileReaderLoaderDone` (`:294`) converts the buffer using `InspectorPageAgent::SegmentedBufferContent` — same UTF-8-vs-base64 logic.

#### Mechanism 3: Durable Messages (network-service-backed)

`Network.enable(enableDurableMessages=true)` or `Network.configureDurableMessages` moves body storage **to the network service** via `network::mojom::DurableMessageCollector`. The renderer doesn't store, and `NetworkHandler::GetResponseBody` (`network_handler.cc:3728`) queries the collector first:

```cpp
void NetworkHandler::GetResponseBody(const String& request_id, ...) {
  network::mojom::DurableMessageCollector* collector =
      root_session_->MaybeGetDurableMessageCollector();
  if (collector) {
    collector->Retrieve(request_id,
        base::BindOnce(&NetworkHandler::ProcessDurableMessageOrGetLocalData, ...));
    return;
  }
  ProcessDurableMessageOrGetLocalData(request_id, std::move(callback),
                                       std::nullopt);
}
```

**Durable messages survive cross-process navigations** and offload storage to the network service — important for scraping because they prevent memory blowup when capturing many large responses.

### 2.5.2 Network.getResponseBody Implementation

```cpp
// inspector_network_agent.cc:2490 (renderer side, entry point)
void InspectorNetworkAgent::getResponseBody(
    const String& request_id,
    std::unique_ptr<GetResponseBodyCallback> callback) {
  if (CanGetResponseBodyBlob(request_id)) {
    GetResponseBodyBlob(request_id, std::move(callback));      // blob path
    return;
  }
  String content;
  bool base64_encoded;
  protocol::Response response =
      GetResponseBody(request_id, &content, &base64_encoded);  // sync path
  if (response.IsSuccess()) {
    callback->sendSuccess(content, base64_encoded);
  } else {
    callback->sendFailure(response);
  }
}
```

The synchronous fall-through path (`:2727`):

```cpp
protocol::Response InspectorNetworkAgent::GetResponseBody(
    const String& request_id, String* content, bool* base64_encoded) {
  NetworkResourcesData::ResourceData const* resource_data =
      resources_data_->Data(request_id);
  if (!resource_data)
    return protocol::Response::ServerError("No resource with given identifier found");
  if (resource_data->HasContent()) {
    *content = resource_data->Content();
    *base64_encoded = resource_data->Base64Encoded();
    return protocol::Response::Success();
  }
  if (resource_data->IsContentEvicted())
    return protocol::Response::ServerError(
        "Request content was evicted from inspector cache");
  if (resource_data->CachedResource() &&
      InspectorPageAgent::CachedResourceContent(
          resource_data->CachedResource(), content, base64_encoded, nullptr)) {
    return protocol::Response::Success();
  }
  return protocol::Response::ServerError(
      "No data found for resource with given identifier");
}
```

### 2.5.3 Streaming Response Bodies

`Network.getResponseBody` does **NOT** stream — it returns the whole body in one response. For streaming you must use:

#### Option A: `Network.streamResourceContent`

```cpp
// inspector_network_agent.cc:1697
void InspectorNetworkAgent::streamResourceContent(
    const String& request_id,
    std::unique_ptr<StreamResourceContentCallback> callback) {
  // ... validation ...
  NetworkResourcesData::ResourceData const* resource_data =
      resources_data_->Data(request_id);
  // ... returns the currently buffered bytes ...
  // AND marks the requestId as a streaming request in streaming_request_ids_
  streaming_request_ids_.insert(request_id);
  // From then on, every Network.dataReceived event for that requestId
  // carries the live binary data payload:
  if (streaming_request_ids_.Contains(request_id)) {
    binary_data = protocol::Binary::fromSpan(base::as_bytes(*data_span));
  }
  callback->sendSuccess(std::move(binary_data));
}
```

From that point, every `Network.dataReceived` event for that requestId carries the live `data` field with raw bytes.

#### Option B: `Fetch.takeResponseBodyAsStream` (for intercepted responses)

```cpp
// fetch_handler.cc:540
void FetchHandler::TakeResponseBodyAsStream(
    const String& requestId,
    std::unique_ptr<TakeResponseBodyAsStreamCallback> callback) {
  // Returns an IO.StreamHandle
  // Client then issues IO.read calls (default 10MB chunks)
}
```

The `IO.read` implementation (`io_handler.cc:56`) reads from the stream in chunks:

```cpp
constexpr int kDefaultChunkSize = 10 * 1000 * 1000;  // 10 MB
```

### 2.5.4 POST Body Capture

POST body is captured **separately** from response body. It's stored as `scoped_refptr<EncodedFormData> post_data_` on `ResourceData`:

```cpp
// inspector_network_agent.cc:1384 (inside WillSendRequestInternal)
scoped_refptr<EncodedFormData> post_data;
if (data &&
    (redirect_response.HttpStatusCode() == net::HTTP_TEMPORARY_REDIRECT ||
     redirect_response.HttpStatusCode() == net::HTTP_PERMANENT_REDIRECT)) {
  post_data = data->PostData();             // Preserve POST body across 307/308
} else if (request.HttpBody()) {
  post_data = request.HttpBody()->DeepCopy();
}
resources_data_->ResourceCreated(request_id, loader_id, request.Url(),
                                 post_data);
```

The `max_post_data_size` parameter on `Network.enable` controls how much is **inlined into `requestWillBeSent`**:

```cpp
static bool FormDataToString(
    scoped_refptr<EncodedFormData> body,
    size_t max_body_size,
    protocol::Array<protocol::Network::PostDataEntry>* data_entries,
    String* content) {
  *content = "";
  if (!body || body->IsEmpty()) return false;
  for (const auto& element : body->Elements())
    if (element.type_ != FormDataElement::kData)
      return true;                       // Has non-bytes element → don't inline
  if (max_body_size != 0 && body->SizeInBytes() > max_body_size)
    return true;                        // Too big → don't inline
  // ... concatenate and return as string + entries ...
}
```

If `max_post_data_size == 0` (default if not passed to `Network.enable`), NO postData is inlined — you must call `Network.getRequestPostData` separately.

### 2.5.5 Network.getRequestPostData Implementation

```cpp
// inspector_network_agent.cc:2883
void InspectorNetworkAgent::getRequestPostData(
    const String& request_id,
    std::unique_ptr<GetRequestPostDataCallback> callback) {
  NetworkResourcesData::ResourceData const* resource_data =
      resources_data_->Data(request_id);
  if (!resource_data) {
    callback->sendFailure(
        protocol::Response::ServerError("No resource with given id was found"));
    return;
  }
  scoped_refptr<EncodedFormData> post_data = resource_data->PostData();
  if (!post_data || post_data->IsEmpty()) {
    callback->sendFailure(protocol::Response::ServerError(
        "No post data available for the request"));
    return;
  }
  // InspectorPostBodyParser handles kData (raw), kEncodedBlob (async read),
  // kEncodedFile/kDataPipe (not supported)
  scoped_refptr<InspectorPostBodyParser> parser =
      base::MakeRefCounted<InspectorPostBodyParser>(
          std::move(callback), context->GetTaskRunner(TaskType::kFileReading));
  parser->Parse(post_data.get());
}
```

The parser (`:316-405`) handles `EncodedFormData` elements: `kData` (raw bytes), `kEncodedBlob` (async read via `InspectorFileReaderLoaderClient`), `kEncodedFile`/`kDataPipe` (not supported). It concatenates all parts, tries UTF-8 decode, falls back to base64.

---

## 2.6 The Fetch Domain — Request Interception

### 2.6.1 Fetch.enable with Patterns

```cpp
// fetch_handler.cc:194
void FetchHandler::Enable(
    std::unique_ptr<Array<Fetch::RequestPattern>> patterns,
    std::optional<bool> handle_auth,
    std::unique_ptr<EnableCallback> callback) {
  if (!interceptor_) {
    interceptor_ = std::make_unique<DevToolsURLLoaderInterceptor>(
        base::BindRepeating(&FetchHandler::RequestIntercepted,
                            weak_factory_.GetWeakPtr()),
        base::BindRepeating(/* CanAccessCookie lambda */));
  }
  std::vector<DevToolsURLLoaderInterceptor::Pattern> interception_patterns;
  Response response = ToInterceptionPatterns(patterns, &interception_patterns);
  if (!response.IsSuccess()) { callback->sendFailure(response); return; }
  if (!interception_patterns.size() && handle_auth.value_or(false)) {
    callback->sendFailure(Response::InvalidParams(
        "Can\'t specify empty patterns with handleAuth set"));
    return;
  }
  interceptor_->SetPatterns(std::move(interception_patterns),
                            handle_auth.value_or(false));
  update_loader_factories_callback_.Run(
      base::BindOnce(&EnableCallback::sendSuccess, std::move(callback)));
}
```

Pattern translation (`fetch_handler.cc:147`):

```cpp
Response ToInterceptionPatterns(
    std::unique_ptr<Array<Fetch::RequestPattern>>& maybe_patterns,
    std::vector<DevToolsURLLoaderInterceptor::Pattern>* result) {
  result->clear();
  if (!maybe_patterns) {
    // No patterns = intercept EVERYTHING at Request stage
    result->emplace_back("*", base::flat_set<blink::mojom::ResourceType>(),
                         DevToolsURLLoaderInterceptor::kRequest);
    return Response::Success();
  }
  for (const auto& pattern : *maybe_patterns) {
    base::flat_set<blink::mojom::ResourceType> resource_types;
    std::string resource_type = pattern->GetResourceType("");
    if (!resource_type.empty()) {
      if (!AddInterceptedResourceType(resource_type, &resource_types))
        return Response::InvalidParams("Unknown resource type in fetch filter");
    }
    auto stage = RequestStageToInterceptorStage(
        pattern->GetRequestStage(Fetch::RequestStageEnum::Request));
    if (!stage.has_value())
      return Response::InvalidParams("Unsupported request stage");
    result->emplace_back(pattern->GetUrlPattern("*"),
                         std::move(resource_types), stage.value());
  }
  return Response::Success();
}
```

### 2.6.2 Pattern Matching

`Pattern::Matches` (`devtools_url_loader_interceptor.cc:154`) uses `base::MatchPattern` (glob-style `*` and `?`):

```cpp
bool DevToolsURLLoaderInterceptor::Pattern::Matches(
    const std::string& url, blink::mojom::ResourceType resource_type) const {
  if (!resource_types.empty() && !resource_types.contains(resource_type))
    return false;
  return base::MatchPattern(url, url_pattern);
}
```

### 2.6.3 How Interception Is Wired into URLLoader

`Fetch.enable` calls `update_loader_factories_callback_.Run(...)` which triggers `WillCreateURLLoaderFactoryParams::Run` (`devtools_instrumentation.cc:1815`) the next time *any* URLLoaderFactory is created for this target — for the frame, its workers, its service worker, navigations, downloads:

```cpp
bool WillCreateURLLoaderFactoryParams::Run(
    bool is_navigation, bool is_download,
    network::URLLoaderFactoryBuilder& factory_builder,
    network::mojom::URLLoaderFactoryOverridePtr* factory_override,
    mojo::PendingRemote<network::mojom::TrustedURLLoaderHeaderClient>*
        header_client) {
  bool had_interceptors =
      MaybeCreateProxyForInterception<protocol::FetchHandler>(
          agent_host_, process_id_, storage_partition_, devtools_token_,
          is_navigation, is_download, handler_override, header_client);
  for (auto* browser_agent_host : BrowserDevToolsAgentHost::Instances()) {
    had_interceptors |= MaybeCreateProxyForInterception<protocol::FetchHandler>(
        browser_agent_host, process_id_, storage_partition_, devtools_token_,
        is_navigation, is_download, handler_override, header_client);
  }
  // ...fuse pipes into the factory builder chain...
}
```

`FetchHandler::MaybeCreateProxyForInterception` creates a `DevToolsURLLoaderFactoryProxy` that wraps the network service's actual factory. Every `CreateLoaderAndStart` call goes through the proxy:

```cpp
// devtools_url_loader_interceptor.cc:799
void DevToolsURLLoaderFactoryProxy::CreateLoaderAndStart(
    mojo::PendingReceiver<network::mojom::URLLoader> loader,
    int32_t request_id, uint32_t options,
    const network::ResourceRequest& request,
    mojo::PendingRemote<network::mojom::URLLoaderClient> client,
    const net::MutableNetworkTrafficAnnotationTag& traffic_annotation) {
  auto creation_params = std::make_unique<CreateLoaderParameters>(
      request_id, options, request, traffic_annotation);
  interceptor->CreateJob(frame_token_, process_id_, is_download_,
                         request.devtools_request_id,
                         std::move(creation_params), std::move(loader),
                         std::move(client), std::move(factory_clone),
                         std::move(cookie_manager_clone));
}
```

### 2.6.4 Fetch.requestPaused Event

`InterceptionJob::StartJobAndMaybeNotify` (`:1077`) calls `NotifyClient` (`:1763`), which fetches cookies + request bodies asynchronously (via `FetchCookies` + `RequestBodyCollector::Collect`), then calls `CompleteNotifyingClient` (`:1819`):

```cpp
void InterceptionJob::CompleteNotifyingClient(
    std::unique_ptr<InterceptedRequestInfo> request_info) {
  request_info->network_request =
      protocol::NetworkHandler::CreateRequestFromResourceRequest(
          create_loader_params_->request,
          request_cookies_.value_or(std::string()), request_bodies_);
  waiting_for_resolution_ = ResolutionState::kWaitingForClient;
  interceptor_->request_intercepted_callback_.Run(std::move(request_info));
}
```

The callback fires `FetchHandler::RequestIntercepted` (`fetch_handler.cc:586`):

```cpp
void FetchHandler::RequestIntercepted(
    std::unique_ptr<InterceptedRequestInfo> info) {
  if (info->auth_challenge) {
    frontend_->AuthRequired(
        info->interception_id, std::move(info->network_request),
        info->frame_id.ToString(),
        NetworkHandler::ResourceTypeToString(info->resource_type),
        std::move(auth_challenge));
    return;
  }
  frontend_->RequestPaused(
      info->interception_id, std::move(info->network_request),
      info->frame_id.ToString(),
      NetworkHandler::ResourceTypeToString(info->resource_type),
      std::move(error_reason), std::move(status_code), std::move(status_text),
      std::move(response_headers), std::move(info->renderer_request_id),
      std::move(info->redirected_request_id));
}
```

### 2.6.5 Fetch.requestPaused Event Fields

```
event requestPaused
  RequestId requestId              # Fetch interception ID (NOT same as Network.requestId!)
  Request request                  # Network.Request
  FrameId frameId
  ResourceType resourceType
  optional string responseErrorReason      # only at Response stage
  optional integer responseStatusCode     # only at Response stage
  optional string responseStatusText      # only at Response stage
  optional Headers responseHeaders         # only at Response stage
  experimental optional RequestId networkId    # Network.requestId when both domains enabled
  experimental optional RequestId redirectedRequestId
```

### 2.6.6 Fetch.continueRequest

```cpp
// fetch_handler.cc:427
void FetchHandler::ContinueRequest(
    const String& requestId,
    std::optional<String> url,
    std::optional<String> method,
    std::optional<protocol::Binary> postData,
    std::unique_ptr<Array<Fetch::HeaderEntry>> headers,
    std::optional<bool> interceptResponse,
    std::unique_ptr<ContinueRequestCallback> callback) {
  // ... validation ...
  did_modifications_ = url.has_value() || method.has_value() ||
                      postData.has_value() || request_headers;
  auto modifications =
      std::make_unique<DevToolsURLLoaderInterceptor::Modifications>(
          std::move(url), std::move(method), std::move(postData),
          std::move(request_headers), std::move(interceptResponse));
  interceptor_->ContinueInterceptedRequest(requestId, std::move(modifications),
                                           std::move(callback));
}
```

The actual modification is applied in `InterceptionJob::ApplyModificationsToRequest` (`:1376`):

```cpp
void InterceptionJob::ApplyModificationsToRequest(
    std::unique_ptr<Modifications> modifications) {
  network::ResourceRequest* request = &create_loader_params_->request;
  if (modifications->modified_url.has_value()) {
    const GURL new_url(modifications->modified_url.value());
    request->url = new_url;
    url_chain_.back() = new_url;
  }
  if (modifications->modified_method.has_value())
    request->method = modifications->modified_method.value();
  if (modifications->modified_post_data.has_value()) {
    const auto& post_data = modifications->modified_post_data.value();
    request->request_body =
        network::ResourceRequestBody::CreateFromCopyOfBytes(post_data);
  }
  if (modifications->modified_headers) {
    headers_override_ = HeadersOverride::SaveAndOverride(
        *request, std::move(*modifications->modified_headers));
  }
}
```

**Important**: URL changes via `Fetch.continueRequest` are *stealth* — the page's `fetch()` promise resolves with the new URL's body but `request.url` from the page's perspective is unchanged. The comment in the source: "Note this redirect is not visible to the page by design."

### 2.6.7 Fetch.fulfillRequest

```cpp
// fetch_handler.cc:371
void FetchHandler::FulfillRequest(
    const String& requestId,
    int responseCode,
    std::unique_ptr<Array<Fetch::HeaderEntry>> responseHeaders,
    std::optional<protocol::Binary> body,
    std::optional<String> responsePhrase,
    std::unique_ptr<FulfillRequestCallback> callback) {
  std::string headers =
      base::StringPrintf("HTTP/1.1 %d %s", responseCode, status_phrase.c_str());
  headers.append(1, '\0');
  if (responseHeaders) {
    for (const auto& entry : *responseHeaders) {
      headers.append(entry->GetName());
      headers.append(":");
      headers.append(entry->GetValue());
      headers.append(1, '\0');
    }
  }
  headers.append(1, '\0');
  auto modifications =
      std::make_unique<DevToolsURLLoaderInterceptor::Modifications>(
          base::MakeRefCounted<net::HttpResponseHeaders>(headers),
          body ? body->bytes() : nullptr);
  interceptor_->ContinueInterceptedRequest(requestId, std::move(modifications),
                                           WrapCallback(std::move(callback)));
}
```

`InterceptionJob::ProcessResponseOverride` (`:1451`) builds a `network::mojom::URLResponseHead` with synthesized load timing, optionally sniffs MIME type, creates a data pipe, writes the body, then delivers `OnReceiveResponse` to the client.

### 2.6.8 Fetch.failRequest

```cpp
// fetch_handler.cc:344
void FetchHandler::FailRequest(
    const String& requestId,
    const String& errorReason,
    std::unique_ptr<FailRequestCallback> callback) {
  net::Error reason = NetErrorFromErrorReason(errorReason);
  auto modifications =
      std::make_unique<DevToolsURLLoaderInterceptor::Modifications>(reason);
  interceptor_->ContinueInterceptedRequest(requestId, std::move(modifications),
                                           WrapCallback(std::move(callback)));
}
```

`InnerContinueRequest` (`:1244`) then constructs a `URLLoaderCompletionStatus` with the error code, marks it as inspector-blocked if `ERR_BLOCKED_BY_CLIENT`:

```cpp
if (modifications->error_reason) {
  network::URLLoaderCompletionStatus status(modifications->error_reason.value());
  status.completion_time = base::TimeTicks::Now();
  if (modifications->error_reason == net::ERR_BLOCKED_BY_CLIENT) {
    status.extended_error_code =
        static_cast<int>(blink::ResourceRequestBlockedReason::kInspector);
  }
  CompleteRequest(status);
  return Response::Success();
}
```

---

## 2.7 WebSocket Tracking

### 2.7.1 The Event Flow

WebSocket events fire from the **renderer** (`InspectorNetworkAgent`) because that's where `WebSocketChannel` runs. The Inspector overrides probe methods called by `WebSocketChannel`.

```
Page:  new WebSocket("wss://echo.example.com")
   │
   ▼
[renderer] InspectorNetworkAgent::WillCreateWebSocket  [inspector_network_agent.cc:2031]
   │ - captures stack trace
   │ - fires Network.webSocketCreated(requestId, url, initiator?)
   ▼
[renderer] WebSocketChannel sends HTTP Upgrade request
   │
   ▼
[renderer] InspectorNetworkAgent::WillSendWebSocketHandshakeRequest  [:2058]
   │ - fires Network.webSocketWillSendHandshakeRequest(requestId, timestamp, wallTime, request{headers})
   ▼
[renderer] HTTP 101 Switching Protocols response arrives
   │
   ▼
[renderer] InspectorNetworkAgent::DidReceiveWebSocketHandshakeResponse  [:2076]
   │ - fires Network.webSocketHandshakeResponseReceived(requestId, timestamp, response{status, headers, requestHeaders})
   ▼
[renderer] Page calls ws.send("hello")
   │
   ▼
[renderer] InspectorNetworkAgent::DidSendWebSocketMessage  [:2147]
   │ - fires Network.webSocketFrameSent(requestId, timestamp, WebSocketFrame{opcode=1, mask=true, payloadData="hello"})
   ▼
[renderer] Frame received from server
   │
   ▼
[renderer] InspectorNetworkAgent::DidReceiveWebSocketMessage  [:2127]
   │ - flattens spans, fires Network.webSocketFrameReceived(requestId, timestamp, WebSocketFrame{opcode, mask, payloadData})
   ▼
[renderer] ws.close() or server closes
   │
   ▼
[renderer] InspectorNetworkAgent::DidCloseWebSocket  [:2120]
   │ - fires Network.webSocketClosed(requestId, timestamp)
   │ NOTE: no close code/reason in CDP event
```

### 2.7.2 The Payload Encoding Helper

```cpp
// inspector_network_agent.cc:693
std::unique_ptr<protocol::Network::WebSocketFrame> WebSocketMessageToProtocol(
    int op_code, bool masked, base::span<const uint8_t> payload) {
  return protocol::Network::WebSocketFrame::create()
      .setOpcode(op_code)
      .setMask(masked)
      // Only interpret the payload as UTF-8 when it's a text message
      .setPayloadData(op_code == 1 ? String::FromUtf8WithLatin1Fallback(payload)
                                   : Base64Encode(payload))
      .build();
}
```

**Opcodes** (RFC 6455):
- 1 = text → UTF-8 string in CDP
- 2 = binary → base64 in CDP
- 8 = close → base64 (treated as binary)
- 9 = ping → base64
- 10 = pong → base64

### 2.7.3 Limitations

- **No frame modification via CDP** — `Fetch.enable` does NOT intercept WebSocket handshake upgrades today (they bypass `URLLoaderFactory` proxy).
- **No close code/reason in `webSocketClosed`** event.
- **Large frames**: payload is inlined; there's no streaming option for WebSocket frames.

For WebSocket interception, you must inject JS via `Page.addScriptToEvaluateOnNewDocument` to wrap `WebSocket.prototype.send` and the `MessageEvent` handler.

---

## 2.8 Server-Sent Events (SSE)

```cpp
// inspector_network_agent.cc:1898
void InspectorNetworkAgent::WillDispatchEventSourceEvent(
    uint64_t identifier, const AtomicString& event_name,
    const AtomicString& event_id, const String& data) {
  GetFrontend()->eventSourceMessageReceived(
      IdentifiersFactory::SubresourceRequestId(identifier),
      base::TimeTicks::Now().since_origin().InSecondsF(),
      event_name.GetString(), event_id.GetString(), data);
}
```

Called from `EventSource` (in `third_party/blink/renderer/core/event_source/`) when it parses an SSE event (`event:`, `id:`, `data:` lines from the stream). The probe is enabled by setting `pending_request_type_ = kEventSourceResource` in `WillSendEventSourceRequest` (`:1893`) when the page calls `new EventSource(url)`.

The CDP event:
```json
{
  "method": "Network.eventSourceMessageReceived",
  "params": {
    "requestId": "...",
    "timestamp": 12345.678,
    "eventName": "update",
    "eventId": "42",
    "data": "{\"foo\":1}"
  }
}
```

The matching `Network.requestWillBeSent` for the SSE GET will have `type: "EventSource"`.

---

## 2.9 Redirects (301/302/303/307/308)

### 2.9.1 Renderer Side (Subresource Requests)

When the network service returns a 3xx, `ResourceFetcher` calls `InspectorNetworkAgent::DidReceiveCorsRedirectResponse` (`:1809`):

```cpp
void InspectorNetworkAgent::DidReceiveCorsRedirectResponse(
    uint64_t identifier, DocumentLoader* loader,
    const ResourceResponse& response, Resource* resource) {
  DidReceiveResourceResponse(identifier, loader, response, resource);
  DidFinishLoading(identifier, loader, base::TimeTicks(),
                   URLLoaderClient::kUnknownEncodedDataLength, 0);
}
```

So it immediately fires `responseReceived` + `loadingFinished` for the *original* request URL — that closes out the redirect hop. Then for the new URL, `WillSendRequest` is called again with the previous response as `redirect_response` parameter:

```cpp
// WillSendRequestInternal (inspector_network_agent.cc:1370)
scoped_refptr<EncodedFormData> post_data;
if (data &&
    (redirect_response.HttpStatusCode() == net::HTTP_TEMPORARY_REDIRECT ||
     redirect_response.HttpStatusCode() == net::HTTP_PERMANENT_REDIRECT)) {
  post_data = data->PostData();             // Preserve POST across 307/308
} else if (request.HttpBody()) {
  post_data = request.HttpBody()->DeepCopy();
}
// ...
GetFrontend()->requestWillBeSent(
    request_id, loader_id, documentURL, std::move(request_info),
    timestamp.since_origin().InSecondsF(),
    base::Time::Now().InSecondsFSinceUnixEpoch(),
    std::move(initiator_object),
    redirect_response.EmittedExtraInfo(),                  // redirectHasExtraInfo
    BuildObjectForResourceResponse(redirect_response,
                                   GetTargetExecutionContext()),  // redirectResponse
    resource_type, std::move(maybe_frame_id), request.HasUserGesture(),
    std::move(protocol_render_blocking_behavior));
```

So on the CDP side, a redirect produces a `Network.requestWillBeSent` for the *new* URL with `redirectResponse` populated with the old URL's full response object.

### 2.9.2 Browser Side (Navigations)

`NetworkHandler::NavigationRequestWillBeSent` (`network_handler.cc:3166`) examines `commit_params.redirect_params` and `commit_params.redirects`:

```cpp
std::unique_ptr<Network::Response> redirect_response;
const blink::mojom::CommitNavigationParams& commit_params =
    nav_request.commit_params();
bool redirect_emitted_extra_info = false;
if (!commit_params.redirect_params.empty()) {
  const network::mojom::URLResponseHead& head =
      *commit_params.redirect_params.back()->response_head;
  network::mojom::URLResponseHeadDevToolsInfoPtr head_info =
      network::ExtractDevToolsInfo(head);
  redirect_response =
      BuildResponse(commit_params.redirects.back(), *head_info);
  redirect_emitted_extra_info = head_info->emitted_extra_info;
}
```

### 2.9.3 Fetch Interception of Redirects

`InterceptionJob::OnReceiveRedirect` (`devtools_url_loader_interceptor.cc:1962`) is called when the underlying `URLLoader` delivers a redirect. If `kResponse` stage is intercepted, it pauses the client and fires a `Fetch.requestPaused` with `responseStatusCode` 301/302/303/307/308 and the `location` header. The client can then:
1. `Fetch.continueRequest` (with a new `url` to override the redirect target)
2. `Fetch.fulfillRequest` to substitute a different response (e.g. turn a 302 into a 200)
3. `Fetch.failRequest` to cancel
4. `Fetch.continueResponse` to pass through unchanged

If the client changes the URL on a `kRedirectReceived` state, `InterceptionJob::InnerContinueRequest` calls `ProcessRedirectByClient(redirect_url)` (`:1600`) which calls `client_->OnReceiveRedirect` with the modified location.

---

## 2.10 Service Worker Interception

When a service worker intercepts a fetch via `event.respondWith(new Response(...))`:

| Step | What fires | What the UI shows |
|---|---|---|
| 1 | `service-worker-synthesized-response` (Necko internal) | `fromServiceWorker = true`; channel is added to `#interceptedChannels` WeakSet |
| 2 | `#httpResponseExaminer(channel, "http-on-examine-cached-response")` (faked) | `addResponseStart`, `addCacheDetails({fromCache:false, fromServiceWorker:true})` |
| 3 | No `http-on-modify-request` or `REQUEST_HEADER` ever fired | `#prepareRequestBody` + `#sendRequestBody` called to synthesize the request body |
| 4 | All HAR timings zeroed out | `extractHarTimings` returns `getEmptyHARTimings()` |
| 5 | `extractServiceWorkerTimings` runs | `launchServiceWorker`, `requestToServiceWorker`, `handledByServiceWorker` (from `nsITimedChannel`) |
| 6 | `nsITraceableChannel.setNewListener(tee)` still works for synthesized response | `NetworkResponseListener.onDataAvailable` captures synthesized body normally |

### 2.10.1 SW-served vs Network-served — Differences on the Wire

For a **network-served** load:
- `fromServiceWorker: false`
- `fromDiskCache: true/false`
- `protocol: "h2"` or `"http/1.1"` etc.
- `remoteIPAddress` and `remotePort` populated
- `securityDetails` populated (TLS)

For a **SW-served** load:
- `fromServiceWorker: true`
- `serviceWorkerResponseSource: "network"` (passthrough) / `"http-cache"` (SW read from HTTP cache) / `"cache-storage"` (SW read from CacheStorage) / `"fallback-code"`
- `protocol` typically `"http"` (the SW synthesizes the response)
- `remoteIPAddress` / `remotePort` may be empty (no real network connection)
- `securityDetails` may be missing (no TLS handshake)

A SW-served load **does** fire `Network.requestWillBeSent` (because the request started in the renderer). It also fires `Network.responseReceived`, `Network.dataReceived`, `Network.loadingFinished`. The difference is in the response object's fields.

### 2.10.2 Service Worker Router Info (newer SW static routing API)

Populates `serviceWorkerRouterInfo` on the Response (`network_handler.cc:2952`), exposing `ruleIdMatched`, `matchedSourceType`, and `actualSourceType`:
- `Network` — fetched from network
- `FetchEvent` — handled by SW fetch event
- `Cache` — served from SW CacheStorage
- `RaceNetworkAndFetchHandler` — race between network and SW handler
- `RaceNetworkAndCache` — race between network and cache

### 2.10.3 Network.setBypassServiceWorker

```cpp
// inspector_network_agent.cc:2671
protocol::Response InspectorNetworkAgent::setBypassServiceWorker(bool bypass) {
  bypass_service_worker_.Set(bypass);
  return protocol::Response::Success();
}
```

And it's consulted in `InspectorNetworkAgent::ShouldBypassServiceWorker` (line 1306) and applied in `PrepareRequest` (line 1573: `request.SetSkipServiceWorker(true);`). Browser-side: `NetworkHandler::SetBypassServiceWorker` sets `bypass_service_worker_`, which `ApplyOverrides` ORs into `*skip_service_worker`.

---

## 2.11 CORS Error Capture

### 2.11.1 In Network.loadingFailed

```cpp
// inspector_network_agent.cc:1820
void InspectorNetworkAgent::DidFailLoading(
    CoreProbeSink* sink, uint64_t identifier, DocumentLoader* loader,
    const ResourceError& error,
    const base::UnguessableToken& devtools_frame_or_worker_token) {
  // ...
  auto cors_error_status = error.CorsErrorStatus();
  std::unique_ptr<protocol::Network::CorsErrorStatus> protocol_cors_error_status;
  if (cors_error_status) {
    protocol_cors_error_status = BuildCorsErrorStatus(*cors_error_status);
  }
  GetFrontend()->loadingFailed(
      request_id, /*ts*/, InspectorPageAgent::ResourceTypeJson(...),
      error.LocalizedDescription(), canceled,
      std::move(blocked_reason),
      std::move(protocol_cors_error_status));   // corsErrorStatus
}
```

`BuildCorsErrorStatus` (`:601`):

```cpp
std::unique_ptr<protocol::Network::CorsErrorStatus> BuildCorsErrorStatus(
    const network::CorsErrorStatus& status) {
  return protocol::Network::CorsErrorStatus::create()
      .setCorsError(BuildCorsError(status.cors_error))
      .setFailedParameter(String::FromUtf8(status.failed_parameter))
      .build();
}
```

`BuildCorsError` (`:514`) maps all 25 `network::mojom::CorsError` enum values to the CDP `CorsError` enum. Examples: `DisallowedByMode`, `InvalidResponse`, `WildcardOriginNotAllowed`, `MissingAllowOriginHeader`, `AllowOriginMismatch`, `DisabledAllowCredentials`, `InvalidAllowMethods`, `InsecureLocalNetwork`, `InvalidLocalNetworkAccess`, `LocalNetworkAccessPermissionDenied`.

### 2.11.2 As Audits Issues

For CORS errors that don't fail the request (warnings) or detected in the browser, `NetworkServiceDevToolsObserver::OnCorsError` (`network_service_devtools_observer.cc:229`) builds an `Audits::InspectorIssue` of code `CorsIssue` and reports it via `devtools_instrumentation::ReportBrowserInitiatedIssue`. Similarly `OnLocalNetworkRequest` (`:131`) reports local-network-access issues.

---

## 2.12 Security Info (TLS/SSL)

### 2.12.1 How TLS Info Is Collected

When the network service completes a TLS handshake, it captures the `net::SSLInfo` struct (cert chain, cipher suite, connection status, SCTs, ECH status, key exchange group, peer signature algorithm, CT policy compliance). Stored on `network::mojom::URLResponseHead` and delivered to the browser process. The renderer also receives it via `ResourceResponse::GetSSLInfo()`.

### 2.12.2 How TLS Info Is Exposed in CDP

Renderer side — `BuildObjectForResourceResponse` (`inspector_network_agent.cc:1259`):

```cpp
const std::optional<net::SSLInfo>& ssl_info = response.GetSSLInfo();
if (ssl_info.has_value()) {
  response_object->setSecurityDetails(BuildSecurityDetails(*ssl_info));
}
```

Browser side — `BuildResponse` (`network_handler.cc:2985`):

```cpp
if (info.ssl_info.has_value()) {
  response->SetSecurityDetails(BuildSecurityDetails(*info.ssl_info));
}
```

`BuildSecurityDetails` (`inspector_network_agent.cc:941` and parallel `network_handler.cc:2703`):

```cpp
std::unique_ptr<protocol::Network::SecurityDetails> security_details =
    protocol::Network::SecurityDetails::create()
        .setProtocol(protocol)              // "TLSv1.3" / "TLSv1.2" / "QUIC"
        .setKeyExchange(key_exchange)       // "ECDHE_RSA" etc.
        .setCipher(cipher)                 // "AES_256_GCM" etc.
        .setSubjectName(/* cert CN */)
        .setSanList(std::move(san_list))   // DNS + IP SANs
        .setIssuer(/* issuer CN */)
        .setValidFrom(valid_start.InSecondsFSinceUnixEpoch())
        .setValidTo(valid_expiry.InSecondsFSinceUnixEpoch())
        .setCertificateId(0)               // Deprecated, kept for compat
        .setSignedCertificateTimestampList(std::move(sct_list))
        .setCertificateTransparencyCompliance(
            SerializeCTPolicyCompliance(ssl_info.ct_policy_compliance))
        .setEncryptedClientHello(ssl_info.encrypted_client_hello)
        .build();
if (ssl_info.key_exchange_group != 0) {
  const char* key_exchange_group = SSL_get_curve_name(ssl_info.key_exchange_group);
  if (key_exchange_group)
    security_details->setKeyExchangeGroup(key_exchange_group);  // "X25519"
}
if (mac)
  security_details->setMac(mac);              // Only for non-AEAD ciphers
if (ssl_info.peer_signature_algorithm != 0)
  security_details->setServerSignatureAlgorithm(ssl_info.peer_signature_algorithm);
```

### 2.12.3 The securityState Field

`securityState` is one of `unknown`, `neutral`, `insecure`, `secure`, `insecure-broken`. Computed from `response.GetSecurityStyle()` (renderer side, `:1138`) or from `securityState(url, info.cert_status)` (browser side, `network_handler.cc:647`):

```cpp
String securityState(const GURL& url, const net::CertStatus& cert_status) {
  if (!url.SchemeIsCryptographic()) {
    if (network::IsUrlPotentiallyTrustworthy(url))
      return Security::SecurityStateEnum::Secure;
    return Security::SecurityStateEnum::Insecure;
  }
  if (net::IsCertStatusError(cert_status))
    return Security::SecurityStateEnum::Insecure;
  return Security::SecurityStateEnum::Secure;
}
```

### 2.12.4 The Security Domain (Certificate Errors)

`SecurityHandler` (`browser/devtools/protocol/security_handler.cc`) handles certificate errors at the navigation level:

```cpp
bool SecurityHandler::NotifyCertificateError(int cert_error,
                                             const GURL& request_url,
                                             CertErrorCallback handler) {
  if (cert_error_override_mode_ == CertErrorOverrideMode::kIgnoreAll) {
    if (handler)
      std::move(handler).Run(content::CERTIFICATE_REQUEST_RESULT_TYPE_CONTINUE);
    return true;
  }
  if (!enabled_) return false;
  frontend_->CertificateError(++last_cert_error_id_,
                              net::ErrorToShortString(cert_error),
                              request_url.spec());
  if (!handler ||
      cert_error_override_mode_ != CertErrorOverrideMode::kHandleEvents) {
    return false;
  }
  cert_error_callbacks_[last_cert_error_id_] = std::move(handler);
  return true;
}
```

Two override modes:
- `SetIgnoreCertificateErrors(true)` — bypasses all cert errors (`kIgnoreAll`)
- `SetOverrideCertificateErrors(true)` — captures the callback, fires `Security.certificateError` event with `eventId`, client must call `Security.handleCertificateError(eventId, "continue"|"cancel")` to resolve

### 2.12.5 Network.getCertificate

Returns the DER-encoded certificate chain for any origin seen in the current session:

```cpp
// inspector_network_agent.cc:2676
protocol::Response InspectorNetworkAgent::getCertificate(
    const String& origin,
    std::unique_ptr<protocol::Array<String>>* certificate) {
  *certificate = std::make_unique<protocol::Array<String>>();
  scoped_refptr<const SecurityOrigin> security_origin =
      SecurityOrigin::CreateFromString(origin);
  for (auto& resource : resources_data_->Resources()) {
    scoped_refptr<const SecurityOrigin> resource_origin =
        SecurityOrigin::Create(resource->RequestedURL());
    net::X509Certificate* cert = resource->Certificate();
    if (resource_origin->IsSameOriginWith(security_origin.get()) && cert) {
      for (const auto& buf : cert->cert_buffers()) {
        (*certificate)->push_back(
            Base64Encode(net::x509_util::CryptoBufferAsSpan(buf.get())));
      }
      return protocol::Response::Success();
    }
  }
  return protocol::Response::Success();
}
```

---

## 2.13 HTTP/2 and HTTP/3 Detection

`Network.Response.protocol` is computed by `GetProtocol` (`network_handler.cc:764`):

```cpp
String GetProtocol(const GURL& url,
                   const network::mojom::URLResponseHeadDevToolsInfo& info) {
  std::string protocol = info.alpn_negotiated_protocol;
  if (protocol.empty() || protocol == "unknown") {
    if (info.was_fetched_via_spdy) {
      protocol = "h2";
    } else if (url.SchemeIsHTTPOrHTTPS()) {
      protocol = "http";
      if (info.headers) {
        if (info.headers->GetHttpVersion() == net::HttpVersion(0, 9))
          protocol = "http/0.9";
        else if (info.headers->GetHttpVersion() == net::HttpVersion(1, 0))
          protocol = "http/1.0";
        else if (info.headers->GetHttpVersion() == net::HttpVersion(1, 1))
          protocol = "http/1.1";
      }
    } else {
      protocol = url.GetScheme();
    }
  }
  return protocol;
}
```

Renderer-side mirror is `InspectorNetworkAgent::GetProtocolAsString` (`:2001`).

**Precedence**:
1. `alpn_negotiated_protocol` — set by the network service to the ALPN-negotiated protocol string ("h2" for HTTP/2, "h3" for HTTP/3 over QUIC).
2. `was_fetched_via_spdy` — fallback to "h2".
3. HTTP version from response headers → "http/1.1", "http/1.0", "http/0.9".
4. URL scheme for non-HTTP (data:, blob:, file:).

For HTTP/3 specifically, `alpn_negotiated_protocol == "h3"` is set when the QUIC connection is used.

### 2.13.1 Alternate Protocol Usage

The `alternateProtocolUsage` field tells you *why* HTTP/3 was or wasn't used:
- `AlternativeJobWonWithoutRace`
- `AlternativeJobWonRace`
- `MainJobWonRace`
- `MappingMissing`
- `Broken`
- `DnsAlpnH3JobWonWithoutRace`
- `DnsAlpnH3JobWonRace`
- `UnspecifiedReason`

---

## 2.14 Remote IP and Port

The `net::IPEndPoint` (resolved IP + port) is set on the response head by the network service after the TCP/TLS connection is established.

Renderer side — `BuildObjectForResourceResponse` (`:1243`):

```cpp
const net::IPEndPoint& remote_ip_endpoint = response.RemoteIPEndpoint();
if (remote_ip_endpoint.address().IsValid()) {
  response_object->setRemoteIPAddress(
      IPAddressToString(remote_ip_endpoint.address()));
  response_object->setRemotePort(remote_ip_endpoint.port());
}
```

Browser side — `BuildResponse` (`network_handler.cc:2982`):

```cpp
response->SetRemoteIPAddress(
    net::HostPortPair::FromIPEndPoint(info.remote_endpoint).HostForURL());
response->SetRemotePort(info.remote_endpoint.port());
```

`IPAddressToString` (`:778`) brackets-encloses IPv6 addresses:

```cpp
String IPAddressToString(const net::IPAddress& address) {
  String unbracketed = String::FromUtf8(address.ToString());
  if (!address.IsIPv6()) return unbracketed;
  return StrCat({"[", unbracketed, "]"});
}
```

Note: for HTTP/3 over QUIC, the `remote_endpoint` is still set to the resolved UDP endpoint; only the `protocol` field differs ("h3" vs "h2").

---

## 2.15 The Complete CDP Command & Event Reference

### 2.15.1 Network Domain Commands

| Command | Implementation | What it does |
|---|---|---|
| `Network.enable` | `inspector_network_agent.cc:2415` + `network_handler.cc` | Enable network tracking with optional `maxTotalBufferSize`, `maxResourceBufferSize`, `maxPostDataSize`, `enableDurableMessages` |
| `Network.disable` | reverse of enable | Stop tracking |
| `Network.setExtraHTTPHeaders` | `inspector_network_agent.cc:2436` + `network_handler.cc:2594` | Add headers to every outgoing request |
| `Network.setUserAgentOverride` | redirects to `Emulation.setUserAgentOverride` | Override UA + UA-CH |
| `Network.setCacheDisabled` | `inspector_network_agent.cc:2660` + `network_handler.cc:2281` | Disable HTTP cache + evict MemoryCache |
| `Network.setBypassServiceWorker` | `inspector_network_agent.cc:2671` + `network_handler.cc:2696` | Skip SW fetch event handler |
| `Network.emulateNetworkConditions` | `network_handler.cc:2623` | Simulate offline/3G/2G (latency, throughput) |
| `Network.emulateNetworkConditionsByRule` | `network_handler.cc:2658` | Per-URL throttling rules |
| `Network.setBlockedURLs` | `inspector_network_agent.cc:2509` | Block URLs by pattern |
| `Network.getCookies` | `network_handler.cc:2355` | Get cookies for URLs (SameSite-aware) |
| `Network.setCookie` | `network_handler.cc:2399` | Set a single cookie |
| `Network.deleteCookies` | `network_handler.cc:2549` | Delete by name+domain+path |
| `Network.clearBrowserCookies` | `network_handler.cc:2325` | Clear ALL cookies |
| `Network.getResponseBody` | `inspector_network_agent.cc:2490` | Get response body (sync) |
| `Network.streamResourceContent` | `inspector_network_agent.cc:1697` | Stream response body chunks via `dataReceived` |
| `Network.getRequestPostData` | `inspector_network_agent.cc:2883` | Get POST body |
| `Network.getCertificate` | `inspector_network_agent.cc:2676` | Get DER cert chain for origin |
| `Network.setBlockedURLs` | `inspector_network_agent.cc:2509` | Block URLs (wildcard patterns) |
| `Network.searchInResponseBody` | `inspector_network_agent.cc` | Search response body (regex) |
| `Network.loadNetworkResource` | `network_handler.cc` | Load a network resource in context |
| `Network.replayXHR` | `inspector_network_agent.cc` | Replay an XHR |
| `Network.setAttachRequestBody` | toggles POST body capture |
| `Network.configureDurableMessages` | `network_handler.cc:4428` | Configure durable message storage |
| `Network.takeResponseBodyAsStream` | (alternative to streamResourceContent) |

### 2.15.2 Fetch Domain Commands

| Command | Implementation | What it does |
|---|---|---|
| `Fetch.enable` | `fetch_handler.cc:194` | Enable interception with patterns |
| `Fetch.disable` | `fetch_handler.cc` | Stop interception |
| `Fetch.continueRequest` | `fetch_handler.cc:427` | Continue with optional URL/method/headers/postData modifications |
| `Fetch.continueResponse` | `fetch_handler.cc` | Continue with response headers (for Response stage) |
| `Fetch.continueWithAuth` | `fetch_handler.cc` | Continue with auth challenge response |
| `Fetch.fulfillRequest` | `fetch_handler.cc:371` | Provide a complete mocked response |
| `Fetch.failRequest` | `fetch_handler.cc:344` | Fail the request with an error |
| `Fetch.getResponseBody` | `fetch_handler.cc:530` | Get body of intercepted response |
| `Fetch.takeResponseBodyAsStream` | `fetch_handler.cc:540` | Stream body of intercepted response |
| `Fetch.continueResponse` | `fetch_handler.cc` | Continue at Response stage |

### 2.15.3 IO Domain Commands (for streaming)

| Command | Implementation | What it does |
|---|---|---|
| `IO.read` | `io_handler.cc:52` | Read from stream handle (default 10MB chunks) |
| `IO.close` | `io_handler.cc` | Close stream |

---

## 2.16 Qt6 WebEngine C++ Implementation

### 2.16.1 The NetworkCapture Class

Here is a complete, production-ready Qt6 network capture implementation that uses CDP for full control:

#### `NetworkCapture.h`

```cpp
#pragma once

#include <QObject>
#include <QWebSocket>
#include <QHash>
#include <QSet>
#include <QJsonObject>
#include <QJsonArray>
#include <QTimer>
#include <QFile>
#include <QDir>
#include <functional>
#include <memory>
#include <optional>
#include <variant>

// Forward declaration
struct CapturedRequest;

// === Data structures ===

struct RequestHeader {
    QString name;
    QString value;
};

struct ResponseHeader {
    QString name;
    QString value;
};

struct InitiatorStackFrame {
    QString functionName;
    QString scriptId;
    QString url;
    int lineNumber = 0;
    int columnNumber = 0;
};

struct Initiator {
    QString type;                          // "parser" | "script" | "preload" | "SignedExchange" | "Other"
    std::optional<QString> url;
    std::optional<int> lineNumber;
    std::optional<int> columnNumber;
    QList<InitiatorStackFrame> stack;
    std::optional<QString> parentId;
};

struct ResourceTiming {
    double requestTime = 0;
    double proxyStart = -1, proxyEnd = -1;
    double dnsStart = -1, dnsEnd = -1;
    double connectStart = -1, connectEnd = -1;
    double sslStart = -1, sslEnd = -1;
    double workerStart = -1, workerReady = -1;
    double workerFetchStart = -1, workerRespondWithSettled = -1;
    double sendStart = -1, sendEnd = -1;
    double receiveHeadersStart = -1, receiveHeadersEnd = -1;
    double pushStart = 0, pushEnd = 0;
};

struct SecurityDetails {
    QString protocol;                     // "TLSv1.3" / "TLSv1.2" / "QUIC"
    QString keyExchange;
    QString keyExchangeGroup;
    QString cipher;
    QString mac;
    QString subjectName;
    QStringList sanList;
    QString issuer;
    double validFrom = 0;
    double validTo = 0;
    bool encryptedClientHello = false;
    QString certificateTransparencyCompliance;
    QList<QJsonObject> signedCertificateTimestampList;
    int serverSignatureAlgorithm = 0;
};

struct CapturedRequest {
    QString requestId;
    QString loaderId;
    QString frameId;
    QUrl url;
    QString method;
    QList<RequestHeader> requestHeaders;
    QList<RequestHeader> responseHeaders;
    QString requestHeadersText;
    QString responseHeadersText;
    int status = 0;
    QString statusText;
    QString mimeType;
    QString charset;
    QString resourceType;                  // "Document" | "Script" | "XHR" | "Fetch" | etc.
    QString protocol;                      // "h2" | "h3" | "http/1.1" | etc.
    QString remoteIPAddress;
    int remotePort = 0;
    bool fromDiskCache = false;
    bool fromServiceWorker = false;
    bool fromPrefetchCache = false;
    qint64 encodedDataLength = 0;
    qint64 dataLength = 0;
    qint64 decodedBodySize = 0;
    bool hasUserGesture = false;
    bool isRedirect = false;
    QString serviceWorkerResponseSource;  // "network" | "http-cache" | "cache-storage" | "fallback-code"
    
    Initiator initiator;
    ResourceTiming timing;
    SecurityDetails securityDetails;
    QString securityState;                 // "unknown" | "neutral" | "insecure" | "secure" | "insecure-broken"
    
    // Body capture
    QByteArray responseBody;               // assembled
    bool responseBodyBase64Encoded = false;
    bool responseBodyTruncated = false;
    bool responseBodyEvicted = false;
    QByteArray requestBody;
    bool requestBodyBase64Encoded = false;
    
    // Timing
    double timestamp = 0;
    double wallTime = 0;
    double responseTime = 0;
    bool finished = false;
    bool failed = false;
    QString errorText;
    bool canceled = false;
    QString blockedReason;
    QJsonObject corsErrorStatus;
    
    // Cookies
    QList<QJsonObject> requestCookies;
    QList<QJsonObject> responseSetCookies;
    QList<QJsonObject> blockedCookies;
    
    // WebSocket (if type is WebSocket)
    bool isWebSocket = false;
    QList<QJsonObject> wsFrames;           // sent + received
    
    // SSE (if type is EventSource)
    bool isEventSource = false;
    QList<QJsonObject> sseEvents;
};

class NetworkCapture : public QObject {
    Q_OBJECT
public:
    explicit NetworkCapture(const QUrl& devtoolsUrl, QObject* parent = nullptr);
    ~NetworkCapture();
    
    // === Enable/Disable ===
    void enable(int maxTotalBufferSize = 200 * 1024 * 1024,
                int maxResourceBufferSize = 20 * 1024 * 1024,
                int maxPostDataSize = 1024 * 1024,
                bool enableDurableMessages = true);
    void disable();
    
    // === Body capture ===
    void getResponseBody(const QString& requestId,
                        std::function<void(const QByteArray&, bool base64Encoded)> callback);
    void getRequestPostData(const QString& requestId,
                          std::function<void(const QByteArray&, bool base64Encoded)> callback);
    
    // === Header modification ===
    void setExtraHTTPHeaders(const QHash<QString, QString>& headers);
    void clearExtraHTTPHeaders();
    
    // === Cache control ===
    void setCacheDisabled(bool disabled);
    void setBypassServiceWorker(bool bypass);
    
    // === Network throttling ===
    void emulateNetworkConditions(bool offline, double latencyMs,
                                   double downloadThroughputBps,
                                   double uploadThroughputBps,
                                   const QString& connectionType = "");
    void clearNetworkConditions();
    
    // === URL blocking ===
    void setBlockedURLs(const QStringList& patterns);
    void blockURL(const QString& pattern);
    void unblockURL(const QString& pattern);
    
    // === Request interception (Fetch domain) ===
    using FetchInterceptor = std::function<QJsonObject(const QJsonObject& requestPausedEvent)>;
    //   Returns one of:
    //     {"action": "continue"}
    //     {"action": "continue", "url": "...", "method": "...", "headers": [...], "postData": "base64..."}
    //     {"action": "fulfill", "responseCode": 200, "responseHeaders": [...], "body": "base64..."}
    //     {"action": "fail", "errorReason": "BlockedByClient"}
    
    void enableFetchInterception(const QStringList& urlPatterns,
                                 const QString& requestStage = "Request",
                                 FetchInterceptor interceptor = {});
    void disableFetchInterception();
    
    // === Certificate override ===
    void setIgnoreCertificateErrors(bool ignore);
    void setOverrideCertificateErrors(std::function<bool(int, const QString&, const QUrl&)> handler);
    
    // === Snapshot / Query ===
    QList<CapturedRequest> allRequests() const;
    CapturedRequest request(const QString& requestId) const;
    QList<CapturedRequest> requestsForUrl(const QUrl& url) const;
    QList<CapturedRequest> requestsByType(const QString& resourceType) const;
    
    // === Export ===
    void exportHar(const QString& filepath);
    QJsonObject harJson() const;
    
    // === Download files ===
    void downloadResponseToDisk(const QString& requestId, const QString& filepath,
                                std::function<void(bool success)> callback);
    
signals:
    void requestWillBeSent(const CapturedRequest& req);
    void responseReceived(const CapturedRequest& req);
    void dataReceived(const QString& requestId, qint64 dataLength, qint64 encodedDataLength);
    void loadingFinished(const QString& requestId, qint64 encodedDataLength);
    void loadingFailed(const QString& requestId, const QString& errorText, bool canceled,
                      const QString& blockedReason, const QJsonObject& corsErrorStatus);
    void requestServedFromCache(const QString& requestId);
    
    void webSocketCreated(const QString& requestId, const QUrl& url);
    void webSocketHandshakeRequest(const QString& requestId, const QList<RequestHeader>& headers);
    void webSocketHandshakeResponse(const QString& requestId, int status, const QList<RequestHeader>& headers);
    void webSocketFrameSent(const QString& requestId, int opcode, bool masked, const QByteArray& payload);
    void webSocketFrameReceived(const QString& requestId, int opcode, bool masked, const QByteArray& payload);
    void webSocketFrameError(const QString& requestId, const QString& errorMessage);
    void webSocketClosed(const QString& requestId);
    
    void eventSourceMessageReceived(const QString& requestId, const QString& eventName,
                                    const QString& eventId, const QString& data);
    
    void fetchRequestPaused(const QJsonObject& requestPausedEvent);
    
private:
    void sendCommand(const QString& method, const QJsonObject& params,
                    std::function<void(const QJsonObject&)> callback = {});
    void handleMessage(const QString& message);
    
    // Helpers
    static QList<RequestHeader> parseHeaders(const QJsonObject& obj, const QString& key);
    static QList<RequestHeader> parseHeadersArray(const QJsonArray& arr);
    static QString resourceTypeFromCdp(const QString& type);
    static CapturedRequest mergeIntoRequest(CapturedRequest existing, const QJsonObject& params);
    
    QWebSocket* m_ws;
    int m_nextId = 1;
    QHash<int, std::function<void(const QJsonObject&)>> m_callbacks;
    QString m_sessionId;
    
    QHash<QString, CapturedRequest> m_requests;       // requestId → CapturedRequest
    QSet<QString> m_blockedPatterns;
    
    // Fetch interception
    FetchInterceptor m_fetchInterceptor;
    bool m_fetchEnabled = false;
    
    // Cert override
    std::function<bool(int, const QString&, const QUrl&)> m_certOverrideHandler;
    QHash<int, std::function<void(bool)>> m_pendingCertCallbacks;
    
    // HAR export timer
    QTimer m_harTimer;
};
```

#### `NetworkCapture.cpp` (key methods)

```cpp
#include "NetworkCapture.h"
#include <QJsonDocument>
#include <QDateTime>
#include <QDir>
#include <QStandardPaths>

// === Constructor / Destructor ===

NetworkCapture::NetworkCapture(const QUrl& devtoolsUrl, QObject* parent)
    : QObject(parent), m_ws(new QWebSocket) {
    
    connect(m_ws, &QWebSocket::textMessageReceived,
            this, &NetworkCapture::handleMessage);
    m_ws->open(devtoolsUrl);
}

NetworkCapture::~NetworkCapture() {
    if (m_ws->isValid()) m_ws->close();
}

// === CDP plumbing ===

void NetworkCapture::sendCommand(const QString& method, const QJsonObject& params,
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

void NetworkCapture::handleMessage(const QString& message) {
    const auto doc = QJsonDocument::fromJson(message.toUtf8()).object();
    
    // Response to our command
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
    
    // Update our internal m_requests map
    if (method == "Network.requestWillBeSent") {
        const QString reqId = params.value("requestId").toString();
        CapturedRequest& req = m_requests[reqId];
        req.requestId = reqId;
        req.loaderId = params.value("loaderId").toString();
        req.frameId = params.value("frameId").toString();
        req.url = QUrl(params.value("request").toObject().value("url").toString());
        req.method = params.value("request").toObject().value("method").toString();
        req.requestHeaders = parseHeaders(params.value("request").toObject(), "headers");
        req.timestamp = params.value("timestamp").toDouble();
        req.wallTime = params.value("wallTime").toDouble();
        req.resourceType = resourceTypeFromCdp(params.value("type").toString());
        req.hasUserGesture = params.value("hasUserGesture").toBool(false);
        
        // Parse initiator
        const QJsonObject init = params.value("initiator").toObject();
        req.initiator.type = init.value("type").toString();
        if (init.contains("url")) req.initiator.url = init.value("url").toString();
        if (init.contains("lineNumber")) req.initiator.lineNumber = init.value("lineNumber").toInt();
        if (init.contains("columnNumber")) req.initiator.columnNumber = init.value("columnNumber").toInt();
        if (init.contains("stack")) {
            const QJsonArray frames = init.value("stack").toObject().value("callFrames").toArray();
            for (const QJsonValue& f : frames) {
                const QJsonObject fo = f.toObject();
                InitiatorStackFrame frame;
                frame.functionName = fo.value("functionName").toString();
                frame.scriptId = fo.value("scriptId").toString();
                frame.url = fo.value("url").toString();
                frame.lineNumber = fo.value("lineNumber").toInt();
                frame.columnNumber = fo.value("columnNumber").toInt();
                req.initiator.stack.append(frame);
            }
        }
        
        // postData (if maxPostDataSize was set)
        const QJsonObject reqObj = params.value("request").toObject();
        if (reqObj.contains("postData")) {
            req.requestBody = reqObj.value("postData").toString().toUtf8();
            req.requestBodyBase64Encoded = false;
        }
        if (reqObj.contains("postDataEntries")) {
            const QJsonArray entries = reqObj.value("postDataEntries").toArray();
            for (const QJsonValue& e : entries) {
                const QString bytes = e.toObject().value("bytes").toString();
                req.requestBody.append(QByteArray::fromBase64(bytes.toUtf8()));
                req.requestBodyBase64Encoded = false;
            }
        }
        
        // redirectResponse (if this is a redirect)
        if (params.contains("redirectResponse")) {
            // Close out the previous hop
            // (the requestId is reused for the new URL)
            req.isRedirect = true;
        }
        
        if (req.resourceType == "WebSocket") req.isWebSocket = true;
        if (req.resourceType == "EventSource") req.isEventSource = true;
        
        emit requestWillBeSent(req);
    }
    else if (method == "Network.requestWillBeSentExtraInfo") {
        const QString reqId = params.value("requestId").toString();
        CapturedRequest& req = m_requests[reqId];
        // Parse cookies and raw headers
        const QJsonArray cookies = params.value("cookies").toArray();
        for (const QJsonValue& c : cookies) {
            req.requestCookies.append(c.toObject());
        }
        // raw headers in 'headers' field
    }
    else if (method == "Network.responseReceived") {
        const QString reqId = params.value("requestId").toString();
        CapturedRequest& req = m_requests[reqId];
        const QJsonObject resp = params.value("response").toObject();
        req.url = QUrl(resp.value("url").toString());
        req.status = resp.value("status").toInt();
        req.statusText = resp.value("statusText").toString();
        req.responseHeaders = parseHeaders(resp, "headers");
        req.mimeType = resp.value("mimeType").toString();
        req.protocol = resp.value("protocol").toString();
        req.remoteIPAddress = resp.value("remoteIPAddress").toString();
        req.remotePort = resp.value("remotePort").toInt();
        req.fromDiskCache = resp.value("fromDiskCache").toBool(false);
        req.fromServiceWorker = resp.value("fromServiceWorker").toBool(false);
        req.fromPrefetchCache = resp.value("fromPrefetchCache").toBool(false);
        req.encodedDataLength = static_cast<qint64>(resp.value("encodedDataLength").toDouble());
        req.securityState = resp.value("securityState").toString();
        req.responseTime = resp.value("responseTime").toDouble();
        
        if (resp.contains("timing")) {
            const QJsonObject t = resp.value("timing").toObject();
            req.timing.requestTime = t.value("requestTime").toDouble();
            req.timing.proxyStart = t.value("proxyStart").toDouble(-1);
            req.timing.proxyEnd = t.value("proxyEnd").toDouble(-1);
            req.timing.dnsStart = t.value("dnsStart").toDouble(-1);
            req.timing.dnsEnd = t.value("dnsEnd").toDouble(-1);
            req.timing.connectStart = t.value("connectStart").toDouble(-1);
            req.timing.connectEnd = t.value("connectEnd").toDouble(-1);
            req.timing.sslStart = t.value("sslStart").toDouble(-1);
            req.timing.sslEnd = t.value("sslEnd").toDouble(-1);
            req.timing.sendStart = t.value("sendStart").toDouble(-1);
            req.timing.sendEnd = t.value("sendEnd").toDouble(-1);
            req.timing.receiveHeadersStart = t.value("receiveHeadersStart").toDouble(-1);
            req.timing.receiveHeadersEnd = t.value("receiveHeadersEnd").toDouble(-1);
        }
        
        if (resp.contains("securityDetails")) {
            const QJsonObject sd = resp.value("securityDetails").toObject();
            req.securityDetails.protocol = sd.value("protocol").toString();
            req.securityDetails.keyExchange = sd.value("keyExchange").toString();
            req.securityDetails.keyExchangeGroup = sd.value("keyExchangeGroup").toString();
            req.securityDetails.cipher = sd.value("cipher").toString();
            req.securityDetails.mac = sd.value("mac").toString();
            req.securityDetails.subjectName = sd.value("subjectName").toString();
            req.securityDetails.issuer = sd.value("issuer").toString();
            req.securityDetails.validFrom = sd.value("validFrom").toDouble();
            req.securityDetails.validTo = sd.value("validTo").toDouble();
            req.securityDetails.encryptedClientHello = sd.value("encryptedClientHello").toBool(false);
            req.securityDetails.certificateTransparencyCompliance = 
                sd.value("certificateTransparencyCompliance").toString();
            const QJsonArray sans = sd.value("sanList").toArray();
            for (const QJsonValue& s : sans) req.securityDetails.sanList.append(s.toString());
        }
        
        if (resp.contains("serviceWorkerResponseSource")) {
            req.serviceWorkerResponseSource = resp.value("serviceWorkerResponseSource").toString();
        }
        
        emit responseReceived(req);
    }
    else if (method == "Network.responseReceivedExtraInfo") {
        const QString reqId = params.value("requestId").toString();
        CapturedRequest& req = m_requests[reqId];
        // Parse blocked cookies
        const QJsonArray blocked = params.value("blockedCookies").toArray();
        for (const QJsonValue& c : blocked) {
            req.blockedCookies.append(c.toObject());
        }
    }
    else if (method == "Network.dataReceived") {
        const QString reqId = params.value("requestId").toString();
        CapturedRequest& req = m_requests[reqId];
        req.dataLength += params.value("dataLength").toInt();
        req.encodedDataLength += params.value("encodedDataLength").toInt();
        req.decodedBodySize += params.value("dataLength").toInt();
        emit dataReceived(reqId, params.value("dataLength").toInt(),
                          params.value("encodedDataLength").toInt());
    }
    else if (method == "Network.loadingFinished") {
        const QString reqId = params.value("requestId").toString();
        CapturedRequest& req = m_requests[reqId];
        req.finished = true;
        req.encodedDataLength = static_cast<qint64>(params.value("encodedDataLength").toDouble());
        emit loadingFinished(reqId, req.encodedDataLength);
    }
    else if (method == "Network.loadingFailed") {
        const QString reqId = params.value("requestId").toString();
        CapturedRequest& req = m_requests[reqId];
        req.failed = true;
        req.errorText = params.value("errorText").toString();
        req.canceled = params.value("canceled").toBool(false);
        if (params.contains("blockedReason"))
            req.blockedReason = params.value("blockedReason").toString();
        if (params.contains("corsErrorStatus"))
            req.corsErrorStatus = params.value("corsErrorStatus").toObject();
        emit loadingFailed(reqId, req.errorText, req.canceled, req.blockedReason, req.corsErrorStatus);
    }
    else if (method == "Network.requestServedFromCache") {
        const QString reqId = params.value("requestId").toString();
        CapturedRequest& req = m_requests[reqId];
        req.fromDiskCache = true;
        emit requestServedFromCache(reqId);
    }
    
    // === WebSocket events ===
    else if (method == "Network.webSocketCreated") {
        const QString reqId = params.value("requestId").toString();
        const QUrl url(params.value("url").toString());
        m_requests[reqId].isWebSocket = true;
        m_requests[reqId].url = url;
        emit webSocketCreated(reqId, url);
    }
    else if (method == "Network.webSocketWillSendHandshakeRequest") {
        const QString reqId = params.value("requestId").toString();
        const QList<RequestHeader> headers = parseHeaders(
            params.value("request").toObject(), "headers");
        emit webSocketHandshakeRequest(reqId, headers);
    }
    else if (method == "Network.webSocketHandshakeResponseReceived") {
        const QString reqId = params.value("requestId").toString();
        const QJsonObject resp = params.value("response").toObject();
        emit webSocketHandshakeResponse(reqId, resp.value("status").toInt(),
            parseHeaders(resp, "headers"));
    }
    else if (method == "Network.webSocketFrameSent") {
        const QString reqId = params.value("requestId").toString();
        const QJsonObject frame = params.value("response").toObject();
        const int opcode = frame.value("opcode").toInt();
        const bool masked = frame.value("mask").toBool();
        const QString payloadStr = frame.value("payloadData").toString();
        QByteArray payload;
        if (opcode == 1) payload = payloadStr.toUtf8();          // text
        else payload = QByteArray::fromBase64(payloadStr.toUtf8());  // binary
        
        m_requests[reqId].wsFrames.append(frame);
        emit webSocketFrameSent(reqId, opcode, masked, payload);
    }
    else if (method == "Network.webSocketFrameReceived") {
        const QString reqId = params.value("requestId").toString();
        const QJsonObject frame = params.value("response").toObject();
        const int opcode = frame.value("opcode").toInt();
        const bool masked = frame.value("mask").toBool();
        const QString payloadStr = frame.value("payloadData").toString();
        QByteArray payload;
        if (opcode == 1) payload = payloadStr.toUtf8();
        else payload = QByteArray::fromBase64(payloadStr.toUtf8());
        
        m_requests[reqId].wsFrames.append(frame);
        emit webSocketFrameReceived(reqId, opcode, masked, payload);
    }
    else if (method == "Network.webSocketFrameError") {
        const QString reqId = params.value("requestId").toString();
        emit webSocketFrameError(reqId, params.value("errorMessage").toString());
    }
    else if (method == "Network.webSocketClosed") {
        const QString reqId = params.value("requestId").toString();
        emit webSocketClosed(reqId);
    }
    
    // === SSE events ===
    else if (method == "Network.eventSourceMessageReceived") {
        const QString reqId = params.value("requestId").toString();
        const QString eventName = params.value("eventName").toString();
        const QString eventId = params.value("eventId").toString();
        const QString data = params.value("data").toString();
        
        QJsonObject ev;
        ev["eventName"] = eventName;
        ev["eventId"] = eventId;
        ev["data"] = data;
        m_requests[reqId].sseEvents.append(ev);
        emit eventSourceMessageReceived(reqId, eventName, eventId, data);
    }
    
    // === Fetch interception ===
    else if (method == "Fetch.requestPaused") {
        const QString reqId = params.value("requestId").toString();
        emit fetchRequestPaused(params);
        
        if (m_fetchInterceptor) {
            const QJsonObject decision = m_fetchInterceptor(params);
            const QString action = decision.value("action").toString();
            
            if (action == "continue") {
                QJsonObject p;
                p["requestId"] = reqId;
                if (decision.contains("url")) p["url"] = decision.value("url").toString();
                if (decision.contains("method")) p["method"] = decision.value("method").toString();
                if (decision.contains("postData")) {
                    p["postData"] = decision.value("postData").toString();
                }
                if (decision.contains("headers")) {
                    p["headers"] = decision.value("headers").toArray();
                }
                sendCommand("Fetch.continueRequest", p);
            } else if (action == "fulfill") {
                QJsonObject p;
                p["requestId"] = reqId;
                p["responseCode"] = decision.value("responseCode").toInt();
                if (decision.contains("responseHeaders"))
                    p["responseHeaders"] = decision.value("responseHeaders").toArray();
                if (decision.contains("body")) p["body"] = decision.value("body").toString();
                if (decision.contains("responsePhrase"))
                    p["responsePhrase"] = decision.value("responsePhrase").toString();
                sendCommand("Fetch.fulfillRequest", p);
            } else if (action == "fail") {
                QJsonObject p;
                p["requestId"] = reqId;
                p["errorReason"] = decision.value("errorReason").toString("BlockedByClient");
                sendCommand("Fetch.failRequest", p);
            }
        } else {
            // Auto-continue if no interceptor
            QJsonObject p;
            p["requestId"] = reqId;
            sendCommand("Fetch.continueRequest", p);
        }
    }
    else if (method == "Fetch.authRequired") {
        // Auto-continue with default credentials
        QJsonObject p;
        p["requestId"] = params.value("requestId").toString();
        p["response"] = "Default";
        sendCommand("Fetch.continueWithAuth", p);
    }
    
    // === Security domain ===
    else if (method == "Security.certificateError") {
        const int eventId = params.value("eventId").toInt();
        const QString errorType = params.value("errorType").toString();
        const QUrl requestUrl(params.value("requestURL").toString());
        
        if (m_certOverrideHandler) {
            const bool shouldContinue = m_certOverrideHandler(eventId, errorType, requestUrl);
            QJsonObject p;
            p["eventId"] = eventId;
            p["action"] = shouldContinue ? "continue" : "cancel";
            sendCommand("Security.handleCertificateError", p);
        } else {
            // Default: cancel
            QJsonObject p;
            p["eventId"] = eventId;
            p["action"] = "cancel";
            sendCommand("Security.handleCertificateError", p);
        }
    }
}

// === Helpers ===

QList<RequestHeader> NetworkCapture::parseHeaders(const QJsonObject& obj, const QString& key) {
    QList<RequestHeader> result;
    const QJsonObject headers = obj.value(key).toObject();
    for (auto it = headers.begin(); it != headers.end(); ++it) {
        RequestHeader h;
        h.name = it.key();
        h.value = it.value().toString();
        result.append(h);
    }
    return result;
}

QList<RequestHeader> NetworkCapture::parseHeadersArray(const QJsonArray& arr) {
    QList<RequestHeader> result;
    for (const QJsonValue& v : arr) {
        const QJsonObject o = v.toObject();
        RequestHeader h;
        h.name = o.value("name").toString();
        h.value = o.value("value").toString();
        result.append(h);
    }
    return result;
}

QString NetworkCapture::resourceTypeFromCdp(const QString& type) {
    if (type.isEmpty()) return "Other";
    return type;  // already "Document", "Script", "XHR", etc.
}

// === Enable/Disable ===

void NetworkCapture::enable(int maxTotalBufferSize, int maxResourceBufferSize,
                            int maxPostDataSize, bool enableDurableMessages) {
    QJsonObject params;
    params["maxTotalBufferSize"] = maxTotalBufferSize;
    params["maxResourceBufferSize"] = maxResourceBufferSize;
    params["maxPostDataSize"] = maxPostDataSize;
    if (enableDurableMessages) {
        params["enableDurableMessages"] = true;
    }
    sendCommand("Network.enable", params);
}

void NetworkCapture::disable() {
    sendCommand("Network.disable");
}

// === Body capture ===

void NetworkCapture::getResponseBody(const QString& requestId,
                                      std::function<void(const QByteArray&, bool)> callback) {
    QJsonObject params;
    params["requestId"] = requestId;
    sendCommand("Network.getResponseBody", params, [callback](const QJsonObject& result) {
        const QString body = result.value("body").toString();
        const bool base64 = result.value("base64Encoded").toBool(false);
        if (base64) callback(QByteArray::fromBase64(body.toUtf8()), true);
        else callback(body.toUtf8(), false);
    });
}

void NetworkCapture::getRequestPostData(const QString& requestId,
                                        std::function<void(const QByteArray&, bool)> callback) {
    QJsonObject params;
    params["requestId"] = requestId;
    sendCommand("Network.getRequestPostData", params, [callback](const QJsonObject& result) {
        const QString body = result.value("postData").toString();
        const bool base64 = result.value("base64Encoded").toBool(false);
        if (base64) callback(QByteArray::fromBase64(body.toUtf8()), true);
        else callback(body.toUtf8(), false);
    });
}

// === Header modification ===

void NetworkCapture::setExtraHTTPHeaders(const QHash<QString, QString>& headers) {
    QJsonObject headersObj;
    for (auto it = headers.begin(); it != headers.end(); ++it) {
        headersObj[it.key()] = it.value();
    }
    QJsonObject params;
    params["headers"] = headersObj;
    sendCommand("Network.setExtraHTTPHeaders", params);
}

void NetworkCapture::clearExtraHTTPHeaders() {
    setExtraHTTPHeaders({});
}

// === Cache control ===

void NetworkCapture::setCacheDisabled(bool disabled) {
    QJsonObject params;
    params["cacheDisabled"] = disabled;
    sendCommand("Network.setCacheDisabled", params);
}

void NetworkCapture::setBypassServiceWorker(bool bypass) {
    QJsonObject params;
    params["bypass"] = bypass;
    sendCommand("Network.setBypassServiceWorker", params);
}

// === Network throttling ===

void NetworkCapture::emulateNetworkConditions(bool offline, double latencyMs,
                                               double downloadThroughputBps,
                                               double uploadThroughputBps,
                                               const QString& connectionType) {
    QJsonObject params;
    params["offline"] = offline;
    params["latency"] = latencyMs;
    params["downloadThroughput"] = downloadThroughputBps;
    params["uploadThroughput"] = uploadThroughputBps;
    if (!connectionType.isEmpty()) params["connectionType"] = connectionType;
    sendCommand("Network.emulateNetworkConditions", params);
}

void NetworkCapture::clearNetworkConditions() {
    emulateNetworkConditions(false, 0, -1, -1);
}

// === URL blocking ===

void NetworkCapture::setBlockedURLs(const QStringList& patterns) {
    QJsonArray arr;
    for (const QString& p : patterns) arr.append(p);
    QJsonObject params;
    params["urls"] = arr;
    sendCommand("Network.setBlockedURLs", params);
    m_blockedPatterns = QSet<QString>(patterns.begin(), patterns.end());
}

void NetworkCapture::blockURL(const QString& pattern) {
    m_blockedPatterns.insert(pattern);
    QStringList list = m_blockedPatterns.values();
    setBlockedURLs(list);
}

void NetworkCapture::unblockURL(const QString& pattern) {
    m_blockedPatterns.remove(pattern);
    QStringList list = m_blockedPatterns.values();
    setBlockedURLs(list);
}

// === Fetch interception ===

void NetworkCapture::enableFetchInterception(const QStringList& urlPatterns,
                                             const QString& requestStage,
                                             FetchInterceptor interceptor) {
    m_fetchInterceptor = interceptor;
    
    QJsonArray patterns;
    for (const QString& p : urlPatterns) {
        QJsonObject pattern;
        pattern["urlPattern"] = p;
        pattern["requestStage"] = requestStage;
        patterns.append(pattern);
    }
    QJsonObject params;
    params["patterns"] = patterns;
    sendCommand("Fetch.enable", params);
    m_fetchEnabled = true;
}

void NetworkCapture::disableFetchInterception() {
    sendCommand("Fetch.disable");
    m_fetchEnabled = false;
    m_fetchInterceptor = {};
}

// === Certificate override ===

void NetworkCapture::setIgnoreCertificateErrors(bool ignore) {
    QJsonObject params;
    params["ignore"] = ignore;
    sendCommand("Security.setIgnoreCertificateErrors", params);
}

void NetworkCapture::setOverrideCertificateErrors(
        std::function<bool(int, const QString&, const QUrl&)> handler) {
    m_certOverrideHandler = handler;
    sendCommand("Security.enable");
    QJsonObject params;
    params["override"] = true;
    sendCommand("Security.setOverrideCertificateErrors", params);
}

// === Snapshot / Query ===

QList<CapturedRequest> NetworkCapture::allRequests() const {
    return m_requests.values();
}

CapturedRequest NetworkCapture::request(const QString& requestId) const {
    return m_requests.value(requestId);
}

QList<CapturedRequest> NetworkCapture::requestsForUrl(const QUrl& url) const {
    QList<CapturedRequest> result;
    for (const CapturedRequest& r : m_requests) {
        if (r.url == url) result.append(r);
    }
    return result;
}

QList<CapturedRequest> NetworkCapture::requestsByType(const QString& resourceType) const {
    QList<CapturedRequest> result;
    for (const CapturedRequest& r : m_requests) {
        if (r.resourceType == resourceType) result.append(r);
    }
    return result;
}

// === HAR export ===

QJsonObject NetworkCapture::harJson() const {
    QJsonObject har;
    QJsonObject log;
    log["version"] = "1.2";
    log["creator"] = QJsonObject{
        {"name", "Qt6 WebEngine Scraping Browser"},
        {"version", "1.0"}
    };
    
    QJsonArray entries;
    for (const CapturedRequest& r : m_requests) {
        if (r.url.isEmpty()) continue;
        
        QJsonObject entry;
        QJsonObject request;
        request["method"] = r.method;
        request["url"] = r.url.toString();
        request["httpVersion"] = r.protocol.isEmpty() ? "HTTP/1.1" : r.protocol;
        
        QJsonArray reqHeaders;
        for (const RequestHeader& h : r.requestHeaders) {
            reqHeaders.append(QJsonObject{
                {"name", h.name}, {"value", h.value}
            });
        }
        request["headers"] = reqHeaders;
        
        if (!r.requestBody.isEmpty()) {
            QJsonObject postData;
            postData["mimeType"] = "application/octet-stream";
            if (r.requestBodyBase64Encoded) {
                postData["text"] = QString::fromUtf8(r.requestBody.toBase64());
                postData["encoding"] = "base64";
            } else {
                postData["text"] = QString::fromUtf8(r.requestBody);
            }
            request["postData"] = postData;
        }
        
        request["headersSize"] = -1;
        request["bodySize"] = r.requestBody.size();
        entry["request"] = request;
        
        QJsonObject response;
        response["status"] = r.status;
        response["statusText"] = r.statusText;
        response["httpVersion"] = r.protocol.isEmpty() ? "HTTP/1.1" : r.protocol;
        
        QJsonArray respHeaders;
        for (const ResponseHeader& h : r.responseHeaders) {
            respHeaders.append(QJsonObject{
                {"name", h.name}, {"value", h.value}
            });
        }
        response["headers"] = respHeaders;
        response["redirectURL"] = r.isRedirect ? r.url.toString() : "";
        response["headersSize"] = -1;
        response["bodySize"] = r.encodedDataLength;
        response["content"] = QJsonObject{
            {"size", static_cast<int>(r.decodedBodySize)},
            {"mimeType", r.mimeType},
            {"text", QString::fromUtf8(r.responseBody.toBase64())},
            {"encoding", "base64"}
        };
        entry["response"] = response;
        
        // Timings
        QJsonObject timings;
        const ResourceTiming& t = r.timing;
        double blocked = (t.proxyStart >= 0) ? t.proxyEnd - t.proxyStart : -1;
        double dns = (t.dnsStart >= 0) ? t.dnsEnd - t.dnsStart : -1;
        double connect = (t.connectStart >= 0) ? t.connectEnd - t.connectStart : -1;
        double ssl = (t.sslStart >= 0) ? t.sslEnd - t.sslStart : -1;
        double send = (t.sendStart >= 0) ? t.sendEnd - t.sendStart : -1;
        double wait = (t.sendEnd >= 0 && t.receiveHeadersStart >= 0) ? 
                      t.receiveHeadersStart - t.sendEnd : -1;
        double receive = (t.receiveHeadersStart >= 0 && r.finished) ? 
                         t.receiveHeadersEnd - t.receiveHeadersStart : -1;
        
        timings["blocked"] = blocked;
        timings["dns"] = dns;
        timings["connect"] = connect;
        timings["ssl"] = ssl;
        timings["send"] = send;
        timings["wait"] = wait;
        timings["receive"] = receive;
        entry["timings"] = timings;
        
        // Time
        entry["startedDateTime"] = QDateTime::fromMSecsSinceEpoch(
            static_cast<qint64>(r.wallTime * 1000)).toUTC().toString(Qt::ISODate);
        entry["time"] = (t.receiveHeadersEnd >= 0) ? t.receiveHeadersEnd - t.requestTime : 0;
        
        // Server info
        QJsonObject server;
        server["ipAddress"] = r.remoteIPAddress;
        server["port"] = r.remotePort;
        entry["serverIPAddress"] = r.remoteIPAddress;
        
        entries.append(entry);
    }
    
    log["entries"] = entries;
    har["log"] = log;
    return har;
}

void NetworkCapture::exportHar(const QString& filepath) {
    QFile f(filepath);
    if (!f.open(QIODevice::WriteOnly)) {
        qWarning() << "Failed to open HAR file for writing:" << filepath;
        return;
    }
    QJsonDocument doc(harJson());
    f.write(doc.toJson(QJsonDocument::Indented));
    f.close();
}

// === Download response to disk ===

void NetworkCapture::downloadResponseToDisk(const QString& requestId,
                                            const QString& filepath,
                                            std::function<void(bool)> callback) {
    getResponseBody(requestId, [filepath, callback](const QByteArray& body, bool) {
        QFile f(filepath);
        if (f.open(QIODevice::WriteOnly)) {
            f.write(body);
            f.close();
            if (callback) callback(true);
        } else {
            if (callback) callback(false);
        }
    });
}
```

### 2.16.2 Using the Network Capture

```cpp
// In your scraper:
auto* netCapture = new NetworkCapture(QUrl("ws://127.0.0.1:9222/devtools/page/<id>"));

// Wait for connection, then enable
connect(netCapture, &QWebSocket::connected, [=]() {
    netCapture->enable(200 * 1024 * 1024,    // 200MB total
                       20 * 1024 * 1024,      // 20MB per resource
                       1024 * 1024,            // 1MB POST data
                       true);                  // durable messages
});

// Capture every request
connect(netCapture, &NetworkCapture::requestWillBeSent, [](const CapturedRequest& req) {
    qDebug() << "[REQ]" << req.method << req.url.toString() 
             << "type:" << req.resourceType
             << "initiator:" << req.initiator.type;
});

// Capture every response
connect(netCapture, &NetworkCapture::responseReceived, [](const CapturedRequest& req) {
    qDebug() << "[RESP]" << req.status << req.url.toString()
             << "protocol:" << req.protocol
             << "remote:" << req.remoteIPAddress << ":" << req.remotePort
             << "TLS:" << req.securityDetails.protocol
             << "cipher:" << req.securityDetails.cipher;
});

// Download every script
connect(netCapture, &NetworkCapture::loadingFinished, [=](const QString& requestId, qint64) {
    const CapturedRequest req = netCapture->request(requestId);
    if (req.resourceType == "Script") {
        QDir().mkpath("scripts");
        netCapture->downloadResponseToDisk(requestId, 
            "scripts/" + req.url.fileName(),
            [](bool ok) { qDebug() << "Downloaded:" << ok; });
    }
});

// Block tracking domains
netCapture->blockURL("*://*.doubleclick.net/*");
netCapture->blockURL("*://*.google-analytics.com/*");
netCapture->blockURL("*://*.facebook.net/*");

// Set custom UA on all requests
netCapture->setExtraHTTPHeaders({
    {"User-Agent", "Mozilla/5.0 (compatible; MyBot/1.0)"},
    {"X-Custom-Header", "value"}
});

// Intercept and modify specific requests
netCapture->enableFetchInterception({"*://api.example.com/*"}, "Request",
    [](const QJsonObject& event) {
        const QJsonObject request = event.value("request").toObject();
        const QString url = request.value("url").toString();
        
        if (url.contains("/api/v1/")) {
            // Redirect v1 to v2
            return QJsonObject{
                {"action", "continue"},
                {"url", url.replace("/api/v1/", "/api/v2/")}
            };
        }
        return QJsonObject{{"action", "continue"}};
    });

// Throttle to 3G
netCapture->emulateNetworkConditions(false,  // offline
                                     562.5,   // latency (ms)
                                     1.6e6,   // download (1.6 Mbps)
                                     750e3);  // upload (750 Kbps)

// Disable cache for fresh data
netCapture->setCacheDisabled(true);
netCapture->setBypassServiceWorker(true);

// Capture WebSocket frames
connect(netCapture, &NetworkCapture::webSocketFrameReceived, 
        [](const QString& reqId, int opcode, bool masked, const QByteArray& payload) {
    qDebug() << "[WS<-]" << (opcode == 1 ? "text" : "binary") 
             << "size:" << payload.size();
    if (opcode == 1) qDebug() << "  data:" << QString::fromUtf8(payload);
});

connect(netCapture, &NetworkCapture::webSocketFrameSent,
        [](const QString& reqId, int opcode, bool masked, const QByteArray& payload) {
    qDebug() << "[WS->]" << (opcode == 1 ? "text" : "binary")
             << "size:" << payload.size();
});

// Capture SSE events
connect(netCapture, &NetworkCapture::eventSourceMessageReceived,
        [](const QString& reqId, const QString& eventName, const QString& eventId, const QString& data) {
    qDebug() << "[SSE]" << "event:" << eventName << "id:" << eventId
             << "data:" << data.left(100);
});

// Handle certificate errors (for self-signed dev servers)
netCapture->setOverrideCertificateErrors([](int eventId, const QString& errorType, const QUrl& url) {
    qDebug() << "Cert error:" << errorType << "for" << url.toString();
    // Allow self-signed for dev
    return url.host().endsWith(".local") || url.host() == "localhost";
});

// Export HAR at end
QTimer::singleShot(30000, [netCapture]() {
    netCapture->exportHar("capture.har");
    qDebug() << "HAR exported";
});
```

---

## 2.17 Edge Cases

### 2.17.1 Large Response Bodies

| Scenario | Behavior |
|---|---|
| Response > 20 MB (desktop) / 5 MB (Android) | LRU evicted from `NetworkResourcesData` — `getResponseBody` returns "Request content was evicted from inspector cache" |
| Response with `kDoNotBufferData` (e.g. media) | Bytes never enter `data_buffer_` — `getResponseBody` returns "No data found" |
| Streaming response (chunked transfer encoding) | Each chunk fires `Network.dataReceived`. Use `Network.streamResourceContent` to get live bytes via subsequent `dataReceived` events |
| Response delivered as a Blob | `DownloadedFileBlob` is set; `getResponseBody` reads asynchronously via `FileReaderLoader` (file-backed) |
| Durable messages enabled | Body lives in network service; `NetworkHandler::GetResponseBody` queries the collector first |
| Response with invalid UTF-8 | Falls back to base64 encoding in `getResponseBody` |

**For scraping large responses** (videos, images, large JSON):
1. Use `Network.streamResourceContent` for live streaming
2. Use `Fetch.enable` + `Fetch.takeResponseBodyAsStream` + `IO.read` for chunked reads (10MB chunks)
3. Use `enableDurableMessages=true` on `Network.enable` to offload to network service

### 2.17.2 POST Body Edge Cases

| Scenario | Behavior |
|---|---|
| POST with `Content-Type: application/x-www-form-urlencoded` | Body captured as raw bytes; UTF-8 decoded if possible |
| POST with `Content-Type: multipart/form-data` | Each `kData` element captured; `kEncodedBlob` elements read asynchronously; `kEncodedFile`/`kDataPipe` NOT supported (silently skipped) |
| POST body > `maxPostDataSize` | Not inlined into `requestWillBeSent`; must call `getRequestPostData` |
| POST body with binary data | UTF-8 decode fails → base64 encoded in `getRequestPostData` response |
| POST body preserved across 307/308 redirect | `data->PostData()` reused (not `DeepCopy`); preserved in the redirect chain |
| POST body across 301/302/303 redirect | Converted to GET (per HTTP spec); body NOT preserved |

### 2.17.3 Redirect Edge Cases

| Status Code | Method Change | Body Preservation |
|---|---|---|
| 301 (Moved Permanently) | POST → GET | Body NOT preserved |
| 302 (Found) | POST → GET | Body NOT preserved |
| 303 (See Other) | POST → GET (always) | Body NOT preserved |
| 307 (Temporary Redirect) | Method preserved | Body IS preserved |
| 308 (Permanent Redirect) | Method preserved | Body IS preserved |

### 2.17.4 Service Worker Edge Cases

| Scenario | Behavior |
|---|---|
| SW `event.respondWith(new Response(...))` | `fromServiceWorker: true`, `serviceWorkerResponseSource: "fallback-code"` |
| SW `event.respondWith(fetch(event.request))` (passthrough) | `fromServiceWorker: true`, `serviceWorkerResponseSource: "network"` |
| SW `event.respondWith(caches.match(event.request))` | `fromServiceWorker: true`, `serviceWorkerResponseSource: "cache-storage"` |
| SW doesn't call `respondWith` | Falls through to network; `fromServiceWorker: false` |
| `Network.setBypassServiceWorker(true)` | SW fetch event handler NEVER called; CacheStorage also bypassed |
| SW static routing API | `serviceWorkerRouterInfo` populated with `ruleIdMatched`, `matchedSourceType`, `actualSourceType` |

### 2.17.5 Cache Edge Cases

| Scenario | Behavior |
|---|---|
| Response served from HTTP cache | `fromDiskCache: true`, fires `Network.requestServedFromCache` instead of `requestWillBeSent` |
| Response served from memory cache (Blink) | `fromDiskCache: false`, but timings are zeroed |
| `Network.setCacheDisabled(true)` | Evicts MemoryCache, sets `kBypassCache` on every request, forces CORS preflight |
| Response served from prefetch cache | `fromPrefetchCache: true` |
| 304 Not Modified | Body pulled from cache via `NetworkResponseListener.#getCacheInformation()` |

### 2.17.6 CORS Edge Cases

| Scenario | CDP Event |
|---|---|
| CORS preflight fails | `loadingFailed` with `corsErrorStatus: {corsError: "DisallowedByMode", failedParameter: "..."}` |
| CORS preflight succeeds, actual request fails | `loadingFailed` with `corsErrorStatus` |
| CORS warning (not failure) | `Audits.issueAdded` with `CorsIssueDetails` |
| Local network access (private network) | `Audits.issueAdded` with `InsecureLocalNetwork` issue |

### 2.17.7 WebSocket Edge Cases

| Scenario | Behavior |
|---|---|
| Large WS frame | Inlined in `webSocketFrameReceived` (no streaming) |
| Binary frame | base64 encoded in `payloadData` |
| Text frame with invalid UTF-8 | `webSocketFrameError` fires |
| WS close with code/reason | NOT exposed in CDP (only `webSocketClosed` with requestId + timestamp) |
| WS handshake fails | `loadingFailed` for the underlying HTTP request |
| `Fetch.enable` doesn't intercept WS | WebSocket handshakes bypass `URLLoaderFactory` proxy |

### 2.17.8 Out-of-Order Events

The CDP protocol explicitly documents that `requestWillBeSent` and `requestWillBeSentExtraInfo` (and `responseReceived` vs `responseReceivedExtraInfo`) can arrive in **either order**. Your client must:
1. Buffer both events keyed by `requestId`
2. Merge them when both arrive
3. Never assume `requestWillBeSent` arrives before `requestWillBeSentExtraInfo`

### 2.17.9 Request ID Stability

- The `requestId` is **stable** across redirects (same ID for the original and redirected request)
- The `requestId` is **NOT** the same as the Fetch domain's `requestId` (Fetch has its own interception ID)
- The `networkId` field in `Fetch.requestPaused` correlates the two
- For redirects, `redirectedRequestId` in `Fetch.requestPaused` points to the previous hop's ID

---

## 2.18 Performance Impact

### 2.18.1 Memory Overhead

| Setting | Default | Memory Cost |
|---|---|---|
| `maxTotalBufferSize` | 200 MB (desktop) / 10 MB (Android) | Total cap on captured response bodies |
| `maxResourceBufferSize` | 20 MB (desktop) / 5 MB (Android) | Per-resource cap |
| `maxPostDataSize` | 0 (no inline) | If set, inlines POST bodies up to this size |
| `enableDurableMessages` | false | If true, offloads to network service (less renderer memory) |

### 2.18.2 CPU Overhead

| Operation | Cost |
|---|---|
| `Network.enable` (no agents active) | Near-zero (one bool check per probe) |
| `Network.enable` (with body capture) | ~5-10% overhead on page load (due to `SegmentedBuffer` copies) |
| `Network.enable` with `enableDurableMessages` | ~3-5% overhead (network service handles storage) |
| `Fetch.enable` (interception active) | ~10-15% overhead (every request paused + resumed) |
| `Network.streamResourceContent` per request | ~5% overhead (binary data in `dataReceived` events) |
| `Network.setExtraHTTPHeaders` | Near-zero (header merge on every request) |
| `Network.emulateNetworkConditions` | Latency simulation adds wall-clock delay; throughput throttling uses CPU to pace |

### 2.18.3 Optimization Tips for Scraping

1. **Use durable messages** for large captures — offloads storage to network service
2. **Use `Network.streamResourceContent` selectively** — only for requests you actually need bodies for
3. **Disable body capture for media** — set `kDoNotBufferData` (resource type aware)
4. **Use `Fetch.enable` with specific patterns** — don't intercept everything
5. **Batch `getResponseBody` calls** — they're async; parallelize
6. **Avoid `document.cookie` in loops** — use `Network.getCookies` (async, doesn't block JS)
7. **Use pipe transport instead of WebSocket** for high-throughput scraping (CBOR binary protocol, no JSON parsing overhead)
8. **Limit `maxPostDataSize`** to what you need (1MB is plenty for most form data)
9. **Evict old requests** from your client-side map — don't keep 100,000 CapturedRequest objects in memory
10. **Use `Network.emulateNetworkConditionsByRule`** for per-URL throttling — avoids global slowdown

---

## 2.19 Security & Privacy Impact

### 2.19.1 What CDP Network Domain Can Access

A CDP client with `Network.enable` can:
- Read ALL request and response headers (including `Authorization`, `Cookie`, `Set-Cookie`)
- Read ALL request and response bodies (including HttpOnly cookies in `Set-Cookie`)
- Read the TLS certificate chain for any HTTPS request
- See the remote IP address and port (post-DNS-resolution)
- Intercept and modify ANY request before it's sent
- Intercept and modify ANY response before the page sees it
- Block any URL by pattern
- Throttle network speed
- Disable cache and bypass service workers
- See all WebSocket frames (sent and received)
- See all SSE events
- Override certificate errors (accept self-signed certs)

### 2.19.2 Detection of CDP Network Interception

A sophisticated anti-bot script can detect CDP-based network interception:

1. **`Fetch.enable` adds latency** — every request is paused then resumed; measurable timing difference
2. **Modified headers appear in `fetch()`'s `Request.headers`** — if you added `X-Custom: value`, the page can see it
3. **`Network.setUserAgentOverride` changes `navigator.userAgent`** — detectable if inconsistent with `navigator.platform` or UA-CH
4. **`Network.setCacheDisabled` causes re-fetches** — `Performance.getEntriesByType("resource")` will show fresh fetches instead of cache hits
5. **`Network.setBlockedURLs` causes `ERR_BLOCKED_BY_CLIENT`** — detectable via `fetch()` error
6. **`Network.emulateNetworkConditions` affects `navigator.connection`** — `effectiveType`, `rtt`, `downlink` will reflect the throttling
7. **`Fetch.fulfillRequest` synthesizes responses** — `Response.url` may not match the request URL; `Response.headers.get('Date')` may be wrong
8. **`Security.setIgnoreCertificateErrors(true)` disables bfcache** — measurable via `performance.getEntriesByType("navigation")[0].type`

### 2.19.3 Stealth Scraping Best Practices

For stealth scraping that avoids detection:

1. **Don't use `Fetch.enable` globally** — only intercept specific URL patterns you need
2. **Don't modify `User-Agent` via `Network.setUserAgentOverride` alone** — also override `navigator.platform`, `navigator.hardwareConcurrency`, `navigator.userAgentData` via `Emulation.setUserAgentOverride` with full `userAgentMetadata`
3. **Don't disable cache globally** — it causes re-fetches that look unnatural; instead, clear cache per-origin via `Storage.clearDataForOrigin`
4. **Don't block URLs via `Network.setBlockedURLs` if the page expects them** — use `Fetch.failRequest` with `errorReason: "TimedOut"` instead (looks more natural)
5. **Don't use `Security.setIgnoreCertificateErrors(true)` globally** — handle per-error via `Security.setOverrideCertificateErrors` + `Security.handleCertificateError`
6. **Match `Emulation.setNetworkEmulation` to the UA's claimed device** — if UA says mobile, throttle to mobile speeds; if UA says desktop, don't throttle
7. **Use `Page.addScriptToEvaluateOnNewDocument` in an isolated world** to patch `navigator` properties before page scripts run — this is harder to detect than CDP overrides
8. **Avoid `Network.streamResourceContent`** for every request — it adds overhead and the binary data in `dataReceived` can be detected via timing

---

## 2.20 Testing

### 2.20.1 Unit Tests

```cpp
#include <QtTest>
#include "NetworkCapture.h"

class TestNetworkCapture : public QObject {
    Q_OBJECT
private slots:
    void testEnableDisable();
    void testRequestResponseCapture();
    void testResponseBody();
    void testWebSocketCapture();
    void testSSECapture();
    void testRedirectCapture();
    void testBlockedURLs();
    void testFetchInterception();
    void testCertificateOverride();
    void testHARExport();
};

void TestNetworkCapture::testRequestResponseCapture() {
    NetworkCapture cap(QUrl("ws://127.0.0.1:9222/devtools/page/test"));
    
    QSignalSpy reqSpy(&cap, &NetworkCapture::requestWillBeSent);
    QSignalSpy respSpy(&cap, &NetworkCapture::responseReceived);
    QSignalSpy finSpy(&cap, &NetworkCapture::loadingFinished);
    
    cap.enable();
    
    // Trigger a navigation (via Page.navigate, not shown here)
    // ...
    
    QVERIFY(reqSpy.wait(10000));
    QCOMPARE(reqSpy.count(), 1);
    
    QVERIFY(respSpy.wait(10000));
    QCOMPARE(respSpy.count(), 1);
    
    QVERIFY(finSpy.wait(10000));
    QCOMPARE(finSpy.count(), 1);
    
    const CapturedRequest req = cap.request(reqSpy.takeFirst().at(0).value<CapturedRequest>().requestId);
    QVERIFY(!req.url.isEmpty());
    QVERIFY(req.status > 0);
}

void TestNetworkCapture::testWebSocketCapture() {
    NetworkCapture cap(QUrl("ws://127.0.0.1:9222/devtools/page/test"));
    cap.enable();
    
    QSignalSpy createdSpy(&cap, &NetworkCapture::webSocketCreated);
    QSignalSpy frameSentSpy(&cap, &NetworkCapture::webSocketFrameSent);
    QSignalSpy frameRecvSpy(&cap, &NetworkCapture::webSocketFrameReceived);
    
    // Page calls: new WebSocket("wss://echo.example.com").send("hello")
    // ...
    
    QVERIFY(createdSpy.wait(5000));
    QVERIFY(frameSentSpy.wait(5000));
    QVERIFY(frameRecvSpy.wait(5000));
    
    QCOMPARE(frameSentSpy.count(), 1);
    const QByteArray sentPayload = frameSentSpy.takeFirst().at(3).toByteArray();
    QCOMPARE(QString::fromUtf8(sentPayload), QString("hello"));
}

void TestNetworkCapture::testFetchInterception() {
    NetworkCapture cap(QUrl("ws://127.0.0.1:9222/devtools/page/test"));
    cap.enable();
    
    // Intercept API calls and mock the response
    cap.enableFetchInterception({"*://api.example.com/*"}, "Request",
        [](const QJsonObject& event) -> QJsonObject {
            const QString url = event.value("request").toObject().value("url").toString();
            if (url.contains("/users/me")) {
                // Mock the response
                return QJsonObject{
                    {"action", "fulfill"},
                    {"responseCode", 200},
                    {"responseHeaders", QJsonArray{
                        QJsonObject{{"name", "Content-Type"}, {"value", "application/json"}}
                    }},
                    {"body", QByteArray(R"({"id":123,"name":"mocked"})").toBase64().data()}
                };
            }
            return QJsonObject{{"action", "continue"}};
        });
    
    // Page calls fetch("https://api.example.com/users/me")
    // Should get the mocked response
    // ...
}
```

### 2.20.2 Integration Test: Full Capture Round-Trip

```cpp
void TestNetworkCapture::testHARExport() {
    NetworkCapture cap(QUrl("ws://127.0.0.1:9222/devtools/page/test"));
    cap.enable();
    
    // Navigate to a test page with various resources
    // (CSS, JS, images, XHR, WebSocket, SSE)
    // ...
    
    // Wait for all resources to load
    QTest::qWait(10000);
    
    // Export HAR
    QTemporaryFile harFile;
    harFile.open();
    cap.exportHar(harFile.fileName());
    
    // Verify HAR
    QJsonDocument doc = QJsonDocument::fromJson(harFile.readAll());
    const QJsonArray entries = doc.object().value("log").toObject().value("entries").toArray();
    
    QVERIFY(entries.size() > 5);  // at least 5 resources captured
    
    // Check each entry has required fields
    for (const QJsonValue& v : entries) {
        const QJsonObject entry = v.toObject();
        QVERIFY(entry.contains("request"));
        QVERIFY(entry.contains("response"));
        QVERIFY(entry.contains("timings"));
        QVERIFY(entry.contains("startedDateTime"));
    }
}
```

---

## 2.21 Roadmap: Unique Features That Beat Puppeteer/Playwright

Based on this analysis, here are network-management features you can build that existing tools lack:

### 2.21.1 "Network Replay" — Record and Replay Entire Sessions

```cpp
class NetworkReplay {
public:
    // Record all requests/responses to a file
    void startRecording(const QString& filepath);
    void stopRecording();
    
    // Replay a recorded session (with optional modifications)
    void replay(const QString& filepath,
               const QHash<QString, QString>& urlReplacements = {});
    
    // Match requests by URL+method+body hash
    void setMatchStrategy(MatchStrategy strategy);  // Exact, URLOnly, Fuzzy
};
```

### 2.21.2 "Smart Mock" — AI-Powered Response Mocking

```cpp
class SmartMock {
public:
    // Auto-generate mock responses based on schema
    void enableAutoMock(const QString& urlPattern);
    
    // Learn response patterns from real traffic
    void learnFromTraffic(int durationSeconds);
    
    // Generate mock responses with realistic data
    QJsonObject generateMockResponse(const QUrl& url, const QString& method);
};
```

### 2.21.3 "Network Diff" — Compare Network Traffic Between Sessions

```cpp
class NetworkDiff {
public:
    // Capture two sessions and diff them
    void captureSession(const QString& name);
    
    // Compare two captured sessions
    QList<NetworkDifference> diff(const QString& session1, const QString& session2);
    
    // Highlight: new requests, missing requests, changed headers,
    // changed response bodies, changed timings
};
```

### 2.21.4 "Request Inspector" — Real-Time Request Visualization

```cpp
class RequestInspector : public QAbstractTableModel {
public:
    // Columns: Method, URL, Status, Type, MIME, Protocol, Remote IP,
    // Size, Transferred, Time, Initiator, Waterfall
    int columnCount() const override { return 12; }
    
    // Filter by: URL pattern, status code, type, timing
    void setFilter(const NetworkFilter& filter);
    
    // Sort by any column
    void sortByColumn(int column, Qt::SortOrder order);
};
```

### 2.21.5 "CORS Proxy" — Transparent CORS Bypass

```cpp
class CorsProxy {
public:
    // Intercept all CORS-failing requests and proxy them
    void enable();
    
    // Inject Access-Control-Allow-Origin: * on responses
    void setAllowAllOrigins(bool allow);
    
    // Strip CORS headers from requests
    void setStripCorsHeaders(bool strip);
    
    // Log CORS errors for debugging
    void setCorsErrorLogger(std::function<void(const QJsonObject&)> logger);
};
```

### 2.21.6 "Bandwidth Profiler" — Per-Domain Traffic Analytics

```cpp
class BandwidthProfiler {
public:
    // Track bytes per domain
    void enable();
    
    // Get bandwidth usage per domain
    QHash<QString, qint64> bytesPerDomain() const;
    
    // Get bandwidth over time (per-second buckets)
    QList<qint64> bandwidthOverTime(int seconds) const;
    
    // Identify top bandwidth consumers
    QList<QPair<QString, qint64>> topConsumers(int count) const;
};
```

---

## 2.22 Summary Cheat Sheet

| Operation | CDP Command | Implementation File:Line |
|---|---|---|
| Enable network capture | `Network.enable` | `inspector_network_agent.cc:2415` + `network_handler.cc` |
| Get response body | `Network.getResponseBody` | `inspector_network_agent.cc:2490` |
| Stream response body | `Network.streamResourceContent` | `inspector_network_agent.cc:1697` |
| Get POST body | `Network.getRequestPostData` | `inspector_network_agent.cc:2883` |
| Set extra headers | `Network.setExtraHTTPHeaders` | `inspector_network_agent.cc:2436` + `network_handler.cc:2594` |
| Override UA | `Emulation.setUserAgentOverride` | `emulation_handler.cc:889` |
| Disable cache | `Network.setCacheDisabled` | `inspector_network_agent.cc:2660` + `network_handler.cc:2281` |
| Bypass SW | `Network.setBypassServiceWorker` | `inspector_network_agent.cc:2671` + `network_handler.cc:2696` |
| Throttle network | `Network.emulateNetworkConditions` | `network_handler.cc:2623` |
| Block URLs | `Network.setBlockedURLs` | `inspector_network_agent.cc:2509` |
| Enable interception | `Fetch.enable` | `fetch_handler.cc:194` |
| Continue request | `Fetch.continueRequest` | `fetch_handler.cc:427` |
| Mock response | `Fetch.fulfillRequest` | `fetch_handler.cc:371` |
| Fail request | `Fetch.failRequest` | `fetch_handler.cc:344` |
| Get intercepted body | `Fetch.getResponseBody` | `fetch_handler.cc:530` |
| Stream intercepted body | `Fetch.takeResponseBodyAsStream` | `fetch_handler.cc:540` |
| Ignore cert errors | `Security.setIgnoreCertificateErrors` | `security_handler.cc:168` |
| Handle cert error per-event | `Security.handleCertificateError` | `security_handler.cc:130` |
| Get TLS cert chain | `Network.getCertificate` | `inspector_network_agent.cc:2676` |

---

## End of Part 2

This concludes **Part 2: Network Tab** — approximately 12,000 words covering the complete request/response lifecycle, the dual-agent architecture, body capture mechanisms, the Fetch domain for interception, WebSocket tracking, SSE tracking, redirects, service worker interception, CORS errors, TLS/SSL details, HTTP/2 and HTTP/3 detection, remote IP/port capture, full Qt6 C++ implementation, edge cases, performance, security, testing, and unique features.

---

## What's Next?

**Part 3: Runtime.evaluate / V8 Inspector** (your #3 priority) will cover:
- The complete `Runtime.evaluate` call chain (CDP → V8InspectorSession → V8RuntimeAgentImpl → v8::debug::EvaluateGlobal)
- All 16 parameters (expression, contextId, returnByValue, awaitPromise, throwOnSideEffect, disableBreaks, timeout, etc.)
- RemoteObject serialization (4 wrap modes: kJson, kIdOnly, kPreview, kDeep)
- objectId generation and lifecycle (bind/release)
- Execution contexts (main world, isolated world, worker)
- Console API (console.log/info/warn/error/trace/table/group/count/time)
- Exception handling (sync + promise rejections)
- The InspectorInstrumentation system (CoreProbeSink + AgentRegistry + probe namespace)
- The InspectorSession (V8InspectorSession + DevToolsSession)
- V8 Inspector integration (V8InspectorImpl, V8InspectorClient)
- Debugger (setBreakpoint, pause-on-exceptions, async stacks)
- Full Qt6 C++ implementation of a `JavaScriptExecutor` class
- Edge cases, performance, security, testing
- Unique features (JS hot-reload, function hooking, isolated world scraping, etc.)

