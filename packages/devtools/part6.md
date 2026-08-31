# PART 6: BROWSER-LEVEL MANAGEMENT

## The Ultimate Qt6 WebEngine Scraping Browser Guide

*Exhaustive implementation reference — every target, every session, every transport to your scraper.*

---

## 6.1 The DevToolsAgentHost Hierarchy

### 6.1.1 The Base Interface

The public interface is `content::DevToolsAgentHost` (defined in `content/public/browser/devtools_agent_host.h`). The **implementation** base class is `DevToolsAgentHostImpl` (`browser/devtools/devtools_agent_host_impl.h:38-192`):

```cpp
class CONTENT_EXPORT DevToolsAgentHostImpl : public DevToolsAgentHost {
 public:
  static scoped_refptr<DevToolsAgentHostImpl> GetForId(const std::string& id);

  bool AttachClient(DevToolsAgentHostClient* client) override;
  bool DetachClient(DevToolsAgentHostClient* client) override;
  void DispatchProtocolMessage(DevToolsAgentHostClient* client,
                               base::span<const uint8_t> message) override;
  bool IsAttached() override;
  void InspectElement(RenderFrameHost* frame_host, int x, int y) override;
  std::string GetId() override;
  ...
 protected:
  virtual bool AttachSession(DevToolsSession* session);
  virtual void DetachSession(DevToolsSession* session);
  virtual void UpdateRendererChannel(bool force);
  DevToolsIOContext* GetIOContext() { return &io_context_; }
  DevToolsRendererChannel* GetRendererChannel() { return &renderer_channel_; }
  const std::vector<raw_ptr<DevToolsSession, VectorExperimental>>& sessions()
      const { return sessions_; }
 private:
  const std::string id_;
  std::vector<raw_ptr<DevToolsSession, VectorExperimental>> sessions_;
  base::flat_map<DevToolsAgentHostClient*, std::unique_ptr<DevToolsSession>>
      session_by_client_;
  DevToolsIOContext io_context_;
  DevToolsRendererChannel renderer_channel_;
  base::ProcessId process_id_ = base::kNullProcessId;
};
```

### 6.1.2 Concrete Subclasses

Static type strings are registered in `browser/devtools/devtools_agent_host_impl.cc:140-153`:

```cpp
const char DevToolsAgentHost::kTypeTab[]             = "tab";
const char DevToolsAgentHost::kTypePage[]            = "page";
const char DevToolsAgentHost::kTypeFrame[]          = "iframe";
const char DevToolsAgentHost::kTypeDedicatedWorker[]= "worker";
const char DevToolsAgentHost::kTypeSharedWorker[]   = "shared_worker";
const char DevToolsAgentHost::kTypeServiceWorker[]  = "service_worker";
const char DevToolsAgentHost::kTypeWorklet[]        = "worklet";
const char DevToolsAgentHost::kTypeBrowser[]        = "browser";
const char DevToolsAgentHost::kTypeGuest[]         = "webview";
const char DevToolsAgentHost::kTypeOther[]         = "other";
const char DevToolsAgentHost::kTypeAuctionWorklet[]="auction_worklet";
const char DevToolsAgentHost::kTypeAssistiveTechnology[] = "assistive_technology";
const char DevToolsAgentHost::kTypeBrowserUI[]      = "browser_ui";
```

| Class | Header | Created by | Type string |
|---|---|---|---|
| `RenderFrameDevToolsAgentHost` | `render_frame_devtools_agent_host.h:37` | `GetOrCreateFor(FrameTreeNode*)` | `page`, `iframe`, or `webview` |
| `WebContentsDevToolsAgentHost` | `web_contents_devtools_agent_host.h:19` | `GetOrCreateForTab(WebContents*)` | `tab` |
| `BrowserDevToolsAgentHost` | `browser_devtools_agent_host.h:13` | `CreateForBrowser(...)` / `CreateForDiscovery()` | `browser` |
| `ServiceWorkerDevToolsAgentHost` | `service_worker_devtools_agent_host.h:34` | `ServiceWorkerDevToolsManager` | `service_worker` |
| `SharedWorkerDevToolsAgentHost` | `shared_worker_devtools_agent_host.h` | `SharedWorkerDevToolsManager` | `shared_worker` |
| `DedicatedWorkerDevToolsAgentHost` | `dedicated_worker_devtools_agent_host.h` | `WorkerDevToolsManager` (token-keyed) | `worker` |
| `WorkletDevToolsAgentHost` | `worklet_devtools_agent_host.h` | created on demand in `DevToolsRendererChannel::ChildTargetCreated` | `worklet` / `auction_worklet` |
| `ForwardingAgentHost` | `forwarding_agent_host.h` | `DevToolsAgentHost::Forward(id, delegate)` for extensions | (any) |

### 6.1.3 Creating a Host for a WebContents / Frame / Worker

**WebContents (Tab target)** — `web_contents_devtools_agent_host.cc:139-178`:

```cpp
WebContentsDevToolsAgentHost* WebContentsDevToolsAgentHost::GetOrCreateFor(
    WebContents* web_contents) {
  if (auto* host = FindAgentHost(web_contents)) return host;
  return new WebContentsDevToolsAgentHost(web_contents);
}

WebContentsDevToolsAgentHost::WebContentsDevToolsAgentHost(WebContents* wc)
    : DevToolsAgentHostImpl(base::UnguessableToken::Create().ToString()),
      auto_attacher_(std::make_unique<AutoAttacher>()) {
  InnerAttach(wc);
  NotifyCreated();
}
```

**Frame (page or iframe) target** — `render_frame_devtools_agent_host.cc:215-224`:

```cpp
scoped_refptr<DevToolsAgentHost> RenderFrameDevToolsAgentHost::GetOrCreateFor(
    FrameTreeNode* frame_tree_node) {
  frame_tree_node = GetFrameTreeNodeAncestor(frame_tree_node);
  RenderFrameDevToolsAgentHost* result = FindAgentHost(frame_tree_node);
  if (!result) {
    result = new RenderFrameDevToolsAgentHost(
        frame_tree_node, frame_tree_node->current_frame_host());
  }
  return result;
}
```

Crucially, only **local roots** get their own `RenderFrameDevToolsAgentHost` — see `render_frame_devtools_agent_host.cc:120-125`:

```cpp
bool ShouldCreateDevToolsForNode(FrameTreeNode* ftn) {
  return !ftn->parent() ||
         (ftn->current_frame_host() &&
          RenderFrameDevToolsAgentHost::ShouldCreateDevToolsForHost(
              ftn->current_frame_host()));
}
// ...
bool RenderFrameDevToolsAgentHost::ShouldCreateDevToolsForHost(
    RenderFrameHostImpl* rfh) {
  DCHECK(rfh);
  return rfh->is_local_root();
}
```

This is the OOPIF model: same-origin subframes share the parent's RFH and have no agent host of their own; cross-origin subframes (local roots) get their own `RenderFrameDevToolsAgentHost` and appear as `type: "iframe"` targets.

### 6.1.4 How a Host Connects to a Renderer (Mojo Channel)

The owner-side handle for the renderer channel is `DevToolsRendererChannel` (`browser/devtools/devtools_renderer_channel.h:36-107`). The agent host owns one. Each `RenderFrameDevToolsAgentHost::UpdateRendererChannel` (`render_frame_devtools_agent_host.cc:1011-1041`) obtains a fresh `blink::mojom::DevToolsAgent` pending associated remote from the renderer and hands it to the channel:

```cpp
void RenderFrameDevToolsAgentHost::UpdateRendererChannel(bool force) {
  is_debugger_paused_ = false;
  is_debugger_pause_situation_recorded_ = false;

  if (force && frame_host_ && !render_frame_alive_ &&
      !did_try_to_initialize_prerender_primary_main_frame_) {
    bool did_try_to_initialize = false;
    if (MaybeInitializePrerenderPrimaryMainFrame(frame_host_,
                                                 &did_try_to_initialize)) {
      render_frame_alive_ = true;
    }
    did_try_to_initialize_prerender_primary_main_frame_ = did_try_to_initialize;
  }

  mojo::PendingAssociatedRemote<blink::mojom::DevToolsAgent> agent_remote;
  mojo::PendingAssociatedReceiver<blink::mojom::DevToolsAgentHost>
      host_receiver;
  if (frame_host_ && render_frame_alive_ && force) {
    mojo::PendingAssociatedRemote<blink::mojom::DevToolsAgentHost> host_remote;
    host_receiver = host_remote.InitWithNewEndpointAndPassReceiver();
    frame_host_->BindDevToolsAgent(
        std::move(host_remote),
        agent_remote.InitWithNewEndpointAndPassReceiver());
  }
  int process_id = frame_host_ ? frame_host_->GetProcess()->GetDeprecatedID()
                               : ChildProcessHost::kInvalidUniqueID;
  GetRendererChannel()->SetRendererAssociated(std::move(agent_remote),
                                              std::move(host_receiver),
                                              process_id, frame_host_);
  auto_attacher_->SetRenderFrameHost(frame_host_);
}
```

The channel then attaches every existing session to that agent (`browser/devtools/devtools_renderer_channel.cc:84-101`):

```cpp
void DevToolsRendererChannel::SetRendererInternal(
    blink::mojom::DevToolsAgent* agent,
    int process_id,
    RenderFrameHostImpl* frame_host,
    bool force_using_io) {
  ReportChildTargetsCallback();
  process_id_ = process_id;
  frame_host_ = frame_host;
  if (agent && child_target_created_callback_) {
    agent->ReportChildTargets(true /* report */, wait_for_debugger_,
                              base::DoNothing());
  }
  for (DevToolsSession* session : owner_->sessions()) {
    for (auto& pair : session->handlers())
      pair.second->SetRenderer(process_id_, frame_host_);
    session->AttachToAgent(agent, force_using_io);
  }
}
```

The renderer-side counterpart is `blink::DevToolsAgent` (`third_party/blink/renderer/core/inspector/devtools_agent.h:41-162`). Two binding modes — associated for frames (`BindReceiver`, line 219) and non-associated for workers (`BindReceiverForWorker`, line 197). Workers additionally use an `IOAgent` (`devtools_agent.cc:69-164`) that lives on the IO thread so `Debugger.pause` can interrupt a stuck worker.

### 6.1.5 How Messages Are Routed Between Browser and Renderer

`DevToolsAgentHostImpl::DispatchProtocolMessage` (`devtools_agent_host_impl.cc:353-359`) just forwards to the matching `DevToolsSession`:

```cpp
void DevToolsAgentHostImpl::DispatchProtocolMessage(
    DevToolsAgentHostClient* client,
    base::span<const uint8_t> message) {
  DevToolsSession* session = SessionByClient(client);
  if (session)
    session->DispatchProtocolMessage(message);
}
```

Inside `DevToolsSession::DispatchToAgent` (`browser/devtools/devtools_session.cc` around line 504-524 in the saved read), the message chooses between the **main session** Mojo pipe and the **IO session** Mojo pipe based on the method name (see §6.2.2). On the renderer side, `blink::DevToolsSession::IOSession::DispatchProtocolCommand` (`third_party/blink/renderer/core/inspector/devtools_session.cc:137-162`) routes interrupt-safe methods via `inspector_task_runner_->AppendTask(...)` (which interrupts V8) and JS-running methods via `AppendTaskDontInterrupt`.

---

## 6.2 DevToolsSession (Browser-Side)

### 6.2.1 What It Is

`content::DevToolsSession` (`browser/devtools/devtools_session.h:59-307`) is the **browser-side per-connection state machine** for one client (one WebSocket / one pipe / one flattened session). It owns:

- the **`UberDispatcher`** that routes CDP methods to browser-process handlers;
- the **Mojo pipes** to the renderer (`mojo::AssociatedRemote<blink::mojom::DevToolsSession>` + `io_session_`);
- the **handler map** (`HandlersMap` = `flat_map<string, unique_ptr<DevToolsDomainHandler>>`);
- a **list of child sessions** for the flattened protocol (`child_sessions_`);
- a **`session_state_cookie_`** (a `blink::mojom::DevToolsSessionStatePtr`) used for re-attach after cross-process navigation.

A session exists in one of two modes (`browser/devtools/devtools_session.h:79-82`):

```cpp
enum class Mode {
  kSupportsTabTarget,         // attached to a Tab target; auto-attach doesn't recurse into subframes
  kDoesNotSupportTabTarget,   // legacy / page-level
};
```

### 6.2.2 Incoming CDP Message Flow

The full dispatch pipeline (`browser/devtools/devtools_session.cc`, `DispatchProtocolMessage` and friends):

```cpp
void DevToolsSession::DispatchProtocolMessage(base::span<const uint8_t> message) {
  // (1) If client is binary, validate CBOR envelope. Else convert JSON->CBOR.
  if (client_->UsesBinaryProtocol()) {
    crdtp::StatusOr<size_t> status =
        crdtp::cbor::CheckCBORMessage(crdtp::SpanFrom(message));
    if (!status.ok()) { /* send parse error notification */ return; }
  }
  if (proxy_delegate_) { /* external session wants JSON */ ... return; }
  if (!client_->UsesBinaryProtocol()) {  // convert JSON -> CBOR
    crdtp::Status status = crdtp::json::ConvertJSONToCBOR(...);
    if (!status.ok()) { /* parse error */ return; }
    message = converted_cbor_message;
  }

  // (2) Build a crdtp::Dispatchable; install FallThrough callback.
  crdtp::Dispatchable dispatchable(crdtp::SpanFrom(message), std::string_view(),
      [cb = base::BindRepeating(&DevToolsSession::FallThrough,
                                weak_factory_.GetWeakPtr())](
          int call_id, crdtp::span<uint8_t> method,
          crdtp::span<uint8_t> message, std::string_view fallthrough_data) {
        cb.Run(call_id, method, message, fallthrough_data);
      });

  // (3) Flatten: if message carries a sessionId, dispatch to child session.
  if (!dispatchable.SessionId().empty()) {
    std::string session_id(dispatchable.SessionId().begin(),
                           dispatchable.SessionId().end());
    auto it = child_sessions_.find(session_id);
    if (it == child_sessions_.end()) {
      // SessionNotFound error response
      return;
    }
    it->second->DispatchProtocolMessageInternal(std::move(dispatchable), message);
    return;
  }
  DispatchProtocolMessageInternal(std::move(dispatchable), message);
}
```

`DispatchProtocolMessageInternal` checks for `Runtime.runIfWaitingForDebugger` (handled locally if the session is in browser-only or wait-for-debugger mode), then either delegates to the embedder's `DevToolsManagerDelegate::HandleCommand` (so Chrome can intercept), or directly calls `HandleCommandInternal`, which calls `dispatcher_->Dispatch(dispatchable)`.

The dispatcher walks the registered handlers; if a handler wants the message forwarded to the renderer, it returns `Response::FallThrough()` and `FallThrough` is invoked:

```cpp
void DevToolsSession::FallThrough(int call_id, crdtp::span<uint8_t> method,
                                  crdtp::span<uint8_t> message,
                                  std::string_view fallthrough_data) {
  if (browser_only_) {
    dispatcher_->SendMethodNotFound(call_id, method);
    return;
  }
  if (waiting_for_response_.contains(call_id)) {
    DispatchProtocolMessageToClient(/* duplicate id error */);
  }
  auto it = pending_messages_.emplace(pending_messages_.end(), call_id, method,
                                      message, std::string(fallthrough_data));
  if (suspended_sending_messages_to_agent_ &&
      ShouldSuspendDuringNavigation(method))
    return;
  DispatchToAgent(pending_messages_.back());
  waiting_for_response_[call_id] = it;
}
```

`DispatchToAgent` chooses between two Mojo pipes (`browser/devtools/devtools_session.cc:501-524`):

```cpp
void DevToolsSession::DispatchToAgent(const PendingMessage& message) {
  DCHECK(!browser_only_);
  // We send all messages on the IO channel for workers so that messages like
  // Debugger.pause don't get stuck behind other blocking messages.
  if (ShouldSendOnIO(crdtp::SpanFrom(message.method)) || use_io_session_) {
    if (io_session_) {
      io_session_->DispatchProtocolCommand(message.call_id, message.method,
                                            message.payload,
                                            message.fallthrough_data);
    }
  } else {
    if (session_) {
      session_->DispatchProtocolCommand(message.call_id, message.method,
                                        message.payload,
                                        message.fallthrough_data);
    }
  }
}
```

`ShouldSendOnIO` (top of the file) lists methods that must interrupt V8:

```cpp
bool ShouldSendOnIO(crdtp::span<uint8_t> method) {
  static auto* kEntries = new std::vector<crdtp::span<uint8_t>>{
      crdtp::SpanFrom("Debugger.getPossibleBreakpoints"),
      crdtp::SpanFrom("Debugger.getScriptSource"),
      crdtp::SpanFrom("Debugger.getStackTrace"),
      crdtp::SpanFrom("Debugger.pause"),
      crdtp::SpanFrom("Debugger.removeBreakpoint"),
      crdtp::SpanFrom("Debugger.resume"),
      crdtp::SpanFrom("Debugger.setBreakpoint"),
      crdtp::SpanFrom("Debugger.setBreakpointByUrl"),
      crdtp::SpanFrom("Debugger.setBreakpointsActive"),
      crdtp::SpanFrom("Emulation.setScriptExecutionDisabled"),
      crdtp::SpanFrom("Page.crash"),
      crdtp::SpanFrom("Performance.getMetrics"),
      crdtp::SpanFrom("Runtime.terminateExecution"),
  };
  ...
}
```

### 6.2.3 Responses and Events

Browser-process handlers call `SendProtocolResponse` / `SendProtocolNotification` on the `FrontendChannel` (which is `DevToolsSession`). These funnel through `DispatchProtocolMessageToClient`, which **appends the `sessionId` field** if non-root, then converts CBOR→JSON if the client is text-mode, then calls `client_->DispatchProtocolMessage(agent_host_, message)`:

```cpp
void DevToolsSession::DispatchProtocolMessageToClient(std::vector<uint8_t> message) {
  DCHECK(crdtp::cbor::IsCBORMessage(crdtp::SpanFrom(message)));
  if (!session_id_.empty()) {
    crdtp::Status status = crdtp::cbor::AppendString8EntryToCBORMap(
        crdtp::SpanFrom(kSessionId), crdtp::SpanFrom(session_id_), &message);
    DCHECK(status.ok()) << status.ToASCIIString();
  }
  if (!client_->UsesBinaryProtocol()) {
    std::vector<uint8_t> json;
    crdtp::Status status =
        crdtp::json::ConvertCBORToJSON(crdtp::SpanFrom(message), &json);
    DCHECK(status.ok()) << status.ToASCIIString();
    message = std::move(json);
  }
  client_->DispatchProtocolMessage(agent_host_, message);
}
```

Renderer-originating messages come back through `blink::mojom::DevToolsSessionHost::DispatchProtocolResponse` / `DispatchProtocolNotification` (`browser/devtools/devtools_session.cc:486-515`). Critically, those are routed through `DispatchProtocolResponseOrNotification`, which **validates** them and never lets a compromised renderer talk directly to the client (it copies shared-memory-backed BigBuffer to prevent TOCTOU).

### 6.2.4 Handler Availability for Untrusted Clients

`browser/devtools/devtools_session.h:240-261` lists the handler types allowed for **untrusted** clients (i.e. external renderer-driven extensions). Everything else (e.g. `BrowserHandler`, `StorageHandler`, `SystemInfoHandler`, `TracingHandler`) requires `client_->IsTrusted()`. The WebSocket and pipe transports are always trusted; renderer-driven extension sessions are not.

---

## 6.3 DevToolsHttpHandler — The WebSocket Server

### 6.3.1 Listening and Port Discovery

`DevToolsAgentHost::StartRemoteDebuggingServer` (`devtools_agent_host_impl.cc:206-217`) is invoked by `--remote-debugging-port=N`:

```cpp
void DevToolsAgentHost::StartRemoteDebuggingServer(
    std::unique_ptr<DevToolsSocketFactory> server_socket_factory,
    const base::FilePath& active_port_output_directory,
    const base::FilePath& debug_frontend_dir,
    RemoteDebuggingServerMode mode) {
  DevToolsManagerDelegate* delegate =
      DevToolsManager::GetInstance()->delegate();
  CHECK(delegate);
  SetDevToolsHttpHandler(std::make_unique<DevToolsHttpHandler>(
      delegate, std::move(server_socket_factory), active_port_output_directory,
      debug_frontend_dir, mode));
}
```

`DevToolsHttpHandler` constructor (`devtools_http_handler.cc:900-934`) spawns a dedicated `Chrome_DevToolsHandlerThread` of type `MessagePumpType::IO`, parses `--remote-allow-origins`, and posts `StartServerOnHandlerThread`. That thread (`devtools_http_handler.cc:267-320`) creates the `net::ServerSocket` via `DevToolsSocketFactory::CreateForHttpServer`, wraps it in a `net::HttpServer` inside a `ServerWrapper`, and prints to stderr:

```
DevTools listening on ws://127.0.0.1:9222/devtools/browser/<guid>
```

If `active_port_output_directory` is set, it writes `<port>\n<browser_guid>` to `DevToolsActivePort` file in the profile dir (so Telemetry / ChromeDriver can find it).

### 6.3.2 Origin Allow-List (DNS Rebinding Protection)

```cpp
bool RequestIsSafeToServe(const net::HttpServerRequestInfo& info) {
  // For browser-originating requests, serve only those that are coming from
  // pages loaded off localhost or fixed IPs.
  std::string header = info.GetHeaderValue("host");
  if (header.empty())
    return true;
  GURL url = GURL("https://" + header);
  return url.HostIsIPAddress() || net::IsLocalHostname(url.GetHost());
}
```

Plus the explicit Origin check at WebSocket upgrade (`devtools_http_handler.cc:811-830`):

```cpp
if (request.headers.count("origin") &&
    !remote_allow_origins_.count(request.headers.at("origin")) &&
    !remote_allow_origins_.count("*")) {
  const std::string& origin = request.headers.at("origin");
  const std::string message = base::StringPrintf(
      "Rejected an incoming WebSocket connection from the %s origin. "
      "Use the command line flag --remote-allow-origins=%s to allow "
      "connections from this origin or --remote-allow-origins=* to allow all "
      "origins.", origin.c_str(), origin.c_str());
  Send403(connection_id, message);
  LOG(ERROR) << message;
  return;
}
```

So the model is: (a) Host header must be IP/localhost (default binds 127.0.0.1), (b) Origin header (if present) must match `--remote-allow-origins`. **Both** must pass.

### 6.3.3 HTTP `/json` Endpoints

`ServerWrapper::OnHttpRequest` (`devtools_http_handler.cc:459-511`) routes:

- `/json` (and sub-paths) → `OnJsonRequest` on UI thread
- `/` → `OnDiscoveryPageRequest` (HTML discovery page)
- `/devtools/...` → static frontend resources (only if `bundles_resources_` or `debug_frontend_dir_` set)

`OnJsonRequest` (`devtools_http_handler.cc:583-722`) implements:

- `/json/version` → returns `Protocol-Version`, `WebKit-Version`, `Browser`, `User-Agent`, `V8-Version`, and `webSocketDebuggerUrl`:

```cpp
if (command == "version") {
  base::DictValue version;
  version.Set("Protocol-Version", DevToolsAgentHost::GetProtocolVersion());
  version.Set("WebKit-Version", GetWebKitVersion());
  version.Set("Browser", GetContentClient()->browser()->GetProduct());
  version.Set("User-Agent", GetContentClient()->browser()->GetUserAgent());
  version.Set("V8-Version", V8_VERSION_STRING);
  std::string host = info.GetHeaderValue("host");
  version.Set(kTargetWebSocketDebuggerUrlField,
              base::StringPrintf("ws://%s%s", host.c_str(), browser_guid_.c_str()));
  ...
  SendJson(connection_id, net::HTTP_OK, version, "");
  return;
}
```

- `/json/protocol` → decompresses the protocol JSON (only with `ENABLE_DEVTOOLS_FRONTEND`).
- `/json/list` (and `/json?for_tab`) → enumerates targets via `DevToolsManagerDelegate::RemoteDebuggingTargets(kTab|kFrame)` or fallback `DevToolsAgentHost::GetOrCreateAll()`. Filters out `tab`-type entries when `?for_tab` is absent.
- `/json/new?<url>` (PUT only) → `delegate_->CreateNewTarget(url, target_type, new_window=false)` returns a fresh page target descriptor (uses `DevToolsManagerDelegate`).
- `/json/activate/<id>` → `agent_host->Activate()`.
- `/json/close/<id>` → `agent_host->Close()`.

The descriptor returned is built by `SerializeDescriptor` (`devtools_http_handler.cc:1032-1060`):

```cpp
base::DictValue DevToolsHttpHandler::SerializeDescriptor(
    scoped_refptr<DevToolsAgentHost> agent_host, const std::string& host) {
  base::DictValue dictionary;
  std::string id = agent_host->GetId();
  dictionary.Set(kTargetIdField, id);
  std::string parent_id = agent_host->GetParentId();
  if (!parent_id.empty())
    dictionary.Set(kTargetParentIdField, parent_id);
  dictionary.Set(kTargetTypeField, agent_host->GetType());
  dictionary.Set(kTargetTitleField, base::EscapeForHTML(agent_host->GetTitle()));
  dictionary.Set(kTargetDescriptionField, agent_host->GetDescription());
  dictionary.Set(kTargetUrlField, agent_host->GetURL().spec());
  GURL favicon_url = agent_host->GetFaviconURL();
  if (favicon_url.is_valid())
    dictionary.Set(kTargetFaviconUrlField, favicon_url.spec());
  dictionary.Set(kTargetWebSocketDebuggerUrlField,
                 base::StringPrintf("ws://%s%s%s", host.c_str(),
                                    kPageUrlPrefix, id.c_str()));
  dictionary.Set(kTargetDevtoolsFrontendUrlField,
                 GetFrontendURLInternal(agent_host, id, host));
  return dictionary;
}
```

### 6.3.4 WebSocket Upgrade and Message Routing

`OnWebSocketRequest` (`devtools_http_handler.cc:811-881`) decides which target the connection will attach to based on the URL path:

```cpp
// (1) origin check above

// (2) approval-only mode: when --remote-debugging-port=ask
if (mode_ == RemoteDebuggingServerMode::kWithApprovalOnly) {
  if (base::StartsWith(request.path, kBrowserUrlPrefix, ...)) {
    delegate_->AcceptDebugging(
        base::BindOnce(&DevToolsHttpHandler::HandleDebuggingApproval,
                       weak_factory_.GetWeakPtr(), connection_id, request));
    return;
  }
  Send403(connection_id, "Connection rejected");
  return;
}

// (3) Browser target: /devtools/browser or /devtools/browser/<guid>
if (base::StartsWith(request.path, browser_guid_, ...)) {
  scoped_refptr<DevToolsAgentHost> browser_agent =
      DevToolsAgentHost::CreateForBrowser(
          thread_->task_runner(),
          base::BindRepeating(&DevToolsSocketFactory::CreateForTethering,
                              base::Unretained(socket_factory_.get())));
  connection_to_client_[connection_id] =
      std::make_unique<DevToolsAgentHostClientImpl>(
          thread_->task_runner(), server_wrapper_.get(), connection_id,
          browser_agent);
  AcceptWebSocket(connection_id, request);
  return;
}

// (4) Page target: /devtools/page/<targetId>
if (!base::StartsWith(request.path, kPageUrlPrefix, ...)) {
  Send404(connection_id); return;
}
std::string target_id = request.path.substr(strlen(kPageUrlPrefix));
scoped_refptr<DevToolsAgentHost> agent = DevToolsAgentHost::GetForId(target_id);
if (!agent) { Send500(connection_id, "No such target id: " + target_id); return; }

connection_to_client_[connection_id] =
    std::make_unique<DevToolsAgentHostClientImpl>(
        thread_->task_runner(), server_wrapper_.get(), connection_id, agent);
AcceptWebSocket(connection_id, request);
```

The glue is `DevToolsAgentHostClientImpl` (`devtools_http_handler.cc:325-387`) — it implements `DevToolsAgentHostClient`:

```cpp
class DevToolsAgentHostClientImpl : public DevToolsAgentHostClient {
  DevToolsAgentHostClientImpl(...) {
    agent_host_->AttachClient(this);  // attaches browser-side DevToolsSession
  }
  ~DevToolsAgentHostClientImpl() override {
    if (agent_host_) agent_host_->DetachClient(this);
  }
  void DispatchProtocolMessage(DevToolsAgentHost* agent_host,
                               base::span<const uint8_t> message) override {
    task_runner_->PostTask(FROM_HERE,
        base::BindOnce(&ServerWrapper::SendOverWebSocket,
                       base::Unretained(server_wrapper_), connection_id_,
                       std::string(message.begin(), message.end())));
  }
  void OnMessage(base::span<const uint8_t> message) {
    if (agent_host_) agent_host_->DispatchProtocolMessage(this, message);
  }
};
```

When a WebSocket message arrives, `ServerWrapper::OnWebSocketMessage` posts it to the UI thread which calls `client->OnMessage(...)` which calls `agent_host_->DispatchProtocolMessage(this, message)` — the bridge back into the CDP machinery described in §6.1 and §6.2.

---

## 6.4 DevToolsPipeHandler — The Stdio Alternative

### 6.4.1 When to Use

`--remote-debugging-pipe[=cbor]` invokes `DevToolsAgentHost::StartRemoteDebuggingPipeHandler` (`devtools_agent_host_impl.cc:220-235`):

```cpp
void DevToolsAgentHost::StartRemoteDebuggingPipeHandler(
    base::OnceClosure on_disconnect) {
  int read_fd = kReadFD;  // fd 3 — Chromium reserves these for the pipe
  int write_fd = kWriteFD; // fd 4
#if BUILDFLAG(IS_WIN)
  std::string io_pipes =
      base::CommandLine::ForCurrentProcess()->GetSwitchValueASCII(
          switches::kRemoteDebuggingIoPipes);
  if (!io_pipes.empty() && !AdoptPipes(io_pipes, read_fd, write_fd)) {
    std::move(on_disconnect).Run();
    return;
  }
#endif
  SetDevToolsPipeHandler(std::make_unique<DevToolsPipeHandler>(
      read_fd, write_fd, std::move(on_disconnect)));
}
```

On POSIX, `kReadFD=3` and `kWriteFD=4` (well-known inherited descriptors). On Windows, the parent passes serialized HANDLE values via `--remote-debugging-io-pipes=HANDLE_IN,HANDLE_OUT` and they are adopted via `_open_osfhandle`.

### 6.4.2 The Handler

The handler (`browser/devtools/devtools_pipe_handler.cc:396-427`) creates **only a browser target** — there is no per-page connection. It auto-attaches to whatever the client requests via `Target.attachToTarget` / `Target.setAutoAttach`:

```cpp
DevToolsPipeHandler::DevToolsPipeHandler(int read_fd, int write_fd,
                                         base::OnceClosure on_disconnect)
    : on_disconnect_(std::move(on_disconnect)),
      read_fd_(read_fd), write_fd_(write_fd) {
  browser_target_ = DevToolsAgentHost::CreateForBrowser(
      nullptr, DevToolsAgentHost::CreateServerSocketCallback());
  browser_target_->AttachClient(this);

  std::string str_mode = base::ToLowerASCII(
      base::CommandLine::ForCurrentProcess()->GetSwitchValueASCII(
          switches::kRemoteDebuggingPipe));
  mode_ = str_mode == "cbor" ? DevToolsPipeHandler::ProtocolMode::kCBOR
                             : DevToolsPipeHandler::ProtocolMode::kASCIIZ;

  switch (mode_) {
    case ProtocolMode::kASCIIZ:
      pipe_reader_ = std::make_unique<PipeReaderASCIIZ>(weak_factory_.GetWeakPtr(), read_fd_);
      pipe_writer_ = std::make_unique<PipeWriterASCIIZ>(write_fd_);
      break;
    case ProtocolMode::kCBOR:
      pipe_reader_ = std::make_unique<PipeReaderCBOR>(weak_factory_.GetWeakPtr(), read_fd_);
      pipe_writer_ = std::make_unique<PipeWriterCBOR>(write_fd_);
      break;
  }
  if (!pipe_reader_->Start() || !pipe_writer_->Start())
    Shutdown();
}
```

Two wire formats (`browser/devtools/devtools_pipe_handler.cc:290-390`):

- **ASCIIZ** (default): each CDP message is written as raw UTF-8 JSON followed by a NUL byte; reader scans for `\0` to delimit messages.
- **CBOR** (`--remote-debugging-pipe=cbor`): each message is a complete CBOR envelope with a tag-24 length prefix; reader peeks 8 bytes for the envelope header, then reads exactly the announced length. This is faster and avoids escaping.

Two dedicated threads (`DevToolsPipeHandlerReadThread`, `...WriteThread`) — both `MessagePumpType::IO`. Send-side writes are chunked at 64KB per `write()` call (`kWritePacketSize = 1 << 16`). Receive buffer is 100MB.

### 6.4.3 Key Differences vs WebSocket

| Feature | WebSocket (`--remote-debugging-port`) | Pipe (`--remote-debugging-pipe`) |
|---|---|---|
| Network exposure | Yes (default 127.0.0.1) | No — pure stdio |
| Origin check | Yes | Not applicable |
| Initial target | None (you choose via `/devtools/page/<id>` or `/devtools/browser/<guid>` URL path) | Always the browser target |
| Wire format | Text JSON frames | JSON+NUL or CBOR envelope |
| Latency | Higher (HTTP/WS framing, JSON) | Lower (no framing overhead, CBOR option) |
| Multiplexing | One connection per target (pre-flatten) or one connection with `sessionId` field | One connection with `sessionId` field |
| Use case | ChromeDriver, remote debugging, frontend | Browser process embedding (e.g. test harness, embedded scraping) |

For a Qt6 WebEngine scraping browser, **pipe is strongly recommended** — no port to listen on, no DNS-rebinding risk, no need to coordinate port discovery, CBOR available.

---

## 6.5 The Target CDP Domain

Implementation: `browser/devtools/protocol/target_handler.{h,cc}` (~2k lines). Wired by `RenderFrameDevToolsAgentHost::AttachSession`, `WebContentsDevToolsAgentHost::AttachSession`, and `BrowserDevToolsAgentHost::AttachSession` with one of three access modes (`browser/devtools/protocol/target_handler.h:37-46`):

```cpp
enum class AccessMode {
  kAutoAttachOnly,  // only setAutoAttach; no discovery
  kRegular,         // auto-attach + discovery
  kBrowser,         // also: exposeDevToolsProtocol, createBrowserContext, ...
};
```

### 6.5.1 `Target.getTargets`

```cpp
Response TargetHandler::GetTargets(
    std::unique_ptr<protocol::Array<protocol::Target::FilterEntry>> filter,
    std::unique_ptr<protocol::Array<Target::TargetInfo>>* target_infos) {
  if (access_mode_ == AccessMode::kAutoAttachOnly)
    return Response::ServerError(kNotAllowedError);
  std::unique_ptr<TargetFilter> passed_filter = filter || !discover_target_filter_
      ? TargetFilter::Create(std::move(filter)) : nullptr;
  const TargetFilter* effective_filter =
      passed_filter ? passed_filter.get() : discover_target_filter_.get();
  *target_infos = std::make_unique<protocol::Array<Target::TargetInfo>>();
  for (const auto& host : DevToolsAgentHost::GetOrCreateAll()) {
    if (effective_filter->Match(*host)) {
      (*target_infos)->emplace_back(BuildTargetInfo(host.get()));
    }
  }
  return Response::Success();
}
```

`DevToolsAgentHost::GetOrCreateAll` (`devtools_agent_host_impl.cc:179-203`) walks all WebContents + SharedWorkerDevToolsManager + ServiceWorkerDevToolsManager + DedicatedWorkerDevToolsAgentHost + RenderFrameDevToolsAgentHost.

`BuildTargetInfo` (`browser/devtools/protocol/target_handler.cc:96-139`) builds the JSON shape clients see:

```cpp
auto target_info = Target::TargetInfo::Create()
    .SetTargetId(host->GetId())
    .SetTitle(host->GetTitle())
    .SetUrl(host->GetURL().spec())
    .SetType(host->GetType())
    .SetAttached(host->IsAttached())
    .SetCanAccessOpener(host->CanAccessOpener())
    .Build();
if (!host->GetParentId().empty())   target_info->SetParentId(host->GetParentId());
if (!host->GetOpenerId().empty())   target_info->SetOpenerId(host->GetOpenerId());
if (!host->GetOpenerFrameId().empty()) target_info->SetOpenerFrameId(host->GetOpenerFrameId());
if (!host->GetParentFrameId().empty()) target_info->SetParentFrameId(host->GetParentFrameId());
if (host->GetBrowserContext())     target_info->SetBrowserContextId(host->GetBrowserContext()->UniqueId());
std::string subtype = host->GetSubtype();
if (!subtype.empty())              target_info->SetSubtype(subtype);
// Tab targets may carry embedder-supplied metadata:
if (host->GetType() == DevToolsAgentHost::kTypeTab) {
  if (auto embedder_data = delegate->GetTargetEmbedderData(host))
    if (!embedder_data->empty()) target_info->SetEmbedderData(std::move(embedder_data));
}
```

### 6.5.2 `Target.attachToTarget`

```cpp
Response TargetHandler::AttachToTarget(const std::string& target_id,
                                       std::optional<bool> flatten,
                                       std::string* out_session_id) {
  if (access_mode_ == AccessMode::kAutoAttachOnly)
    return Response::ServerError(kNotAllowedError);
  scoped_refptr<DevToolsAgentHost> agent_host =
      DevToolsAgentHost::GetForId(target_id);
  if (!agent_host) return Response::InvalidParams(kTargetNotFound);
  std::optional<std::string> session_id =
      Session::Attach(this, agent_host.get(), false, flatten.value_or(false));
  if (!session_id) return Response::ServerError(kNotAllowedError);
  *out_session_id = *session_id;
  return Response::Success();
}
```

`Session::Attach` (`browser/devtools/protocol/target_handler.cc:460-508`) is the heart of the flatten-or-not decision:

```cpp
static std::optional<std::string> Attach(TargetHandler* handler,
                                         scoped_refptr<DevToolsAgentHost> agent_host,
                                         bool waiting_for_debugger,
                                         bool flatten_protocol) {
  std::string id = base::UnguessableToken::Create().ToString();
  // We don't support or allow the non-flattened protocol when in binary mode.
  if (handler->root_session_->GetClient()->UsesBinaryProtocol()) {
    flatten_protocol = true;
  }
  auto session = base::WrapUnique(
      new Session(handler, agent_host, id, flatten_protocol));
  DevToolsAgentHostImpl* agent_host_impl =
      static_cast<DevToolsAgentHostImpl*>(agent_host.get());
  if (flatten_protocol) {
    using Mode = DevToolsSession::Mode;
    const Mode mode = agent_host_impl->GetSessionMode() == Mode::kSupportsTabTarget
                          ? Mode::kSupportsTabTarget
                          : handler->session_mode_;

    base::OnceClosure resume_callback;
    if (waiting_for_debugger)
      resume_callback = base::BindOnce(&Session::ResumeIfThrottled,
                                       base::Unretained(session.get()));
    DevToolsSession* devtools_session =
        handler->root_session_->AttachChildSession(
            id, agent_host_impl, session.get(), mode,
            std::move(resume_callback));
    if (!devtools_session) { session->agent_host_ = nullptr; return std::nullopt; }
    session->devtools_session_ = devtools_session;
  } else {
    if (!agent_host_impl->AttachClient(session.get())) {
      session->agent_host_ = nullptr;
      return std::nullopt;
    }
  }
  handler->attached_sessions_[id] = std::move(session);
  handler->frontend_->AttachedToTarget(id, BuildTargetInfo(agent_host.get()),
                                       waiting_for_debugger);
  return id;
}
```

Two paths:

- **Flatten** — calls `root_session_->AttachChildSession(...)` which creates a new `DevToolsSession` whose `session_id_` is non-empty. From then on, the client simply sends messages with `"sessionId": "<id>"` on the same WebSocket; the root session's `DispatchProtocolMessage` extracts the sessionId and dispatches to the child session (see §6.2.2). The `Target.attachedToTarget` event carries the new sessionId.
- **Non-flatten (legacy)** — calls `agent_host_impl->AttachClient(session.get())`. Now the client must use `Target.sendMessageToTarget(message, sessionId)` and listen for `Target.receivedMessageFromTarget` events. This path is **deprecated** and explicitly rejected in CBOR mode.

### 6.5.3 `Target.setAutoAttach`

```cpp
void TargetHandler::SetAutoAttach(
    bool auto_attach,
    bool wait_for_debugger_on_start,
    std::optional<bool> flatten,
    std::unique_ptr<protocol::Array<protocol::Target::FilterEntry>> filter,
    std::unique_ptr<SetAutoAttachCallback> callback) {
  if (access_mode_ == AccessMode::kBrowser && !flatten.value_or(false)) {
    callback->sendFailure(Response::InvalidParams(
        "Only flatten protocol is supported with browser level auto-attach"));
    return;
  }
  if (!auto_attach && filter && !filter->empty()) {
    callback->sendFailure(Response::InvalidParams(
        "Target filter should be empty when disabling auto-attach"));
    return;
  }
  auto_attach_target_filter_ =
      auto_attach ? TargetFilter::Create(std::move(filter)) : nullptr;
  if (auto_attach_target_filter_ && access_mode_ == AccessMode::kBrowser &&
      auto_attach_target_filter_->Match(DevToolsAgentHost::kTypeTab) &&
      auto_attach_target_filter_->Match(DevToolsAgentHost::kTypePage)) {
    callback->sendFailure(Response::InvalidParams(
        "Filter should not simultaneously allow \"tab\" and \"page\", "
        "page targets are attached via tab targets"));
    return;
  }
  SetAutoAttachInternal(
      auto_attach, wait_for_debugger_on_start, flatten.value_or(false),
      base::BindOnce(&SetAutoAttachCallback::sendSuccess, std::move(callback)));
}
```

`SetAutoAttachInternal` (`browser/devtools/protocol/target_handler.cc:894-919`) calls `TargetAutoAttacher::AddClient`/`RemoveClient`. The attacher is per-host: `BrowserAutoAttacher` for browser target, `FrameAutoAttacher` for frame targets, `WebContentsDevToolsAgentHost::AutoAttacher` for tab targets.

When a new candidate target appears, `TargetHandler::AutoAttach` is invoked:

```cpp
bool TargetHandler::AutoAttach(TargetAutoAttacher* source,
                               DevToolsAgentHost* host,
                               bool waiting_for_debugger) {
  DCHECK(host);
  DCHECK(auto_attach_target_filter_);
  if (!auto_attach_target_filter_->Match(*host)) return false;
  if (auto_attached_sessions_.contains(host)) return false;
  if (!auto_attach_service_workers_ &&
      host->GetType() == DevToolsAgentHost::kTypeServiceWorker) return false;
  std::optional<std::string> session_id =
      Session::Attach(this, host, waiting_for_debugger, flatten_auto_attach_);
  if (!session_id) return false;
  Session* session = attached_sessions_[*session_id].get();
  session->auto_attacher_id_ = reinterpret_cast<uintptr_t>(source);
  auto_attached_sessions_[host] = session;
  return true;
}
```

### 6.5.4 Target Events

- `Target.targetCreated` — emitted from `DevToolsAgentHostObserver::DevToolsAgentHostCreated` for every host that matches the discovery filter (`browser/devtools/protocol/target_handler.cc:1468-1480`):

```cpp
void TargetHandler::DevToolsAgentHostCreated(DevToolsAgentHost* host) {
  DCHECK(discover());
  if (!discover_target_filter_->Match(*host)) return;
  if (!reported_hosts_.contains(host)) {
    frontend_->TargetCreated(BuildTargetInfo(host));
    reported_hosts_.insert(host);
  }
}
```

- `Target.targetDestroyed` — emitted from `DevToolsAgentHostDestroyed` (`browser/devtools/protocol/target_handler.cc:1486-1492`):

```cpp
void TargetHandler::DevToolsAgentHostDestroyed(DevToolsAgentHost* host) {
  if (!reported_hosts_.contains(host)) return;
  frontend_->TargetDestroyed(host->GetId());
  reported_hosts_.erase(host);
}
```

- `Target.targetInfoChanged` — emitted from `DevToolsAgentHostNavigated`, `DevToolsAgentHostAttached`, `DevToolsAgentHostDetached`, plus the attacher's `TargetInfoChanged` callback (`browser/devtools/protocol/target_handler.cc:990-997`).
- `Target.targetCrashed` — emitted from `DevToolsAgentHostCrashed` (`browser/devtools/protocol/target_handler.cc:1502-1511`):

```cpp
frontend_->TargetCrashed(host->GetId(), TerminationStatusToString(status),
                         host->GetWebContents()
                             ? host->GetWebContents()->GetCrashedErrorCode() : 0);
```

- `Target.attachedToTarget` / `Target.detachedFromTarget` — emitted from `Session::Attach` / `Session::Detach` (see §6.5.2). The `attachedToTarget` event includes a `targetInfo` and `sessionId` and a `waitingForDebugger` flag.

### 6.5.5 `Target.sendMessageToTarget` (Legacy, Non-Flatten)

```cpp
Response TargetHandler::SendMessageToTarget(
    const std::string& message,
    std::optional<std::string> session_id,
    std::optional<std::string> target_id) {
  Session* session = nullptr;
  Response response =
      FindSession(std::move(session_id), std::move(target_id), &session);
  if (!response.IsSuccess()) return response;
  if (session->flatten_protocol_) {
    return Response::ServerError(
        "When using flat protocol, messages are routed to the target "
        "via the sessionId attribute.");
  }
  session->SendMessageToAgentHost(base::as_byte_span(message));
  return Response::Success();
}
```

So this method literally only exists for non-flattened mode. Note that **in flattened mode you simply send the message directly with `sessionId` set on the root WebSocket/pipe**; the dispatcher routes it.

### 6.5.6 `Target.detachFromTarget`

```cpp
Response TargetHandler::DetachFromTarget(std::optional<std::string> session_id,
                                         std::optional<std::string> target_id) {
  Session* session = nullptr;
  Response response =
      FindSession(std::move(session_id), std::move(target_id), &session);
  if (!response.IsSuccess()) return response;
  session->Detach(false);
  return Response::Success();
}
```

### 6.5.7 `Target.createTarget` and `Target.closeTarget`

```cpp
Response TargetHandler::CreateTarget(const std::string& url,
                                     std::optional<int> left, ..., std::string* out_target_id) {
  if (access_mode_ == AccessMode::kAutoAttachOnly)
    return Response::ServerError(kNotAllowedError);
  GURL gurl(url);
  if (gurl.is_empty()) gurl = GURL(url::kAboutBlankURL);
  // Hidden target path: only when remote debugging enabled, no window, no for_tab
  if (hidden.value_or(false)) {
    ...
    *out_target_id = hidden_target_manager_.CreateHiddenTarget(gurl, browser_context);
    return Response::Success();
  }
  DevToolsManagerDelegate* delegate = DevToolsManager::GetInstance()->delegate();
  if (!delegate) return Response::ServerError("Not supported");
  DevToolsManagerDelegate::TargetType target_type =
      for_tab.value_or(session_mode_ == DevToolsSession::Mode::kSupportsTabTarget)
          ? DevToolsManagerDelegate::kTab
          : DevToolsManagerDelegate::kFrame;
  scoped_refptr<DevToolsAgentHost> agent_host =
      delegate->CreateNewTarget(gurl, target_type, new_window.value_or(false));
  if (!agent_host) return Response::ServerError("Not supported");
  *out_target_id = agent_host->GetId();
  return Response::Success();
}
```

`Target.closeTarget` calls `agent_host->Close()` which for `RenderFrameDevToolsAgentHost::Close` calls `web_contents()->ClosePage()` (`render_frame_devtools_agent_host.cc:997-1003`).

### 6.5.8 `Target.exposeDevToolsProtocol`

For browser extensions. Spawns a `BrowserToPageConnector` (`browser/devtools/protocol/target_handler.cc:194-370`) that creates a *new* browser-level host behind the scenes (`BrowserDevToolsAgentHost::CreateForDiscovery()`), binds a JS-side binding `window.<bindingName>` via `Runtime.addBinding`, and forwards messages both ways (base64-encoded through `Runtime.evaluate`).

Only available in `AccessMode::kBrowser` (`browser/devtools/protocol/target_handler.cc:1301-1337`).

### 6.5.9 How Target Types Map to Renderer Processes

| `type` | Browser-side class | Renderer-side context |
|---|---|---|
| `page` | `RenderFrameDevToolsAgentHost` for outermost main FrameTreeNode | Blink main frame, LocalFrame |
| `iframe` | `RenderFrameDevToolsAgentHost` for a *local root* subframe | Blink OOPIF (separate process if `--site-per-process`) |
| `tab` | `WebContentsDevToolsAgentHost` (aggregates pages in the same WebContents) | (browser-side only; never has a renderer of its own) |
| `browser` | `BrowserDevToolsAgentHost` (singleton-ish; multiple if `CreateForBrowser` called repeatedly) | (browser-side only) |
| `worker` | `DedicatedWorkerDevToolsAgentHost` | Blink dedicated worker thread inside the parent renderer |
| `shared_worker` | `SharedWorkerDevToolsAgentHost` | Blink SharedWorker process |
| `service_worker` | `ServiceWorkerDevToolsAgentHost` | The ServiceWorker process |
| `worklet` | `WorkletDevToolsAgentHost` | RenderWorklet / AudioWorklet etc. inside parent renderer |
| `auction_worklet` | (similar) | FLEDGE auction worklet |

The mapping for child workers/worklets happens via `DevToolsRendererChannel::ChildTargetCreated` (`browser/devtools/devtools_renderer_channel.cc:164-241`) — the renderer calls back into the browser with a `mojo::PendingRemote<blink::mojom::DevToolsAgent>` for each new worker, and the browser creates the appropriate agent host and registers it.

---

## 6.6 The Emulation CDP Domain

Implementation is **split**: browser-side `browser/devtools/protocol/emulation_handler.{h,cc}` handles things that need access to `WebContents`/`RenderWidgetHost`; renderer-side `third_party/blink/renderer/core/inspector/inspector_emulation_agent.{h,cc}` handles things that need V8/Blink state. The browser-side handlers use `Response::FallThrough()` to forward the message to the renderer after applying the browser-side effect.

### 6.6.1 `Emulation.setDeviceMetricsOverride`

Browser-side (`browser/devtools/protocol/emulation_handler.cc:659-852`). Validates parameters, builds `blink::DeviceEmulationParams`:

```cpp
blink::DeviceEmulationParams params;
params.screen_type = mobile ? blink::mojom::EmulatedScreenType::kMobile
                            : blink::mojom::EmulatedScreenType::kDesktop;
params.screen_size = gfx::Size(screen_width.value_or(0), screen_height.value_or(0));
if (position_x.has_value() && position_y.has_value())
  params.view_position = gfx::Point(position_x.value_or(0), position_y.value_or(0));
params.device_scale_factor = device_scale_factor;
if (width > 0 || height > 0) params.view_size = gfx::Size(width, height);
params.scale = scale.value_or(1);
params.screen_orientation_type = orientationType;
params.screen_orientation_angle = orientationAngle;
if (content_display_feature) {
  params.viewport_segments =
      content_display_feature->ComputeViewportSegments(params.view_size);
}
if (device_posture) {
  params.device_posture = DevicePostureTypeFromString(device_posture->GetType()).value();
  SetDevicePostureOverride(std::move(device_posture));
}
if (mobile || (scrollbar_type && *scrollbar_type == ...::ScrollbarTypeEnum::Overlay)) {
  params.force_android_overlay_scrollbar = true;
}
if (viewport) {
  params.viewport_offset.SetPoint(viewport->GetX(), viewport->GetY());
  double dpfactor = device_scale_factor
      ? device_scale_factor / host_->GetRenderWidgetHost()->GetDeviceScaleFactor()
      : 1;
  params.viewport_scale = viewport->GetScale() * dpfactor;
  width = base::ClampRound(viewport->GetWidth() * params.viewport_scale);
  height = base::ClampRound(viewport->GetHeight() * params.viewport_scale);
}
bool size_changed = false;
if (!dont_set_visible_size.value_or(false) && width > 0 && height > 0) {
  if (GetWebContents())
    size_changed = GetWebContents()->SetDeviceEmulationSize(gfx::Size(width, height));
}
...
device_emulation_enabled_ = true;
device_emulation_params_ = params;
UpdateDeviceEmulationState();   // sends to renderer via mojo
return Response::FallThrough(); // renderer also applies
```

`UpdateDeviceEmulationState` walks every main-frame `RenderWidgetHostImpl` and calls `frame_widget->EnableDeviceEmulation(params, cache_behavior)` (`browser/devtools/protocol/emulation_handler.cc:1114-1151`):

```cpp
void EmulationHandler::UpdateDeviceEmulationStateForHost(
    RenderWidgetHostImpl* render_widget_host,
    const blink::mojom::DeviceEmulationCacheBehavior& cache_behavior) {
  auto& frame_widget = render_widget_host->GetAssociatedFrameWidget();
  if (!frame_widget) return;
  if (device_emulation_enabled_) {
    frame_widget->EnableDeviceEmulation(device_emulation_params_, cache_behavior);
  } else {
    frame_widget->DisableDeviceEmulation();
  }
}
```

**State storage**: `device_emulation_params_` and `device_emulation_enabled_` are member fields of `EmulationHandler`. Persisted across navigations via `SetRenderer` re-applying the params.

### 6.6.2 `Emulation.setUserAgentOverride`

Browser-side (`browser/devtools/protocol/emulation_handler.cc:889-1012`). Stores `user_agent_`, `accept_language_`, and optionally `user_agent_metadata_` (UA-CH). The header overrides are applied via `ApplyOverrides` (`browser/devtools/protocol/emulation_handler.cc:1234-1295`):

```cpp
void EmulationHandler::ApplyOverrides(net::HttpRequestHeaders* headers,
                                      bool* user_agent_overridden,
                                      bool* accept_language_overridden) {
  if (!user_agent_.empty())
    headers->SetHeader(net::HttpRequestHeaders::kUserAgent, user_agent_);
  *user_agent_overridden = !user_agent_.empty();
  if (!accept_language_.empty())
    headers->SetHeader(
        net::HttpRequestHeaders::kAcceptLanguage,
        net::HttpUtil::GenerateAcceptLanguageHeader(accept_language_));
  *accept_language_overridden = !accept_language_.empty();
  // prefers-color-scheme / prefers-reduced-motion / prefers-reduced-transparency
  // client hint overrides follow
  ...
}
```

The `user_agent_metadata_` is fed into `ApplyUserAgentMetadataOverrides`:

```cpp
bool EmulationHandler::ApplyUserAgentMetadataOverrides(
    std::optional<blink::UserAgentMetadata>* override_out) {
  if (user_agent_.empty()) return false;                       // No UA override → no CH-UA override
  *override_out = user_agent_metadata_;
  return true;
}
```

The UA-CH metadata includes `brand_version_list`, `brand_full_version_list`, `full_version`, `platform`, `platform_version`, `architecture`, `model`, `mobile`, `bitness`, `wow64`, `form_factors`. Anything not specified falls back to defaults from `GetContentClient()->browser()->GetUserAgentMetadata()`.

Then `Response::FallThrough()` forwards to `InspectorEmulationAgent::setUserAgentOverride` (`third_party/blink/renderer/core/inspector/inspector_emulation_agent.cc:938-1049`), which stores the override in `InspectorAgentState` (so it survives cross-process navigation), updates `navigator.userAgent`, `navigator.platform`, etc.

### 6.6.3 `Emulation.setLocaleOverride`

Renderer-side only. `third_party/blink/renderer/core/inspector/inspector_emulation_agent.cc:1051-1060`:

```cpp
protocol::Response InspectorEmulationAgent::setLocaleOverride(
    std::optional<String> maybe_locale) {
  String locale = maybe_locale.value_or(String());
  String error = LocaleController::instance().SetLocaleOverride(
      locale, locale_override_.Get().empty());
  if (!error.empty()) return protocol::Response::ServerError(error.Utf8());
  locale_override_.Set(locale);
  return protocol::Response::Success();
}
```

`LocaleController::SetLocaleOverride` reconfigures ICU for the renderer process, affecting `Intl.DateTimeFormat`, `NumberFormat`, `navigator.language`, etc.

### 6.6.4 `Emulation.setTimezoneOverride`

Renderer-side. `third_party/blink/renderer/core/inspector/inspector_emulation_agent.cc:1062-1089`:

```cpp
protocol::Response InspectorEmulationAgent::setTimezoneOverride(
    const String& timezone_id) {
  if (timezone_id.empty()) {
    timezone_override_.reset();
  } else {
    if (timezone_override_) {
      timezone_override_->change(timezone_id);
    } else {
      auto result = TimeZoneController::SetTimeZoneOverride(timezone_id);
      switch (result.status) {
        case TimeZoneController::TimeZoneOverrideStatus::kSuccess:
          if (result.handle) timezone_override_ = std::move(result.handle);
          break;
        case TimeZoneController::TimeZoneOverrideStatus::kAlreadyInEffect:
          return protocol::Response::ServerError("Timezone override is already in effect");
        case TimeZoneController::TimeZoneOverrideStatus::kInvalidTimezone:
          return protocol::Response::InvalidParams("Invalid timezone id");
      }
    }
  }
  timezone_id_override_.Set(timezone_id);
  return protocol::Response::Success();
}
```

`TimeZoneController::SetTimeZoneOverride` reconfigures ICU and V8's timezone for the renderer.

### 6.6.5 `Emulation.setGeolocationOverride`

Browser-side. `browser/devtools/protocol/emulation_handler.cc:572-618`:

```cpp
Response EmulationHandler::SetGeolocationOverride(
    std::optional<double> latitude, std::optional<double> longitude,
    std::optional<double> accuracy, std::optional<double> altitude,
    std::optional<double> altitude_accuracy, std::optional<double> heading,
    std::optional<double> speed) {
  if (!host_) return Response::InternalError();
  auto* geolocation_context = GetWebContents()->GetGeolocationContext();
  device::mojom::GeopositionResultPtr override_result;
  if (latitude.has_value() && longitude.has_value() && accuracy.has_value()) {
    auto position = device::mojom::Geoposition::New();
    position->latitude = latitude.value();
    position->longitude = longitude.value();
    position->accuracy = accuracy.value();
    if (altitude.has_value())         position->altitude = altitude.value();
    if (altitude_accuracy.has_value()) position->altitude_accuracy = altitude_accuracy.value();
    if (heading.has_value())          position->heading = heading.value();
    if (speed.has_value())            position->speed = speed.value();
    position->timestamp = base::Time::Now();
    if (!device::ValidateGeoposition(*position))
      return Response::ServerError("Invalid geolocation");
    override_result = device::mojom::GeopositionResult::NewPosition(std::move(position));
  } else {
    override_result = device::mojom::GeopositionResult::NewError(
        device::mojom::GeopositionError::New(
            device::mojom::GeopositionErrorCode::kPositionUnavailable,
            /*error_message=*/"", /*error_technical=*/""));
  }
  geolocation_context->SetOverride(std::move(override_result));
  geolocation_overridden_ = true;
  return Response::Success();
}
```

### 6.6.6 `Emulation.setIdleOverride`

Browser-side. `browser/devtools/protocol/emulation_handler.cc:557-570`:

```cpp
Response EmulationHandler::SetIdleOverride(bool is_user_active,
                                           bool is_screen_unlocked) {
  if (!host_) return Response::InternalError();
  host_->GetIdleManager()->SetIdleOverride(is_user_active, is_screen_unlocked);
  return Response::Success();
}
```

Affects `navigator.idle.query()` (Idle Detection API).

### 6.6.7 `Emulation.setScriptExecutionDisabled`

Renderer-side. `third_party/blink/renderer/core/inspector/inspector_emulation_agent.cc:402-412`:

```cpp
protocol::Response InspectorEmulationAgent::setScriptExecutionDisabled(bool value) {
  protocol::Response response = AssertPage();
  if (!response.IsSuccess()) return response;
  if (script_execution_disabled_.Get() == value) return response;
  script_execution_disabled_.Set(value);
  GetWebViewImpl()->GetDevToolsEmulator()->SetScriptExecutionDisabled(value);
  return response;
}
```

Note that this method is listed in `ShouldSendOnIO` (`browser/devtools/devtools_session.cc:42`) — it gets sent on the IO session so the renderer can be interrupted even if it's stuck in JS. This is the only `Emulation.*` method that uses the IO path.

### 6.6.8 `Emulation.setTouchEmulationEnabled`

Renderer-side. `third_party/blink/renderer/core/inspector/inspector_emulation_agent.cc:437-455`:

```cpp
protocol::Response InspectorEmulationAgent::setTouchEmulationEnabled(
    bool enabled, std::optional<int> max_touch_points) {
  protocol::Response response = AssertPage();
  if (!response.IsSuccess()) return response;
  int max_points = max_touch_points.value_or(1);
  if (max_points < 1 || max_points > WebTouchEvent::kTouchesLengthCap) {
    String msg = StrCat({"Touch points must be between 1 and ",
                         String::Number(static_cast<uint16_t>(WebTouchEvent::kTouchesLengthCap))});
    return protocol::Response::InvalidParams(msg.Utf8());
  }
  touch_event_emulation_enabled_.Set(enabled);
  max_touch_points_.Set(max_points);
  GetWebViewImpl()->GetDevToolsEmulator()->SetTouchEventEmulationEnabled(enabled, max_points);
  return response;
}
```

Browser-side analog: `Emulation.setEmitTouchEventsForMouse` (`browser/devtools/protocol/emulation_handler.cc:630-643`) — wires up mouse-to-touch synthesis.

### 6.6.9 `Emulation.setEmulatedMedia`

**Both** browser side and renderer side implement it. Browser side (`browser/devtools/protocol/emulation_handler.cc:1029-1059`) extracts the media-features into `prefers_color_scheme_`, `prefers_reduced_motion_`, `prefers_reduced_transparency_` (which then override client-hint HTTP headers via `ApplyOverrides`), then falls through to renderer (`third_party/blink/renderer/core/inspector/inspector_emulation_agent.cc:457-484`):

```cpp
protocol::Response InspectorEmulationAgent::setEmulatedMedia(
    std::optional<String> media,
    std::unique_ptr<protocol::Array<protocol::Emulation::MediaFeature>> features) {
  protocol::Response response = AssertPage();
  if (!response.IsSuccess()) return response;
  String media_value = media.value_or("");
  emulated_media_.Set(media_value);
  GetWebViewImpl()->GetPage()->GetSettings().SetMediaTypeOverride(media_value);
  // ... also updates CSS media features like prefers-color-scheme, prefers-reduced-motion
  // and (if "forced-colors: active") flips the system theme state.
  ...
}
```

### 6.6.10 `Emulation.setCPUThrottlingRate`

Renderer-side. `third_party/blink/renderer/core/inspector/inspector_emulation_agent.cc:594-601`:

```cpp
protocol::Response InspectorEmulationAgent::setCPUThrottlingRate(double rate) {
  protocol::Response response = AssertPage();
  if (!response.IsSuccess()) return response;
  cpu_throttling_rate_.Set(rate);
  scheduler::ThreadCPUThrottler::GetInstance()->SetThrottlingRate(rate);
  return response;
}
```

Throttles V8 and the renderer thread scheduler by `rate` (e.g. 4.0 = 4x slowdown). Browser-process is not throttled.

### 6.6.11 Other Emulation Commands (Browser Side, Brief Table)

| Method | Implementation file:line | Browser-side effect |
|---|---|---|
| `Emulation.canEmulate` | `emulation_handler.cc:645-657` | Returns true on non-Android |
| `Emulation.setVisibleSize` | `emulation_handler.cc:879-887` | `GetWebContents()->SetDeviceEmulationSize(...)` |
| `Emulation.setFocusEmulationEnabled` | `emulation_handler.cc:1014-1027` | `IncrementCapturerCount` — keeps page "focused" even when hidden |
| `Emulation.setSensorOverrideEnabled` / `setSensorOverrideReadings` | `emulation_handler.cc:359-444` | Creates a `ScopedVirtualSensorForDevTools` for the matching `device::mojom::SensorType` |
| `Emulation.setPressureSourceOverrideEnabled` | `emulation_handler.cc:487-519` | Compute Pressure API |
| `Emulation.setDevicePostureOverride` | `emulation_handler.cc:1153-1162` | `GetDevicePostureProvider()->OverrideDevicePostureForEmulation(posture_type)` |
| `Emulation.setDisplayFeaturesOverride` | `emulation_handler.cc:1175-1224` | `host_->GetView()->OverrideDisplayFeatureForEmulation(&feature)` |
| `Emulation.clearDeviceMetricsOverride` | `emulation_handler.cc:854-877` | Restores default view size, disables posture override |
| `Emulation.setAutoDarkModeOverride` | (renderer) `inspector_emulation_agent.cc:615-630` | `GetDevToolsEmulator()->SetAutoDarkModeOverride(...)` |
| `Emulation.setHardwareConcurrencyOverride` | (renderer) `inspector_emulation_agent.cc:1130-1134` | `navigator.hardwareConcurrency` |
| `Emulation.setNavigatorOverrides` | (renderer) `inspector_emulation_agent.cc` (sets `navigator_platform_override_`) | `navigator.platform` |
| `Emulation.setAutomationOverride` | (renderer) `inspector_emulation_agent.cc:1222-1228` | Sets `automation_override_` which is `|=`ed into the `navigator.webdriver` flag |
| `Emulation.setVirtualTimePolicy` | (renderer) `inspector_emulation_agent.cc:632-...` | Virtual time for testing |

---

## 6.7 The Browser CDP Domain

Implementation: `browser/devtools/protocol/browser_handler.{h,cc}`. Wired by both `BrowserDevToolsAgentHost` and `RenderFrameDevToolsAgentHost`. Methods present in this snapshot (per `protocol_config.json:21-23`):

```
getVersion, getHistograms, getHistogram, getBrowserCommandLine,
grantPermissions, setDownloadBehavior, cancelDownload, resetPermissions,
crash, crashGpuProcess, setPermission, getGlobalPrivacyControl, setGlobalPrivacyControl, addMockCamera
```

**Notably absent**: `Browser.setWindowBounds`, `Browser.getWindowBounds`, `Browser.getWindowForTarget`, `Browser.close`, `Browser.closeBrowserProxy`, `Browser.executeBrowserCommand`. These live in the Chrome-layer `BrowserHandler` (`chrome/browser/devtools/protocol/browser_handler.cc`), which is layered on top of content and is not in this snapshot. From Qt6 WebEngine you have two options: implement them in your `DevToolsManagerDelegate`, or just call the equivalent `Target.closeTarget`/etc.

### 6.7.1 `Browser.getVersion`

```cpp
Response BrowserHandler::GetVersion(std::string* protocol_version,
                                    std::string* product,
                                    std::string* revision,
                                    std::string* user_agent,
                                    std::string* js_version) {
  *protocol_version = DevToolsAgentHost::GetProtocolVersion();  // "1.3"
  *revision = CHROMIUM_GIT_REVISION;
  *product = GetContentClient()->browser()->GetProduct();
  *user_agent = GetContentClient()->browser()->GetUserAgent();
  *js_version = V8_VERSION_STRING;
  return Response::Success();
}
```

Returns:
```json
{
  "protocolVersion": "1.3",
  "product": "QtWebEngine/6.x Chrome/...",
  "revision": "@<git_hash>",
  "userAgent": "Mozilla/5.0 ...",
  "jsVersion": "12.x.x"
}
```

### 6.7.2 `Browser.getHistograms` / `Browser.getHistogram`

Returns UMA histograms optionally as delta-from-last-call. Useful for performance benchmarking.

### 6.7.3 `Browser.getBrowserCommandLine`

```cpp
Response BrowserHandler::GetBrowserCommandLine(
    std::unique_ptr<protocol::Array<std::string>>* arguments) {
  *arguments = std::make_unique<protocol::Array<std::string>>();
  base::CommandLine* command_line = base::CommandLine::ForCurrentProcess();
  // The commandline is potentially sensitive, only return it if it
  // contains kEnableAutomation.
  if (command_line->HasSwitch(switches::kEnableAutomation)) {
    for (const auto& arg : command_line->argv()) {
      (*arguments)->emplace_back(/* utf8 conversion */);
    }
    return Response::Success();
  } else {
    return Response::ServerError(
        "Command line not returned because --enable-automation not set.");
  }
}
```

### 6.7.4 `Browser.grantPermissions` / `Browser.setPermission` / `Browser.resetPermissions`

Calls `PermissionControllerImpl::GrantPermissionOverrides` / `SetPermissionOverride` / `ResetPermissionOverrides`. Honors per-browser-context permissions. See `browser/devtools/protocol/browser_handler.cc:394-556`.

### 6.7.5 `Browser.crash` / `Browser.crashGpuProcess`

```cpp
Response BrowserHandler::Crash() {
  base::ImmediateCrash();
}
Response BrowserHandler::CrashGpuProcess() {
  auto* host = GpuProcessHost::Get();
  if (host) host->gpu_service()->Crash();
  return Response::Success();
}
```

### 6.7.6 `Browser.setDownloadBehavior` / `Browser.cancelDownload`

Drives `DevToolsDownloadManagerDelegate` — `allow`/`deny`/`allowAndName`/`default` behaviors with optional `downloadPath`. Only available if `MayWriteLocalFiles()` returned true (i.e. trusted client).

### 6.7.7 `Browser.addMockCamera`

Only on browser target. Sets up a virtual camera via `MockCaptureDeviceController`.

### 6.7.8 `Browser.getGlobalPrivacyControl` / `setGlobalPrivacyControl`

Returns/sets the GPC flag (Sec-GPC header). Browser target only.

---

## 6.8 The Security CDP Domain

Implementation: `browser/devtools/protocol/security_handler.{h,cc}` (small, ~196 lines). The methods present (per `protocol_config.json:181`): full Security domain — but `securityStateChanged` event has been **removed** (replaced by `Network.getSecurityIsolationStatus` for COEP/COOP/CSP info). What remains:

### 6.8.1 `Security.setOverrideCertificateErrors`

```cpp
Response SecurityHandler::SetOverrideCertificateErrors(bool override) {
  if (override) {
    if (!enabled_) return Response::ServerError("Security domain not enabled");
    if (cert_error_override_mode_ == CertErrorOverrideMode::kIgnoreAll)
      return Response::ServerError(
          "Certificate errors are already being ignored.");
    cert_error_override_mode_ = CertErrorOverrideMode::kHandleEvents;
  } else {
    cert_error_override_mode_ = CertErrorOverrideMode::kDisabled;
    FlushPendingCertificateErrorNotifications();
  }
  return Response::Success();
}
```

The `CertErrorOverrideMode` enum (`security_handler.h:75-77`):

```cpp
enum class CertErrorOverrideMode { kDisabled, kHandleEvents, kIgnoreAll };
```

### 6.8.2 `Security.setIgnoreCertificateErrors`

A simpler "ignore everything" mode (added later, doesn't require per-event handling):

```cpp
Response SecurityHandler::SetIgnoreCertificateErrors(bool ignore) {
  if (ignore) {
    if (cert_error_override_mode_ == CertErrorOverrideMode::kHandleEvents)
      return Response::ServerError("Certificate errors are already overridden.");
    cert_error_override_mode_ = CertErrorOverrideMode::kIgnoreAll;
  } else {
    cert_error_override_mode_ = CertErrorOverrideMode::kDisabled;
  }
  return Response::Success();
}
```

### 6.8.3 `Security.certificateError` Event

Emitted from `SecurityHandler::NotifyCertificateError` (`security_handler.cc:84-107`) which is called by the SSL layer on every cert error:

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

The event payload (defined in `protocol/security.h` generated code) is:
```json
{ "method": "Security.certificateError",
  "params": { "eventId": <int>, "errorType": "<short error string>",
              "requestURL": "<url>" } }
```

### 6.8.4 `Security.handleCertificateError`

```cpp
Response SecurityHandler::HandleCertificateError(int event_id, const String& action) {
  if (!cert_error_callbacks_.contains(event_id))
    return Response::ServerError(String("Unknown event id: " + base::NumberToString(event_id)));
  content::CertificateRequestResultType type =
      content::CERTIFICATE_REQUEST_RESULT_TYPE_CANCEL;
  Response response = Response::Success();
  if (action == Security::CertificateErrorActionEnum::Continue) {
    type = content::CERTIFICATE_REQUEST_RESULT_TYPE_CONTINUE;
  } else if (action == Security::CertificateErrorActionEnum::Cancel) {
    type = content::CERTIFICATE_REQUEST_RESULT_TYPE_CANCEL;
  } else {
    response = Response::ServerError(String("Unknown Certificate Error Action: " + action));
  }
  std::move(cert_error_callbacks_[event_id]).Run(type);
  cert_error_callbacks_.erase(event_id);
  return response;
}
```

### 6.8.5 What about `Security.securityStateChanged`?

**Removed** from this snapshot. Modern replacement: `Network.getSecurityIsolationStatus(frame_id?)` returns `coep`, `coop`, `csp` (`browser/devtools/protocol/network_handler.cc:3964-3985`):

```cpp
DispatchResponse NetworkHandler::GetSecurityIsolationStatus(
    std::optional<String> frame_id,
    std::unique_ptr<protocol::Network::SecurityIsolationStatus>* out_info) {
  scoped_refptr<DevToolsAgentHostImpl> host =
      DevToolsAgentHostImpl::GetForId(host_id_);
  std::string id = frame_id.value_or("");
  auto maybe_coep = host->cross_origin_embedder_policy(id);
  auto maybe_coop = host->cross_origin_opener_policy(id);
  auto maybe_csp = host->content_security_policy(id);
  auto status = protocol::Network::SecurityIsolationStatus::Create().Build();
  if (maybe_coep) status->SetCoep(makeCrossOriginEmbedderPolicyStatus(*maybe_coep));
  if (maybe_coop) status->SetCoop(makeCrossOriginOpenerPolicyStatus(*maybe_coop));
  if (maybe_csp)  status->SetCsp(makeContentSecurityPolicyStatus(*maybe_csp));
  *out_info = std::move(status);
  return Response::Success();
}
```

And per-request security state is attached to `Network.requestWillBeSentExtraInfo` events as `clientSecurityState`.

### 6.8.6 `Security.enable` / `Security.disable`

`Enable` (`security_handler.cc:109-120`) calls `AssureTopLevelActiveFrame()` (top-level only), then `AttachToRenderFrameHost()` which calls `DidChangeVisibleSecurityState()` to emit an initial state. Note that there's no `Security.visibleSecurityState` event in this snapshot either — only `DidChangeVisibleSecurityState()` (a `WebContentsObserver` virtual) is called but no event is emitted by it. (The state was formerly sent as a `securityStateChanged` event with `state`, `explanations`, `mixedContentStatus`, `scheme`. In current Chromium, this state must be polled via `Network.getSecurityIsolationStatus`.)

---

## 6.9 SystemInfo CDP Domain

Implementation: `browser/devtools/protocol/system_info_handler.{h,cc}`. Only present on browser target — `browser/devtools/protocol/system_info_handler.cc:299-310` rejects with `"SystemInfo.getInfo is only supported on the browser target"`:

### 6.9.1 `SystemInfo.getInfo`

```cpp
void SystemInfoHandler::GetInfo(std::unique_ptr<GetInfoCallback> callback) {
  if (!is_browser_session_) {
    callback->sendFailure(Response::ServerError(
        "SystemInfo.getInfo is only supported on the browser target"));
    return;
  }
  // We will be able to get more information from the GpuDataManager.
  // Register a transient observer with it to call us back when the
  // information is available.
  new SystemInfoHandlerGpuObserver(std::move(callback));
}
```

`SystemInfoHandlerGpuObserver` waits for the GPU process to populate `GpuDataManagerImpl`, with a watchdog (`NOTREACHED` if it takes > `kGPUInfoWatchdogTimeoutMs`, 10s×OS×ASAN multipliers). When ready, `SendGetInfoResponse` (`system_info_handler.cc:175-238`) packs:

```cpp
auto gpu = GPUInfo::Create()
    .SetDevices(std::move(devices))               // GPUDevice[] (vendor, device, driver)
    .SetAuxAttributes(std::move(aux_attributes))  // processCrashCount, etc.
    .SetFeatureStatus(std::move(feature_status))  // 2d_canvas: enabled, etc.
    .SetDriverBugWorkarounds(std::move(driver_bug_workarounds))
    .SetVideoDecoding(std::move(decoding_profiles))
    .SetVideoEncoding(std::move(encoding_profiles))
    .Build();
base::CommandLine* command = base::CommandLine::ForCurrentProcess();
std::string command_string = command->GetCommandLineString();
callback->sendSuccess(std::move(gpu), gpu_info.machine_model_name,
                      gpu_info.machine_model_version, command_string);
```

Returns: `gpu` (object), `modelName`, `modelVersion`, `commandLine`.

### 6.9.2 `SystemInfo.getProcessInfo`

```cpp
void SystemInfoHandler::GetProcessInfo(
    std::unique_ptr<GetProcessInfoCallback> callback) {
  if (!is_browser_session_) {
    callback->sendFailure(Response::ServerError(
        "SystemInfo.getProcessInfo is only supported on the browser target"));
    return;
  }
  auto process_info =
      std::make_unique<protocol::Array<SystemInfo::ProcessInfo>>();
  AddBrowserProcessInfo(process_info.get());
  AddRendererProcessInfo(process_info.get());
  AddChildProcessInfo(process_info.get());
  callback->sendSuccess(std::move(process_info));
}
```

Each `ProcessInfo` has `id` (PID), `type` ("browser"/"renderer"/etc.), `cpuTime` (cumulative seconds).

---

## 6.10 Flattened Sessions — Modern CDP

### 6.10.1 Legacy (Pre-Flatten) Flow

1. Client attaches to browser target via WebSocket.
2. Client calls `Target.attachToTarget(targetId)` with `flatten: false` (or no flatten field). Receives `{sessionId: "<uuid>"}`.
3. For every message destined for that target, client calls `Target.sendMessageToTarget(message=..., sessionId=...)`. Server emits `Target.receivedMessageFromTarget(sessionId=..., message=...)` for every response/event.

This requires wrapping/unwrapping every message — slow, error-prone, and double-JSON-parses.

### 6.10.2 Flatten Flow

1. Client attaches to browser target.
2. Client calls `Target.setAutoAttach(autoAttach=true, waitForDebuggerOnStart=false, flatten=true)`. Or for explicit attach: `Target.attachToTarget(targetId, flatten=true)`.
3. Server emits `Target.attachedToTarget` event with `{sessionId, targetInfo, waitingForDebugger}`.
4. Client now sends CDP commands **directly on the same WebSocket/pipe** with an extra `"sessionId"` field. Responses/events come back with the same `"sessionId"` field.
5. No `Target.sendMessageToTarget` needed.

### 6.10.3 How `flatten=true` Is Implemented

In `TargetHandler::SetAutoAttach` (`target_handler.cc:1099-1128`) and `TargetHandler::AttachToTarget` (`target_handler.cc:1180-1199`), the `flatten` parameter is propagated to `Session::Attach` (`target_handler.cc:460-508`), which chooses one of two paths (see §6.5.2).

When `flatten_protocol_ == true`:

```cpp
DevToolsSession* devtools_session =
    handler->root_session_->AttachChildSession(
        id, agent_host_impl, session.get(), mode,
        std::move(resume_callback));
```

`DevToolsSession::AttachChildSession` (`browser/devtools/devtools_session.cc` around line 700 in the saved read) creates a new `DevToolsSession` with `session_id_ = id`, calls `agent_host->AttachInternal(session)`, registers the child in `child_sessions_`, and notifies observers.

Then when a message arrives at the root session, `DevToolsSession::DispatchProtocolMessage` extracts the `sessionId` field from the CBOR message (via `crdtp::Dispatchable::SessionId()`), looks it up in `child_sessions_`, and dispatches there. The child session then goes through `DispatchProtocolMessageInternal` which dispatches locally (browser-side handlers) or falls through to the renderer.

Responses/events from the child session are tagged with `sessionId` in `DevToolsSession::DispatchProtocolMessageToClient` (`browser/devtools/devtools_session.cc` ~line 446, see §6.2.3).

### 6.10.4 Why Flattening Is Better

- **Lower latency**: no `Target.sendMessageToTarget` wrapping step.
- **Simpler client**: one connection, one message format, one routing field.
- **CBOR-friendly**: avoids re-parsing JSON twice.
- **Better backpressure**: the `waiting_for_response_` map and pending message queue are per-session, so a slow child session doesn't block others.
- **Multiple targets, one connection**: critical for OOPIF / worker scraping where you may have 50+ targets.

**Always use `flatten: true` in new code.** This snapshot rejects `flatten: false` on the browser target entirely (`target_handler.cc:1105-1109`).

---

## 6.11 CDP and Automation Detection

### 6.11.1 `navigator.webdriver`

In current Chromium, `navigator.webdriver` is `true` when either:
1. The browser was launched with `--enable-automation`, OR
2. A CDP session attaches and the page has not opted out.

The renderer-side hook is `InspectorEmulationAgent::setAutomationOverride(bool enabled)` (`third_party/blink/renderer/core/inspector/inspector_emulation_agent.cc:1222-1228`):

```cpp
protocol::Response InspectorEmulationAgent::setAutomationOverride(bool enabled) {
  if (enabled) InnerEnable();
  automation_override_.Set(enabled);
  return protocol::Response::Success();
}
```

The override is applied via `ApplyAutomationOverride` (line 1270-1272):

```cpp
void InspectorEmulationAgent::ApplyAutomationOverride(bool& enabled) const {
  enabled |= automation_override_.Get();
}
```

So `Emulation.setAutomationOverride(false)` programmatically un-sets the webdriver flag on a per-target basis (it's an `|=` so it can only add, not remove — useful when CDP attaches and would otherwise set it to true). Conversely `setAutomationOverride(true)` forces it true even if you didn't pass `--enable-automation`.

### 6.11.2 `--enable-automation` Switch

Behavior in this snapshot:
- `Browser.getBrowserCommandLine` returns the actual command line **only if** `--enable-automation` was passed (`browser/devtools/protocol/browser_handler.cc:675-694`). So absence of this switch hides the command line from CDP clients.
- Setting `--enable-automation` triggers several Chrome-side effects (not in this snapshot but well documented elsewhere):
  - `navigator.webdriver = true`
  - The "Chrome is being controlled by automated test software" infobar
  - Disables some password manager features
  - Enables `Browser.getBrowserCommandLine`

### 6.11.3 Other Detectable Signals of CDP/Automation

For a scraping browser, the following are commonly tested by anti-bot systems:

1. **`navigator.webdriver`** — addressed above.
2. **`window.cdc_*` properties** — ChromeDriver injects these. Not present when driving CDP directly via Qt.
3. **CDP-induced re-attach of frames** — when DevTools attaches, certain pages (especially with `Runtime.addBinding` calls) can detect changes to `window` shape.
4. **`navigator.plugins.length === 0`** and `navigator.languages` inconsistencies — handled via `Emulation.setUserAgentOverride` + `setLocaleOverride`.
5. **`navigator.permissions.query` for `notifications`** — normally `denied` in headless/automation; `Browser.setPermission` can override.
6. **Chrome devtools-frontend presence in `window`** — `Runtime.evaluate` injects scripts that can be detected if they leak globals.
7. **Headless mode (`--headless=new`)** — has different screen dimensions, missing chrome APIs. `Emulation.setDeviceMetricsOverride` mitigates.
8. **Stack traces in errors** — `Runtime.evaluate` returns errors with `[native code]` markers that differ from real user actions.
9. **`Performance.now()` resolution** — timing attacks can detect throttled/synthetic timing.
10. **CPU throttling** — `Emulation.setCPUThrottlingRate` changes scheduler behavior that sophisticated JS can detect.
11. **`chrome.runtime` existence** — extension-only API.
12. **CDP-driven `Page.captureScreenshot` causes `requestAnimationFrame` to fire even in backgrounded tabs** — detectable.

### 6.11.4 How a Scraping Browser Hides These

* **Don't launch with `--enable-automation`.** Use `--remote-debugging-pipe` instead.
* Call `Emulation.setAutomationOverride(false)` immediately after attach (it `|=`s the flag, so calling with `false` removes the CDP-attached contribution).
* Match UA + UA-CH via `Emulation.setUserAgentOverride` with full `userAgentMetadata` (brands, fullVersionList, platform, architecture, model, bitness, mobile, form_factors).
* Match locale via `Emulation.setLocaleOverride`.
* Match timezone via `Emulation.setTimezoneOverride`.
* Set realistic `navigator.platform` via `Emulation.setNavigatorOverrides(platform)`.
* Set realistic `navigator.hardwareConcurrency` via `Emulation.setHardwareConcurrencyOverride`.
* Set device metrics via `Emulation.setDeviceMetricsOverride` matching the UA's claimed device.
* Disable any default extensions/plugins that would leak.
* Use a real browser user profile (cookies, history) so `navigator.storage.estimate()` returns realistic values.

---

## 6.12 Security & Privacy Implications of Using CDP

### 6.12.1 What CDP Can Access

**Everything.** A CDP client connected to a browser target can:
* Read all cookies, localStorage, IndexedDB across all profiles (`Storage.getCookies`, `Storage.getStorageBucketList`, etc.).
* Execute arbitrary JS in any page, in any frame, in any worker (`Runtime.evaluate`, `Runtime.callFunctionOn`, `Page.createIsolatedWorld`).
* Read all network requests including POST bodies (`Network.enable`, `Fetch.enable`).
* Override TLS certificate errors (§6.8.2).
* Read arbitrary local files via `DOM.setFileInputFiles` or `Page.printToPDF` output paths (if `MayWriteLocalFiles()` is true).
* Open new tabs/windows and dismiss permission prompts (`Browser.grantPermissions`).
* Crash the browser or GPU process (`Browser.crash`, `Browser.crashGpuProcess`).
* Read the entire command line (`Browser.getBrowserCommandLine` — gated on `--enable-automation`).
* Enumerate all running processes (`SystemInfo.getProcessInfo`).
* Read GPU info, machine model (`SystemInfo.getInfo`).
* Initiate downloads anywhere on the filesystem (`Browser.setDownloadBehavior`).
* Create isolated browser contexts (`Target.createBrowserContext`) for fingerprint isolation.

### 6.12.2 Origin Allow-List

* Default bind: `127.0.0.1` only.
* `--remote-debugging-address=0.0.0.0` exposes to all interfaces — **DANGEROUS**.
* Origin check (`browser/devtools/devtools_http_handler.cc:811-830`, see §6.3.2): if an `Origin` header is present on the WebSocket upgrade, it must match `--remote-allow-origins`. If you don't pass `--remote-allow-origins`, modern Chromium rejects every browser-originated WebSocket connection that includes an Origin header.
* Host header check (`RequestIsSafeToServe`, §6.3.2): the Host header must be an IP address or `localhost` (prevents DNS rebinding attacks where `evil.com` resolves to 127.0.0.1).

### 6.12.3 Risks of `--remote-debugging-address=0.0.0.0`

If you bind to `0.0.0.0`:
* Anyone on your LAN (or the internet if the host is public) can connect.
* The Origin check protects only against malicious **browser-side** origins, not against external clients that don't send Origin.
* Even with `--remote-allow-origins=*`, an external attacker who can hit the port can drive your browser.

For a scraping browser on a server, **never** bind to 0.0.0.0. Use the pipe transport.

### 6.12.4 Sandbox Escape Risk via CDP

CDP is effectively a sandbox escape channel:
* `Runtime.evaluate` runs JS in the renderer with the same privileges as the page (limited by renderer sandbox).
* `Browser.setDownloadBehavior` writes to disk in the **browser process**, which is outside the renderer sandbox.
* `IO.read` reads any stream registered in `DevToolsIOContext` — these are created by `Page.printToPDF`, `Network.getResponseBody`, etc. The streams are scoped to the session but a session that attached to the browser target can read all of them.
* `Target.exposeDevToolsProtocol` lets a page drive the **browser** CDP — so a page that convinced the browser to call `exposeDevToolsProtocol` for it can effectively promote itself to browser-level CDP. This is why it requires `AccessMode::kBrowser`.

Mitigations in this snapshot:
* `DevToolsSession::IsDomainAvailableToUntrustedClient<T>()` (`browser/devtools/devtools_session.h:240-261`) restricts which handlers an untrusted client can use — but the WebSocket and pipe transports are always trusted.
* `BrowserDevToolsAgentHost::AttachSession` (`browser/devtools/browser_devtools_agent_host.cc:223-225`) explicitly rejects untrusted clients:

```cpp
bool BrowserDevToolsAgentHost::AttachSession(DevToolsSession* session) {
  if (!session->GetClient()->IsTrusted())
    return false;
  session->SetBrowserOnly(true);
  ...
}
```

* `RenderFrameDevToolsAgentHost::OnNavigationRequestWillBeSent` (`render_frame_devtools_agent_host.cc:783-796`) detaches sessions that the client is no longer allowed to attach to (e.g. after navigating to a disallowed URL like `chrome://settings`).
* `MayAttachToURL(url, is_webui)` and `MayAttachToRenderFrameHost(rfh)` callbacks let the embedder restrict further.

---

## 6.13 File Locations Reference

| Component | File Path |
|---|---|
| DevToolsAgentHostImpl | `browser/devtools/devtools_agent_host_impl.cc` + `.h` |
| DevToolsManager | `browser/devtools/devtools_manager.cc` + `.h` |
| DevToolsSession (browser) | `browser/devtools/devtools_session.cc` + `.h` |
| DevToolsRendererChannel | `browser/devtools/devtools_renderer_channel.cc` + `.h` |
| DevToolsIOContext | `browser/devtools/devtools_io_context.cc` + `.h` |
| DevToolsHttpHandler | `browser/devtools/devtools_http_handler.cc` + `.h` |
| DevToolsPipeHandler | `browser/devtools/devtools_pipe_handler.cc` + `.h` |
| BrowserDevToolsAgentHost | `browser/devtools/browser_devtools_agent_host.cc` + `.h` |
| WebContentsDevToolsAgentHost | `browser/devtools/web_contents_devtools_agent_host.cc` + `.h` |
| RenderFrameDevToolsAgentHost | `browser/devtools/render_frame_devtools_agent_host.cc` + `.h` |
| FrameAutoAttacher | `browser/devtools/frame_auto_attacher.cc` + `.h` |
| TargetAutoAttacher | `browser/devtools/protocol/target_auto_attacher.cc` + `.h` |
| TargetHandler | `browser/devtools/protocol/target_handler.cc` + `.h` |
| EmulationHandler | `browser/devtools/protocol/emulation_handler.cc` + `.h` |
| BrowserHandler | `browser/devtools/protocol/browser_handler.cc` + `.h` |
| SecurityHandler | `browser/devtools/protocol/security_handler.cc` + `.h` |
| SystemInfoHandler | `browser/devtools/protocol/system_info_handler.cc` + `.h` |
| DevToolsDomainHandler (base) | `browser/devtools/protocol/devtools_domain_handler.cc` + `.h` |
| Protocol config | `browser/devtools/protocol_config.json` |
| DevToolsAgent (Blink) | `third_party/blink/renderer/core/inspector/devtools_agent.cc` + `.h` |
| DevToolsSession (Blink) | `third_party/blink/renderer/core/inspector/devtools_session.cc` + `.h` |
| InspectorEmulationAgent | `third_party/blink/renderer/core/inspector/inspector_emulation_agent.cc` + `.h` |
| Protocol definitions | `third_party/blink/public/devtools_protocol/browser_protocol.pdl` + `inspector_protocol.pdl` |

---

## 6.14 Class Diagram

```
                          ┌────────────────────────────┐
                          │   DevToolsAgentHost        │
                          │   (public interface)       │
                          └────────────┬───────────────┘
                                       │
                                       ▼
              ┌─────────────────────────────────────────────┐
              │   DevToolsAgentHostImpl                      │
              │   (browser/devtools/devtools_agent_host_    │
              │    impl.h:38)                                │
              │                                              │
              │   - id_: string                              │
              │   - sessions_: vector<DevToolsSession*>      │
              │   - session_by_client_:                      │
              │       map<Client*, unique_ptr<Session>>      │
              │   - io_context_: DevToolsIOContext           │
              │   - renderer_channel_:                       │
              │       DevToolsRendererChannel               │
              │                                              │
              │   + AttachClient(client)                     │
              │   + DetachClient(client)                     │
              │   + DispatchProtocolMessage(client, msg)     │
              │   + AttachSession(session)                   │
              │   + DetachSession(session)                   │
              └──────┬───────────────────────────────────────┘
                     │
       ┌─────────────┼─────────────┬─────────────┬─────────────┐
       ▼             ▼             ▼             ▼             ▼
┌────────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐
│ Render     │ │ Web      │ │ Browser  │ │ Service  │ │ Dedicated│
│ Frame      │ │ Contents │ │ DevTools │ │ Worker   │ │ Worker   │
│ DevTools   │ │ DevTools │ │ Agent    │ │ DevTools │ │ DevTools │
│ AgentHost  │ │ AgentHost│ │ AgentHost│ │ AgentHost│ │ AgentHost│
│            │ │          │ │          │ │          │ │          │
│ type:      │ │ type:    │ │ type:    │ │ type:    │ │ type:    │
│  "page" /  │ │  "tab"   │ │  "browser"│ │ "service │ │ "worker" │
│  "iframe"  │ │          │ │          │ │  _worker"│ │          │
└─────┬──────┘ └────┬─────┘ └────┬─────┘ └──────────┘ └──────────┘
      │              │            │
      │              │            │
      ▼              ▼            ▼
┌─────────────────────────────────────────────────────────────────┐
│ DevToolsSession (browser-side)                                  │
│ (browser/devtools/devtools_session.h:59)                        │
│                                                                 │
│ - dispatcher_: UberDispatcher                                   │
│ - session_: mojo::AssociatedRemote<blink::DevToolsSession>     │
│ - io_session_: mojo::AssociatedRemote<blink::DevToolsSession>  │
│ - handlers_: flat_map<string, unique_ptr<DevToolsDomainHandler>>│
│ - child_sessions_: map<string, unique_ptr<DevToolsSession>>    │
│ - session_state_cookie_: DevToolsSessionStatePtr                │
│ - session_id_: string (empty for root, non-empty for child)     │
│                                                                 │
│ + DispatchProtocolMessage(message)                              │
│ + FallThrough(call_id, method, message, data)                   │
│ + DispatchToAgent(message)                                       │
│ + AttachChildSession(id, host, session, mode)                   │
│ + DispatchProtocolMessageToClient(message)                       │
└─────────────────────────┬───────────────────────────────────────┘
                          │
              ┌───────────┴───────────┐
              │                       │
              ▼                       ▼
┌─────────────────────┐    ┌─────────────────────┐
│ DevToolsHttpHandler │    │ DevToolsPipeHandler  │
│ (WebSocket server)  │    │ (stdio pipe)         │
│                     │    │                     │
│ - server_socket_    │    │ - read_fd_ = 3      │
│ - thread_:          │    │ - write_fd_ = 4     │
│   Chrome_DevTools   │    │ - mode_: ASCIIZ or │
│   HandlerThread     │    │   CBOR              │
│ - remote_allow_     │    │ - read_thread_      │
│   origins_: set     │    │ - write_thread_     │
│                     │    │                     │
│ + OnHttpRequest     │    │ + OnMessage         │
│ + OnJsonRequest     │    │ + SendMessage       │
│ + OnWebSocketRequest│    │                     │
│ + /json/version     │    │                     │
│ + /json/list        │    │                     │
│ + /json/new         │    │                     │
│ + /json/activate    │    │                     │
│ + /json/close       │    │                     │
└─────────────────────┘    └─────────────────────┘


   ┌──────────────────────────────────────────────────┐
   │  DevToolsDomainHandler (base class)              │
   │  (browser/devtools/protocol/                     │
   │   devtools_domain_handler.h)                     │
   │                                                  │
   │  + SetRenderer(process_id, frame_host)           │
   │  + SetStoragePartition(partition)                │
   │  + SetDevToolsAgentHost(host)                     │
   └──────────────┬───────────────────────────────────┘
                  │
    ┌─────────────┼─────────────┬────────────┬────────────┐
    ▼             ▼             ▼            ▼            ▼
┌────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────────┐
│Target  │ │Emulation │ │Browser   │ │Security  │ │SystemInfo    │
│Handler │ │Handler   │ │Handler   │ │Handler   │ │Handler       │
│        │ │          │ │          │ │          │ │              │
│+GetTar-│ │+SetDevice│ │+GetVersion│ │+SetIgnore│ │+GetInfo      │
│ gets() │ │ Metrics()│ │+GetHisto-│ │ CertErr()│ │+GetProcess   │
│+Attach │ │+SetUserA-│ │ grams()  │ │+SetOver- │ │ Info()       │
│ ToTar- │ │ gent()   │ │+GetCmd-  │ │ rideCert │ │              │
│ get()  │ │+SetLocale│ │ Line()   │ │ Errs()   │ │              │
│+SetAut │ │+SetTimez │ │+GrantPer │ │+HandleC- │ │              │
│ oAttach│ │ one()    │ │ missions()│ │ ertErr()│ │              │
│+Create │ │+SetGeo() │ │+SetDown- │ │+CertErr  │ │              │
│ Target │ │+SetIdle()│ │ loadBeh()│ │ Event()  │ │              │
│+Close  │ │+SetScr-  │ │+Crash()  │ │          │ │              │
│ Target │ │ iptDis() │ │+CrashGpu │ │          │ │              │
│+Expose │ │+SetTouch │ │+SetGPC() │ │          │ │              │
│ DTP()  │ │+SetCPU() │ │          │ │          │ │              │
│+SetAuto│ │+SetEmMed │ │          │ │          │ │              │
│ mation │ │ ia()     │ │          │ │          │ │              │
└────────┘ └──────────┘ └──────────┘ └──────────┘ └──────────────┘
```

---

## 6.15 The Complete CDP Command & Event Reference

### 6.15.1 Target Domain Commands

| Command | Implementation | What it does |
|---|---|---|
| `Target.getTargets` | `target_handler.cc:1432` | List all targets (optional filter) |
| `Target.attachToTarget` | `target_handler.cc:1180` | Attach to a specific target by ID |
| `Target.setAutoAttach` | `target_handler.cc:1099` | Auto-attach to new targets (with flatten) |
| `Target.detachFromTarget` | `target_handler.cc:1217` | Detach from a target |
| `Target.createTarget` | `target_handler.cc:1339` | Open a new page/tab |
| `Target.closeTarget` | `target_handler.cc` | Close a target |
| `Target.sendMessageToTarget` | `target_handler.cc:1229` | Send message (legacy, non-flatten) |
| `Target.exposeDevToolsProtocol` | `target_handler.cc:1301` | Expose CDP to a page (for extensions) |
| `Target.createBrowserContext` | `target_handler.cc` | Create an isolated browser context |
| `Target.disposeBrowserContext` | `target_handler.cc` | Dispose a browser context |
| `Target.getBrowserContexts` | `target_handler.cc` | List all browser contexts |
| `Target.activateTarget` | `target_handler.cc` | Activate (focus) a target |
| `Target.getTargetInfo` | `target_handler.cc` | Get info for a single target |
| `Target.setAutoAttach` | `target_handler.cc:1099` | Auto-attach with filter + flatten |

### 6.15.2 Target Domain Events

| Event | When | Payload |
|---|---|---|
| `Target.targetCreated` | New target created | targetInfo{targetId, type, url, title, attached, ...} |
| `Target.targetDestroyed` | Target destroyed | targetId, crashed |
| `Target.targetInfoChanged` | Target info changed | targetInfo |
| `Target.targetCrashed` | Target crashed | targetId, status, errorCode |
| `Target.attachedToTarget` | Session attached | sessionId, targetInfo, waitingForDebugger |
| `Target.detachedFromTarget` | Session detached | sessionId, targetId |

### 6.15.3 Emulation Domain Commands

| Command | Implementation | What it does |
|---|---|---|
| `Emulation.setDeviceMetricsOverride` | `emulation_handler.cc:659` | Set viewport dimensions, DPR, mobile mode |
| `Emulation.clearDeviceMetricsOverride` | `emulation_handler.cc:854` | Restore default viewport |
| `Emulation.setUserAgentOverride` | `emulation_handler.cc:889` | Override UA + UA-CH + Accept-Language |
| `Emulation.setLocaleOverride` | `inspector_emulation_agent.cc:1051` | Override navigator.language + Intl |
| `Emulation.setTimezoneOverride` | `inspector_emulation_agent.cc:1062` | Override timezone |
| `Emulation.setGeolocationOverride` | `emulation_handler.cc:572` | Override geolocation |
| `Emulation.clearGeolocationOverride` | `emulation_handler.cc` | Restore real geolocation |
| `Emulation.setIdleOverride` | `emulation_handler.cc:557` | Override idle detection |
| `Emulation.clearIdleOverride` | `emulation_handler.cc` | Restore real idle state |
| `Emulation.setScriptExecutionDisabled` | `inspector_emulation_agent.cc:402` | Disable/enable JS execution |
| `Emulation.setTouchEmulationEnabled` | `inspector_emulation_agent.cc:437` | Enable touch emulation |
| `Emulation.setEmitTouchEventsForMouse` | `emulation_handler.cc:630` | Convert mouse to touch events |
| `Emulation.setEmulatedMedia` | `emulation_handler.cc:1029` + `inspector_emulation_agent.cc:457` | Override CSS media type + features |
| `Emulation.setCPUThrottlingRate` | `inspector_emulation_agent.cc:594` | Throttle CPU by N× |
| `Emulation.setNetworkEmulation` | (via Network.emulateNetworkConditions) | Throttle network |
| `Emulation.setVisibleSize` | `emulation_handler.cc:879` | Set visible view size |
| `Emulation.setFocusEmulationEnabled` | `emulation_handler.cc:1014` | Keep page "focused" even when hidden |
| `Emulation.setSensorOverrideEnabled` | `emulation_handler.cc:359` | Override device sensors |
| `Emulation.setSensorOverrideReadings` | `emulation_handler.cc` | Set sensor readings |
| `Emulation.setPressureSourceOverrideEnabled` | `emulation_handler.cc:487` | Override compute pressure |
| `Emulation.setDevicePostureOverride` | `emulation_handler.cc:1153` | Override device posture |
| `Emulation.setDisplayFeaturesOverride` | `emulation_handler.cc:1175` | Override display features (foldable) |
| `Emulation.setAutoDarkModeOverride` | `inspector_emulation_agent.cc:615` | Override auto-dark-mode |
| `Emulation.setHardwareConcurrencyOverride` | `inspector_emulation_agent.cc:1130` | Override navigator.hardwareConcurrency |
| `Emulation.setNavigatorOverrides` | `inspector_emulation_agent.cc` | Override navigator.platform |
| `Emulation.setAutomationOverride` | `inspector_emulation_agent.cc:1222` | Override navigator.webdriver |
| `Emulation.setVirtualTimePolicy` | `inspector_emulation_agent.cc:632` | Control virtual time |
| `Emulation.canEmulate` | `emulation_handler.cc:645` | Check if emulation is supported |

### 6.15.4 Browser Domain Commands

| Command | Implementation | What it does |
|---|---|---|
| `Browser.getVersion` | `browser_handler.cc:129` | Get protocol version, product, UA, JS version |
| `Browser.getHistograms` | `browser_handler.cc` | Get UMA histograms |
| `Browser.getHistogram` | `browser_handler.cc` | Get a single UMA histogram |
| `Browser.getBrowserCommandLine` | `browser_handler.cc:675` | Get command line (requires --enable-automation) |
| `Browser.grantPermissions` | `browser_handler.cc:394` | Grant permission overrides |
| `Browser.resetPermissions` | `browser_handler.cc` | Reset permission overrides |
| `Browser.setPermission` | `browser_handler.cc` | Set a single permission |
| `Browser.setDownloadBehavior` | `browser_handler.cc` | Set download behavior (allow/deny/default) |
| `Browser.cancelDownload` | `browser_handler.cc` | Cancel a download |
| `Browser.crash` | `browser_handler.cc` | Crash the browser |
| `Browser.crashGpuProcess` | `browser_handler.cc` | Crash the GPU process |
| `Browser.getGlobalPrivacyControl` | `browser_handler.cc` | Get GPC flag |
| `Browser.setGlobalPrivacyControl` | `browser_handler.cc` | Set GPC flag |
| `Browser.addMockCamera` | `browser_handler.cc` | Add a virtual camera |
| `Browser.setWindowBounds` | (Chrome-layer, not in this slice) | Set window position/size/state |
| `Browser.getWindowBounds` | (Chrome-layer, not in this slice) | Get window position/size/state |
| `Browser.getWindowForTarget` | (Chrome-layer, not in this slice) | Get window ID for a target |
| `Browser.close` | (Chrome-layer, not in this slice) | Close the browser |

### 6.15.5 Security Domain Commands & Events

| Command/Event | What it does |
|---|---|
| `Security.enable` | Enable Security domain (top-level frame only) |
| `Security.disable` | Disable Security domain |
| `Security.setOverrideCertificateErrors` | Enable per-event cert error handling |
| `Security.setIgnoreCertificateErrors` | Ignore all cert errors (blunt) |
| `Security.handleCertificateError` | Handle a specific cert error (continue/cancel) |
| **Event**: `Security.certificateError` | Fired when a cert error occurs (eventId, errorType, requestURL) |
| `Network.getSecurityIsolationStatus` | Get COEP/COOP/CSP status (replaces securityStateChanged) |

### 6.15.6 SystemInfo Domain Commands

| Command | What it does |
|---|---|
| `SystemInfo.getInfo` | Get GPU info, model name, model version, command line (browser target only) |
| `SystemInfo.getProcessInfo` | Get process list with CPU time (browser target only) |

---

## 6.16 Qt6 WebEngine C++ Implementation

### 6.16.1 The BrowserController Class

#### `BrowserController.h`

```cpp
#pragma once

#include <QObject>
#include <QWebSocket>
#include <QHash>
#include <QSet>
#include <QJsonObject>
#include <QJsonArray>
#include <QProcess>
#include <QTimer>
#include <functional>
#include <memory>
#include <optional>

// === Data structures ===

struct TargetInfo {
    QString targetId;
    QString type;                    // "page" | "iframe" | "tab" | "browser" | "worker" | "service_worker" | etc.
    QString title;
    QString url;
    QString parentId;
    QString openerId;
    QString openerFrameId;
    QString parentFrameId;
    QString browserContextId;
    QString subtype;
    bool attached = false;
    bool canAccessOpener = false;
};

struct DeviceMetrics {
    int width = 0;
    int height = 0;
    double deviceScaleFactor = 1.0;
    bool mobile = false;
    QString screenOrientationType;   // "portraitPrimary" | "landscapePrimary" | etc.
    int screenOrientationAngle = 0;
};

struct UserAgentOverride {
    QString userAgent;
    QString acceptLanguage;
    QString platform;
    QJsonObject userAgentMetadata;   // UA-CH brands, platform, model, etc.
};

struct CertificateError {
    int eventId;
    QString errorType;
    QUrl requestUrl;
};

struct SystemInfoGpu {
    QJsonArray devices;              // [{vendorId, deviceId, vendorString, deviceString}]
    QJsonObject auxAttributes;
    QString featureStatus;
    QJsonArray driverBugWorkarounds;
    QJsonArray videoDecoding;
    QJsonArray videoEncoding;
};

struct ProcessInfo {
    int id;                          // PID
    QString type;                    // "browser" | "renderer" | "gpu-process" | etc.
    double cpuTime;                  // cumulative seconds
};

enum class TransportMode {
    WebSocket,
    Pipe
};

class BrowserController : public QObject {
    Q_OBJECT
public:
    explicit BrowserController(TransportMode mode = TransportMode::Pipe,
                                QObject* parent = nullptr);
    ~BrowserController();
    
    // === Connection management ===
    bool connectToBrowser(const QUrl& url = QUrl("ws://127.0.0.1:9222/devtools/browser"));
    bool isConnected() const { return m_ws && m_ws->isValid(); }
    void disconnect();
    
    // === Target management ===
    void getTargets(std::function<void(const QList<TargetInfo>&)> callback);
    void attachToTarget(const QString& targetId, bool flatten = true,
                       std::function<void(const QString& sessionId)> callback = {});
    void detachFromTarget(const QString& sessionId);
    void setAutoAttach(bool autoAttach, bool waitForDebuggerOnStart, bool flatten,
                      std::function<void()> callback = {});
    void createTarget(const QUrl& url, bool newWindow = false, bool forTab = false,
                     std::function<void(const QString& targetId)> callback = {});
    void closeTarget(const QString& targetId);
    void activateTarget(const QString& targetId);
    
    // === Session management ===
    void setSessionId(const QString& sessionId) { m_sessionId = sessionId; }
    QString sessionId() const { return m_sessionId; }
    
    // === Emulation ===
    void setDeviceMetricsOverride(const DeviceMetrics& metrics,
                                  std::function<void()> callback = {});
    void clearDeviceMetricsOverride();
    
    void setUserAgentOverride(const UserAgentOverride& ua,
                             std::function<void()> callback = {});
    
    void setLocaleOverride(const QString& locale);
    void setTimezoneOverride(const QString& timezoneId);
    void setGeolocationOverride(double lat, double lon, double accuracy,
                               std::function<void()> callback = {});
    void clearGeolocationOverride();
    
    void setIdleOverride(bool isUserActive, bool isScreenUnlocked);
    void clearIdleOverride();
    
    void setScriptExecutionDisabled(bool disabled);
    void setTouchEmulationEnabled(bool enabled, int maxTouchPoints = 1);
    void setCPUThrottlingRate(double rate);
    void setEmulatedMedia(const QString& media, const QHash<QString, QString>& features);
    
    void setAutomationOverride(bool enabled);
    void setHardwareConcurrencyOverride(int count);
    void setNavigatorOverrides(const QString& platform);
    
    // === Browser commands ===
    void getVersion(std::function<void(const QString& protocolVersion,
                                       const QString& product,
                                       const QString& userAgent,
                                       const QString& jsVersion)> callback);
    void getBrowserCommandLine(std::function<void(const QStringList&)> callback);
    void grantPermissions(const QString& origin, const QStringList& permissions);
    void resetPermissions(const QString& origin);
    void crash();
    void crashGpuProcess();
    
    // === Security ===
    void setIgnoreCertificateErrors(bool ignore);
    void setOverrideCertificateErrors(bool override);
    void handleCertificateError(int eventId, bool continueLoading);
    
    // === System info ===
    void getSystemInfo(std::function<void(const SystemInfoGpu&,
                                         const QString& modelName,
                                         const QString& modelVersion,
                                         const QString& commandLine)> callback);
    void getProcessInfo(std::function<void(const QList<ProcessInfo>&)> callback);
    
    // === Browser context ===
    void createBrowserContext(std::function<void(const QString& contextId)> callback);
    void disposeBrowserContext(const QString& contextId);
    void getBrowserContexts(std::function<void(const QStringList&)> callback);
    
    // === Raw CDP communication ===
    void sendCommand(const QString& method, const QJsonObject& params,
                    std::function<void(const QJsonObject&)> callback = {});
    
    // For sending to a specific session
    void sendCommandToSession(const QString& sessionId, const QString& method,
                              const QJsonObject& params,
                              std::function<void(const QJsonObject&)> callback = {});
    
signals:
    void connected();
    void disconnected();
    void commandResponse(int id, const QJsonObject& result);
    void commandError(int id, const QJsonObject& error);
    
    // Target events
    void targetCreated(const TargetInfo& info);
    void targetDestroyed(const QString& targetId);
    void targetInfoChanged(const TargetInfo& info);
    void targetCrashed(const QString& targetId, const QString& status, int errorCode);
    void attachedToTarget(const QString& sessionId, const TargetInfo& info, bool waitingForDebugger);
    void detachedFromTarget(const QString& sessionId, const QString& targetId);
    
    // Security events
    void certificateError(const CertificateError& error);
    
private:
    void handleMessage(const QString& message);
    
    static TargetInfo parseTargetInfo(const QJsonObject& obj);
    static SystemInfoGpu parseGpuInfo(const QJsonObject& obj);
    static QList<ProcessInfo> parseProcessInfo(const QJsonArray& arr);
    
    QWebSocket* m_ws = nullptr;
    QProcess* m_browserProcess = nullptr;
    TransportMode m_transportMode;
    
    int m_nextId = 1;
    QHash<int, std::function<void(const QJsonObject&)>> m_callbacks;
    QHash<int, QString> m_callbackSessions;   // callId → sessionId (for routing responses)
    
    QString m_sessionId;   // current session (empty = root browser session)
    QHash<QString, TargetInfo> m_knownTargets;
    
    // Certificate error callbacks (for Security.setOverrideCertificateErrors)
    QHash<int, std::function<void(bool)>> m_certCallbacks;
};
```

#### `BrowserController.cpp` (key methods)

```cpp
#include "BrowserController.h"
#include <QJsonDocument>
#include <QCoreApplication>
#include <QDir>
#include <QStandardPaths>

// === Constructor / Destructor ===

BrowserController::BrowserController(TransportMode mode, QObject* parent)
    : QObject(parent), m_transportMode(mode) {
    m_ws = new QWebSocket;
    
    connect(m_ws, &QWebSocket::textMessageReceived,
            this, &BrowserController::handleMessage);
    connect(m_ws, &QWebSocket::connected, this, &BrowserController::connected);
    connect(m_ws, &QWebSocket::disconnected, this, [this]() {
        emit disconnected();
    });
}

BrowserController::~BrowserController() {
    disconnect();
    if (m_browserProcess) {
        m_browserProcess->terminate();
        m_browserProcess->waitForFinished(5000);
    }
}

// === Connection management ===

bool BrowserController::connectToBrowser(const QUrl& url) {
    if (m_transportMode == TransportMode::WebSocket) {
        m_ws->open(url);
        return true;
    }
    // Pipe mode: launch browser process with --remote-debugging-pipe=cbor
    // and communicate via stdin/stdout
    // (For Qt6 WebEngine, use QWebEngineProfile's built-in DevTools support instead)
    return false;
}

void BrowserController::disconnect() {
    if (m_ws && m_ws->isValid()) {
        m_ws->close();
    }
}

// === CDP plumbing ===

void BrowserController::sendCommand(const QString& method, const QJsonObject& params,
                                     std::function<void(const QJsonObject&)> callback) {
    sendCommandToSession(m_sessionId, method, params, callback);
}

void BrowserController::sendCommandToSession(const QString& sessionId,
                                              const QString& method,
                                              const QJsonObject& params,
                                              std::function<void(const QJsonObject&)> callback) {
    const int id = m_nextId++;
    if (callback) m_callbacks[id] = callback;
    if (!sessionId.isEmpty()) m_callbackSessions[id] = sessionId;
    
    QJsonObject msg;
    msg["id"] = id;
    msg["method"] = method;
    if (!params.isEmpty()) msg["params"] = params;
    if (!sessionId.isEmpty()) msg["sessionId"] = sessionId;
    
    m_ws->sendTextMessage(QString::fromUtf8(
        QJsonDocument(msg).toJson(QJsonDocument::Compact)));
}

void BrowserController::handleMessage(const QString& message) {
    const auto doc = QJsonDocument::fromJson(message.toUtf8()).object();
    
    // Response to our command
    if (doc.contains("id")) {
        const int id = doc.value("id").toInt();
        auto it = m_callbacks.find(id);
        if (it != m_callbacks.end()) {
            auto cb = it.value();
            m_callbacks.erase(it);
            m_callbackSessions.remove(id);
            if (doc.contains("error")) {
                qWarning() << "CDP error:" << doc.value("error").toObject();
            }
            if (cb) cb(doc.value("result").toObject());
        }
        return;
    }
    
    // Event
    const QString method = doc.value("method").toString();
    const QJsonObject params = doc.value("params").toObject();
    
    if (method == "Target.targetCreated") {
        const TargetInfo info = parseTargetInfo(params.value("targetInfo").toObject());
        m_knownTargets[info.targetId] = info;
        emit targetCreated(info);
    }
    else if (method == "Target.targetDestroyed") {
        const QString targetId = params.value("targetId").toString();
        m_knownTargets.remove(targetId);
        emit targetDestroyed(targetId);
    }
    else if (method == "Target.targetInfoChanged") {
        const TargetInfo info = parseTargetInfo(params.value("targetInfo").toObject());
        m_knownTargets[info.targetId] = info;
        emit targetInfoChanged(info);
    }
    else if (method == "Target.targetCrashed") {
        emit targetCrashed(params.value("targetId").toString(),
                          params.value("status").toString(),
                          params.value("errorCode").toInt());
    }
    else if (method == "Target.attachedToTarget") {
        const QString sessionId = params.value("sessionId").toString();
        const TargetInfo info = parseTargetInfo(params.value("targetInfo").toObject());
        const bool waiting = params.value("waitingForDebugger").toBool();
        emit attachedToTarget(sessionId, info, waiting);
    }
    else if (method == "Target.detachedFromTarget") {
        emit detachedFromTarget(params.value("sessionId").toString(),
                                 params.value("targetId").toString());
    }
    else if (method == "Security.certificateError") {
        CertificateError err;
        err.eventId = params.value("eventId").toInt();
        err.errorType = params.value("errorType").toString();
        err.requestUrl = QUrl(params.value("requestURL").toString());
        emit certificateError(err);
    }
}

// === Helpers ===

TargetInfo BrowserController::parseTargetInfo(const QJsonObject& obj) {
    TargetInfo info;
    info.targetId = obj.value("targetId").toString();
    info.type = obj.value("type").toString();
    info.title = obj.value("title").toString();
    info.url = obj.value("url").toString();
    info.parentId = obj.value("parentId").toString();
    info.openerId = obj.value("openerId").toString();
    info.openerFrameId = obj.value("openerFrameId").toString();
    info.parentFrameId = obj.value("parentFrameId").toString();
    info.browserContextId = obj.value("browserContextId").toString();
    info.subtype = obj.value("subtype").toString();
    info.attached = obj.value("attached").toBool(false);
    info.canAccessOpener = obj.value("canAccessOpener").toBool(false);
    return info;
}

SystemInfoGpu BrowserController::parseGpuInfo(const QJsonObject& obj) {
    SystemInfoGpu gpu;
    gpu.devices = obj.value("devices").toArray();
    gpu.auxAttributes = obj.value("auxAttributes").toObject();
    gpu.featureStatus = obj.value("featureStatus").toString();
    gpu.driverBugWorkarounds = obj.value("driverBugWorkarounds").toArray();
    gpu.videoDecoding = obj.value("videoDecoding").toArray();
    gpu.videoEncoding = obj.value("videoEncoding").toArray();
    return gpu;
}

QList<ProcessInfo> BrowserController::parseProcessInfo(const QJsonArray& arr) {
    QList<ProcessInfo> result;
    for (const QJsonValue& v : arr) {
        const QJsonObject o = v.toObject();
        ProcessInfo p;
        p.id = o.value("id").toInt();
        p.type = o.value("type").toString();
        p.cpuTime = o.value("cpuTime").toDouble();
        result.append(p);
    }
    return result;
}

// === Target management ===

void BrowserController::getTargets(std::function<void(const QList<TargetInfo>&)> callback) {
    sendCommand("Target.getTargets", {}, [callback](const QJsonObject& result) {
        QList<TargetInfo> targets;
        const QJsonArray arr = result.value("targetInfos").toArray();
        for (const QJsonValue& v : arr) {
            targets.append(parseTargetInfo(v.toObject()));
        }
        if (callback) callback(targets);
    });
}

void BrowserController::attachToTarget(const QString& targetId, bool flatten,
                                      std::function<void(const QString&)> callback) {
    QJsonObject params;
    params["targetId"] = targetId;
    params["flatten"] = flatten;
    sendCommand("Target.attachToTarget", params, [callback](const QJsonObject& result) {
        if (callback) callback(result.value("sessionId").toString());
    });
}

void BrowserController::detachFromTarget(const QString& sessionId) {
    QJsonObject params;
    params["sessionId"] = sessionId;
    sendCommand("Target.detachFromTarget", params);
}

void BrowserController::setAutoAttach(bool autoAttach, bool waitForDebugger, bool flatten,
                                     std::function<void()> callback) {
    QJsonObject params;
    params["autoAttach"] = autoAttach;
    params["waitForDebuggerOnStart"] = waitForDebugger;
    params["flatten"] = flatten;
    sendCommand("Target.setAutoAttach", params, [callback](const QJsonObject&) {
        if (callback) callback();
    });
}

void BrowserController::createTarget(const QUrl& url, bool newWindow, bool forTab,
                                     std::function<void(const QString&)> callback) {
    QJsonObject params;
    params["url"] = url.toString();
    if (newWindow) params["newWindow"] = true;
    if (forTab) params["forTab"] = true;
    sendCommand("Target.createTarget", params, [callback](const QJsonObject& result) {
        if (callback) callback(result.value("targetId").toString());
    });
}

void BrowserController::closeTarget(const QString& targetId) {
    QJsonObject params;
    params["targetId"] = targetId;
    sendCommand("Target.closeTarget", params);
}

void BrowserController::activateTarget(const QString& targetId) {
    QJsonObject params;
    params["targetId"] = targetId;
    sendCommand("Target.activateTarget", params);
}

// === Emulation ===

void BrowserController::setDeviceMetricsOverride(const DeviceMetrics& metrics,
                                                 std::function<void()> callback) {
    QJsonObject params;
    if (metrics.width > 0) params["width"] = metrics.width;
    if (metrics.height > 0) params["height"] = metrics.height;
    params["deviceScaleFactor"] = metrics.deviceScaleFactor;
    params["mobile"] = metrics.mobile;
    if (!metrics.screenOrientationType.isEmpty()) {
        QJsonObject orient;
        orient["type"] = metrics.screenOrientationType;
        orient["angle"] = metrics.screenOrientationAngle;
        params["screenOrientation"] = orient;
    }
    sendCommand("Emulation.setDeviceMetricsOverride", params, [callback](const QJsonObject&) {
        if (callback) callback();
    });
}

void BrowserController::clearDeviceMetricsOverride() {
    sendCommand("Emulation.clearDeviceMetricsOverride", {});
}

void BrowserController::setUserAgentOverride(const UserAgentOverride& ua,
                                             std::function<void()> callback) {
    QJsonObject params;
    params["userAgent"] = ua.userAgent;
    if (!ua.acceptLanguage.isEmpty()) params["acceptLanguage"] = ua.acceptLanguage;
    if (!ua.platform.isEmpty()) params["platform"] = ua.platform;
    if (!ua.userAgentMetadata.isEmpty()) params["userAgentMetadata"] = ua.userAgentMetadata;
    sendCommand("Emulation.setUserAgentOverride", params, [callback](const QJsonObject&) {
        if (callback) callback();
    });
}

void BrowserController::setLocaleOverride(const QString& locale) {
    QJsonObject params;
    params["locale"] = locale;
    sendCommand("Emulation.setLocaleOverride", params);
}

void BrowserController::setTimezoneOverride(const QString& timezoneId) {
    QJsonObject params;
    params["timezoneId"] = timezoneId;
    sendCommand("Emulation.setTimezoneOverride", params);
}

void BrowserController::setGeolocationOverride(double lat, double lon, double accuracy,
                                                std::function<void()> callback) {
    QJsonObject params;
    params["latitude"] = lat;
    params["longitude"] = lon;
    params["accuracy"] = accuracy;
    sendCommand("Emulation.setGeolocationOverride", params, [callback](const QJsonObject&) {
        if (callback) callback();
    });
}

void BrowserController::clearGeolocationOverride() {
    sendCommand("Emulation.clearGeolocationOverride", {});
}

void BrowserController::setIdleOverride(bool isUserActive, bool isScreenUnlocked) {
    QJsonObject params;
    params["isUserActive"] = isUserActive;
    params["isScreenUnlocked"] = isScreenUnlocked;
    sendCommand("Emulation.setIdleOverride", params);
}

void BrowserController::clearIdleOverride() {
    sendCommand("Emulation.clearIdleOverride", {});
}

void BrowserController::setScriptExecutionDisabled(bool disabled) {
    QJsonObject params;
    params["value"] = disabled;
    sendCommand("Emulation.setScriptExecutionDisabled", params);
}

void BrowserController::setTouchEmulationEnabled(bool enabled, int maxTouchPoints) {
    QJsonObject params;
    params["enabled"] = enabled;
    params["maxTouchPoints"] = maxTouchPoints;
    sendCommand("Emulation.setTouchEmulationEnabled", params);
}

void BrowserController::setCPUThrottlingRate(double rate) {
    QJsonObject params;
    params["rate"] = rate;
    sendCommand("Emulation.setCPUThrottlingRate", params);
}

void BrowserController::setEmulatedMedia(const QString& media,
                                        const QHash<QString, QString>& features) {
    QJsonObject params;
    if (!media.isEmpty()) params["media"] = media;
    if (!features.isEmpty()) {
        QJsonArray arr;
        for (auto it = features.begin(); it != features.end(); ++it) {
            QJsonObject f;
            f["name"] = it.key();
            f["value"] = it.value();
            arr.append(f);
        }
        params["features"] = arr;
    }
    sendCommand("Emulation.setEmulatedMedia", params);
}

void BrowserController::setAutomationOverride(bool enabled) {
    QJsonObject params;
    params["enabled"] = enabled;
    sendCommand("Emulation.setAutomationOverride", params);
}

void BrowserController::setHardwareConcurrencyOverride(int count) {
    QJsonObject params;
    params["hardwareConcurrency"] = count;
    sendCommand("Emulation.setHardwareConcurrencyOverride", params);
}

void BrowserController::setNavigatorOverrides(const QString& platform) {
    QJsonObject params;
    params["platform"] = platform;
    sendCommand("Emulation.setNavigatorOverrides", params);
}

// === Browser commands ===

void BrowserController::getVersion(
        std::function<void(const QString&, const QString&, const QString&, const QString&)> callback) {
    sendCommand("Browser.getVersion", {}, [callback](const QJsonObject& result) {
        if (callback) callback(
            result.value("protocolVersion").toString(),
            result.value("product").toString(),
            result.value("userAgent").toString(),
            result.value("jsVersion").toString()
        );
    });
}

void BrowserController::getBrowserCommandLine(std::function<void(const QStringList&)> callback) {
    sendCommand("Browser.getBrowserCommandLine", {}, [callback](const QJsonObject& result) {
        QStringList args;
        const QJsonArray arr = result.value("arguments").toArray();
        for (const QJsonValue& v : arr) args.append(v.toString());
        if (callback) callback(args);
    });
}

void BrowserController::grantPermissions(const QString& origin, const QStringList& permissions) {
    QJsonObject params;
    params["origin"] = origin;
    QJsonArray permArr;
    for (const QString& p : permissions) permArr.append(p);
    params["permissions"] = permArr;
    sendCommand("Browser.grantPermissions", params);
}

void BrowserController::resetPermissions(const QString& origin) {
    QJsonObject params;
    params["origin"] = origin;
    sendCommand("Browser.resetPermissions", params);
}

void BrowserController::crash() {
    sendCommand("Browser.crash", {});
}

void BrowserController::crashGpuProcess() {
    sendCommand("Browser.crashGpuProcess", {});
}

// === Security ===

void BrowserController::setIgnoreCertificateErrors(bool ignore) {
    QJsonObject params;
    params["ignore"] = ignore;
    sendCommand("Security.setIgnoreCertificateErrors", params);
}

void BrowserController::setOverrideCertificateErrors(bool override) {
    QJsonObject params;
    params["override"] = override;
    sendCommand("Security.setOverrideCertificateErrors", params);
}

void BrowserController::handleCertificateError(int eventId, bool continueLoading) {
    QJsonObject params;
    params["eventId"] = eventId;
    params["action"] = continueLoading ? "continue" : "cancel";
    sendCommand("Security.handleCertificateError", params);
}

// === System info ===

void BrowserController::getSystemInfo(
        std::function<void(const SystemInfoGpu&, const QString&, const QString&, const QString&)> callback) {
    sendCommand("SystemInfo.getInfo", {}, [callback](const QJsonObject& result) {
        SystemInfoGpu gpu = parseGpuInfo(result.value("gpu").toObject());
        if (callback) callback(
            gpu,
            result.value("modelName").toString(),
            result.value("modelVersion").toString(),
            result.value("commandLine").toString()
        );
    });
}

void BrowserController::getProcessInfo(std::function<void(const QList<ProcessInfo>&)> callback) {
    sendCommand("SystemInfo.getProcessInfo", {}, [callback](const QJsonObject& result) {
        QList<ProcessInfo> procs = parseProcessInfo(result.value("processInfo").toArray());
        if (callback) callback(procs);
    });
}

// === Browser context ===

void BrowserController::createBrowserContext(std::function<void(const QString&)> callback) {
    sendCommand("Target.createBrowserContext", {}, [callback](const QJsonObject& result) {
        if (callback) callback(result.value("browserContextId").toString());
    });
}

void BrowserController::disposeBrowserContext(const QString& contextId) {
    QJsonObject params;
    params["browserContextId"] = contextId;
    sendCommand("Target.disposeBrowserContext", params);
}

void BrowserController::getBrowserContexts(std::function<void(const QStringList&)> callback) {
    sendCommand("Target.getBrowserContexts", {}, [callback](const QJsonObject& result) {
        QStringList contexts;
        const QJsonArray arr = result.value("browserContextIds").toArray();
        for (const QJsonValue& v : arr) contexts.append(v.toString());
        if (callback) callback(contexts);
    });
}
```

### 6.16.2 Using the BrowserController

```cpp
// In your scraper:
auto* browser = new BrowserController(BrowserController::TransportMode::WebSocket);

// Connect to the browser-level DevTools endpoint
browser->connectToBrowser(QUrl("ws://127.0.0.1:9222/devtools/browser"));

// Wait for connection
connect(browser, &BrowserController::connected, [=]() {
    qDebug() << "Connected to browser DevTools";
    
    // Get version info
    browser->getVersion([](const QString& proto, const QString& product,
                          const QString& ua, const QString& jsVer) {
        qDebug() << "Protocol:" << proto;
        qDebug() << "Product:" << product;
        qDebug() << "JS Version:" << jsVer;
    });
    
    // Auto-attach to ALL targets (pages, iframes, workers) with flatten
    browser->setAutoAttach(true, false, true, []() {
        qDebug() << "Auto-attach enabled";
    });
    
    // Hide automation signals
    browser->setAutomationOverride(false);
    
    // Set up device metrics for mobile emulation
    DeviceMetrics metrics;
    metrics.width = 390;
    metrics.height = 844;
    metrics.deviceScaleFactor = 3.0;
    metrics.mobile = true;
    metrics.screenOrientationType = "portraitPrimary";
    metrics.screenOrientationAngle = 0;
    // (Apply this per page session, not on browser target)
    
    // Ignore cert errors for dev scraping
    browser->setIgnoreCertificateErrors(true);
    
    // Get system info
    browser->getSystemInfo([](const SystemInfoGpu& gpu,
                             const QString& modelName,
                             const QString& modelVersion,
                             const QString& cmdLine) {
        qDebug() << "GPU devices:" << gpu.devices.size();
        qDebug() << "Model:" << modelName << modelVersion;
    });
    
    // Get process info
    browser->getProcessInfo([](const QList<ProcessInfo>& procs) {
        for (const ProcessInfo& p : procs) {
            qDebug() << "  PID:" << p.id << "type:" << p.type 
                     << "CPU:" << p.cpuTime << "s";
        }
    });
});

// Track new targets
connect(browser, &BrowserController::targetCreated,
        [](const TargetInfo& info) {
    qDebug() << "[TARGET+] " << info.type << info.url 
             << "id:" << info.targetId;
});

connect(browser, &BrowserController::attachedToTarget,
        [=](const QString& sessionId, const TargetInfo& info, bool waiting) {
    qDebug() << "[ATTACHED] session:" << sessionId 
             << "target:" << info.type << info.url;
    
    // Now you can send commands with this sessionId
    // e.g., browser->sendCommandToSession(sessionId, "Page.enable", {});
});

// Handle certificate errors
connect(browser, &BrowserController::certificateError,
        [browser](const CertificateError& err) {
    qDebug() << "[CERT ERROR]" << err.errorType << "for" << err.requestUrl;
    // Allow self-signed certs for dev
    browser->handleCertificateError(err.eventId, true);
});

// Create a new tab
browser->createTarget(QUrl("https://example.com"), false, true, 
    [](const QString& targetId) {
    qDebug() << "Created tab:" << targetId;
});

// Open a new window
browser->createTarget(QUrl("https://example.com"), true, false,
    [](const QString& targetId) {
    qDebug() << "Created window:" << targetId;
});

// Create an isolated browser context (for fingerprint isolation)
browser->createBrowserContext([](const QString& contextId) {
    qDebug() << "Created browser context:" << contextId;
    // Use this contextId when creating new targets
    // Target.createTarget({browserContextId: contextId, url: "..."})
});

// Get all targets
browser->getTargets([](const QList<TargetInfo>& targets) {
    qDebug() << "Total targets:" << targets.size();
    for (const TargetInfo& t : targets) {
        qDebug() << "  " << t.type << t.url << (t.attached ? "[attached]" : "");
    }
});

// Override UA on a specific page session
// (After attaching to a page target:)
QString pageSessionId = "...";
browser->sendCommandToSession(pageSessionId, "Emulation.setUserAgentOverride", 
    QJsonObject{
        {"userAgent", "Mozilla/5.0 (iPhone; CPU iPhone OS 17_0 like Mac OS X)"},
        {"acceptLanguage", "en-US,en;q=0.9"},
        {"platform", "iPhone"},
        {"userAgentMetadata", QJsonObject{
            {"brands", QJsonArray{QJsonObject{{"brand", "Not/A)Brand"}, {"version", "8"}}}},
            {"fullVersion", "17.0"},
            {"platform", "iOS"},
            {"platformVersion", "17.0"},
            {"architecture", ""},
            {"model", "iPhone15,3"},
            {"mobile", true},
            {"bitness", ""},
            {"wow64", false},
            {"formFactors", QJsonArray{"Mobile"}}
        }}
    });

// Set device metrics for mobile emulation
browser->sendCommandToSession(pageSessionId, "Emulation.setDeviceMetricsOverride",
    QJsonObject{
        {"width", 390},
        {"height", 844},
        {"deviceScaleFactor", 3.0},
        {"mobile", true},
        {"screenOrientation", QJsonObject{
            {"type", "portraitPrimary"},
            {"angle", 0}
        }}
    });

// Set locale + timezone + geolocation
browser->sendCommandToSession(pageSessionId, "Emulation.setLocaleOverride",
    QJsonObject{{"locale", "en-US"}});

browser->sendCommandToSession(pageSessionId, "Emulation.setTimezoneOverride",
    QJsonObject{{"timezoneId", "America/New_York"}});

browser->sendCommandToSession(pageSessionId, "Emulation.setGeolocationOverride",
    QJsonObject{
        {"latitude", 40.7128},
        {"longitude", -74.0060},
        {"accuracy", 100.0}
    });

// Disable JS on demand
// browser->sendCommandToSession(pageSessionId, "Emulation.setScriptExecutionDisabled",
//     QJsonObject{{"value", true}});

// Emulate touch
browser->sendCommandToSession(pageSessionId, "Emulation.setTouchEmulationEnabled",
    QJsonObject{{"enabled", true}, {"maxTouchPoints", 5}});

// Throttle CPU (for testing performance under slow devices)
browser->sendCommandToSession(pageSessionId, "Emulation.setCPUThrottlingRate",
    QJsonObject{{"rate", 4.0}});
```

---

## 6.17 Edge Cases

### 6.17.1 Flattened Session Routing

When using `Target.setAutoAttach({flatten: true})`, every message to/from a child session carries a `"sessionId"` field. The root session's `DispatchProtocolMessage` extracts the sessionId and routes to the child session. If the sessionId doesn't match any child session, the message is dropped silently (no error response).

**For scraping**: always check the `sessionId` field on incoming events to route them to the correct page/worker handler.

### 6.17.2 Worker Target Attachment

Workers (dedicated, shared, service) are separate targets that must be auto-attached. When `Target.setAutoAttach({flatten: true})` is called on a page target, child workers are auto-attached. When called on the browser target, all top-level workers are auto-attached.

Worker targets have:
- `type: "worker"` / `"shared_worker"` / `"service_worker"`
- No DOM (DOM/CSS agents are not available)
- A single execution context (the worker's main world)
- `Runtime.enable` + `Runtime.evaluate` work but `document`, `window`, `navigator` are limited

### 6.17.3 `Target.createBrowserContext` for Fingerprint Isolation

```json
{
  "method": "Target.createBrowserContext",
  "params": {
    "disposeOnDetach": false,
    "proxyServer": "http://proxy.example.com:8080",
    "proxyBypassList": "*.local",
    "originsWithUniversalNetworkAccess": []
  }
}
```

Returns `browserContextId`. Then pass it to `Target.createTarget`:

```json
{
  "method": "Target.createTarget",
  "params": {
    "url": "https://example.com",
    "browserContextId": "<contextId>"
  }
}
```

Each browser context has its own:
- Cookie jar
- localStorage / sessionStorage
- IndexedDB
- CacheStorage
- HTTP cache
- Permissions

This is the **foundation for multi-account scraping** — each context is a fresh identity.

### 6.17.4 `Security.setIgnoreCertificateErrors` and Bfcache

Setting `Security.setIgnoreCertificateErrors(true)` disables the back-forward cache (bfcache). This means:
- `history.back()` will re-fetch the page instead of restoring from bfcache
- Navigation performance is degraded
- `performance.getEntriesByType("navigation")[0].type` will be `"navigate"` instead of `"back_forward"`

**For scraping**: if you need bfcache, handle cert errors per-event via `Security.setOverrideCertificateErrors` + `Security.handleCertificateError` instead.

### 6.17.5 `Emulation.setDeviceMetricsOverride` and Viewport

Setting device metrics:
1. Resizes the `RenderWidgetHostView` to the specified dimensions
2. Sets the device scale factor (affects rendering, not just CSS `devicePixelRatio`)
3. If `mobile: true`, forces Android-style scrollbars and touch events
4. If `screenOrientation` is set, overrides `screen.orientation`

The override **persists across navigations** within the same target session. To clear it, call `Emulation.clearDeviceMetricsOverride`.

### 6.17.6 `Emulation.setUserAgentOverride` Coherence

When you override the UA via `Emulation.setUserAgentOverride`:
1. The HTTP `User-Agent` header is overridden for ALL requests from this target
2. `navigator.userAgent` in JS is overridden
3. `navigator.appVersion` is overridden (derived from UA)
4. If `userAgentMetadata` is provided, `navigator.userAgentData` is overridden
5. If `acceptLanguage` is provided, the `Accept-Language` header is overridden
6. If `platform` is provided, `navigator.platform` is overridden

**For stealth**: make sure ALL of these are consistent. If UA says iPhone but platform says Win32, you're detectable.

### 6.17.7 `Target.closeTarget` vs `Page.close`

- `Target.closeTarget(targetId)` — closes the target by ID (works from the browser session)
- `Page.close()` — closes the current page (works from the page session)

Both result in `Target.targetDestroyed` event. `Target.closeTarget` is more reliable because it works even if the page session is disconnected.

### 6.17.8 `SystemInfo.getInfo` Timeout

`SystemInfo.getInfo` waits for the GPU process to populate `GpuDataManagerImpl`. If the GPU process hasn't started (e.g. `--disable-gpu`), the call may hang indefinitely. The watchdog (`kGPUInfoWatchdogTimeoutMs`) is 10 seconds on most platforms.

**For scraping**: if you don't need GPU info, skip `SystemInfo.getInfo`. It's expensive and can timeout.

### 6.17.9 Hidden Targets

`Target.createTarget({hidden: true})` creates a "hidden" target — no window, no tab. Used by extensions and for background processing. Hidden targets:
- Don't appear in `Target.getTargets` unless `hidden: true` filter is passed
- Can be attached to normally
- Can navigate, execute JS, etc.
- Can be promoted to visible via `Target.activateTarget`

### 6.17.10 `--remote-debugging-pipe` vs `--remote-debugging-port` Performance

| Metric | WebSocket (port) | Pipe (ASCIIZ) | Pipe (CBOR) |
|---|---|---|---|
| Latency per message | ~0.5-2ms (TCP + WS framing) | ~0.1-0.5ms (no framing) | ~0.05-0.2ms (binary, no parsing) |
| Throughput | ~10K messages/sec | ~50K messages/sec | ~100K+ messages/sec |
| CPU overhead | JSON parse + TCP | JSON parse only | CBOR decode only |
| Memory per message | ~2× message size (JSON + WS frame) | ~1.5× message size | ~1× message size |

For high-throughput scraping (hundreds of `Runtime.evaluate` calls per second), the pipe with CBOR is 5-10× faster than WebSocket.

---

## 6.18 Performance Impact

### 6.18.1 Cost of Browser-Level Operations

| Operation | Cost |
|---|---|
| `Target.getTargets` | ~1-5ms (walks all agent hosts) |
| `Target.attachToTarget` | ~5-10ms (creates session, attaches to renderer) |
| `Target.setAutoAttach` | ~5-10ms (registers auto-attacher) |
| `Target.createTarget` | ~50-200ms (creates new renderer process or reuses existing) |
| `Target.closeTarget` | ~10-50ms (terminates renderer) |
| `Emulation.setDeviceMetricsOverride` | ~10-50ms (resizes view + re-lays-out) |
| `Emulation.setUserAgentOverride` | ~1-2ms (stores override, applies on next request) |
| `Emulation.setLocaleOverride` | ~5-20ms (reconfigures ICU) |
| `Emulation.setTimezoneOverride` | ~5-20ms (reconfigures ICU + V8 timezone) |
| `Emulation.setCPUThrottlingRate` | ~1ms (sets scheduler rate) |
| `Security.setIgnoreCertificateErrors` | ~1ms |
| `SystemInfo.getInfo` | ~10ms-10s (waits for GPU process; watchdog at 10s) |
| `Browser.getVersion` | ~1ms |

### 6.18.2 Memory Overhead

| Per-Item | Memory |
|---|---|
| Per DevToolsSession (browser-side) | ~5-10 KB |
| Per child session (flattened) | ~5-10 KB |
| Per TargetInfo | ~200-500 bytes |
| Per cert error callback | ~200 bytes |
| `EmulationHandler` state | ~1 KB per session |

### 6.18.3 Optimization Tips for Scraping

1. **Use pipe transport with CBOR** (`--remote-debugging-pipe=cbor`) — no port, no origin check, lower latency
2. **Always use `flatten: true`** in `Target.setAutoAttach` — avoids per-target WebSocket connections
3. **Use `Target.createBrowserContext`** for fingerprint isolation — each context is a fresh identity
4. **Don't call `SystemInfo.getInfo` on every session** — it's expensive and the info doesn't change
5. **Use `Security.setIgnoreCertificateErrors(true)` for dev scraping** — simpler than per-error handling, but be aware it disables bfcache
6. **Call `Emulation.setAutomationOverride(false)` immediately after attach** — removes the CDP-attached `navigator.webdriver = true` flag
7. **Batch `Target.createTarget` calls** — each one creates a renderer process (expensive)
8. **Reuse browser contexts** — `Target.createBrowserContext` returns an ID you can pass to multiple `Target.createTarget` calls
9. **Don't call `Target.getTargets` in a loop** — subscribe to `Target.targetCreated`/`targetDestroyed` events instead
10. **Use `Target.activateTarget` before `Page.captureScreenshot`** — ensures the page is in the foreground for accurate rendering

---

## 6.19 Security & Privacy Impact

### 6.19.1 What CDP Browser-Level Can Access

A CDP client connected to the browser target can:
- **Create and close any tab/page** — full tab management
- **Create isolated browser contexts** — each with its own cookies, storage, permissions, proxy
- **Read all targets** — every page, iframe, worker across all profiles
- **Read the GPU info and machine model** — fingerprinting the host hardware
- **Read all running processes** — PID, type, CPU time
- **Read the browser command line** — (if `--enable-automation` is set)
- **Crash the browser or GPU process** — DoS
- **Override certificate errors** — accept any self-signed cert
- **Grant any permission** — geolocation, camera, microphone, notifications
- **Set download behavior** — write files anywhere on disk
- **Override UA, locale, timezone, geolocation, device metrics** — full spoofing
- **Disable JS execution** — freeze any page
- **Throttle CPU** — simulate slow devices
- **Emulate touch** — simulate mobile interaction

### 6.19.2 Transport Security Comparison

| Security Aspect | WebSocket (`--remote-debugging-port`) | Pipe (`--remote-debugging-pipe`) |
|---|---|---|
| Network exposure | Yes (default 127.0.0.1; `0.0.0.0` if `--remote-debugging-address`) | None — pure stdio |
| Origin check | Yes (must match `--remote-allow-origins`) | Not applicable |
| Host header check | Yes (must be IP/localhost) | Not applicable |
| DNS rebinding risk | Yes (if bound to `0.0.0.0`) | None |
| Process isolation | External process can connect | Only the parent of the browser process |
| Wire format security | JSON (human-readable) | JSON or CBOR (binary) |

**For scraping**: **always use pipe transport**. No port, no origin check, no DNS rebinding risk, lower latency, CBOR available.

### 6.19.3 Detection of CDP Browser-Level Interference

1. **`Target.setAutoAttach` with `flatten: true` adds latency** — every new target is paused then resumed; measurable timing difference
2. **`Emulation.setDeviceMetricsOverride` changes `screen.width`/`screen.height`** — detectable if they don't match `window.innerWidth`/`innerHeight`
3. **`Emulation.setUserAgentOverride` changes `navigator.userAgent`** — detectable if inconsistent with `navigator.platform` or `navigator.userAgentData`
4. **`Emulation.setCPUThrottlingRate` changes scheduler behavior** — detectable via `performance.now()` timing analysis
5. **`Emulation.setTouchEmulationEnabled` changes `navigator.maxTouchPoints`** — detectable if UA says desktop but `maxTouchPoints > 0`
6. **`Security.setIgnoreCertificateErrors(true)` disables bfcache** — `performance.getEntriesByType("navigation")[0].type` is `"navigate"` instead of `"back_forward"`
7. **`Target.createBrowserContext` creates new contexts** — each has a fresh cookie jar, detectable if a site checks cookie persistence

### 6.19.4 Stealth Scraping Best Practices for Browser-Level

1. **Don't pass `--enable-automation`** — use `--remote-debugging-pipe` instead
2. **Call `Emulation.setAutomationOverride(false)` after attach** — removes the `navigator.webdriver = true` flag
3. **Match UA + UA-CH + locale + timezone + platform + hardwareConcurrency + device metrics consistently** — any mismatch is a detection signal
4. **Use `Target.createBrowserContext` for each account** — isolates cookies, storage, and fingerprint
5. **Don't call `Browser.crash` or `Browser.crashGpuProcess`** — these are for testing, not scraping
6. **Don't call `SystemInfo.getInfo` on production scrapers** — it's expensive and unnecessary
7. **Use `Security.setOverrideCertificateErrors` + `Security.handleCertificateError`** instead of `Security.setIgnoreCertificateErrors(true)` — preserves bfcache
8. **Don't use `Emulation.setCPUThrottlingRate` unless needed** — it changes timing behavior that sophisticated JS can detect
9. **Don't use `Emulation.setTouchEmulationEnabled` on desktop UAs** — desktop + touch is a strong detection signal
10. **Use real browser profiles** — real history, bookmarks, extensions, cookies make the fingerprint more natural

---

## 6.20 Testing

### 6.20.1 Unit Tests

```cpp
#include <QtTest>
#include "BrowserController.h"

class TestBrowserController : public QObject {
    Q_OBJECT
private slots:
    void testGetVersion();
    void testAutoAttach();
    void testCreateTarget();
    void testCreateBrowserContext();
    void testEmulationOverrides();
    void testCertificateError();
    void testSystemInfo();
};

void TestBrowserController::testGetVersion() {
    BrowserController browser;
    browser.connectToBrowser(QUrl("ws://127.0.0.1:9222/devtools/browser"));
    
    QSemaphore sem;
    browser.getVersion([&sem](const QString& proto, const QString& product,
                              const QString& ua, const QString& jsVer) {
        QVERIFY(!proto.isEmpty());
        QVERIFY(!product.isEmpty());
        QVERIFY(!ua.isEmpty());
        QVERIFY(!jsVer.isEmpty());
        sem.release();
    });
    QVERIFY(sem.tryAcquire(1, 5000));
}

void TestBrowserController::testAutoAttach() {
    BrowserController browser;
    browser.connectToBrowser(QUrl("ws://127.0.0.1:9222/devtools/browser"));
    
    QSignalSpy spy(&browser, &BrowserController::attachedToTarget);
    
    QSemaphore sem;
    browser.setAutoAttach(true, false, true, [&sem]() {
        sem.release();
    });
    QVERIFY(sem.tryAcquire(1, 5000));
    
    // Create a new target — should trigger attachedToTarget
    browser.createTarget(QUrl("https://example.com"), false, false);
    
    QVERIFY(spy.wait(10000));
    QCOMPARE(spy.count(), 1);
}

void TestBrowserController::testCreateTarget() {
    BrowserController browser;
    browser.connectToBrowser(QUrl("ws://127.0.0.1:9222/devtools/browser"));
    
    QSemaphore sem;
    browser.createTarget(QUrl("https://example.com"), false, true, 
        [&sem](const QString& targetId) {
        QVERIFY(!targetId.isEmpty());
        sem.release();
    });
    QVERIFY(sem.tryAcquire(1, 10000));
}

void TestBrowserController::testCreateBrowserContext() {
    BrowserController browser;
    browser.connectToBrowser(QUrl("ws://127.0.0.1:9222/devtools/browser"));
    
    QSemaphore sem;
    browser.createBrowserContext([&sem](const QString& contextId) {
        QVERIFY(!contextId.isEmpty());
        sem.release();
    });
    QVERIFY(sem.tryAcquire(1, 5000));
}

void TestBrowserController::testEmulationOverrides() {
    BrowserController browser;
    browser.connectToBrowser(QUrl("ws://127.0.0.1:9222/devtools/browser"));
    
    // Attach to a page first
    QSemaphore sem;
    browser.setAutoAttach(true, false, true, [&]() { sem.release(); });
    sem.acquire();
    
    // Wait for a page target
    QSignalSpy spy(&browser, &BrowserController::attachedToTarget);
    browser.createTarget(QUrl("https://example.com"), false, false);
    spy.wait(10000);
    
    QString sessionId = spy.takeFirst().at(0).toString();
    
    // Test UA override
    UserAgentOverride ua;
    ua.userAgent = "Mozilla/5.0 (iPhone; CPU iPhone OS 17_0 like Mac OS X)";
    ua.acceptLanguage = "en-US,en;q=0.9";
    ua.platform = "iPhone";
    
    browser.sendCommandToSession(sessionId, "Emulation.setUserAgentOverride",
        QJsonObject{
            {"userAgent", ua.userAgent},
            {"acceptLanguage", ua.acceptLanguage},
            {"platform", ua.platform}
        });
    
    // Verify via Runtime.evaluate
    browser.sendCommandToSession(sessionId, "Runtime.evaluate",
        QJsonObject{
            {"expression", "navigator.userAgent"},
            {"returnByValue", true}
        }, [](const QJsonObject& result) {
            QString ua = result.value("result").toObject().value("value").toString();
            QVERIFY(ua.contains("iPhone"));
        });
}

void TestBrowserController::testCertificateError() {
    BrowserController browser;
    browser.connectToBrowser(QUrl("ws://127.0.0.1:9222/devtools/browser"));
    
    QSignalSpy spy(&browser, &BrowserController::certificateError);
    
    // Navigate to a site with a self-signed cert
    // (requires a page target attached)
    // ...
    
    QVERIFY(spy.wait(10000));
    
    // Auto-accept
    const auto args = spy.takeFirst();
    const CertificateError err = args.at(0).value<CertificateError>();
    browser.handleCertificateError(err.eventId, true);
}

void TestBrowserController::testSystemInfo() {
    BrowserController browser;
    browser.connectToBrowser(QUrl("ws://127.0.0.1:9222/devtools/browser"));
    
    QSemaphore sem;
    browser.getSystemInfo([&sem](const SystemInfoGpu& gpu,
                                  const QString& modelName,
                                  const QString& modelVersion,
                                  const QString& cmdLine) {
        QVERIFY(gpu.devices.size() > 0);
        QVERIFY(!cmdLine.isEmpty());
        sem.release();
    });
    QVERIFY(sem.tryAcquire(1, 15000));  // 15s timeout for GPU info
}
```

---

## 6.21 Roadmap: Unique Features That Beat Puppeteer/Playwright

### 6.21.1 "Multi-Account Manager" — Browser Context Pool

```cpp
class MultiAccountManager {
public:
    // Create a browser context per account
    QString createAccount(const QString& name);
    
    // Switch between accounts
    void switchToAccount(const QString& name);
    
    // Import/export account state (cookies, localStorage, etc.)
    void exportAccount(const QString& name, const QString& filepath);
    void importAccount(const QString& filepath, const QString& name);
    
    // List all accounts
    QStringList accounts() const;
    
    // Delete an account
    void deleteAccount(const QString& name);
};
```

### 6.21.2 "Device Profile Manager" — Pre-configured Device Presets

```cpp
class DeviceProfileManager {
public:
    struct DeviceProfile {
        QString name;
        int viewportWidth;
        int viewportHeight;
        double deviceScaleFactor;
        bool mobile;
        QString userAgent;
        QString platform;
        QJsonObject userAgentMetadata;
        int maxTouchPoints;
        QString locale;
        QString timezone;
    };
    
    // Built-in profiles (iPhone 15, Galaxy S24, iPad Pro, etc.)
    QList<DeviceProfile> builtInProfiles() const;
    
    // Apply a profile to a session
    void applyProfile(const QString& sessionId, const DeviceProfile& profile);
    
    // Create a custom profile
    void addCustomProfile(const DeviceProfile& profile);
};
```

### 6.21.3 "Stealth Engine" — Comprehensive Anti-Detection

```cpp
class StealthEngine {
public:
    // Apply ALL stealth overrides at once
    void enable(const QString& sessionId);
    
    // Individual stealth features:
    void hideWebdriver(const QString& sessionId);
    void spoofUserAgent(const QString& sessionId, const QString& ua);
    void spoofHardwareConcurrency(const QString& sessionId, int count);
    void spoofPlatform(const QString& sessionId, const QString& platform);
    void spoofLanguage(const QString& sessionId, const QString& locale);
    void spoofTimezone(const QString& sessionId, const QString& tz);
    void spoofGeolocation(const QString& sessionId, double lat, double lon);
    void spoofScreen(const QString& sessionId, int w, int h, double dpr);
    void spoofWebGL(const QString& sessionId, const QString& vendor, const QString& renderer);
    void spoofCanvas(const QString& sessionId);  // canvas noise injection
    void spoofAudio(const QString& sessionId);  // audio noise injection
    void spoofFonts(const QString& sessionId, const QStringList& fonts);
    
    // Check detection
    struct DetectionResult {
        QStringList detectedSignals;
        QStringList inconsistentOverrides;
    };
    DetectionResult checkDetection(const QString& sessionId);
};
```

### 6.21.4 "Session Manager" — Persistent DevTools Sessions

```cpp
class SessionManager {
public:
    // Save current session state (enabled domains, overrides, breakpoints)
    void saveSession(const QString& name);
    
    // Restore a saved session
    void restoreSession(const QString& name);
    
    // List saved sessions
    QStringList savedSessions() const;
    
    // Delete a saved session
    void deleteSession(const QString& name);
    
    // Export session for sharing
    void exportSession(const QString& name, const QString& filepath);
    void importSession(const QString& filepath);
};
```

### 6.21.5 "Proxy Manager" — Per-Target Proxy Configuration

```cpp
class ProxyManager {
public:
    // Set proxy per browser context
    void setProxyForContext(const QString& contextId, const QString& proxyServer,
                           const QString& proxyBypassList = "");
    
    // Rotate proxies
    void enableRotation(const QStringList& proxies, int intervalSeconds);
    
    // Test proxy connectivity
    bool testProxy(const QString& proxyServer);
    
    // Get current proxy for a target
    QString proxyForTarget(const QString& targetId) const;
};
```

### 6.21.6 "Process Monitor" — Real-Time Browser Resource Tracking

```cpp
class ProcessMonitor : public QAbstractTableModel {
public:
    // Columns: PID, Type, CPU Time, Memory, Tab URL
    int columnCount() const override { return 5; }
    
    // Auto-refresh every N seconds
    void setRefreshInterval(int seconds);
    
    // Kill a process
    void killProcess(int pid);
    
    // Get total CPU/memory usage
    double totalCpuTime() const;
    qint64 totalMemory() const;
};
```

---

## 6.22 Summary Cheat Sheet

| Operation | CDP Command | Implementation File:Line |
|---|---|---|
| Get all targets | `Target.getTargets` | `target_handler.cc:1432` |
| Attach to target | `Target.attachToTarget` | `target_handler.cc:1180` |
| Auto-attach | `Target.setAutoAttach` | `target_handler.cc:1099` |
| Create target | `Target.createTarget` | `target_handler.cc:1339` |
| Close target | `Target.closeTarget` | `target_handler.cc` |
| Create browser context | `Target.createBrowserContext` | `target_handler.cc` |
| Dispose browser context | `Target.disposeBrowserContext` | `target_handler.cc` |
| Set device metrics | `Emulation.setDeviceMetricsOverride` | `emulation_handler.cc:659` |
| Override UA | `Emulation.setUserAgentOverride` | `emulation_handler.cc:889` |
| Override locale | `Emulation.setLocaleOverride` | `inspector_emulation_agent.cc:1051` |
| Override timezone | `Emulation.setTimezoneOverride` | `inspector_emulation_agent.cc:1062` |
| Override geolocation | `Emulation.setGeolocationOverride` | `emulation_handler.cc:572` |
| Override idle | `Emulation.setIdleOverride` | `emulation_handler.cc:557` |
| Disable JS | `Emulation.setScriptExecutionDisabled` | `inspector_emulation_agent.cc:402` |
| Emulate touch | `Emulation.setTouchEmulationEnabled` | `inspector_emulation_agent.cc:437` |
| Throttle CPU | `Emulation.setCPUThrottlingRate` | `inspector_emulation_agent.cc:594` |
| Override automation | `Emulation.setAutomationOverride` | `inspector_emulation_agent.cc:1222` |
| Override HW concurrency | `Emulation.setHardwareConcurrencyOverride` | `inspector_emulation_agent.cc:1130` |
| Get version | `Browser.getVersion` | `browser_handler.cc:129` |
| Get command line | `Browser.getBrowserCommandLine` | `browser_handler.cc:675` |
| Grant permissions | `Browser.grantPermissions` | `browser_handler.cc:394` |
| Crash browser | `Browser.crash` | `browser_handler.cc` |
| Ignore cert errors | `Security.setIgnoreCertificateErrors` | `security_handler.cc:168` |
| Override cert errors | `Security.setOverrideCertificateErrors` | `security_handler.cc:152` |
| Handle cert error | `Security.handleCertificateError` | `security_handler.cc:130` |
| Get GPU info | `SystemInfo.getInfo` | `system_info_handler.cc:299` |
| Get process info | `SystemInfo.getProcessInfo` | `system_info_handler.cc:377` |

---

## End of Part 6

This concludes **Part 6: Browser-Level Management** — approximately 12,000 words covering the DevToolsAgentHost hierarchy, DevToolsSession, WebSocket and pipe transports, the Target domain (attachToTarget, setAutoAttach, flatten), the Emulation domain (device metrics, UA, locale, timezone, geolocation, idle, touch, CPU throttling), the Browser domain (getVersion, permissions, crash), the Security domain (cert error override), the SystemInfo domain (GPU info, process info), flattened sessions, automation detection and stealth, security implications, full Qt6 C++ implementation, edge cases, performance, security, testing, and unique features.

---

## Complete Series Summary

This concludes the **6-part exhaustive analysis** of the Chromium DevTools Protocol for building a Qt6 WebEngine scraping browser:

| Part | Topic | Word Count |
|---|---|---|
| Part 1 | Cookie Storage & Management | ~8,000 |
| Part 2 | Network Tab (request/response capture) | ~12,000 |
| Part 3 | Runtime.evaluate / V8 Inspector | ~15,000 |
| Part 4 | DOM & Storage Monitoring | ~12,000 |
| Part 5 | Page Lifecycle & Screenshots | ~12,000 |
| Part 6 | Browser-Level Management | ~12,000 |
| **Total** | | **~71,000 words** |

Each part includes:
- Complete data structures
- File:line references into the Chromium source
- Class diagrams
- CDP command/event tables
- Full Qt6 C++ implementation
- Edge cases
- Performance analysis
- Security implications
- Testing strategies
- Unique features that beat Puppeteer/Playwright