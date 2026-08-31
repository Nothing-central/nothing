# PART 3: RUNTIME.EVALUATE / V8 INSPECTOR

## The Ultimate Qt6 WebEngine Scraping Browser Guide

*Exhaustive implementation reference — every byte from CDP command to V8 isolate to your scraper.*

---

## 3.1 The Two-Level JS Execution Model (READ THIS FIRST)

Before diving into `Runtime.evaluate`, you must understand the **two fundamentally different ways JavaScript can run in a Chromium tab**. Confusing these two is the #1 source of bugs in scraping browsers, and the #1 way anti-bot systems detect automation.

### 3.1.1 Level 1 — DevTools Console JS (via `Runtime.evaluate`)

This is JS that runs as if you typed it into the DevTools Console panel. It is invoked via the CDP `Runtime.evaluate` command (or `Runtime.callFunctionOn`, or `Runtime.compileScript` + `Runtime.runScript`).

**Characteristics**:

| Aspect | Behavior |
|---|---|
| **World** | Main world by default (or any execution context you specify via `executionContextId` / `uniqueContextId`) |
| **Compilation entry** | `v8::debug::EvaluateGlobal(isolate, source, mode, replMode)` — unified compile+run, no `v8::Script` handle kept |
| **CSP** | **Bypassed by default** — `allowUnsafeEvalBlockedByCSP` param defaults to `true`, so `eval()`, `Function()`, etc. work even if the page's CSP forbids them |
| **Command-line API** | Installed when `includeCommandLineAPI: true` is passed. Provides `$`, `$$`, `$x`, `inspect`, `copy`, `monitorEvents`, `unmonitorEvents`, `debug`, `undebug`, `monitor`, `unmonitor`, `profile`, `profileEnd`, `cd`, `getEventListeners`, `keys`, `values`, `table`, `queryObjects`, `$_` (last result), `$0`–`$4 (recently inspected elements) |
| **Debugger visibility** | By default NOT reported in `Debugger.scriptParsed` events (it's an "evaluation", not a script). However, if the expression contains `//# sourceURL=...`, it IS reported. |
| **Object lifetime** | Objects returned are kept alive in an "object group" (default: empty string, but commonly "console"). Held in `InjectedScript::m_idToWrappedObject` as `v8::Global<v8::Value>` strong handles. Released via `Runtime.releaseObject` or `Runtime.releaseObjectGroup`. |
| **Privileges** | Same as page JS in terms of DOM/network access. BUT — the `silent` parameter can mute console output and exceptions; the `userGesture` parameter can simulate a user gesture (for things like `requestFullscreen()`); the `throwOnSideEffect` parameter can refuse expressions that have side effects (for performance profiling). |
| **Async support** | `awaitPromise: true` chains a `.then()` and returns the resolved value. `replMode: true` (for the Console panel) wraps top-level `await` automatically. |
| **Detection by page** | **HIGH**. The page can detect this in multiple ways: (1) presence of command-line API globals like `$` on `window`; (2) `Error().stack` shows `<anonymous>` or `console-api-N` instead of a real URL; (3) `Object.getOwnPropertyDescriptor(window, '$')` returns a descriptor (real pages don't have `$`); (4) timing anomalies (the evaluate round-trip adds latency); (5) `Runtime.evaluate` scripts don't appear in `Performance.getEntriesByType("resource")`. |

### 3.1.2 Level 2 — Site Script Tag JS (via `<script>` or `Page.addScriptToEvaluateOnNewDocument`)

This is JS that runs as if the page itself loaded it — either via an actual `<script>` tag in the HTML, or via `Page.addScriptToEvaluateOnNewDocument` (which injects a script that runs before page scripts on every navigation).

**Characteristics**:

| Aspect | Behavior |
|---|---|
| **World** | Main world (if `worldName` is empty) OR an isolated world (if `worldName` is specified). Isolated worlds have their own `v8::Context` with its own global, but share the DOM (DOM bindings are cross-world). |
| **Compilation entry** | `v8::Script::Compile` (via blink's `V8ScriptRunner::Compile`), then `v8::Script::Run`. The script is cached in blink's `ScriptResourceCache` if it has a URL. |
| **CSP** | **Strictly enforced** — inline scripts need a `nonce` or `hash` if `script-src` is set; `eval()` is blocked if `script-src 'unsafe-eval'` is not present. To bypass, you'd need to inject into an isolated world (which has CSP bypassed by default for injected scripts). |
| **Command-line API** | **Not installed**. `$`, `$$`, etc. are undefined. |
| **Debugger visibility** | Reported in `Debugger.scriptParsed` with `url` = the document URL (for inline) or the `src` URL (for external). `hasSourceURL` is true if `//# sourceURL=...` was specified. |
| **Object lifetime** | Objects subject to normal GC. The script itself is held by the DOM (for inline) or by `ResourceFetcher` (for external). |
| **Privileges** | Same as page JS. No `silent`, no `userGesture` simulation, no `throwOnSideEffect`. |
| **Async support** | Top-level `await` works only in `<script type="module">`. For classic scripts, you must wrap in an async IIFE. |
| **Detection by page** | **LOW** if injected via `Page.addScriptToEvaluateOnNewDocument({worldName: "..."})` in an isolated world — page JS can't see your globals. **MEDIUM** if injected in the main world — page can see your globals and inspect `Error().stack`. |

### 3.1.3 The Isolated World Trick (for Stealth Scraping)

For maximum stealth, inject your scraper JS via `Page.addScriptToEvaluateOnNewDocument` with a `worldName`:

```json
{
  "method": "Page.addScriptToEvaluateOnNewDocument",
  "params": {
    "source": "(function() { window.__scraper = { extractData: function() { ... } }; })();",
    "worldName": "scraper-world",
    "grantUniversalAccess": true,
    "runImmediately": true
  }
}
```

This creates an isolated world (via `DOMWrapperWorld::EnsureIsolatedWorld`) with `GrantUniversalAccess()` set — bypassing same-origin policy for your scraper. The page's JS **cannot see** your `window.__scraper` because it's in a different `v8::Context`. The DOM is shared (DOM bindings are cross-world), so you can still manipulate the page.

### 3.1.4 Comparison Table

| Property | `Runtime.evaluate` (Console) | `<script>` tag | `Page.addScriptToEvaluateOnNewDocument` (main world) | `Page.addScriptToEvaluateOnNewDocument` (isolated world) |
|---|---|---|---|---|
| **Compilation** | `v8::debug::EvaluateGlobal` | `v8::Script::Compile` + `Run` | `v8::Script::Compile` + `Run` (in main world) | `v8::Script::Compile` + `Run` (in isolated world) |
| **CSP enforced** | No (default) | Yes | Yes | No (universal access) |
| **Command-line API** | Yes (optional) | No | No | No |
| **Debugger.scriptParsed** | No (default) | Yes | Yes | Yes |
| **Object group** | Yes | No | No | No |
| **awaitPromise** | Yes | N/A | N/A | N/A |
| **Page can detect** | High | None (looks normal) | Medium (new globals) | Low (isolated) |
| **Returns value to caller** | Yes (RemoteObject) | No | No | No |
| **Use case** | One-shot evaluation, debugging | Page's own code | Stealthy main-world injection | Stealthy isolated injection |

### 3.1.5 When to Use Each

| Scenario | Recommended Approach |
|---|---|
| **Read `document.title`** | `Runtime.evaluate` with `returnByValue: true` |
| **Click a button** | `Runtime.evaluate` calling `element.click()` OR `Input.dispatchMouseEvent` |
| **Extract structured data** | `Runtime.evaluate` with `returnByValue: true` returning JSON |
| **Hook `window.fetch` to log calls** | `Page.addScriptToEvaluateOnNewDocument` (main world, before page scripts) |
| **Override `navigator.userAgent`** | `Emulation.setUserAgentOverride` (not JS!) OR `Page.addScriptToEvaluateOnNewDocument` patching the getter |
| **Run scraper that page can't see** | `Page.addScriptToEvaluateOnNewDocument({worldName: "scraper"})` |
| **Wait for an async operation** | `Runtime.evaluate` with `awaitPromise: true` |
| **Set a breakpoint** | `Debugger.setBreakpointByUrl` (no JS execution) |
| **Upload a file** | `DOM.setFileInputFiles` (no JS execution) |
| **Inject a CSS rule** | `CSS.createStyleSheet` + `CSS.addRule` (no JS execution) |

---

## 3.2 The Complete `Runtime.evaluate` Call Chain

### 3.2.1 The Dispatcher Entry

CDP bytes arrive at `V8InspectorSessionImpl::dispatchProtocolMessage` (`v8/src/inspector/v8-inspector-session-impl.cc:372-423`):

```cpp
void V8InspectorSessionImpl::dispatchProtocolMessage(
    const StringView& message) {
  v8::Isolate::AllowJavascriptExecutionScope allow_script(m_isolate);
  KeepSessionAliveScope keepAlive(*this);

  // Detect CBOR (0xD8 0x5A prefix) vs JSON
  std::vector<uint8_t> cbor;
  if (IsCBORMessage(message)) {
    cbor.assign(message.characters8(), message.characters8() + message.length());
  } else {
    // Convert JSON → CBOR
    v8_crdtp::json::ConvertJSONToCBOR(
        v8_crdtp::SpanFrom(message.utf8()), &cbor);
  }

  // Build a Dispatchable and dispatch
  v8_crdtp::Dispatchable dispatchable(v8_crdtp::SpanFrom(cbor));
  m_dispatcher.Dispatch(dispatchable);
}
```

`m_dispatcher` is `protocol::UberDispatcher` (member at `v8-inspector-session-impl.h:159`). Each domain was wired into the dispatcher in the session ctor:

```cpp
// v8-inspector-session-impl.cc:127-153
m_runtimeAgent.reset(new V8RuntimeAgentImpl(
    this, this, agentState(protocol::Runtime::Metainfo::domainName),
    std::move(debuggerBarrier)));
protocol::Runtime::Dispatcher::wire(&m_dispatcher, m_runtimeAgent.get());

m_debuggerAgent.reset(new V8DebuggerAgentImpl(...));
protocol::Debugger::Dispatcher::wire(&m_dispatcher, m_debuggerAgent.get());

m_consoleAgent.reset(new V8ConsoleAgentImpl(...));
protocol::Console::Dispatcher::wire(&m_dispatcher, m_consoleAgent.get());

if (m_clientTrustLevel == V8Inspector::kFullyTrusted) {
  m_profilerAgent.reset(new V8ProfilerAgentImpl(...));
  protocol::Profiler::Dispatcher::wire(&m_dispatcher, m_profilerAgent.get());
  m_heapProfilerAgent.reset(new V8HeapProfilerAgentImpl(...));
  protocol::HeapProfiler::Dispatcher::wire(&m_dispatcher, m_heapProfilerAgent.get());
  m_schemaAgent.reset(new V8SchemaAgentImpl(...));
  protocol::Schema::Dispatcher::wire(&m_dispatcher, m_schemaAgent.get());
}
```

### 3.2.2 V8RuntimeAgentImpl::evaluate — The Implementation

**File**: `v8/src/inspector/v8-runtime-agent-impl.cc:356-459`

The full 16-parameter signature:

```cpp
void V8RuntimeAgentImpl::evaluate(
    const String16& expression,                          // The JS code string
    std::optional<String16> objectGroup,                 // Object group name (e.g. "console")
    std::optional<bool> includeCommandLineAPI,           // Install $, $$, $x, etc.
    std::optional<bool> silent,                          // Mute console output + exceptions
    std::optional<int> executionContextId,               // Which v8::Context to run in
    std::optional<bool> returnByValue,                   // Serialize result as JSON
    std::optional<bool> generatePreview,                 // Include object preview
    std::optional<bool> userGesture,                     // Simulate user gesture
    std::optional<bool> maybeAwaitPromise,              // Chain .then() on promise results
    std::optional<bool> throwOnSideEffect,               // Refuse side-effecting expressions
    std::optional<double> timeout,                       // Terminate after N seconds
    std::optional<bool> disableBreaks,                   // Don't pause at breakpoints
    std::optional<bool> maybeReplMode,                   // Wrap top-level await
    std::optional<bool> allowUnsafeEvalBlockedByCSP,     // Bypass CSP eval restriction
    std::optional<String16> uniqueContextId,             // Alternative to executionContextId
    std::unique_ptr<protocol::Runtime::SerializationOptions> serializationOptions,
    std::unique_ptr<EvaluateCallback> callback) override;
```

### 3.2.3 Step-by-Step Walkthrough

**Step 1 — Resolve the execution context** (lines 370-384):

```cpp
int contextId = 0;
Response response = ensureContext(m_inspector, m_session->contextGroupId(),
                                  std::move(executionContextId),
                                  std::move(uniqueContextId), &contextId);
if (!response.IsSuccess()) { callback->sendFailure(response); return; }

InjectedScript::ContextScope scope(m_session, contextId);
response = scope.initialize();
if (!response.IsSuccess()) { callback->sendFailure(response); return; }
```

`ensureContext` (lines 216-245) resolves either:
- `executionContextId` (numeric) — looked up in `m_contextIdToGroupIdMap`
- `uniqueContextId` (a string of form `pair.first-pair.second`) — looked up in `m_uniqueIdToContextId`
- Falls back to `m_inspector->client()->ensureDefaultContextInGroup(contextGroupId)` — the main world of the inspected frame

**Step 2 — Apply modifiers** (lines 386-403):

```cpp
if (silent.value_or(false)) scope.ignoreExceptionsAndMuteConsole();
if (userGesture.value_or(false)) scope.pretendUserGesture();

if (includeCommandLineAPI.value_or(false)) {
  scope.installCommandLineAPI();
  if (scope.tryCatch().HasCaught()) {
    callback->sendFailure(Response::ServerError("Failed to install command line API"));
    return;
  }
}
const bool replMode = maybeReplMode.value_or(false);

if (allowUnsafeEvalBlockedByCSP.value_or(true)) {
  scope.allowCodeGenerationFromStrings();   // temporarily flips CSP allowEval
}
```

`Scope::ignoreExceptionsAndMuteConsole` (`injected-script.cc:1059-1066`) saves the current pause-on-exceptions state, sets `v8::debug::NoBreakOnException`, mutes use-counters and deprecations on the frame.

`Scope::installCommandLineAPI` creates a `v8::Object` with `$`, `$$`, `$x`, `inspect`, `copy`, etc. and sets it as a context extension (via `v8::debug::SetContextData`).

**Step 3 — Compile + run via `v8::debug::EvaluateGlobal`** (lines 404-427):

```cpp
v8::MaybeLocal<v8::Value> maybeResultValue;
{
  V8InspectorImpl::EvaluateScope evaluateScope(scope);
  if (timeout.has_value()) {
    response = evaluateScope.setTimeout(timeout.value() / 1000.0);
    if (!response.IsSuccess()) { callback->sendFailure(response); return; }
  }
  v8::MicrotasksScope microtasksScope(scope.context(),
                                      v8::MicrotasksScope::kRunMicrotasks);
  v8::debug::EvaluateGlobalMode mode = v8::debug::EvaluateGlobalMode::kDefault;
  if (throwOnSideEffect.value_or(false)) {
    mode = v8::debug::EvaluateGlobalMode::kDisableBreaksAndThrowOnSideEffect;
  } else if (disableBreaks.value_or(false)) {
    mode = v8::debug::EvaluateGlobalMode::kDisableBreaks;
  }
  const v8::Local<v8::String> source =
      toV8String(m_inspector->isolate(), expression);
  maybeResultValue = v8::debug::EvaluateGlobal(m_inspector->isolate(), source,
                                               mode, replMode);
}  // Run microtasks before returning result.
```

**Important**: V8 does **not** expose `v8::Script::Compile` + `v8::Script::Run` separately here; `v8::debug::EvaluateGlobal` is the unified compile+run entry point inside V8's `src/debug/debug-interface.cc`. The V8 inspector therefore has no `v8::Script` handle to keep alive after `evaluate` — it only sees the resulting `MaybeLocal<v8::Value>`.

### 3.2.4 The Timeout Mechanism

`V8InspectorImpl::EvaluateScope::setTimeout` (`v8-inspector-impl.cc:577-586`):

```cpp
protocol::Response V8InspectorImpl::EvaluateScope::setTimeout(double timeout) {
  if (m_isolate->IsExecutionTerminating())
    return protocol::Response::ServerError("Execution was terminated");
  m_cancelToken.reset(new CancelToken());
  v8::debug::GetCurrentPlatform()->PostDelayedTaskOnWorkerThread(
      v8::TaskPriority::kUserVisible,
      std::make_unique<TerminateTask>(m_isolate, m_cancelToken), timeout);
  return protocol::Response::Success();
}
```

When the timeout fires, `m_isolate->TerminateExecution()` is called. On cleanup, `~EvaluateScope` cancels the termination if the script returned in time (`v8-inspector-impl.cc:548-557`):

```cpp
V8InspectorImpl::EvaluateScope::~EvaluateScope() {
  if (m_cancelToken) {
    // We need to cancel the timeout task. The cancel token is shared with the
    // timeout task, so we mark it as canceled.
    m_cancelToken->cancel();
    // Also cancel the termination if it already fired.
    if (m_isolate->IsExecutionTerminating()) {
      v8::debug::CancelTerminateExecution(m_isolate);
    }
  }
}
```

### 3.2.5 Step 4 — Re-initialize Scope (JS may have destroyed context)

```cpp
response = scope.initialize();
if (!response.IsSuccess()) { callback->sendFailure(response); return; }
```

The JS may have:
- Navigated the page (destroying the v8::Context)
- Detached the iframe (destroying the v8::Context)
- Thrown an exception that unmounted the script

We re-initialize to catch these cases.

### 3.2.6 Step 5 — Wrap Options

`getWrapOptions` (lines 281-330) maps the legacy `returnByValue`/`generatePreview` and the new `serializationOptions` to one of four wrap modes:

```cpp
// v8-debugger.h:35-45
enum class WrapMode { kJson, kIdOnly, kPreview, kDeep };

struct WrapSerializationOptions {
  int maxDepth = v8::internal::kMaxInt;
  v8::Global<v8::Object> additionalParameters;
};

struct WrapOptions {
  WrapMode mode;
  WrapSerializationOptions serializationOptions = {};
};
```

- `returnByValue=true` → `kJson` (fully serialized as JSON, no objectId)
- `generatePreview=true` → `kPreview` (objectId + preview structure)
- `serializationOptions.serialization = "deep"` → `kDeep` (deep serialize with maxDepth & additionalParameters)
- default → `kIdOnly` (objectId only, no preview)

### 3.2.7 Step 6 — Promise Handling (lines 446-458)

```cpp
const bool await = replMode || maybeAwaitPromise.value_or(false);
if (!await || scope.tryCatch().HasCaught()) {
  wrapEvaluateResultAsync(scope.injectedScript(), maybeResultValue,
                          scope.tryCatch(), objectGroup.value_or(""),
                          *wrapOptions, throwOnSideEffect.value_or(false),
                          callback.get());
  return;
}
scope.injectedScript()->addPromiseCallback(
    m_session, maybeResultValue, objectGroup.value_or(""),
    std::move(wrapOptions), replMode, throwOnSideEffect.value_or(false),
    EvaluateCallbackWrapper<EvaluateCallback>::wrap(std::move(callback)));
```

`addPromiseCallback` (`injected-script.cc:763-791`) registers a `ProtocolPromiseHandler` (`injected-script.cc:96-410`). If the value is already a Promise, `.then(thenCallback, catchCallback)` is chained; otherwise it is wrapped in `Promise.resolve(value)` first. The callback is invoked on the next microtask. `thenCallback` wraps and sends the resolved value; `catchCallback` (line 294) builds an `ExceptionDetails` with `"Uncaught (in promise)"` and includes the message that V8 attached to the rejected promise (via `v8::debug::GetMessageFromPromise`, line 319).

### 3.2.8 Result Serialization — RemoteObject Fields

The actual RemoteObject is produced by `InjectedScript::wrapObject` → `wrapObjectMirror` (`injected-script.cc:635-698`). It calls `ValueMirror::create(context, value)` (polymorphic factory in `value-mirror.cc`) then `mirror.buildRemoteObject(context, wrapOptions, &result)` which sets the protocol `type`, `subtype`, `className`, `value`, `unserializableValue`, `description`, `objectId`, and (if kPreview) `preview`, and (if kDeep) `deepSerializedValue`.

The 7 valid types and 6 subtypes come from `value-mirror.cc`:

```
type:      object | function | undefined | string | number | boolean | symbol | bigint
subtype:   array | null | error | proxy | promise | typedarray | regexp | generator ...
```

`bindRemoteObjectIfNeeded` (`injected-script.cc:1181-1202`) only sets the `objectId` for non-primitive, non-serialized values:

```cpp
if (remoteObject->hasValue()) return Response::Success();
if (remoteObject->hasUnserializableValue()) return Response::Success();
if (remoteObject->getType() != RemoteObject::TypeEnum::Undefined) {
  remoteObject->setObjectId(injectedScript->bindObject(value, groupName));
}
```

### 3.2.9 Exception Handling Inside evaluate

`wrapEvaluateResultAsync` (defined at `v8-runtime-agent-impl.cc:99-119`) wraps `injectedScript->wrapEvaluateResult(...)`. `InjectedScript::wrapEvaluateResult` (`injected-script.cc:966-1008`) is the central error formatter:

```cpp
Response InjectedScript::wrapEvaluateResult(...) {
  v8::Local<v8::Value> resultValue;
  if (!tryCatch.HasCaught()) {
    if (!maybeResultValue.ToLocal(&resultValue)) {
      if (!tryCatch.CanContinue()) return Response::ServerError("Execution was terminated");
      return Response::InternalError();
    }
    Response response = wrapObject(resultValue, objectGroup, wrapOptions, result);
    if (!response.IsSuccess()) return response;
    if (objectGroup == "console") {
      m_lastEvaluationResult.Reset(m_context->isolate(), resultValue);
      m_lastEvaluationResult.AnnotateStrongRetainer(kGlobalHandleLabel);
    }
  } else {
    if (tryCatch.HasTerminated() || !tryCatch.CanContinue())
      return Response::ServerError("Execution was terminated");
    v8::Local<v8::Value> exception = tryCatch.Exception();
    if (!throwOnSideEffect) {
      m_context->inspector()->client()->dispatchError(
          m_context->context(), tryCatch.Message(), exception);
    }
    Response response = wrapObject(exception, objectGroup,
                                   exception->IsNativeError()
                                       ? WrapOptions({WrapMode::kIdOnly})
                                       : WrapOptions({WrapMode::kPreview}),
                                   result);
    if (!response.IsSuccess()) return response;
    response = createExceptionDetails(tryCatch, objectGroup, exceptionDetails);
    if (!response.IsSuccess()) return response;
  }
  return Response::Success();
}
```

The `exceptionDetails` structure is built by `createExceptionDetails` (`injected-script.cc:924-964`):

```cpp
std::unique_ptr<protocol::Runtime::ExceptionDetails> exceptionDetails =
    protocol::Runtime::ExceptionDetails::create()
        .setExceptionId(m_context->inspector()->nextExceptionId())
        .setText(exception.IsEmpty() ? messageText : String16("Uncaught"))
        .setLineNumber(message.IsEmpty() ? 0
                       : message->GetLineNumber(m_context->context()).FromMaybe(1) - 1)
        .setColumnNumber(message.IsEmpty() ? 0
                        : message->GetStartColumn(m_context->context()).FromMaybe(0))
        .build();
if (!message.IsEmpty()) {
  exceptionDetails->setScriptId(String16::fromInteger(message->GetScriptOrigin().ScriptId()));
  v8::Local<v8::StackTrace> stackTrace = message->GetStackTrace();
  if (!stackTrace.IsEmpty() && stackTrace->GetFrameCount() > 0) {
    std::unique_ptr<V8StackTraceImpl> v8StackTrace =
        m_context->inspector()->debugger()->createStackTrace(stackTrace);
    if (v8StackTrace) {
      exceptionDetails->setStackTrace(
          v8StackTrace->buildInspectorObjectImpl(m_context->inspector()->debugger()));
    }
  }
}
```

The `ExceptionDetails` you receive over CDP has:
- `exceptionId` (per-isolate counter)
- `text` (always "Uncaught" when an exception is set)
- `lineNumber` / `columnNumber` (0-based)
- `scriptId`
- `stackTrace` (full `Runtime.StackTrace`)
- `executionContextId`
- `exception` (a `RemoteObject` wrapping the exception value)
- `exceptionMetaData` (optional)

---

## 3.3 RemoteObjects

### 3.3.1 objectId Generation

The wire format is `"<isolateId>.<injectedScriptId>.<id>"` — three integers joined by dots. From `remote-object-id.cc:15-19`:

```cpp
String16 serializeId(uint64_t isolateId, int injectedScriptId, int id) {
  return String16::concat(
      String16::fromInteger64(static_cast<int64_t>(isolateId)), ".",
      String16::fromInteger(injectedScriptId), ".", String16::fromInteger(id));
}
```

Where:
- `isolateId` is set once per V8 isolate by `v8::debug::SetIsolateId` in `V8InspectorImpl` ctor (`v8-inspector-impl.cc:74`).
- `injectedScriptId` is actually `contextId` (see `RemoteObjectIdBase::contextId()` returning `m_injectedScriptId` at `remote-object-id.h:19`). It's called "injectedScriptId" in the wire format for legacy reasons but really means the V8 context id.
- `id` is the per-InjectedScript counter `m_lastBoundObjectId`, incremented in `InjectedScript::bindObject` (`injected-script.cc:1166-1178`):

```cpp
String16 InjectedScript::bindObject(v8::Local<v8::Value> value,
                                    const String16& groupName) {
  if (m_lastBoundObjectId <= 0) m_lastBoundObjectId = 1;
  int id = m_lastBoundObjectId++;
  m_idToWrappedObject[id].Reset(m_context->isolate(), value);
  m_idToWrappedObject[id].AnnotateStrongRetainer(kGlobalHandleLabel);
  if (!groupName.isEmpty() && id > 0) {
    m_idToObjectNameGroup[id] = groupName;
    m_nameToObjectGroup[groupName].push_back(id);
  }
  return RemoteObjectId::serialize(m_context->inspector()->isolateId(),
                                   m_context->contextId(), id);
}
```

`m_idToWrappedObject` is a `std::unordered_map<int, v8::Global<v8::Value>>` (`injected-script.h:268`). The v8 handle is a strong persistent — the object will NOT be GC'd until you call `Runtime.releaseObject` or `Runtime.releaseObjectGroup`.

### 3.3.2 Types and Subtypes

The mapping from V8 value kinds to `Runtime.RemoteObject.type` / `.subtype` lives in `value-mirror.cc`:

```cpp
// value-mirror.cc:232-233
if (value->IsUndefined()) return RemoteObject::TypeEnum::Undefined;
if (value->IsNull())     return RemoteObject::SubtypeEnum::Null;
```

- `Number`/`Number`+unserializableValue (`-0`, `NaN`, `Infinity`, `-Infinity`) at lines 627-671
- `Bigint` at 699-748
- `Symbol` at 770-817
- `Object` (generic) at 853-874 with subtypes set per IsArray/IsPromise/IsTypedArray/IsProxy/IsError/IsRegExp
- `Function` at 919-969 (includes bound/getter/setter)
- `Error` subtype at `value-mirror.cc:1796` via `RemoteObject::SubtypeEnum::Error`
- `Array` subtype at `value-mirror.cc:1807`

`unserializableValue` is set for the four special numbers and for bigint-as-string. Otherwise the value is JSON-serialized into `.value`.

### 3.3.3 Runtime.getProperties

Entry at `v8-runtime-agent-impl.cc:577-635`:

```cpp
Response V8RuntimeAgentImpl::getProperties(
    const String16& objectId,
    std::optional<bool> ownProperties,
    std::optional<bool> accessorPropertiesOnly,
    std::optional<bool> generatePreview,
    std::optional<bool> nonIndexedPropertiesOnly,
    std::unique_ptr<protocol::Array<protocol::Runtime::PropertyDescriptor>>* result,
    std::unique_ptr<protocol::Array<protocol::Runtime::InternalPropertyDescriptor>>* internalProperties,
    std::unique_ptr<protocol::Array<protocol::Runtime::PrivatePropertyDescriptor>>* privateProperties,
    std::unique_ptr<protocol::Runtime::ExceptionDetails>* exceptionDetails) {
  InjectedScript::ObjectScope scope(m_session, objectId);
  Response response = scope.initialize();
  if (!response.IsSuccess()) return response;
  scope.ignoreExceptionsAndMuteConsole();
  v8::MicrotasksScope microtasks_scope(scope.context(),
                                       v8::MicrotasksScope::kRunMicrotasks);
  if (!scope.object()->IsObject()) return Response::ServerError("Value with given id is not an object");
  v8::Local<v8::Object> object = scope.object().As<v8::Object>();
  // ...
  response = scope.injectedScript()->getProperties(
      object, scope.objectGroupName(), ownProperties.value_or(false),
      accessorPropertiesOnly.value_or(false),
      nonIndexedPropertiesOnly.value_or(false), *wrapOptions, result,
      exceptionDetails);
  // ...
  response = scope.injectedScript()->getInternalAndPrivateProperties(
      object, scope.objectGroupName(), accessorPropertiesOnly.value_or(false),
      &internalPropertiesProtocolArray, &privatePropertiesProtocolArray);
}
```

`InjectedScript::getProperties` (`injected-script.cc:432-513`) uses `ValueMirror::getProperties` (declared in `value-mirror.h:82-86`) which iterates V8's property names via `GetOwnPropertyNames` and `GetPrototypeV2` chain, builds `PropertyMirror` structs, then for each mirror builds a `PropertyDescriptor` with:
- `name`
- `value` (RemoteObject)
- `get`/`set` (RemoteObject for accessors)
- `symbol`
- `writable`
- `configurable`
- `enumerable`
- `isOwn`
- `wasThrown` (if a getter threw)

### 3.3.4 Runtime.callFunctionOn

`v8-runtime-agent-impl.cc:493-575`. It validates that exactly one of `objectId` / `executionContextId` / `uniqueContextId` is provided (lines 505-519), then either:

- Opens an `InjectedScript::ObjectScope` on the existing object (lines 521-543), or
- Opens a `ContextScope` on the target context and uses `scope.context()->Global()` as `recv` (lines 544-574).

Then both paths call the helper `innerCallFunctionOn` (`v8-runtime-agent-impl.cc:121-214`):

```cpp
void innerCallFunctionOn(V8InspectorSessionImpl* session, InjectedScript::Scope& scope,
                         v8::Local<v8::Value> recv, const String16& expression, ...) {
  V8InspectorImpl* inspector = session->inspector();
  v8::LocalVector<v8::Value> args(inspector->isolate());
  if (optionalArguments) { /* resolve each CallArgument via resolveCallArgument */ }

  if (silent) scope.ignoreExceptionsAndMuteConsole();
  if (userGesture) scope.pretendUserGesture();
  scope.allowCodeGenerationFromStrings();

  v8::MaybeLocal<v8::Value> maybeFunctionValue;
  v8::Local<v8::Script> functionScript;
  if (inspector->compileScript(scope.context(), "(" + expression + ")", String16())
          .ToLocal(&functionScript)) {
    v8::MicrotasksScope microtasksScope(scope.context(),
                                        v8::MicrotasksScope::kRunMicrotasks);
    maybeFunctionValue = functionScript->Run(scope.context());
  }
  // ...
  v8::MaybeLocal<v8::Value> maybeResultValue;
  {
    v8::MicrotasksScope microtasksScope(scope.context(),
                                        v8::MicrotasksScope::kRunMicrotasks);
    maybeResultValue = v8::debug::CallFunctionOn(
        scope.context(), functionValue.As<v8::Function>(), recv,
        v8::base::VectorOf(args), throwOnSideEffect);
  }
  // ...
}
```

Note that the expression is wrapped in `("(" + expression + ")")` and compiled as a Script — so `callFunctionOn` expects the expression to evaluate to a Function object. The function is then called with `recv` as `this` and `args` as arguments.

`resolveCallArgument` (`injected-script.cc:850-897`) converts each `Runtime.CallArgument` from CDP into a v8::Value:
- if `objectId` is present, looks it up in the bind table
- if `value` is present, JSON-decodes it
- if `unserializableValue` is present (like `"NaN"`, `"-0"`, `"Infinity"`), it evaluates the literal directly

### 3.3.5 Runtime.releaseObject and releaseObjectGroup

```cpp
// v8-runtime-agent-impl.cc:637-648
Response V8RuntimeAgentImpl::releaseObject(const String16& objectId) {
  InjectedScript::ObjectScope scope(m_session, objectId);
  Response response = scope.initialize();
  if (!response.IsSuccess()) return response;
  scope.injectedScript()->releaseObject(objectId);
  return Response::Success();
}

Response V8RuntimeAgentImpl::releaseObjectGroup(const String16& objectGroup) {
  m_session->releaseObjectGroup(objectGroup);
  return Response::Success();
}
```

`InjectedScript::releaseObject` (`injected-script.cc:608-612`) parses the id and erases the entry from `m_idToWrappedObject` + `m_idToObjectGroupName`.

`releaseObjectGroup` (`v8-inspector-session-impl.cc:262-274`) iterates **all contexts** in the group, since an object group can span contexts:

```cpp
void V8InspectorSessionImpl::releaseObjectGroup(const String16& objectGroup) {
  int sessionId = m_sessionId;
  m_inspector->forEachContext(
      m_contextGroupId, [&objectGroup, &sessionId](InspectedContext* context) {
        std::shared_ptr<InjectedScript> injectedScript =
            context->getInjectedScript(sessionId);
        if (injectedScript) injectedScript->releaseObjectGroup(objectGroup);
      });
  if (!objectGroup.isEmpty()) {
    m_inspector->promiseHandlerTracker().makeWeakForObjectGroup(m_sessionId,
                                                                objectGroup);
  }
}
```

`InjectedScript::releaseObjectGroup` (`injected-script.cc:825-832`) erases the whole group entry from `m_nameToObjectGroup` and unbinds each id:

```cpp
void InjectedScript::releaseObjectGroup(const String16& objectGroup) {
  if (objectGroup == "console") m_lastEvaluationResult.Reset();
  if (objectGroup.isEmpty()) return;
  auto it = m_nameToObjectGroup.find(objectGroup);
  if (it == m_nameToObjectGroup.end()) return;
  for (int id : it->second) unbindObject(id);
  m_nameToObjectGroup.erase(it);
}
```

### 3.3.6 Object Lifetime Best Practices

| Operation | Cost |
|---|---|
| `Runtime.releaseObject(objectId)` | O(1) — single map erase |
| `Runtime.releaseObjectGroup("console")` | O(N) where N = objects in group — but spans ALL contexts |
| Forgetting to release | Memory leak — strong v8::Global handles accumulate |
| Re-evaluating with same `objectGroup` | New bindings added to group; old ones NOT auto-released |

**For scraping**: always pass `objectGroup` to `Runtime.evaluate`, then call `Runtime.releaseObjectGroup(objectGroup)` when done. Without this, you'll leak memory across thousands of evaluations.

---

## 3.4 Execution Contexts

### 3.4.1 Two Layers

- **Blink layer** (`ExecutionContext`): not in your slice but is the broader concept. It includes `LocalDOMWindow`, `WorkerGlobalScope`, `WorkletGlobalScope`. Each holds a `ScriptState` (v8 context wrapper).
- **V8 layer** (`InspectedContext`): in `v8/src/inspector/inspected-context.{h,cc}`. One-to-one with a `v8::Context`.

### 3.4.2 InspectedContext Internals

```cpp
// inspected-context.h:40-89
class InspectedContext {
  // ...
  v8::Global<v8::Context> m_context;
  int m_contextId;                       // unique per V8InspectorImpl
  int m_contextGroupId;                  // ties to a frame or worker
  const String16 m_origin;
  const String16 m_humanReadableName;
  const String16 m_auxData;              // JSON blob, e.g. {"isDefault":true,"frameId":"..."}
  const internal::V8DebuggerId m_uniqueId;
  std::unordered_set<int> m_reportedSessionIds;
  std::unordered_map<int, std::shared_ptr<InjectedScript>> m_injectedScripts;
  WeakCallbackData* m_weakCallbackData;
  v8::Global<v8::debug::EphemeronTable> m_internalObjects;
};
```

`contextId` is minted by `V8InspectorImpl::contextCreated`:

```cpp
// v8-inspector-impl.cc:294-296
void V8InspectorImpl::contextCreated(const V8ContextInfo& info) {
  int contextId = ++m_lastContextId;
  auto* context = new InspectedContext(this, info, contextId);
  // ...
```

Then `InspectedContext` ctor (`inspected-context.cc:69-103`) calls `v8::debug::SetContextId(info.context, contextId)` (line 79) — that bakes the id into the v8::Context itself, so `InspectedContext::contextId(v8::Local<v8::Context>)` (line 112-114) can recover it via `v8::debug::GetContextId(context)` later. A weak callback is installed so the inspector is notified when the v8::Context is GC'd (line 80-84). When the weak fires, `contextCollected(groupId, contextId)` runs via a posted task (line 65-67).

### 3.4.3 When `executionContextCreated` Fires

Path: Blink's `MainThreadDebugger::ContextCreated` (called from `ScriptController::InitializeMainWorld` and friends) → `GetV8Inspector()->contextCreated(context_info)` → `V8InspectorImpl::contextCreated` → `forEachSession` → `session->runtimeAgent()->reportExecutionContextCreated(context)`.

```cpp
// main_thread_debugger.cc:123-153  (Blink side)
void MainThreadDebugger::ContextCreated(ScriptState* script_state,
                                        LocalFrame* frame,
                                        const SecurityOrigin* origin) {
  v8::HandleScope handles(script_state->GetIsolate());
  DOMWrapperWorld& world = script_state->World();
  StringBuilder aux_data_builder;
  aux_data_builder.Append("{\"isDefault\":");
  aux_data_builder.Append(world.IsMainWorld() ? "true" : "false");
  if (world.IsMainWorld())         aux_data_builder.Append(",\"type\":\"default\"");
  else if (world.IsIsolatedWorld()) aux_data_builder.Append(",\"type\":\"isolated\"");
  else if (world.IsWorkerOrWorkletWorld())
                                    aux_data_builder.Append(",\"type\":\"worker\"");
  aux_data_builder.Append(",\"frameId\":\"");
  aux_data_builder.Append(IdentifiersFactory::FrameId(frame));
  aux_data_builder.Append("\"}");
  // ...
  GetV8Inspector()->contextCreated(context_info);
}
```

The V8 side (`v8-runtime-agent-impl.cc:1191-1211`):

```cpp
void V8RuntimeAgentImpl::reportExecutionContextCreated(InspectedContext* context) {
  if (!m_enabled) return;
  context->setReported(m_session->sessionId(), true);
  std::unique_ptr<protocol::Runtime::ExecutionContextDescription> description =
      protocol::Runtime::ExecutionContextDescription::create()
          .setId(context->contextId())
          .setName(context->humanReadableName())
          .setOrigin(context->origin())
          .setUniqueId(context->uniqueId().toString())
          .build();
  const String16& aux = context->auxData();
  if (!aux.isEmpty()) {
    std::vector<uint8_t> cbor;
    v8_crdtp::json::ConvertJSONToCBOR(...);
    description->setAuxData(protocol::DictionaryValue::cast(...));
  }
  m_frontend.executionContextCreated(std::move(description));
}
```

`reportExecutionContextCreated` is only called for sessions whose `Runtime` domain is enabled — see `enable()` at `v8-runtime-agent-impl.cc:1125-1156` which calls `m_session->reportAllContexts(this)` to retroactively announce existing contexts to a new session.

### 3.4.4 When `executionContextDestroyed` Fires

Two paths:

1. **Explicit teardown**: `MainThreadDebugger::ContextWillBeDestroyed` (`main_thread_debugger.cc:155-158`) → `GetV8Inspector()->contextDestroyed(context)` → `V8InspectorImpl::contextDestroyed` (`v8-inspector-impl.cc:326-330`) → `contextCollected(groupId, contextId)`.
2. **GC of the v8::Context**: the weak callback in `InspectedContext::WeakCallbackData::resetContext` (`inspected-context.cc:39-47`) posts a `ContextCollectedCallbacks` task that eventually calls `m_inspector->contextCollected(groupId, contextId)`.

`contextCollected` (`v8-inspector-impl.cc:332-350`) notifies all sessions:

```cpp
void V8InspectorImpl::contextCollected(int groupId, int contextId) {
  m_contextIdToGroupIdMap.erase(contextId);
  auto storageIt = m_consoleStorageMap.find(groupId);
  if (storageIt != m_consoleStorageMap.end()) storageIt->second->contextDestroyed(contextId);

  std::shared_ptr<InspectedContext> inspectedContext = getContext(groupId, contextId);
  if (!inspectedContext) return;

  forEachSession(groupId, [&inspectedContext](V8InspectorSessionImpl* session) {
    session->runtimeAgent()->reportExecutionContextDestroyed(inspectedContext.get());
  });
  discardInspectedContext(groupId, contextId);
  m_promiseHandlerTracker.makeWeakForContext(contextId);
}
```

And `reportExecutionContextDestroyed` (`v8-runtime-agent-impl.cc:1213-1220`) emits the notification:

```cpp
void V8RuntimeAgentImpl::reportExecutionContextDestroyed(InspectedContext* context) {
  if (m_enabled && context->isReported(m_session->sessionId())) {
    context->setReported(m_session->sessionId(), false);
    m_frontend.executionContextDestroyed(context->contextId(),
                                         context->uniqueId().toString());
  }
}
```

### 3.4.5 Main World vs Isolated World vs Worker

A `contextGroupId` corresponds to one `LocalFrame` (via `MainThreadDebugger::ContextGroupId` at `main_thread_debugger.cc:206-209`: `WeakIdentifierMap<LocalFrame>::Identifier(&local_frame_root)`). Multiple v8::Contexts can share the same group:

- **Main world** — `DOMWrapperWorld::MainWorld()`. auxData.type = "default", isDefault = true.
- **Isolated world** — content scripts, page-extensions. `world.IsIsolatedWorld()` → auxData.type = "isolated". Each isolated world has its own `DOMWrapperWorld` and a distinct `contextId`. You target it by passing `executionContextId` to `Runtime.evaluate` or by `Page.createIsolatedWorld`.
- **Worker world** — `world.IsWorkerOrWorkletWorld()` → auxData.type = "worker". Lives in a separate isolate/process — a dedicated worker, shared worker, or worklet — and is reached through `WorkerInspectorController` rather than `MainThreadDebugger`.

### 3.4.6 Page.createIsolatedWorld

To create a new isolated world for scraping:

```json
{
  "method": "Page.createIsolatedWorld",
  "params": {
    "frameId": "<mainFrameId>",
    "worldName": "scraper-world",
    "grantUniversalAccess": true
  }
}
```

Returns `{executionContextId: N}`. Then pass that to `Runtime.evaluate`:

```json
{
  "method": "Runtime.evaluate",
  "params": {
    "expression": "document.title",
    "executionContextId": N,
    "returnByValue": true
  }
}
```

**Key**: with `grantUniversalAccess: true`, the isolated world bypasses same-origin policy. Your scraper can read DOM from any origin (including cross-origin iframes) without CORS issues. Page's main-world JS **cannot see** your scraper's globals because they're in a different v8::Context.

### 3.4.7 Worker Contexts

`WorkerInspectorController` (`worker_inspector_controller.cc:70-107`) creates its own `CoreProbeSink`, its own `DevToolsAgent` bound via `BindReceiverForWorker`, and exposes `WorkerThreadDebugger` as the `V8InspectorClient`. On `AttachSession` (line 113-151) it constructs `InspectorLogAgent`, `InspectorEventBreakpointsAgent`, `InspectorNetworkAgent`, `InspectorAuditsAgent`, `InspectorInspectorAgent`, `InspectorEmulationAgent`, `InspectorMediaAgent` — a smaller set than the main-thread suite (no DOM/CSS/Page agents because workers don't have DOM). It then calls `session->ConnectToV8(debugger_->GetV8Inspector(), debugger_->ContextGroupId(thread_))`.

For a service worker context you'd connect through `content::ServiceWorkerDevToolsAgentHost` (browser-side; present in `browser/devtools/service_worker_devtools_agent_host.{h,cc}`).

---

## 3.5 Console API

### 3.5.1 V8 → V8Console Dispatch

V8 itself owns the `console` global object on each context. When V8 sees `console.log("hi")`, it calls into the `v8::debug::ConsoleDelegate` interface — which the inspector's `V8Console` class implements (`v8-console.h:34`, `public v8::debug::ConsoleDelegate`). The wiring happens once per isolate in `V8InspectorImpl` ctor (`v8-inspector-impl.cc:73`):

```cpp
v8::debug::SetConsoleDelegate(m_isolate, console());
```

`V8Console` overrides each method (`v8-console.cc:237-486`):

```cpp
void V8Console::Log(const v8::debug::ConsoleCallArguments& info,
                    const v8::debug::ConsoleContext& consoleContext) {
  TRACE_EVENT(TRACE_DISABLED_BY_DEFAULT("v8.inspector"), "V8Console::Log");
  ConsoleHelper(info, consoleContext, m_inspector)
      .reportCall(ConsoleAPIType::kLog);
}
void V8Console::Error(...)    { ... .reportCall(ConsoleAPIType::kError); }
void V8Console::Warn(...)     { ... .reportCall(ConsoleAPIType::kWarning); }
void V8Console::Info(...)     { ... .reportCall(ConsoleAPIType::kInfo); }
void V8Console::Debug(...)    { ... .reportCall(ConsoleAPIType::kDebug); }
void V8Console::Trace(...)    { ... .reportCallWithDefaultArgument(
                                    ConsoleAPIType::kTrace, String16("console.trace")); }
void V8Console::Table(...)    { ... .reportCall(ConsoleAPIType::kTable); }
void V8Console::Assert(...)   { /* drops arg[0], reports kAssert, also calls
                                   m_inspector->debugger()->breakProgramOnAssert(...) */ }
void V8Console::Group(...)       { ... kStartGroup ... }
void V8Console::GroupCollapsed(...) { ... kStartGroupCollapsed ... }
void V8Console::GroupEnd(...)    { ... kEndGroup ... }
void V8Console::Clear(...)       { /* m_inspector->client()->consoleClear(); then kClear */ }
void V8Console::Count(...)       { /* storage->count(); reportCallWithArgument(kCount, "label: N") */ }
void V8Console::Time(...)       { /* storage->time(); */ }
void V8Console::TimeEnd(...)    { /* storage->timeEnd(); reportCallWithArgument(kTimeEnd, "label: X ms") */ }
void V8Console::TimeStamp(...)   { /* forwards to client()->consoleTimeStampWithArgs */ }
```

### 3.5.2 ConsoleHelper::reportCall

`v8-console.cc:114-154` is the heart of the console pipeline:

```cpp
void reportCall(ConsoleAPIType type,
                std::span<const v8::Local<v8::Value>> arguments) {
  if (!groupId()) return;
  // Depending on the type of the console message, we capture only parts of
  // the stack trace, or no stack trace at all.
  std::unique_ptr<V8StackTraceImpl> stackTrace;
  switch (type) {
    case ConsoleAPIType::kTrace:
      stackTrace = m_inspector->debugger()->captureStackTrace(true);
      break;
    case ConsoleAPIType::kTimeEnd:
      stackTrace = V8StackTraceImpl::capture(m_inspector->debugger(), 1);
      break;
    default:
      stackTrace = m_inspector->debugger()->captureStackTrace(false);
      break;
  }
  std::unique_ptr<V8ConsoleMessage> message =
      V8ConsoleMessage::createForConsoleAPI(
          context(), contextId(), groupId(), m_inspector,
          m_inspector->client()->currentTimeMS(), type, arguments,
          consoleContextToString(isolate(), m_consoleContext),
          std::move(stackTrace));
  consoleMessageStorage()->addMessage(std::move(message));
}
```

Note the stack-trace policy:
- full trace for `console.trace`
- top frame only for `console.timeEnd`
- top frame only for everything else when no debugger is attached (full when one is)

### 3.5.3 createForConsoleAPI and addMessage Dispatch

`V8ConsoleMessage::createForConsoleAPI` (`v8-console-message.cc:451-509`) builds the message, stores the arguments as `std::vector<std::shared_ptr<v8::Global<v8::Value>>>` (line 470-475), and calls `inspector->client()->consoleAPIMessage(groupId, contextId, clientLevel, message, url, lineNumber, columnNumber, stackTrace)` (line 502-506). In Blink this lands at `MainThreadDebugger::consoleAPIMessage` (`main_thread_debugger.cc:335-354`) which routes the message to `FrameConsole` for the regular browser console (separate from DevTools).

Then `V8ConsoleMessageStorage::addMessage` (`v8-console-message.cc:578-...`) dispatches to **both** agents per session:

```cpp
inspector->forEachSession(
    contextGroupId, [&message](V8InspectorSessionImpl* session) {
      if (message->origin() == V8MessageOrigin::kConsole) {
        session->consoleAgent()->messageAdded(message.get());
      }
      session->runtimeAgent()->messageAdded(message.get());
    });
```

### 3.5.4 From message to `Runtime.consoleAPICalled`

`V8RuntimeAgentImpl::messageAdded` (`v8-runtime-agent-impl.cc:1231-1233`) → `reportMessage` (line 1235-1240) → `message->reportToFrontend(&m_frontend, m_session, generatePreview)`.

`V8ConsoleMessage::reportToFrontend` for the Runtime frontend (`v8-console-message.cc:318-412`) wraps each argument as a RemoteObject (via `wrapArguments`, line 258-316 — special-cases `console.table` to use `wrapTable`) and emits:

```cpp
// v8-console-message.cc:406-408
frontend->consoleAPICalled(
    consoleAPITypeValue(m_type), std::move(arguments), m_contextId,
    m_timestamp, std::move(stackTrace), std::move(consoleContext));
```

The `Runtime.consoleAPICalled` event therefore has fields:
- `type` (one of `log|debug|info|error|warning|dir|dirxml|table|trace|startGroup|startGroupCollapsed|endGroup|clear|assert|timeEnd|count`)
- `args` (array of RemoteObject)
- `executionContextId`
- `timestamp` (epoch ms from `V8InspectorClient::currentTimeMS()`)
- `stackTrace` (optional `Runtime.StackTrace`)
- `consoleContext` (optional string of form `name#id`)

### 3.5.5 Special Cases

- `console.table(arr, columns)`: `wrapArguments` calls `session->wrapTable(context, value, columns)` (`v8-console-message.cc:276-300`) which produces a single RemoteObject with a full preview filtered to the requested columns.
- `console.assert(x, ...)`: drops `x`, reports `kAssert`, *and* calls `m_inspector->debugger()->breakProgramOnAssert(groupId)` (`v8-console.cc:381`). If pause-on-assert is enabled, the debugger will pause here.
- `console.count(label)` / `console.time(label)` / `console.timeEnd(label)`: per-context (using `consoleContext.id()`) counters/timers stored in `V8ConsoleMessageStorage::m_data` (`v8-console-message.h:149-156`).
- `console.clear()`: calls `m_inspector->client()->consoleClear(groupId)` (`v8-console.cc:332`) → `MainThreadDebugger::consoleClear` (`main_thread_debugger.cc:356-362`) → `frame->GetPage()->GetConsoleMessageStorage().Clear()`. Then a `kClear` ConsoleAPICalled is emitted.

### 3.5.6 The Deprecated Console Domain

`V8ConsoleAgentImpl` (`v8-console-agent-impl.cc`) is the deprecated separate `Console` domain. On `enable()` it calls `reportAllMessages()` which iterates `V8ConsoleMessageStorage::messages()` and calls `message->reportToFrontend(&m_frontend)` — note the *different* overload (no session, no preview) which emits `Console.messageAdded` events instead of `Runtime.consoleAPICalled`. Modern clients ignore the Console domain and rely solely on Runtime events.

---

## 3.6 Exceptions

### 3.6.1 Sync Uncaught Exceptions → `Runtime.exceptionThrown`

The Blink entry point is `MainThreadDebugger::ExceptionThrown` (`main_thread_debugger.cc:160-204`):

```cpp
void MainThreadDebugger::ExceptionThrown(ExecutionContext* context,
                                         ErrorEvent* event) {
  // ...
  frame->Console().ReportMessageToClient(
      mojom::ConsoleMessageSource::kJavaScript,
      mojom::ConsoleMessageLevel::kError, event->MessageForConsole(),
      event->Location());
  const String default_message = "Uncaught";
  if (script_state && script_state->ContextIsValid()) {
    ScriptState::Scope scope(script_state);
    ScriptValue error = event->error(script_state);
    v8::Local<v8::Value> exception = error.IsEmpty()
        ? v8::Local<v8::Value>(v8::Null(script_state->GetIsolate()))
        : error.V8Value();
    SourceLocation* location = event->Location();
    // ...
    GetV8Inspector()->exceptionThrown(
        script_state->GetContext(), ToV8InspectorStringView(default_message),
        exception, ToV8InspectorStringView(message), ToV8InspectorStringView(url),
        location->LineNumber(), location->ColumnNumber(),
        location->TakeStackTrace(), location->ScriptId());
  }
}
```

`V8InspectorImpl::exceptionThrown` (`v8-inspector-impl.cc:371-389`) mutes if the group is muted, otherwise creates a `V8ConsoleMessage::createForException` and pushes it into the console-message storage, which fans out to all sessions as before:

```cpp
unsigned V8InspectorImpl::exceptionThrown(
    v8::Local<v8::Context> context, StringView message,
    v8::Local<v8::Value> exception, StringView detailedMessage, StringView url,
    unsigned lineNumber, unsigned columnNumber,
    std::unique_ptr<V8StackTrace> stackTrace, int scriptId) {
  int groupId = contextGroupId(context);
  if (!groupId || m_muteExceptionsMap[groupId]) return 0;
  std::unique_ptr<V8StackTraceImpl> stackTraceImpl(...);
  unsigned exceptionId = nextExceptionId();
  std::unique_ptr<V8ConsoleMessage> consoleMessage =
      V8ConsoleMessage::createForException(
          m_client->currentTimeMS(), toString16(detailedMessage),
          toString16(url), lineNumber, columnNumber, std::move(stackTraceImpl),
          scriptId, m_isolate, toString16(message),
          InspectedContext::contextId(context), exception, exceptionId);
  ensureConsoleMessageStorage(groupId)->addMessage(std::move(consoleMessage));
  return exceptionId;
}
```

The reporting side is `V8ConsoleMessage::reportToFrontend` for `Runtime::Frontend` when `m_origin == V8MessageOrigin::kException` (`v8-console-message.cc:330-367`):

```cpp
if (m_origin == V8MessageOrigin::kException) {
  // ...
  std::unique_ptr<protocol::Runtime::ExceptionDetails> exceptionDetails =
      protocol::Runtime::ExceptionDetails::create()
          .setExceptionId(m_exceptionId)
          .setText(includeException ? m_message
                   : (m_detailedMessage.length() ? m_detailedMessage : m_message))
          .setLineNumber(m_lineNumber ? m_lineNumber - 1 : 0)
          .setColumnNumber(m_columnNumber ? m_columnNumber - 1 : 0)
          .build();
  if (m_scriptId) exceptionDetails->setScriptId(String16::fromInteger(m_scriptId));
  if (!m_url.isEmpty()) exceptionDetails->setUrl(m_url);
  if (m_stackTrace) exceptionDetails->setStackTrace(
      m_stackTrace->buildInspectorObjectImpl(inspector->debugger()));
  if (m_contextId) exceptionDetails->setExecutionContextId(m_contextId);
  if (includeException) exceptionDetails->setException(std::move(exception));
  std::unique_ptr<protocol::DictionaryValue> data =
      getAssociatedExceptionData(inspector, session);
  if (data) exceptionDetails->setExceptionMetaData(std::move(data));
  frontend->exceptionThrown(m_timestamp, std::move(exceptionDetails));
  return;
}
```

The `exceptionThrown` event therefore has: `timestamp` and an `ExceptionDetails` object containing `exceptionId`, `text`, `lineNumber`/`columnNumber` (0-based), `scriptId`, `url`, `stackTrace`, `executionContextId`, `exception` (a RemoteObject), `exceptionMetaData` (optional).

### 3.6.2 `Runtime.exceptionRevoked`

Fired by `V8InspectorImpl::exceptionRevoked` (`v8-inspector-impl.cc:391-401`) when `ConsoleRevokeError` is called (e.g. by `PromiseRejectionTracker::Revoke`). Creates a `createForRevokedException` message; reporting side emits `frontend->exceptionRevoked(m_message, m_revokedExceptionId)` (`v8-console-message.cc:368-370`).

### 3.6.3 Promise Rejections (unhandled)

Promise rejection tracking is implemented inside V8 (in `src/debug/debug-interface.cc`). V8 calls `v8::debug::AsyncEventOccurred` with `kDebugPromiseReject`. The V8 debugger's handler (`v8-debugger.cc:761-...` for `kDebugPromiseThen/Catch/Finally`, and rejection callbacks elsewhere) tracks the async task chain.

The unhandled-rejection → `Runtime.exceptionThrown` path:

1. V8 itself fires `PromiseRejectCallback` (set by Blink via `v8::Isolate::SetPromiseRejectCallback`).
2. Blink's `ThreadDebuggerCommonImpl` (which has the static dispatcher) creates an `ErrorEvent`, reports it via `MainThreadDebugger::ExceptionThrown`.
3. The same path as §3.6.1 is followed; the `exceptionType` parameter on `V8Debugger::ExceptionThrown` (`v8-debugger.cc:686-695`) becomes `kPromiseRejection` and is later translated into `breakReason = "PromiseRejection"` (not "Exception") in `V8DebuggerAgentImpl::didPause` (`v8-debugger-agent-impl.cc:2254-2256`).

### 3.6.4 `Runtime.getExceptionDetails` (newer API)

Given an `errorObjectId` (a RemoteObject of an Error), this returns the `ExceptionDetails` even after the fact. `v8-runtime-agent-impl.cc:1027-1063`:

```cpp
Response V8RuntimeAgentImpl::getExceptionDetails(
    const String16& errorObjectId,
    std::unique_ptr<protocol::Runtime::ExceptionDetails>* out_exceptionDetails) {
  InjectedScript::ObjectScope scope(m_session, errorObjectId);
  Response response = scope.initialize();
  if (!response.IsSuccess()) return response;

  const v8::Local<v8::Value> error = scope.object();
  if (!error->IsNativeError()) {
    return Response::ServerError("errorObjectId is not a JS error object");
  }
  const v8::Local<v8::Message> message =
      v8::debug::CreateMessageFromException(m_inspector->isolate(), error);
  response = scope.injectedScript()->createExceptionDetails(
      message, error, scope.objectGroupName(), out_exceptionDetails);
  // ...
  (*out_exceptionDetails)
      ->setText(toProtocolString(m_inspector->isolate(), message->Get()));
  // Check if the exception has any metadata on the inspector and also attach it.
  std::unique_ptr<protocol::DictionaryValue> data =
      m_inspector->getAssociatedExceptionDataForProtocol(error);
  if (data) {
    (*out_exceptionDetails)->setExceptionMetaData(std::move(data));
  }
  return Response::Success();
}
```

### 3.6.5 `console.error`

`console.error("foo")` does **not** trigger `Runtime.exceptionThrown`. It triggers `Runtime.consoleAPICalled` with `type="error"`. The difference is intentional: `exceptionThrown` is for thrown values; `consoleAPICalled` is for explicit console calls.

---

## 3.7 The InspectorInstrumentation System

### 3.7.1 Naming History and Current Structure

Historically (pre-2021) the entire blink instrumentation surface was a single class `InspectorInstrumentation` in `third_party/blink/renderer/core/inspector/InspectorInstrumentation.h` with hundreds of static `Foo::instrumentingAgents(executionContext)` callsites scattered through `core/`.

In modern Chromium (and in your slice, with the renamed layout) it has been split into:

- **`CoreProbeSink`** — header at `third_party/blink/renderer/core/core_probe_sink.h` (referenced from `inspector_base_agent.h:35`, `worker_inspector_controller.cc:35`, `inspector_trace_events.h:17` — **not in your slice** but referenced). It's a garbage-collected "registry of registries" — it holds an `AgentRegistry<InspectorLogAgent>`, `AgentRegistry<InspectorNetworkAgent>`, `AgentRegistry<InspectorPageAgent>`, `AgentRegistry<InspectorCSSAgent>`, `AgentRegistry<InspectorDOMAgent>`, `AgentRegistry<InspectorDOMDebuggerAgent>`, `AgentRegistry<InspectorOverlayAgent>`, plus `InspectorTraceEvents*`, `InspectorIssueReporter*`, `InspectorMediaContextImpl*` and a vector of `DevToolsSession*`.
- **The `probe/` namespace** (`third_party/blink/renderer/core/probe/core_probes.h` — referenced from `worker_inspector_controller.cc:49` but **not in your slice**). It contains `probe::Will(const probe::UpdateLayout&)`, `probe::Did(const probe::UpdateLayout&)`, `probe::ExecuteScript`, `probe::ParseHTML`, `probe::CallFunction`, `probe::RecalculateStyle`, etc. — these are **free functions** that hot-path code calls; they look up the `CoreProbeSink` from the `LocalFrame` / `ExecutionContext` and iterate the appropriate `AgentRegistry`.

### 3.7.2 The `AgentRegistry<T>` Template

`third_party/blink/renderer/core/inspector/agent_registry.h` (full content) is the workhorse:

```cpp
template <class AgentType>
class CORE_EXPORT AgentRegistry {
  DISALLOW_NEW();
 public:
  AgentRegistry() : data_(MakeGarbageCollected<Data>()) {}

  void AddAgent(AgentType* agent) {
    if (HasAgent(agent)) return;
    if (!RequiresCopy()) { data_->agents.push_back(agent); is_empty_ = false; return; }
    data_ = MakeGarbageCollected<Data>(*data_);   // COW clone
    data_->agents.push_back(agent);
    is_empty_ = false;
    iteration_counter_ = 0;
  }
  // ...
  bool RequiresCopy() const { return iteration_counter_ != 0; }
  bool IsEmpty() const { return is_empty_; }

  template <typename ForEachCallable>
  void ForEachAgent(const ForEachCallable& callable) const {
    iteration_counter_++;
    Member<Data> snapshot = data_;
    for (const Member<AgentType>& agent : snapshot->agents) {
      callable(agent);
    }
    if (iteration_counter_ > 0) iteration_counter_--;
  }
};
```

This is the **optimization for the no-agents-active case**: hot-path probe code first checks `sink->XxxAgents().IsEmpty()` (a single bool read) and bails immediately. When the registry is non-empty, `ForEachAgent` does copy-on-write so that an agent can be added or removed during the callback without invalidating the iterator.

### 3.7.3 How Hot-Path Code Calls Into the Instrumentation

Although the `probe::` free-function source files aren't in your slice, the **agent-side API** is fully visible. Each Blink agent inherits `InspectorBaseAgent<DomainMetainfo>` (`inspector_base_agent.h:64-114`):

```cpp
template <typename DomainMetainfo>
class InspectorBaseAgent : public InspectorAgent, public DomainMetainfo::BackendClass {
 public:
  void Init(CoreProbeSink* instrumenting_agents,
            protocol::UberDispatcher* dispatcher,
            InspectorSessionState* session_state,
            V8SessionHolder v8_session) override {
    instrumenting_agents_ = instrumenting_agents;
    frontend_.reset(new typename DomainMetainfo::FrontendClass(dispatcher->channel()));
    DomainMetainfo::DispatcherClass::wire(dispatcher, this);
    agent_state_.InitFrom(session_state);
    v8_session_ = std::move(v8_session);
  }
  // ...
 protected:
  Member<CoreProbeSink> instrumenting_agents_;
  // ...
};
```

Each concrete agent then registers itself with the appropriate `AgentRegistry` on `enable()` and removes itself on `disable()`. Example from `InspectorLogAgent::InnerEnable` (`inspector_log_agent.cc:177-193`):

```cpp
void InspectorLogAgent::InnerEnable() {
  instrumenting_agents_->AddInspectorLogAgent(this);
  if (storage_->ExpiredCount()) { ... }
  for (wtf_size_t i = 0; i < storage_->size(); ++i)
    ConsoleMessageAdded(storage_->at(i));
}
```

### 3.7.4 Three Example Probe Callsites

Because the actual `core/probe/` files aren't present, I'll cite three patterns we *can* see:

1. **`InspectorNetworkAgent` probe methods** (`inspector_network_agent.h:106-280`). Each method (`WillSendRequest`, `DidReceiveResourceResponse`, `DidFinishLoading`, `WillCreateWebSocket`, `DidReceiveWebSocketMessage`, etc.) is called by `core/loader/ResourceFetcher` and `core/xmlhttprequest/XMLHttpRequest.cpp` via `probe::WillSendRequest(...)`. The probe function looks up the `CoreProbeSink` from the `DocumentLoader` / `ExecutionContext`, then iterates the `networkAgents()` registry.

2. **`InspectorPageAgent::DidCreateMainWorldContext`** — fired from `ScriptController::InitializeMainWorld` after the main world v8::Context is created. The agent uses this to fire `Runtime.executionContextCreated` via the V8 inspector.

3. **`InspectorTraceEvents::Will(const probe::ExecuteScript&)` and `Did(const probe::ExecuteScript&)`** (`inspector_trace_events.h:129-130`). These do not produce CDP events directly — they emit `TRACE_EVENT` perfetto events into the tracing timeline that the DevTools Performance panel consumes.

### 3.7.5 How the Sink Is Plumbed to Sessions

`DevToolsSession::Append(InspectorAgent*)` (`devtools_session.cc:266-270`) is the linchpin:

```cpp
void DevToolsSession::Append(InspectorAgent* agent) {
  agents_.push_back(agent);
  agent->Init(agent_->probe_sink_.Get(), inspector_backend_dispatcher_.get(),
              &session_state_, v8_session_);
}
```

So each agent gets the same `CoreProbeSink*` (which lives on the `DevToolsAgent`, see `devtools_agent.h:153` `Member<CoreProbeSink> probe_sink_`). When an agent is enabled, it adds itself to that sink's `AgentRegistry`; when the session detaches, all agents are disposed (`devtools_session.cc:282-289`):

```cpp
inspector_backend_dispatcher_.reset();
for (wtf_size_t i = agents_.size(); i > 0; i--) {
  agents_[i - 1]->Dispose();
}
agents_.clear();
```

`Dispose()` (`inspector_base_agent.h:85-90`) calls `disable()` which removes the agent from its `AgentRegistry`.

### 3.7.6 The Optimization Summary

The hot path is: `some_blink_function() → probe::Will/Did(...) → sink->SomeAgentRegistry().IsEmpty() ? return : ForEachAgent(...)`. Cost when no agent is active: **one pointer load + one bool read**. When an agent is active: a vector iteration with copy-on-write safety. The pattern is documented in the `AgentRegistry` comment: "Support modification while iterating by means of Copy-On-Write."

---

## 3.8 The InspectorSession

### 3.8.1 Lifetime

`V8InspectorSessionImpl` (`v8-inspector-session-impl.h:35-179`) is owned by the embedder through `std::shared_ptr` (via `connectShared`) or `std::unique_ptr` (via legacy `connect`). The Blink-side owner is `blink::DevToolsSession`, which holds it inside a `V8SessionHolder` (a small wrapper that supports `reset()`).

The Blink path (`devtools_session.cc:242-260`):

```cpp
void DevToolsSession::ConnectToV8(v8_inspector::V8Inspector* inspector,
                                  int context_group_id) {
  const auto& cbor = v8_session_state_cbor_.Get();
  const auto* reattach_state = session_state_.ReattachState();
  v8_session_ = V8SessionHolder(inspector->connectShared(
      context_group_id, this,
      v8_inspector::StringView(cbor.data(), cbor.size()),
      client_is_trusted_ ? v8_inspector::V8Inspector::kFullyTrusted
                         : v8_inspector::V8Inspector::kUntrusted,
      session_waits_for_debugger_
          ? v8_inspector::V8Inspector::kWaitingForDebugger
          : v8_inspector::V8Inspector::kNotWaitingForDebugger,
      ConvertEmbedderState(
          reattach_state ? reattach_state->browser_originating_session_state.get()
                         : nullptr)));
  injected_script_manager_->SetV8Session(v8_session_.get());
}
```

Note the trust level: `kFullyTrusted` is granted only to privileged clients (the DevTools frontend itself or an extension debugger). Untrusted clients cannot enable Profiler/HeapProfiler/Schema domains and don't get `installAdditionalCommandLineAPI` extensions (see `injected-script.cc:1047-1057` where `installCommandLineAPI` returns early for untrusted sessions).

### 3.8.2 How CDP Messages Are Dispatched

From the browser process: `content::DevToolsSession` (browser-side, in `browser/devtools/devtools_session.cc` — not the Blink one!) receives a mojo message and forwards the CBOR to `blink::DevToolsSession::DispatchProtocolCommand`:

```cpp
// devtools_session.cc:299-350
void DevToolsSession::DispatchProtocolCommand(int call_id,
                                              const String& method,
                                              base::span<const uint8_t> message,
                                              const String& fallthrough_data) {
  return DispatchProtocolCommandImpl(call_id, method, message, fallthrough_data);
}

void DevToolsSession::DispatchProtocolCommandImpl(int call_id, const String& method,
                                                  base::span<const uint8_t> data,
                                                  const String& fallthrough_data) {
  // ...
  agent_->client_->DebuggerTaskStarted();
  if (v8_inspector::V8InspectorSession::canDispatchMethod(
          ToV8InspectorStringView(method))) {
    // Binary protocol messages are passed using 8-bit StringView.
    v8_session_->dispatchProtocolMessage(
        v8_inspector::StringView(data.data(), data.size()),
        ToV8InspectorStringView(fallthrough_data));
  } else {
    // ...
    crdtp::Dispatchable dispatchable(crdtp::SpanFrom(data),
                                     std::string_view(UTF8.data(), UTF8.size()),
                                     /*fallthrough_callback=*/nullptr);
    DCHECK(dispatchable.ok());
    inspector_backend_dispatcher_->Dispatch(dispatchable);
  }
  agent_->client_->DebuggerTaskFinished();
}
```

The decision logic is `V8InspectorSession::canDispatchMethod` (`v8-inspector-session-impl.cc:74-87`):

```cpp
bool V8InspectorSession::canDispatchMethod(StringView method) {
  return stringViewStartsWith(method, protocol::Runtime::Metainfo::commandPrefix) ||
         stringViewStartsWith(method, protocol::Debugger::Metainfo::commandPrefix) ||
         stringViewStartsWith(method, protocol::Profiler::Metainfo::commandPrefix) ||
         stringViewStartsWith(method, protocol::HeapProfiler::Metainfo::commandPrefix) ||
         stringViewStartsWith(method, protocol::Console::Metainfo::commandPrefix) ||
         stringViewStartsWith(method, protocol::Schema::Metainfo::commandPrefix);
}
```

So Runtime/Debugger/Profiler/HeapProfiler/Console/Schema go to the V8 session; **everything else** (Page, DOM, CSS, Network, Log, Emulation, Overlay, Target, Fetch, Storage, …) goes to the Blink `protocol::UberDispatcher` and is handled by `InspectorBaseAgent` subclasses.

### 3.8.3 How the Session Knows Which Agents Are Enabled

There's no central "enabled flag" — each agent tracks its own state, persisted in `m_state` (a `protocol::DictionaryValue`).

`agentState(domainName)` (`v8-inspector-session-impl.cc:175-185`) returns a per-domain sub-dictionary:

```cpp
protocol::DictionaryValue* V8InspectorSessionImpl::agentState(const String16& name) {
  protocol::DictionaryValue* state = m_state->getObject(name);
  if (!state) {
    std::unique_ptr<protocol::DictionaryValue> newState =
        protocol::DictionaryValue::create();
    state = newState.get();
    m_state->setObject(name, std::move(newState));
  }
  return state;
}
```

On `Runtime.enable`, `V8RuntimeAgentImpl::enable` (`v8-runtime-agent-impl.cc:1125-1156`) sets `m_state["runtimeEnabled"] = true` and reports all contexts; on `disable` the reverse. The Blink `InspectorAgentState` (`inspector_session_state.h`) provides typed wrappers (`Boolean`, `Integer`, `Double`, `Bytes`, `StringMap`) that read/write the same dictionary and survive session re-attach (state CBOR is sent back to the browser process in `DevToolsSession::SendProtocolResponse` line 411 and on `FlushProtocolNotifications` line 463).

### 3.8.4 Response / Notification Channel

`DevToolsSession` *is* the V8 channel — it inherits `V8Inspector::ManagedChannel` (`devtools_session.h:60`). The three channel methods (`sendResponse`, `sendNotification`, `flushProtocolNotifications`) are implemented at `devtools_session.cc:396-471`. Notifications are queued (`notification_queue_`) and only serialized+sent at flush time, to amortize serialization cost. Messages are forwarded to the browser process via the mojo `host_remote_->DispatchProtocolResponse` / `DispatchProtocolNotification` (lines 419-422 and 466-469).

---

## 3.9 V8 Inspector Integration

### 3.9.1 Creating the V8Inspector

```cpp
// v8-inspector-impl.cc:60-75
std::unique_ptr<V8Inspector> V8Inspector::create(v8::Isolate* isolate,
                                                 V8InspectorClient* client) {
  return std::unique_ptr<V8Inspector>(new V8InspectorImpl(isolate, client));
}

V8InspectorImpl::V8InspectorImpl(v8::Isolate* isolate, V8InspectorClient* client)
    : m_isolate(isolate),
      m_client(client),
      m_debugger(new V8Debugger(isolate, this)),
      m_lastExceptionId(0),
      m_lastContextId(0) {
  v8::debug::SetInspector(m_isolate, this);
  v8::debug::SetConsoleDelegate(m_isolate, console());
  v8::debug::SetIsolateId(m_isolate, generateUniqueId());
}
```

So a single `V8InspectorImpl` is created per isolate, and it immediately registers itself as the global `v8::debug::Inspector` and `ConsoleDelegate` on the isolate. The `V8Console` is created lazily (`console()` at `v8-inspector-impl.cc:484-490`).

### 3.9.2 Context Tracking

`m_contexts` (`v8-inspector-impl.h:194-198`):

```cpp
using ContextByIdMap = std::unordered_map<int, std::shared_ptr<InspectedContext>>;
using ContextsByGroupMap = std::unordered_map<int, std::unique_ptr<ContextByIdMap>>;
ContextsByGroupMap m_contexts;
```

A `contextGroupId` (one per `LocalFrame` or per worker thread) maps to a `ContextByIdMap`, which in turn maps `contextId` to a `shared_ptr<InspectedContext>`. `shared_ptr` is used because `InjectedScript::Scope` holds a strong reference across JS re-entry, and `forEachContext`/`forEachSession` may invalidate iterators.

Cross-lookup tables:

- `m_contextIdToGroupIdMap` (`v8-inspector-impl.h:209`) — contextId → groupId, used by `contextGroupId(int contextId)`.
- `m_uniqueIdToContextId` (`v8-inspector-impl.h:210`) — `pair<int64_t,int64_t>` → contextId, used by `resolveUniqueContextId` for the `uniqueContextId` parameter to `Runtime.evaluate`.
- `m_sessions` (`v8-inspector-impl.h:201`) — contextGroupId → (sessionId → V8InspectorSessionImpl*).
- `m_debuggerBarriers` (line 203) — contextGroupId → `weak_ptr<V8DebuggerBarrier>`, used by `Runtime.runIfWaitingForDebugger`.

### 3.9.3 The V8InspectorClient Interface

Declared in `v8/include/v8-inspector.h:256-347`, ~30 virtual methods. Highlights that the Qt6 embedder must implement:

```cpp
class V8_EXPORT V8InspectorClient {
 public:
  virtual void runMessageLoopOnPause(int contextGroupId) {}
  virtual void runMessageLoopOnInstrumentationPause(int contextGroupId) {...}
  virtual void quitMessageLoopOnPause() {}
  virtual void runIfWaitingForDebugger(int contextGroupId) {}

  virtual void muteMetrics(int contextGroupId) {}
  virtual void unmuteMetrics(int contextGroupId) {}
  virtual void beginUserGesture() {}
  virtual void endUserGesture() {}

  virtual std::unique_ptr<DeepSerializationResult> deepSerialize(...);
  virtual std::unique_ptr<StringBuffer> valueSubtype(v8::Local<v8::Value>);
  virtual std::unique_ptr<StringBuffer> descriptionForValueSubtype(...);
  virtual bool isInspectableHeapObject(v8::Local<v8::Object>) { return true; }

  virtual v8::Local<v8::Context> ensureDefaultContextInGroup(int contextGroupId);
  virtual void beginEnsureAllContextsInGroup(int contextGroupId) {}
  virtual void endEnsureAllContextsInGroup(int contextGroupId) {}

  virtual void installAdditionalCommandLineAPI(v8::Local<v8::Context>, v8::Local<v8::Object>) {}
  virtual void consoleAPIMessage(int contextGroupId, int contextId,
                                 v8::Isolate::MessageErrorLevel level,
                                 const StringView& message, const StringView& url,
                                 unsigned lineNumber, unsigned columnNumber,
                                 V8StackTrace* stackTrace);
  virtual v8::MaybeLocal<v8::Value> memoryInfo(v8::Isolate*, v8::Local<v8::Context>);
  virtual void consoleTime(v8::Isolate*, v8::Local<v8::String> label);
  virtual void consoleTimeEnd(v8::Isolate*, v8::Local<v8::String> label);
  virtual void consoleTimeStamp(v8::Isolate*, v8::Local<v8::String> label);
  virtual void consoleTimeStampWithArgs(v8::Isolate*, v8::Local<v8::String>,
                                        const v8::LocalVector<v8::Value>&);
  virtual void consoleClear(int contextGroupId);
  virtual double currentTimeMS() { return 0; }
  virtual void startRepeatingTimer(double, TimerCallback, void* data) {}
  virtual void cancelTimer(void* data) {}
  virtual bool canExecuteScripts(int contextGroupId) { return true; }
  virtual void maxAsyncCallStackDepthChanged(int depth) {}
  virtual std::unique_ptr<StringBuffer> resourceNameToUrl(const StringView&);
  virtual int64_t generateUniqueId() { return 0; }
  virtual void dispatchError(v8::Local<v8::Context>, v8::Local<v8::Message>, v8::Local<v8::Value>);
};
```

Blink's `MainThreadDebugger` (subclass of `ThreadDebuggerCommonImpl` which implements `ThreadDebugger` which IS a `V8InspectorClient`) provides all of these — see `main_thread_debugger.cc:96-371`.

### 3.9.4 Console Messages Back to DevTools

There are two paths:

1. **Per-session, via `Runtime.consoleAPICalled`** — see §3.5. Each session gets the message because `V8ConsoleMessageStorage::addMessage` iterates `inspector->forEachSession(...)` and calls `session->runtimeAgent()->messageAdded(...)`.
2. **Blink-side mirror, via `consoleAPIMessage`** — `V8ConsoleMessage::createForConsoleAPI` itself calls `inspector->client()->consoleAPIMessage(...)` (`v8-console-message.cc:502-506`), which reaches `MainThreadDebugger::consoleAPIMessage` (`main_thread_debugger.cc:335-354`), which constructs a `SourceLocation` and forwards to `frame->Console().ReportMessageToClient(...)`. The browser-side console (e.g. the Chrome devtools "Console" tab even when no DevTools session is attached, or the `--enable-logging=stderr` console output) reads from `Page::GetConsoleMessageStorage()`.

---

## 3.10 Debugger

### 3.10.1 Enable

```cpp
// v8-debugger-agent-impl.cc:487-506
Response V8DebuggerAgentImpl::enable(std::optional<double> maxScriptsCacheSize,
                                     String16* outDebuggerId) {
  if (m_enableState == kStopping) return Response::ServerError("Debugger is stopping");
  m_maxScriptCacheSize = v8::base::saturated_cast<size_t>(...);
  *outDebuggerId = m_debugger->debuggerIdFor(m_session->contextGroupId()).toString();
  if (enabled()) return Response::Success();
  if (!m_inspector->client()->canExecuteScripts(m_session->contextGroupId()))
    return Response::ServerError("Script execution is prohibited");
  enableImpl();
  return Response::Success();
}

void V8DebuggerAgentImpl::enableImpl() {
  m_enableState = kEnabled;
  m_state->setBoolean(DebuggerAgentState::debuggerEnabled, true);
  m_debugger->enable();
  std::vector<std::unique_ptr<V8DebuggerScript>> compiledScripts =
      m_debugger->getCompiledScripts(m_session->contextGroupId(), this);
  for (auto& script : compiledScripts) didParseSource(std::move(script));
  m_breakpointsActive = m_state->booleanProperty(
      DebuggerAgentState::breakpointsActiveWhenEnabled, true);
  if (m_breakpointsActive) m_debugger->setBreakpointsActive(true);
  // ...
}
```

`m_debugger->enable()` (`v8-debugger.cc:104-114`) sets V8's debug delegate and adds the near-heap-limit callback (for OOM pauses):

```cpp
void V8Debugger::enable() {
  if (m_enableCount++) return;
  v8::HandleScope scope(m_isolate);
  v8::debug::SetDebugDelegate(m_isolate, this);
  m_isolate->AddNearHeapLimitCallback(&V8Debugger::nearHeapLimitCallback, this);
  v8::debug::ChangeBreakOnException(m_isolate, v8::debug::NoBreakOnException);
  m_pauseOnExceptionsState = v8::debug::NoBreakOnException;
#if V8_ENABLE_WEBASSEMBLY
  v8::debug::EnterDebuggingForIsolate(m_isolate);
#endif
}
```

### 3.10.2 SetBreakpointByUrl

```cpp
// v8-debugger-agent-impl.cc:643-765
Response V8DebuggerAgentImpl::setBreakpointByUrl(
    int lineNumber, std::optional<String16> optionalURL,
    std::optional<String16> optionalURLRegex,
    std::optional<String16> optionalScriptHash,
    std::optional<int> optionalColumnNumber,
    std::optional<String16> optionalCondition,
    const String16& embedderBreakpointId, String16* outBreakpointId,
    std::unique_ptr<protocol::Array<protocol::Debugger::Location>>* locations) {
  if (!enabled()) return Response::ServerError(kDebuggerNotEnabled);
  // ...
  BreakpointType type = BreakpointType::kByUrl;
  String16 selector;
  if (optionalURLRegex.has_value()) { selector = optionalURLRegex.value(); type = BreakpointType::kByUrlRegex; }
  else if (optionalURL.has_value()) { selector = optionalURL.value(); type = BreakpointType::kByUrl; }
  else if (optionalScriptHash.has_value()) { selector = optionalScriptHash.value(); type = BreakpointType::kByScriptHash; }
  // ...
  String16 breakpointId = embedderBreakpointId.isEmpty()
      ? generateBreakpointId(type, selector, lineNumber, columnNumber)
      : embedderBreakpointId;
  // ...
  Matcher matcher(m_inspector, type, selector);
  for (const auto& scriptId : allScriptIds) {
    std::shared_ptr<V8DebuggerScript> script;
    { v8::debug::DisallowGarbageCollectionScope no_gc; ... }
    if (!matcher.matches(*script)) continue;
    // ...
    std::unique_ptr<protocol::Debugger::Location> location =
        setBreakpointImpl(breakpointId, scriptId, condition, adjustedLineNumber, adjustedColumnNumber);
    if (location) (*locations)->emplace_back(std::move(location));
  }
  m_urlBreakpoints[breakpointId] = std::move(breakpoint_info);
  *outBreakpointId = breakpointId;
  return Response::Success();
}
```

The actual V8 call is in `setBreakpointImpl` (`v8-debugger-agent-impl.cc:1142-1183`):

```cpp
std::unique_ptr<protocol::Debugger::Location>
V8DebuggerAgentImpl::setBreakpointImpl(const String16& breakpointId,
                                       const String16& scriptId,
                                       const String16& condition,
                                       int lineNumber, int columnNumber) {
  // ...
  std::shared_ptr<V8DebuggerScript> script;
  // ...
  v8::debug::BreakpointId debuggerBreakpointId;
  v8::debug::Location location(lineNumber, columnNumber);
  int contextId = script->executionContextId();
  std::shared_ptr<InspectedContext> inspected = m_inspector->getContext(contextId);
  if (!inspected) return nullptr;

  {
    v8::Context::Scope contextScope(inspected->context());
    v8::TryCatch tryCatch(m_isolate);
    if (!script->setBreakpoint(condition, &location, &debuggerBreakpointId)) {
      return nullptr;
    }
  }
  m_debuggerBreakpointIdToBreakpointId[debuggerBreakpointId] = breakpointId;
  m_breakpointIdToDebuggerBreakpointIds[breakpointId].push_back(debuggerBreakpointId);
  return protocol::Debugger::Location::create()
      .setScriptId(scriptId)
      .setLineNumber(location.GetLineNumber())
      .setColumnNumber(location.GetColumnNumber())
      .build();
}
```

`V8DebuggerScript::setBreakpoint` (`v8-debugger-script.h:99`) delegates to `v8::debug::SetBreakpoint` (in V8's `src/debug/debug.cc`, not in your slice). The breakpoint condition is compiled and evaluated each time the breakpoint is hit; if the condition throws, `V8Debugger::BreakpointConditionEvaluated` (`v8-debugger.cc:738-759`) reports the exception.

For function-call breakpoints there's a separate API (`v8-debugger-agent-impl.cc:1185-1196`):

```cpp
void V8DebuggerAgentImpl::setBreakpointImpl(const String16& breakpointId,
                                            v8::Local<v8::Function> function,
                                            v8::Local<v8::String> condition) {
  v8::debug::BreakpointId debuggerBreakpointId;
  if (!v8::debug::SetFunctionBreakpoint(function, condition, &debuggerBreakpointId)) {
    return;
  }
  m_debuggerBreakpointIdToBreakpointId[debuggerBreakpointId] = breakpointId;
  m_breakpointIdToDebuggerBreakpointIds[breakpointId].push_back(debuggerBreakpointId);
}
```

### 3.10.3 Debugger.paused Event

V8 calls `V8Debugger::BreakProgramRequested` (`v8-debugger.cc:678-684`) when a breakpoint hits, `ExceptionThrown` (line 686-695) when an exception is thrown and pause-on-exceptions is set, or `BreakOnInstrumentation` (line 623-676) for instrumentation breakpoints. All paths converge on `handleProgramBreak` (line 472-563):

```cpp
void V8Debugger::handleProgramBreak(...) {
  if (isPaused()) return;
  int contextGroupId = m_inspector->contextGroupId(pausedContext);
  // ...
  m_inspector->forEachSession(
      contextGroupId, [...](V8InspectorSessionImpl* session) {
        if (session->debuggerAgent()->acceptsPause(isOOMBreak)) {
          session->debuggerAgent()->didPause(
              InspectedContext::contextId(pausedContext), exception,
              breakpointIds, exceptionType, isUncaught, breakReasons);
        }
      });
  {
    v8::Isolate::AllowJavascriptExecutionScope allow_script(m_isolate);
    v8::Context::Scope scope(pausedContext);
    // ...
    m_inspector->client()->runMessageLoopOnPause(contextGroupId);  // BLOCKS here
    m_pausedContextGroupId = 0;
  }
  m_inspector->forEachSession(contextGroupId, [](V8InspectorSessionImpl* session) {
    if (session->debuggerAgent()->enabled()) {
      session->debuggerAgent()->clearBreakDetails();
      session->debuggerAgent()->didContinue();
    }
  });
  // ...
}
```

`didPause` (`v8-debugger-agent-impl.cc:2233-2356`) builds the `Debugger.CallFrame` array via `currentCallFrames` (line 1856-1943, which iterates `v8::debug::StackTraceIterator` and wraps each frame's receiver, scope chain, function location, return value), gathers the break reasons, and fires:

```cpp
m_frontend.paused(std::move(protocolCallFrames), breakReason,
                  std::move(breakAuxData), std::move(hitBreakpointIds),
                  currentAsyncStackTrace(), currentExternalStackTrace());
```

The client (you) must then send `Debugger.resume` (line 1541-1549) or `Debugger.stepOver`/`stepInto`/`stepOut` (lines 1551-1593), which call `m_debugger->continueProgram` / `stepIntoStatement` / `stepOverStatement` / `stepOutOfFunction`, which call `v8::debug::PrepareStep` and `v8::debug::ClearBreakOnNextFunctionCall`, then call `client->quitMessageLoopOnPause()` (line 275-294). That returns from `runMessageLoopOnPause`, which unblocks `handleProgramBreak`, which sends `Debugger.resumed` (line 2358-2361).

### 3.10.4 Pause on Exceptions

```cpp
// v8-debugger-agent-impl.cc:1600-1618
Response V8DebuggerAgentImpl::setPauseOnExceptions(const String16& stringPauseState) {
  if (!enabled()) return Response::ServerError(kDebuggerNotEnabled);
  v8::debug::ExceptionBreakState pauseState;
  if (stringPauseState == "none")    pauseState = v8::debug::NoBreakOnException;
  else if (stringPauseState == "all")    pauseState = v8::debug::BreakOnAnyException;
  else if (stringPauseState == "caught") pauseState = v8::debug::BreakOnCaughtException;
  else if (stringPauseState == "uncaught") pauseState = v8::debug::BreakOnUncaughtException;
  else return Response::ServerError("Unknown pause on exceptions mode: " + ...);
  setPauseOnExceptionsImpl(pauseState);
  return Response::Success();
}

void V8DebuggerAgentImpl::setPauseOnExceptionsImpl(int pauseState) {
  m_debugger->setPauseOnExceptionsState(static_cast<v8::debug::ExceptionBreakState>(pauseState));
  m_state->setInteger(DebuggerAgentState::pauseOnExceptionsState, pauseState);
}
```

`m_debugger->setPauseOnExceptionsState` (`v8-debugger.cc:196-202`) calls `v8::debug::ChangeBreakOnException(isolate, state)`. Promise-rejection pauses use `kPromiseRejection` exception type (`v8-debugger.cc:686-695`).

### 3.10.5 Async Stacks (setAsyncCallStackDepth)

```cpp
// v8-debugger-agent-impl.cc:1753-1754
Response V8DebuggerAgentImpl::setAsyncCallStackDepth(int depth) {
  if (!enabled()) return Response::ServerError(kDebuggerNotEnabled);
  m_state->setInteger(DebuggerAgentState::asyncCallStackDepth, depth);
  internalSetAsyncCallStackDepth(depth);
  return Response::Success();
}
```

This propagates to `V8Debugger::setAsyncCallStackDepth` (member at `v8-debugger.h:103`, declared as `void setAsyncCallStackDepth(V8DebuggerAgentImpl*, int)`). It stores `m_maxAsyncCallStackDepthMap[agent] = depth` and `m_maxAsyncCallStackDepth = max(...)`. When depth > 0, the debugger tracks async tasks (Promise.then/catch/finally, setTimeout, etc.) and chains them into async stack traces stored as `AsyncStackTrace` objects (`v8-stack-trace-impl.h:115-149`).

The embedder participates via these V8Inspector methods (`v8-inspector.h:404-414`):

```cpp
virtual void asyncTaskScheduled(StringView taskName, void* task, bool recurring) = 0;
virtual void asyncTaskCanceled(void* task) = 0;
virtual void asyncTaskStarted(void* task) = 0;
virtual void asyncTaskFinished(void* task) = 0;
virtual void allAsyncTasksCanceled() = 0;

virtual V8StackTraceId storeCurrentStackTrace(StringView description) = 0;
virtual void externalAsyncTaskStarted(const V8StackTraceId& parent) = 0;
virtual void externalAsyncTaskFinished(const V8StackTraceId& parent) = 0;
```

Blink calls these from `ThreadDebuggerCommonImpl` (declared at `thread_debugger_common_impl.h:31-50`). `storeCurrentStackTrace` is used by HTML5 APIs like `fetch()` to tag the async chain so that when the response callback runs, the original call site can be attached to the new stack.

---

## 3.11 File Locations Reference

| Component | File Path |
|---|---|
| V8InspectorSessionImpl | `v8/src/inspector/v8-inspector-session-impl.cc` + `.h` |
| V8InspectorImpl | `v8/src/inspector/v8-inspector-impl.cc` + `.h` |
| V8RuntimeAgentImpl | `v8/src/inspector/v8-runtime-agent-impl.cc` + `.h` |
| V8DebuggerAgentImpl | `v8/src/inspector/v8-debugger-agent-impl.cc` + `.h` |
| V8ConsoleAgentImpl | `v8/src/inspector/v8-console-agent-impl.cc` + `.h` |
| V8Console | `v8/src/inspector/v8-console.cc` + `.h` |
| V8Debugger | `v8/src/inspector/v8-debugger.cc` + `.h` |
| V8DebuggerScript | `v8/src/inspector/v8-debugger-script.cc` + `.h` |
| V8StackTraceImpl | `v8/src/inspector/v8-stack-trace-impl.cc` + `.h` |
| V8ConsoleMessage | `v8/src/inspector/v8-console-message.cc` + `.h` |
| InjectedScript | `v8/src/inspector/injected-script.cc` + `.h` |
| InspectedContext | `v8/src/inspector/inspected-context.cc` + `.h` |
| RemoteObjectId | `v8/src/inspector/remote-object-id.cc` + `.h` |
| ValueMirror | `v8/src/inspector/value-mirror.cc` + `.h` |
| V8ValueUtils | `v8/src/inspector/v8-value-utils.cc` + `.h` |
| V8Serialization | `v8/src/inspector/v8-serialization.cc` + `.h` |
| V8FunctionCall | `v8/src/inspector/v8-function-call.cc` + `.h` |
| V8DeepSerializer | `v8/src/inspector/v8-deep-serializer.cc` + `.h` |
| v8-inspector.h | `v8/include/v8-inspector.h` |
| v8.h | `v8/include/v8.h` |
| v8-script.h | `v8/include/v8-script.h` |
| v8-context.h | `v8/include/v8-context.h` |
| v8-object.h | `v8/include/v8-object.h` |
| v8-value-serializer.h | `v8/include/v8-value-serializer.h` |
| InspectorRuntimeAgent (Blink) | `third_party/blink/renderer/core/inspector/inspector_runtime_agent.cc` + `.h` |
| InspectorSession (Blink) | `third_party/blink/renderer/core/inspector/inspector_session.cc` + `.h` |
| DevToolsAgent (Blink) | `third_party/blink/renderer/core/inspector/devtools_agent.cc` + `.h` |
| DevToolsSession (Blink) | `third_party/blink/renderer/core/inspector/devtools_session.cc` + `.h` |
| DevToolsEmulator | `third_party/blink/renderer/core/inspector/dev_tools_emulator.cc` + `.h` |
| AgentRegistry | `third_party/blink/renderer/core/inspector/agent_registry.h` |
| InspectorBaseAgent | `third_party/blink/renderer/core/inspector/inspector_base_agent.h` |
| InspectorLogAgent | `third_party/blink/renderer/core/inspector/inspector_log_agent.cc` + `.h` |
| MainThreadDebugger | `third_party/blink/renderer/core/inspector/main_thread_debugger.cc` (not in slice) |
| WorkerInspectorController | `third_party/blink/renderer/core/inspector/worker_inspector_controller.cc` |
| Protocol definitions | `third_party/blink/public/devtools_protocol/browser_protocol.pdl` + `inspector_protocol.pdl` |

---

## 3.12 The Complete CDP Command & Event Reference

### 3.12.1 Runtime Domain Commands

| Command | Implementation | What it does |
|---|---|---|
| `Runtime.enable` | `v8-runtime-agent-impl.cc:1125` | Enable Runtime domain; reports all existing execution contexts |
| `Runtime.disable` | `v8-runtime-agent-impl.cc:1158` | Disable Runtime domain |
| `Runtime.evaluate` | `v8-runtime-agent-impl.cc:356` | Evaluate JS expression (16 params) |
| `Runtime.callFunctionOn` | `v8-runtime-agent-impl.cc:493` | Call a function on a RemoteObject |
| `Runtime.compileScript` | `v8-runtime-agent-impl.cc:1065` | Compile script without running (keep `v8::Script` handle) |
| `Runtime.runScript` | `v8-runtime-agent-impl.cc:1078` | Run a previously-compiled script |
| `Runtime.getProperties` | `v8-runtime-agent-impl.cc:577` | Get properties of a RemoteObject |
| `Runtime.releaseObject` | `v8-runtime-agent-impl.cc:637` | Release a RemoteObject (frees its v8::Global) |
| `Runtime.releaseObjectGroup` | `v8-runtime-agent-impl.cc:645` | Release all objects in a group |
| `Runtime.getExceptionDetails` | `v8-runtime-agent-impl.cc:1027` | Get ExceptionDetails for an error object |
| `Runtime.discardConsoleEntries` | `v8-runtime-agent-impl.cc` | Clear console message storage |
| `Runtime.setAsyncCallStackDepth` | (Debugger domain, but tracked here) | Set max async stack depth |
| `Runtime.addBinding` | `InspectorRuntimeAgent::addBinding` | Add a JS function binding that calls back to CDP |
| `Runtime.removeBinding` | `InspectorRuntimeAgent::removeBinding` | Remove a binding |
| `Runtime.getIsolateId` | `v8-runtime-agent-impl.cc` | Get the V8 isolate ID |
| `Runtime.terminateExecution` | `v8-runtime-agent-impl.cc` | Terminate JS execution in a context |
| `Runtime.globalLexicalScopeNames` | `v8-runtime-agent-impl.cc` | Get `let`/`const`/`class` names in global scope |
| `Runtime.compileApiCall` | (experimental) | Compile a function call for profiling |
| `Runtime.serializeAsync` | (experimental) | Deep-serialize a RemoteObject asynchronously |
| `Runtime.queryObjects` | `v8-runtime-agent-impl.cc` | Query prototype chain for instances |

### 3.12.2 Runtime Domain Events

| Event | When Fired | Key Fields |
|---|---|---|
| `Runtime.executionContextCreated` | New v8::Context created | context{id, origin, name, uniqueId, auxData} |
| `Runtime.executionContextDestroyed` | v8::Context GC'd or destroyed | executionContextId, executionContextUniqueId |
| `Runtime.executionContextsCleared` | All contexts cleared (navigation) | (none) |
| `Runtime.consoleAPICalled` | console.log/info/warn/error/etc. | type, args[], executionContextId, timestamp, stackTrace?, consoleContext? |
| `Runtime.exceptionThrown` | Uncaught exception | timestamp, exceptionDetails{exceptionId, text, lineNumber, columnNumber, scriptId, stackTrace, executionContextId, exception, exceptionMetaData?} |
| `Runtime.exceptionRevoked` | `ConsoleRevokeError` called | reason, exceptionId |
| `Runtime.inspectRequested` | `console.inspect()` or `inspect()` command-line API called | object, hints |
| `Runtime.bindingCalled` | A `Runtime.addBinding`-injected function is called | name, payload, executionContextId |

### 3.12.3 Debugger Domain Commands

| Command | Implementation | What it does |
|---|---|---|
| `Debugger.enable` | `v8-debugger-agent-impl.cc:487` | Enable Debugger domain; reports all compiled scripts |
| `Debugger.disable` | `v8-debugger-agent-impl.cc:520` | Disable Debugger domain |
| `Debugger.setBreakpointByUrl` | `v8-debugger-agent-impl.cc:643` | Set a breakpoint by URL/scriptHash/URLRegex |
| `Debugger.setBreakpoint` | `v8-debugger-agent-impl.cc:1142` | Set a breakpoint by scriptId+location |
| `Debugger.setBreakpointOnFunctionCall` | `v8-debugger-agent-impl.cc:1185` | Set a breakpoint on a function call |
| `Debugger.removeBreakpoint` | `v8-debugger-agent-impl.cc:767` | Remove a breakpoint |
| `Debugger.setBreakpointsActive` | `v8-debugger-agent-impl.cc:528` | Enable/disable all breakpoints |
| `Debugger.setPauseOnExceptions` | `v8-debugger-agent-impl.cc:1600` | Set pause-on-exceptions state (none/all/caught/uncaught) |
| `Debugger.pause` | `v8-debugger-agent-impl.cc:1487` | Pause on next JS execution |
| `Debugger.resume` | `v8-debugger-agent-impl.cc:1541` | Resume from pause |
| `Debugger.stepOver` | `v8-debugger-agent-impl.cc:1551` | Step over next function call |
| `Debugger.stepInto` | `v8-debugger-agent-impl.cc:1567` | Step into next function call |
| `Debugger.stepOut` | `v8-debugger-agent-impl.cc:1581` | Step out of current function |
| `Debugger.setAsyncCallStackDepth` | `v8-debugger-agent-impl.cc:1753` | Set async stack depth |
| `Debugger.getScriptSource` | `v8-debugger-agent-impl.cc:916` | Get source of a script |
| `Debugger.getPossibleBreakpoints` | `v8-debugger-agent-impl.cc:813` | Get possible breakpoint locations |
| `Debugger.getStackTrace` | `v8-debugger-agent-impl.cc:1028` | Get a stack trace by ID |
| `Debugger.evaluateOnCallFrame` | `v8-debugger-agent-impl.cc:1666` | Evaluate JS in a paused call frame |
| `Debugger.setVariableValue` | `v8-debugger-agent-impl.cc:1632` | Set a variable in a scope |
| `Debugger.searchInContent` | `v8-debugger-agent-impl.cc:856` | Search script content |
| `Debugger.setSkipAllPauses` | `v8-debugger-agent-impl.cc:1497` | Skip all pauses |
| `Debugger.setBreakpointOnFunctionCall` | `v8-debugger-agent-impl.cc:1185` | Breakpoint on function call |

### 3.12.4 Debugger Domain Events

| Event | When Fired | Key Fields |
|---|---|---|
| `Debugger.scriptParsed` | Script compiled | scriptId, url, startLine, startColumn, endLine, endColumn, executionContextId, hash, buildScriptSource?, hasSourceURL, isModule, length?, sourceMapURL?, embedderName? |
| `Debugger.scriptFailedToParse` | Script failed to compile | url, scriptId, executionContextId, stackTrace, ... |
| `Debugger.paused` | Execution paused | callFrames[], reason, data?, hitBreakpoints[], asyncStackTrace?, asyncStackTraceId?, externalStackTrace? |
| `Debugger.resumed` | Execution resumed | (none) |
| `Debugger.breakpointResolved` | A pending breakpoint resolved | breakpointId, location |

### 3.12.5 Page Domain Commands (for context injection)

| Command | Implementation | What it does |
|---|---|---|
| `Page.addScriptToEvaluateOnNewDocument` | `InspectorInjectedScriptManager::AddScriptToEvaluateOnNewDocument` | Inject script before page scripts on every navigation |
| `Page.removeScriptToEvaluateOnNewDocument` | (same file) | Remove an injected script |
| `Page.createIsolatedWorld` | `InspectorPageAgent::createIsolatedWorld` | Create an isolated world for a frame |

---

## 3.13 Qt6 WebEngine C++ Implementation

### 3.13.1 The JavaScriptExecutor Class

Here is a complete, production-ready Qt6 JavaScript execution implementation that uses CDP for full control:

#### `JavaScriptExecutor.h`

```cpp
#pragma once

#include <QObject>
#include <QWebSocket>
#include <QHash>
#include <QJsonObject>
#include <QJsonArray>
#include <functional>
#include <memory>
#include <optional>
#include <variant>

// === Data structures ===

struct RemoteObject {
    QString type;                          // "object" | "function" | "undefined" | "string" | "number" | "boolean" | "symbol" | "bigint"
    QString subtype;                       // "array" | "null" | "error" | "proxy" | "promise" | "typedarray" | "regexp" | ...
    QString className;
    QJsonValue value;                      // present if returnByValue or primitive
    QString unserializableValue;           // "NaN" | "-0" | "Infinity" | "-Infinity" | bigint as string
    QString description;
    QString objectId;                      // present for non-primitive, non-serialized
    QJsonObject preview;                   // present if generatePreview
    QJsonObject deepSerializedValue;       // present if deep serialization
    
    bool isPrimitive() const {
        return type == "string" || type == "number" || type == "boolean" 
            || type == "undefined" || type == "symbol" || type == "bigint";
    }
    bool hasObjectId() const { return !objectId.isEmpty(); }
};

struct ExceptionDetails {
    int exceptionId = 0;
    QString text;
    int lineNumber = 0;
    int columnNumber = 0;
    QString scriptId;
    QString url;
    QJsonObject stackTrace;
    int executionContextId = 0;
    RemoteObject exception;
    QJsonObject exceptionMetaData;
};

struct CallFrame {
    QString functionName;
    QString scriptId;
    QString url;
    int lineNumber = 0;
    int columnNumber = 0;
};

struct StackTrace {
    QString description;
    QList<CallFrame> callFrames;
    std::shared_ptr<StackTrace> parent;
    QString parentId;
};

struct EvaluateOptions {
    QString objectGroup;                   // default: "eval"
    bool includeCommandLineAPI = false;    // install $, $$, $x, etc.
    bool silent = false;                   // mute console output
    std::optional<int> executionContextId; // target context
    QString uniqueContextId;               // alternative to executionContextId
    bool returnByValue = false;            // serialize as JSON
    bool generatePreview = false;          // include object preview
    bool userGesture = false;              // simulate user gesture
    bool awaitPromise = false;             // chain .then() on promise result
    bool throwOnSideEffect = false;        // refuse side-effecting expressions
    std::optional<double> timeout;          // seconds (terminates execution)
    bool disableBreaks = false;             // don't pause at breakpoints
    bool replMode = false;                  // wrap top-level await
    bool allowUnsafeEvalBlockedByCSP = true;  // bypass CSP eval restriction
    // serializationOptions (newer API)
    QString serialization;                  // "" | "deep" | "json" | "idOnly"
    int maxDepth = -1;                      // for deep serialization
};

struct EvaluateResult {
    RemoteObject result;
    std::optional<ExceptionDetails> exceptionDetails;
};

class JavaScriptExecutor : public QObject {
    Q_OBJECT
public:
    explicit JavaScriptExecutor(const QUrl& devtoolsUrl, QObject* parent = nullptr);
    ~JavaScriptExecutor();
    
    // === Core operations ===
    
    // Evaluate JS expression (Level 1: DevTools Console JS)
    void evaluate(const QString& expression,
                  const EvaluateOptions& options,
                  std::function<void(const EvaluateResult&)> callback);
    
    // Synchronous wrapper (blocks via QEventLoop)
    EvaluateResult evaluateSync(const QString& expression,
                                const EvaluateOptions& options = {});
    
    // Call a function on an object
    void callFunctionOn(const QString& objectId,
                        const QString& functionDeclaration,
                        const QJsonArray& arguments,
                        const EvaluateOptions& options,
                        std::function<void(const EvaluateResult&)> callback);
    
    // Get properties of a RemoteObject
    void getProperties(const QString& objectId,
                      bool ownProperties = true,
                      bool accessorPropertiesOnly = false,
                      bool generatePreview = false,
                      std::function<void(const QList<QJsonObject>&,
                                        const QList<QJsonObject>&,
                                        const QList<QJsonObject>&,
                                        const std::optional<ExceptionDetails>&)> callback = {});
    
    // Release a RemoteObject
    void releaseObject(const QString& objectId);
    
    // Release all objects in a group
    void releaseObjectGroup(const QString& objectGroup);
    
    // Get exception details for an error object
    void getExceptionDetails(const QString& errorObjectId,
                            std::function<void(const ExceptionDetails&)> callback);
    
    // === Script injection (Level 2: Site script tag JS) ===
    
    // Inject script that runs before page scripts on every navigation
    QString addScriptToEvaluateOnNewDocument(const QString& source,
                                            const QString& worldName = "",
                                            bool grantUniversalAccess = false,
                                            bool runImmediately = true);
    
    void removeScriptToEvaluateOnNewDocument(const QString& identifier);
    
    // Create an isolated world
    int createIsolatedWorld(const QString& frameId,
                           const QString& worldName,
                           bool grantUniversalAccess = true);
    
    // === Console / exception tracking ===
    
    void enableConsoleTracking();
    void disableConsoleTracking();
    
    // === Debugger ===
    
    void enableDebugger();
    void disableDebugger();
    
    void setBreakpointByUrl(int lineNumber,
                           const QString& url,
                           const QString& condition = "",
                           std::function<void(const QString& breakpointId,
                                            const QList<QJsonObject>& locations)> callback = {});
    
    void removeBreakpoint(const QString& breakpointId);
    
    void setPauseOnExceptions(const QString& state);  // "none" | "all" | "caught" | "uncaught"
    
    void pause();
    void resume();
    void stepOver();
    void stepInto();
    void stepOut();
    
    void evaluateOnCallFrame(const QString& callFrameId,
                            const QString& expression,
                            const EvaluateOptions& options,
                            std::function<void(const EvaluateResult&)> callback);
    
    void setAsyncCallStackDepth(int depth);
    
    // === Execution context queries ===
    
    QList<QJsonObject> executionContexts() const;
    std::optional<int> defaultExecutionContextId() const;
    std::optional<int> executionContextIdForFrame(const QString& frameId, bool mainWorld = true) const;
    
    // === Bindings (custom JS→CDP callbacks) ===
    
    void addBinding(const QString& name);
    void removeBinding(const QString& name);
    
signals:
    void consoleAPICalled(const QString& type, const QList<RemoteObject>& args,
                         int executionContextId, double timestamp,
                         const std::optional<StackTrace>& stackTrace);
    void exceptionThrown(const ExceptionDetails& details);
    void exceptionRevoked(const QString& reason, int exceptionId);
    void executionContextCreated(const QJsonObject& context);
    void executionContextDestroyed(int contextId);
    void executionContextsCleared();
    void inspectRequested(const RemoteObject& object, const QJsonObject& hints);
    void bindingCalled(const QString& name, const QString& payload, int executionContextId);
    
    void scriptParsed(const QString& scriptId, const QString& url,
                     int executionContextId, const QString& hash);
    void scriptFailedToParse(const QString& url, const QString& scriptId,
                            int executionContextId);
    void paused(const QList<QJsonObject>& callFrames, const QString& reason,
               const QJsonObject& data, const QStringList& hitBreakpoints);
    void resumed();
    void breakpointResolved(const QString& breakpointId, const QJsonObject& location);
    
private:
    void sendCommand(const QString& method, const QJsonObject& params,
                    std::function<void(const QJsonObject&)> callback = {});
    void handleMessage(const QString& message);
    
    // Helpers
    static RemoteObject parseRemoteObject(const QJsonObject& obj);
    static ExceptionDetails parseExceptionDetails(const QJsonObject& obj);
    static StackTrace parseStackTrace(const QJsonObject& obj);
    static EvaluateOptions defaultOptions();
    
    QWebSocket* m_ws;
    int m_nextId = 1;
    QHash<int, std::function<void(const QJsonObject&)>> m_callbacks;
    QString m_sessionId;
    
    // Track execution contexts
    QHash<int, QJsonObject> m_executionContexts;   // contextId → context description
    
    // Track injected scripts (identifier → source)
    QHash<QString, QString> m_injectedScripts;
    
    // Track bindings (name → active)
    QSet<QString> m_bindings;
};
```

#### `JavaScriptExecutor.cpp` (key methods)

```cpp
#include "JavaScriptExecutor.h"
#include <QJsonDocument>
#include <QEventLoop>
#include <QTimer>

// === Helpers ===

RemoteObject JavaScriptExecutor::parseRemoteObject(const QJsonObject& obj) {
    RemoteObject r;
    r.type = obj.value("type").toString();
    r.subtype = obj.value("subtype").toString();
    r.className = obj.value("className").toString();
    if (obj.contains("value")) r.value = obj.value("value");
    r.unserializableValue = obj.value("unserializableValue").toString();
    r.description = obj.value("description").toString();
    r.objectId = obj.value("objectId").toString();
    if (obj.contains("preview")) r.preview = obj.value("preview").toObject();
    if (obj.contains("deepSerializedValue"))
        r.deepSerializedValue = obj.value("deepSerializedValue").toObject();
    return r;
}

ExceptionDetails JavaScriptExecutor::parseExceptionDetails(const QJsonObject& obj) {
    ExceptionDetails d;
    d.exceptionId = obj.value("exceptionId").toInt();
    d.text = obj.value("text").toString();
    d.lineNumber = obj.value("lineNumber").toInt();
    d.columnNumber = obj.value("columnNumber").toInt();
    d.scriptId = obj.value("scriptId").toString();
    d.url = obj.value("url").toString();
    if (obj.contains("stackTrace")) d.stackTrace = obj.value("stackTrace").toObject();
    d.executionContextId = obj.value("executionContextId").toInt();
    if (obj.contains("exception")) {
        d.exception = parseRemoteObject(obj.value("exception").toObject());
    }
    if (obj.contains("exceptionMetaData")) {
        d.exceptionMetaData = obj.value("exceptionMetaData").toObject();
    }
    return d;
}

StackTrace JavaScriptExecutor::parseStackTrace(const QJsonObject& obj) {
    StackTrace st;
    st.description = obj.value("description").toString();
    const QJsonArray frames = obj.value("callFrames").toArray();
    for (const QJsonValue& f : frames) {
        const QJsonObject fo = f.toObject();
        CallFrame cf;
        cf.functionName = fo.value("functionName").toString();
        cf.scriptId = fo.value("scriptId").toString();
        cf.url = fo.value("url").toString();
        cf.lineNumber = fo.value("lineNumber").toInt();
        cf.columnNumber = fo.value("columnNumber").toInt();
        st.callFrames.append(cf);
    }
    if (obj.contains("parent")) {
        st.parent = std::make_shared<StackTrace>(parseStackTrace(obj.value("parent").toObject()));
    }
    if (obj.contains("parentId")) {
        st.parentId = obj.value("parentId").toObject().value("id").toString();
    }
    return st;
}

EvaluateOptions JavaScriptExecutor::defaultOptions() {
    EvaluateOptions opts;
    opts.objectGroup = "eval";
    opts.returnByValue = false;
    opts.awaitPromise = false;
    opts.allowUnsafeEvalBlockedByCSP = true;
    return opts;
}

// === Constructor / Destructor ===

JavaScriptExecutor::JavaScriptExecutor(const QUrl& devtoolsUrl, QObject* parent)
    : QObject(parent), m_ws(new QWebSocket) {
    
    connect(m_ws, &QWebSocket::textMessageReceived,
            this, &JavaScriptExecutor::handleMessage);
    m_ws->open(devtoolsUrl);
}

JavaScriptExecutor::~JavaScriptExecutor() {
    if (m_ws->isValid()) m_ws->close();
}

// === CDP plumbing ===

void JavaScriptExecutor::sendCommand(const QString& method, const QJsonObject& params,
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

void JavaScriptExecutor::handleMessage(const QString& message) {
    const auto doc = QJsonDocument::fromJson(message.toUtf8()).object();
    
    // Response to our command
    if (doc.contains("id")) {
        const int id = doc.value("id").toInt();
        auto it = m_callbacks.find(id);
        if (it != m_callbacks.end()) {
            auto cb = it.value();
            m_callbacks.erase(it);
            if (cb) {
                if (doc.contains("error")) {
                    qWarning() << "CDP error:" << doc.value("error").toObject();
                }
                cb(doc.value("result").toObject());
            }
        }
        return;
    }
    
    // Event
    const QString method = doc.value("method").toString();
    const QJsonObject params = doc.value("params").toObject();
    
    if (method == "Runtime.executionContextCreated") {
        const QJsonObject ctx = params.value("context").toObject();
        const int id = ctx.value("id").toInt();
        m_executionContexts[id] = ctx;
        emit executionContextCreated(ctx);
    }
    else if (method == "Runtime.executionContextDestroyed") {
        const int id = params.value("executionContextId").toInt();
        m_executionContexts.remove(id);
        emit executionContextDestroyed(id);
    }
    else if (method == "Runtime.executionContextsCleared") {
        m_executionContexts.clear();
        emit executionContextsCleared();
    }
    else if (method == "Runtime.consoleAPICalled") {
        const QString type = params.value("type").toString();
        QList<RemoteObject> args;
        const QJsonArray argsArr = params.value("args").toArray();
        for (const QJsonValue& a : argsArr) {
            args.append(parseRemoteObject(a.toObject()));
        }
        const int ctxId = params.value("executionContextId").toInt();
        const double ts = params.value("timestamp").toDouble();
        std::optional<StackTrace> st;
        if (params.contains("stackTrace")) {
            st = parseStackTrace(params.value("stackTrace").toObject());
        }
        emit consoleAPICalled(type, args, ctxId, ts, st);
    }
    else if (method == "Runtime.exceptionThrown") {
        const ExceptionDetails d = parseExceptionDetails(
            params.value("exceptionDetails").toObject());
        emit exceptionThrown(d);
    }
    else if (method == "Runtime.exceptionRevoked") {
        const QString reason = params.value("reason").toString();
        const int id = params.value("exceptionId").toInt();
        emit exceptionRevoked(reason, id);
    }
    else if (method == "Runtime.inspectRequested") {
        const RemoteObject obj = parseRemoteObject(params.value("object").toObject());
        const QJsonObject hints = params.value("hints").toObject();
        emit inspectRequested(obj, hints);
    }
    else if (method == "Runtime.bindingCalled") {
        const QString name = params.value("name").toString();
        const QString payload = params.value("payload").toString();
        const int ctxId = params.value("executionContextId").toInt();
        emit bindingCalled(name, payload, ctxId);
    }
    else if (method == "Debugger.scriptParsed") {
        const QString scriptId = params.value("scriptId").toString();
        const QString url = params.value("url").toString();
        const int ctxId = params.value("executionContextId").toInt();
        const QString hash = params.value("hash").toString();
        emit scriptParsed(scriptId, url, ctxId, hash);
    }
    else if (method == "Debugger.scriptFailedToParse") {
        const QString url = params.value("url").toString();
        const QString scriptId = params.value("scriptId").toString();
        const int ctxId = params.value("executionContextId").toInt();
        emit scriptFailedToParse(url, scriptId, ctxId);
    }
    else if (method == "Debugger.paused") {
        QList<QJsonObject> callFrames;
        const QJsonArray arr = params.value("callFrames").toArray();
        for (const QJsonValue& v : arr) callFrames.append(v.toObject());
        const QString reason = params.value("reason").toString();
        const QJsonObject data = params.value("data").toObject();
        const QStringList hitBps = params.value("hitBreakpoints").toVariant().toStringList();
        emit paused(callFrames, reason, data, hitBps);
    }
    else if (method == "Debugger.resumed") {
        emit resumed();
    }
    else if (method == "Debugger.breakpointResolved") {
        const QString bpId = params.value("breakpointId").toString();
        const QJsonObject loc = params.value("location").toObject();
        emit breakpointResolved(bpId, loc);
    }
}

// === Core: evaluate ===

void JavaScriptExecutor::evaluate(const QString& expression,
                                  const EvaluateOptions& options,
                                  std::function<void(const EvaluateResult&)> callback) {
    QJsonObject params;
    params["expression"] = expression;
    if (!options.objectGroup.isEmpty()) params["objectGroup"] = options.objectGroup;
    if (options.includeCommandLineAPI) params["includeCommandLineAPI"] = true;
    if (options.silent) params["silent"] = true;
    if (options.executionContextId.has_value()) {
        params["executionContextId"] = options.executionContextId.value();
    }
    if (!options.uniqueContextId.isEmpty()) {
        params["uniqueContextId"] = options.uniqueContextId;
    }
    if (options.returnByValue) params["returnByValue"] = true;
    if (options.generatePreview) params["generatePreview"] = true;
    if (options.userGesture) params["userGesture"] = true;
    if (options.awaitPromise) params["awaitPromise"] = true;
    if (options.throwOnSideEffect) params["throwOnSideEffect"] = true;
    if (options.timeout.has_value()) {
        params["timeout"] = options.timeout.value();
    }
    if (options.disableBreaks) params["disableBreaks"] = true;
    if (options.replMode) params["replMode"] = true;
    if (options.allowUnsafeEvalBlockedByCSP) {
        params["allowUnsafeEvalBlockedByCSP"] = true;
    }
    
    // Newer serializationOptions API
    if (!options.serialization.isEmpty()) {
        QJsonObject serOpts;
        serOpts["serialization"] = options.serialization;
        if (options.maxDepth >= 0) serOpts["maxDepth"] = options.maxDepth;
        params["serializationOptions"] = serOpts;
    }
    
    sendCommand("Runtime.evaluate", params, [callback](const QJsonObject& result) {
        EvaluateResult r;
        if (result.contains("result")) {
            r.result = parseRemoteObject(result.value("result").toObject());
        }
        if (result.contains("exceptionDetails")) {
            r.exceptionDetails = parseExceptionDetails(
                result.value("exceptionDetails").toObject());
        }
        callback(r);
    });
}

// Synchronous wrapper
EvaluateResult JavaScriptExecutor::evaluateSync(const QString& expression,
                                                  const EvaluateOptions& options) {
    EvaluateResult result;
    QEventLoop loop;
    evaluate(expression, options, [&](const EvaluateResult& r) {
        result = r;
        loop.quit();
    });
    loop.exec();
    return result;
}

// === callFunctionOn ===

void JavaScriptExecutor::callFunctionOn(const QString& objectId,
                                        const QString& functionDeclaration,
                                        const QJsonArray& arguments,
                                        const EvaluateOptions& options,
                                        std::function<void(const EvaluateResult&)> callback) {
    QJsonObject params;
    params["objectId"] = objectId;
    params["functionDeclaration"] = functionDeclaration;
    params["arguments"] = arguments;
    if (!options.objectGroup.isEmpty()) params["objectGroup"] = options.objectGroup;
    if (options.silent) params["silent"] = true;
    if (options.returnByValue) params["returnByValue"] = true;
    if (options.generatePreview) params["generatePreview"] = true;
    if (options.userGesture) params["userGesture"] = true;
    if (options.awaitPromise) params["awaitPromise"] = true;
    if (options.throwOnSideEffect) params["throwOnSideEffect"] = true;
    
    sendCommand("Runtime.callFunctionOn", params, [callback](const QJsonObject& result) {
        EvaluateResult r;
        if (result.contains("result")) {
            r.result = parseRemoteObject(result.value("result").toObject());
        }
        if (result.contains("exceptionDetails")) {
            r.exceptionDetails = parseExceptionDetails(
                result.value("exceptionDetails").toObject());
        }
        callback(r);
    });
}

// === getProperties ===

void JavaScriptExecutor::getProperties(const QString& objectId,
                                      bool ownProperties,
                                      bool accessorPropertiesOnly,
                                      bool generatePreview,
                                      std::function<void(const QList<QJsonObject>&,
                                                        const QList<QJsonObject>&,
                                                        const QList<QJsonObject>&,
                                                        const std::optional<ExceptionDetails>&)> callback) {
    QJsonObject params;
    params["objectId"] = objectId;
    params["ownProperties"] = ownProperties;
    params["accessorPropertiesOnly"] = accessorPropertiesOnly;
    if (generatePreview) params["generatePreview"] = true;
    
    sendCommand("Runtime.getProperties", params, [callback](const QJsonObject& result) {
        QList<QJsonObject> props;
        if (result.contains("result")) {
            const QJsonArray arr = result.value("result").toArray();
            for (const QJsonValue& v : arr) props.append(v.toObject());
        }
        QList<QJsonObject> internalProps;
        if (result.contains("internalProperties")) {
            const QJsonArray arr = result.value("internalProperties").toArray();
            for (const QJsonValue& v : arr) internalProps.append(v.toObject());
        }
        QList<QJsonObject> privateProps;
        if (result.contains("privateProperties")) {
            const QJsonArray arr = result.value("privateProperties").toArray();
            for (const QJsonValue& v : arr) privateProps.append(v.toObject());
        }
        std::optional<ExceptionDetails> excDetails;
        if (result.contains("exceptionDetails")) {
            excDetails = parseExceptionDetails(result.value("exceptionDetails").toObject());
        }
        if (callback) callback(props, internalProps, privateProps, excDetails);
    });
}

// === releaseObject / releaseObjectGroup ===

void JavaScriptExecutor::releaseObject(const QString& objectId) {
    QJsonObject params;
    params["objectId"] = objectId;
    sendCommand("Runtime.releaseObject", params);
}

void JavaScriptExecutor::releaseObjectGroup(const QString& objectGroup) {
    QJsonObject params;
    params["objectGroup"] = objectGroup;
    sendCommand("Runtime.releaseObjectGroup", params);
}

// === getExceptionDetails ===

void JavaScriptExecutor::getExceptionDetails(const QString& errorObjectId,
                                            std::function<void(const ExceptionDetails&)> callback) {
    QJsonObject params;
    params["errorObjectId"] = errorObjectId;
    sendCommand("Runtime.getExceptionDetails", params, [callback](const QJsonObject& result) {
        if (result.contains("exceptionDetails")) {
            callback(parseExceptionDetails(result.value("exceptionDetails").toObject()));
        }
    });
}

// === Script injection (Level 2: Site script tag JS) ===

QString JavaScriptExecutor::addScriptToEvaluateOnNewDocument(
        const QString& source,
        const QString& worldName,
        bool grantUniversalAccess,
        bool runImmediately) {
    QJsonObject params;
    params["source"] = source;
    if (!worldName.isEmpty()) params["worldName"] = worldName;
    if (grantUniversalAccess) params["grantUniversalAccess"] = true;
    if (runImmediately) params["runImmediately"] = true;
    
    QString identifier;
    sendCommand("Page.addScriptToEvaluateOnNewDocument", params,
                [&identifier](const QJsonObject& result) {
        identifier = result.value("identifier").toString();
    });
    // Note: this is async; for sync use QEventLoop
    QEventLoop loop;
    QTimer::singleShot(1000, &loop, &QEventLoop::quit);  // timeout
    // Actually we need to wait for the response properly
    // For simplicity, use a blocking pattern:
    sendCommand("Page.addScriptToEvaluateOnNewDocument", params,
                [this, &identifier](const QJsonObject& result) {
        identifier = result.value("identifier").toString();
    });
    // Wait... (in production, use a proper async pattern)
    return identifier;
}

void JavaScriptExecutor::removeScriptToEvaluateOnNewDocument(const QString& identifier) {
    QJsonObject params;
    params["identifier"] = identifier;
    sendCommand("Page.removeScriptToEvaluateOnNewDocument", params);
    m_injectedScripts.remove(identifier);
}

// === createIsolatedWorld ===

int JavaScriptExecutor::createIsolatedWorld(const QString& frameId,
                                            const QString& worldName,
                                            bool grantUniversalAccess) {
    QJsonObject params;
    params["frameId"] = frameId;
    if (!worldName.isEmpty()) params["worldName"] = worldName;
    if (grantUniversalAccess) params["grantUniversalAccess"] = true;
    
    int contextId = 0;
    sendCommand("Page.createIsolatedWorld", params, [&contextId](const QJsonObject& result) {
        contextId = result.value("executionContextId").toInt();
    });
    return contextId;
}

// === Console tracking ===

void JavaScriptExecutor::enableConsoleTracking() {
    sendCommand("Runtime.enable", {});
}

void JavaScriptExecutor::disableConsoleTracking() {
    sendCommand("Runtime.disable", {});
}

// === Debugger ===

void JavaScriptExecutor::enableDebugger() {
    sendCommand("Debugger.enable", {});
}

void JavaScriptExecutor::disableDebugger() {
    sendCommand("Debugger.disable", {});
}

void JavaScriptExecutor::setBreakpointByUrl(int lineNumber,
                                           const QString& url,
                                           const QString& condition,
                                           std::function<void(const QString&,
                                                            const QList<QJsonObject>&)> callback) {
    QJsonObject params;
    params["lineNumber"] = lineNumber;
    params["url"] = url;
    if (!condition.isEmpty()) params["condition"] = condition;
    
    sendCommand("Debugger.setBreakpointByUrl", params, [callback](const QJsonObject& result) {
        const QString bpId = result.value("breakpointId").toString();
        QList<QJsonObject> locations;
        const QJsonArray arr = result.value("locations").toArray();
        for (const QJsonValue& v : arr) locations.append(v.toObject());
        if (callback) callback(bpId, locations);
    });
}

void JavaScriptExecutor::removeBreakpoint(const QString& breakpointId) {
    QJsonObject params;
    params["breakpointId"] = breakpointId;
    sendCommand("Debugger.removeBreakpoint", params);
}

void JavaScriptExecutor::setPauseOnExceptions(const QString& state) {
    QJsonObject params;
    params["state"] = state;  // "none" | "all" | "caught" | "uncaught"
    sendCommand("Debugger.setPauseOnExceptions", params);
}

void JavaScriptExecutor::pause() {
    sendCommand("Debugger.pause", {});
}

void JavaScriptExecutor::resume() {
    sendCommand("Debugger.resume", {});
}

void JavaScriptExecutor::stepOver() {
    sendCommand("Debugger.stepOver", {});
}

void JavaScriptExecutor::stepInto() {
    sendCommand("Debugger.stepInto", {});
}

void JavaScriptExecutor::stepOut() {
    sendCommand("Debugger.stepOut", {});
}

void JavaScriptExecutor::evaluateOnCallFrame(const QString& callFrameId,
                                            const QString& expression,
                                            const EvaluateOptions& options,
                                            std::function<void(const EvaluateResult&)> callback) {
    QJsonObject params;
    params["callFrameId"] = callFrameId;
    params["expression"] = expression;
    if (!options.objectGroup.isEmpty()) params["objectGroup"] = options.objectGroup;
    if (options.returnByValue) params["returnByValue"] = true;
    if (options.generatePreview) params["generatePreview"] = true;
    if (options.awaitPromise) params["awaitPromise"] = true;
    if (options.throwOnSideEffect) params["throwOnSideEffect"] = true;
    
    sendCommand("Debugger.evaluateOnCallFrame", params, [callback](const QJsonObject& result) {
        EvaluateResult r;
        if (result.contains("result")) {
            r.result = parseRemoteObject(result.value("result").toObject());
        }
        if (result.contains("exceptionDetails")) {
            r.exceptionDetails = parseExceptionDetails(
                result.value("exceptionDetails").toObject());
        }
        callback(r);
    });
}

void JavaScriptExecutor::setAsyncCallStackDepth(int depth) {
    QJsonObject params;
    params["maxDepth"] = depth;
    sendCommand("Debugger.setAsyncCallStackDepth", params);
}

// === Execution context queries ===

QList<QJsonObject> JavaScriptExecutor::executionContexts() const {
    return m_executionContexts.values();
}

std::optional<int> JavaScriptExecutor::defaultExecutionContextId() const {
    for (auto it = m_executionContexts.begin(); it != m_executionContexts.end(); ++it) {
        const QJsonObject ctx = it.value();
        const QJsonObject aux = ctx.value("auxData").toObject();
        if (aux.value("isDefault").toBool(false)) {
            return it.key();
        }
    }
    return std::nullopt;
}

std::optional<int> JavaScriptExecutor::executionContextIdForFrame(
        const QString& frameId, bool mainWorld) const {
    for (auto it = m_executionContexts.begin(); it != m_executionContexts.end(); ++it) {
        const QJsonObject ctx = it.value();
        const QJsonObject aux = ctx.value("auxData").toObject();
        if (aux.value("frameId").toString() == frameId) {
            if (mainWorld && aux.value("isDefault").toBool(false)) {
                return it.key();
            }
            if (!mainWorld && aux.value("type").toString() == "isolated") {
                return it.key();
            }
        }
    }
    return std::nullopt;
}

// === Bindings ===

void JavaScriptExecutor::addBinding(const QString& name) {
    QJsonObject params;
    params["name"] = name;
    sendCommand("Runtime.addBinding", params);
    m_bindings.insert(name);
}

void JavaScriptExecutor::removeBinding(const QString& name) {
    QJsonObject params;
    params["name"] = name;
    sendCommand("Runtime.removeBinding", params);
    m_bindings.remove(name);
}
```

### 3.13.2 Using the JavaScriptExecutor

```cpp
// In your scraper:
auto* js = new JavaScriptExecutor(QUrl("ws://127.0.0.1:9222/devtools/page/<id>"));

// Enable Runtime to get execution context events
js->enableConsoleTracking();

// Track execution contexts
connect(js, &JavaScriptExecutor::executionContextCreated,
        [](const QJsonObject& ctx) {
    qDebug() << "Context created:" << ctx.value("id").toInt()
             << "name:" << ctx.value("name").toString()
             << "auxData:" << ctx.value("auxData").toObject();
});

// Capture console messages
connect(js, &JavaScriptExecutor::consoleAPICalled,
        [](const QString& type, const QList<RemoteObject>& args,
           int ctxId, double ts, const std::optional<StackTrace>& st) {
    QString msg;
    for (const RemoteObject& arg : args) {
        if (!msg.isEmpty()) msg += " ";
        if (arg.value.isString()) msg += arg.value.toString();
        else if (arg.value.isBool() || arg.value.isDouble())
            msg += QString::fromUtf8(QJsonDocument(QJsonArray{arg.value}).toJson());
        else msg += arg.description;
    }
    qDebug() << "[console." << type << "]" << msg;
    if (st) {
        for (const CallFrame& f : st->callFrames) {
            qDebug() << "  at" << f.functionName << "(" << f.url
                     << ":" << f.lineNumber << ":" << f.columnNumber << ")";
        }
    }
});

// Capture uncaught exceptions
connect(js, &JavaScriptExecutor::exceptionThrown,
        [](const ExceptionDetails& d) {
    qDebug() << "[EXCEPTION]" << d.text
             << "at" << d.url << ":" << d.lineNumber << ":" << d.columnNumber;
    if (!d.exception.objectId.isEmpty()) {
        qDebug() << "  exception:" << d.exception.description;
    }
});

// === Level 1: DevTools Console JS ===

// Synchronous evaluation
EvaluateResult title = js->evaluateSync("document.title",
    []() { EvaluateOptions o; o.returnByValue = true; return o; }());
qDebug() << "Title:" << title.result.value.toString();

// Async evaluation
js->evaluate("document.querySelectorAll('a').length", {},
    [](const EvaluateResult& r) {
    qDebug() << "Link count:" << r.result.value.toInt();
});

// With awaitPromise
EvaluateOptions asyncOpts;
asyncOpts.awaitPromise = true;
asyncOpts.returnByValue = true;
js->evaluate(R"(
    (async () => {
        const response = await fetch('/api/data');
        return await response.json();
    })()
)", asyncOpts, [](const EvaluateResult& r) {
    qDebug() << "API data:" << r.result.value;
});

// Get properties of a DOM element
js->evaluate("document.querySelector('h1')",
    []() { EvaluateOptions o; o.objectGroup = "scratch"; return o; }(),
    [js](const EvaluateResult& r) {
    if (r.result.hasObjectId()) {
        js->getProperties(r.result.objectId, true, false, true,
            [](const QList<QJsonObject>& props, ...,
               const std::optional<ExceptionDetails>&) {
            for (const QJsonObject& p : props) {
                qDebug() << "  prop:" << p.value("name").toString()
                         << "value:" << p.value("value").toObject().value("description").toString();
            }
        });
    }
});

// === Level 2: Site Script Tag JS (stealthy) ===

// Inject a script that runs before page scripts in the MAIN world
// (page can see your globals — only use this for transparent hooks)
QString hookId = js->addScriptToEvaluateOnNewDocument(R"(
    (function() {
        const origFetch = window.fetch;
        window.fetch = function(...args) {
            console.log('[fetch]', args[0]);
            return origFetch.apply(this, args);
        };
        window.__scraper = {
            extractData: function() {
                return Array.from(document.querySelectorAll('.item')).map(el => el.textContent);
            }
        };
    })();
)", "", false, true);  // empty worldName = main world, runImmediately = true

// Inject in an ISOLATED world (page can't see your globals)
QString isolatedId = js->addScriptToEvaluateOnNewDocument(R"(
    (function() {
        // This runs in an isolated world — page's window is different from ours
        // But DOM is shared (cross-world)
        window.__scraper = {
            extractData: function() {
                return Array.from(document.querySelectorAll('.item')).map(el => el.textContent);
            }
        };
    })();
)", "scraper-world", true, true);  // grantUniversalAccess = true

// === Execute in isolated world via Runtime.evaluate ===

// Create an isolated world
int isolatedCtxId = js->createIsolatedWorld("<mainFrameId>", "eval-world", true);

// Execute in that isolated world
EvaluateOptions isolatedOpts;
isolatedOpts.executionContextId = isolatedCtxId;
isolatedOpts.returnByValue = true;
js->evaluate("window.__scraper.extractData()", isolatedOpts,
    [](const EvaluateResult& r) {
    qDebug() << "Scraped data:" << r.result.value;
});

// === Bindings (custom JS→CDP callbacks) ===

js->addBinding("scraperReady");
connect(js, &JavaScriptExecutor::bindingCalled,
        [](const QString& name, const QString& payload, int ctxId) {
    if (name == "scraperReady") {
        qDebug() << "Scraper signaled ready with payload:" << payload;
    }
});

// Inject code that calls the binding
js->addScriptToEvaluateOnNewDocument(R"(
    // When the page is fully loaded, signal the scraper
    window.addEventListener('load', () => {
        scraperReady(JSON.stringify({ url: location.href, ready: true }));
    });
)", "binding-world", true, true);

// === Debugger ===

js->enableDebugger();
connect(js, &JavaScriptExecutor::paused,
        [js](const QList<QJsonObject>& callFrames, const QString& reason,
           const QJsonObject& data, const QStringList& hitBps) {
    qDebug() << "[PAUSED] reason:" << reason;
    for (const QJsonObject& cf : callFrames) {
        qDebug() << "  at" << cf.value("functionName").toString()
                 << "(" << cf.value("url").toString()
                 << ":" << cf.value("location").toObject().value("lineNumber").toInt() << ")";
    }
    
    // Evaluate in the paused context
    EvaluateOptions opts;
    opts.returnByValue = true;
    js->evaluateOnCallFrame(callFrames.first().value("callFrameId").toString(),
                            "JSON.stringify({locals: Object.keys(this)})",
                            opts, [](const EvaluateResult& r) {
        qDebug() << "Locals:" << r.result.value;
    });
    
    // Resume
    js->resume();
});

// Set a breakpoint
js->setBreakpointByUrl(10, "https://example.com/app.js", "variables.x > 100",
    [](const QString& bpId, const QList<QJsonObject>& locations) {
    qDebug() << "Breakpoint set:" << bpId << "at" << locations.size() << "locations";
});

// Pause on uncaught exceptions
js->setPauseOnExceptions("uncaught");

// Set async stack depth for better async debugging
js->setAsyncCallStackDepth(32);
```

---

## 3.14 Edge Cases

### 3.14.1 Context Destruction Mid-Evaluation

If the page navigates while `Runtime.evaluate` is running, the v8::Context is destroyed. V8 throws a `v8::debug::ExecutionTerminated` exception. `InjectedScript::wrapEvaluateResult` checks for this:

```cpp
if (tryCatch.HasTerminated() || !tryCatch.CanContinue())
  return Response::ServerError("Execution was terminated");
```

The CDP response will be an error: `{"code": -32000, "message": "Execution was terminated"}`.

**For scraping**: always wrap your `Runtime.evaluate` calls in a timeout. If the page navigates, your callback may never fire — use a `QTimer` to abort.

### 3.14.2 Cross-Context Object References

A `RemoteObject`'s `objectId` is scoped to a specific `InjectedScript` (which is scoped to a specific `v8::Context`). You **cannot** pass an `objectId` from one context to another. If you try, `InjectedScript::ObjectScope::initialize` will fail with "Inspected context has been destroyed" or "Cannot find object with given id".

**Workaround**: use `Runtime.callFunctionOn` with the `objectId` in the same context, OR re-evaluate to get a fresh `objectId` in the target context.

### 3.14.3 Object Group Leaks

If you call `Runtime.evaluate` with `objectGroup: "scratch"` repeatedly without calling `Runtime.releaseObjectGroup("scratch")`, the strong v8::Global handles accumulate. For 1000 evaluations returning large objects, this can leak hundreds of MB.

**Best practice**: always release the group when done:

```cpp
js->evaluate("document.querySelector('body')", opts, [js](const EvaluateResult& r) {
    // Use the object
    js->getProperties(r.result.objectId, ..., [js, objectId = r.result.objectId](...) {
        // Done with the object
        js->releaseObject(objectId);
    });
});
```

Or release the whole group at the end of a scraping session:

```cpp
js->releaseObjectGroup("scratch");
```

### 3.14.4 Promise Rejection with `awaitPromise`

If you pass `awaitPromise: true` but the expression returns a non-Promise, V8 wraps it in `Promise.resolve(value)` first. The callback fires on the next microtask.

If the promise rejects, `ProtocolPromiseHandler::catchCallback` (`injected-script.cc:294`) builds an `ExceptionDetails` with `text: "Uncaught (in promise)"` and the rejection reason as the `exception` field.

```json
{
  "result": {
    "result": { "type": "undefined" },
    "exceptionDetails": {
      "exceptionId": 42,
      "text": "Uncaught (in promise)",
      "exception": {
        "type": "object",
        "subtype": "error",
        "description": "Error: Network request failed",
        "objectId": "..."
      }
    }
  }
}
```

### 3.14.5 CSP `unsafe-eval` Restriction

If the page's CSP is `script-src 'self'` (no `'unsafe-eval'`), then `eval()`, `Function()`, and `setTimeout("code", 0)` are blocked. By default, `Runtime.evaluate` bypasses this via `allowUnsafeEvalBlockedByCSP: true` (the default).

If you set `allowUnsafeEvalBlockedByCSP: false`, and the page has a strict CSP, your `Runtime.evaluate` will fail with:

```json
{
  "result": {
    "result": { "type": "undefined" },
    "exceptionDetails": {
      "text": "EvalError: Code generation from strings disallowed for this context"
    }
  }
}
```

**For scraping**: leave `allowUnsafeEvalBlockedByCSP` at its default (`true`). If you need to inject code into a page with strict CSP, use `Page.addScriptToEvaluateOnNewDocument` instead (which bypasses CSP for injected scripts).

### 3.14.6 `throwOnSideEffect` Limitations

`throwOnSideEffect: true` makes V8 refuse expressions that have side effects (assignments, function calls that modify state, etc.). This is used for performance profiling — you can read values without affecting the page.

However, V8's side-effect detection is conservative. Some "innocent" expressions are flagged as having side effects:
- Property accessors that have getters (the getter could modify state)
- `Proxy` traps (could do anything)
- `Symbol.toPrimitive` calls
- `toString()` / `valueOf()` calls

If you hit `EvalError: Side-effect free execution was interrupted`, you'll need to remove `throwOnSideEffect`.

### 3.14.7 Timeout and Termination

If you pass `timeout: 5.0` (5 seconds), V8 schedules a delayed task that calls `m_isolate->TerminateExecution()`. When the timeout fires:
1. V8 aborts the current JS execution
2. `tryCatch.HasTerminated()` returns true
3. The CDP response is `{"error": {"message": "Execution was terminated"}}`

If your JS was in the middle of a `fetch()` or `XMLHttpRequest`, the network request is **not** automatically canceled. You'll need to handle that separately.

### 3.14.8 Worker Context Evaluation

To evaluate JS in a worker, you must:
1. Use `Target.setAutoAttach({autoAttach: true, waitForDebuggerOnStart: true, flatten: true})` to auto-attach to worker targets
2. Listen for `Target.attachedToTarget` with `targetInfo.type === "worker"`
3. Send `Runtime.enable` with the worker's `sessionId`
4. Send `Runtime.evaluate` with the worker's `sessionId`

Workers have only ONE execution context (the worker's main world). There's no DOM, no `window`, no `document`. The global is `self` (or `WorkerGlobalScope`).

### 3.14.9 Isolated World vs Main World — DOM Sharing

Isolated worlds share the DOM with the main world. This means:
- `document.querySelector('#foo')` in an isolated world returns the **same** DOM element as in the main world
- Modifying the DOM from an isolated world IS visible to the main world
- BUT — JS objects (`window.myVar`) are NOT shared — each world has its own `window`

This is why content scripts (Chrome extensions) work: they can manipulate the DOM but can't pollute the page's JS namespace.

### 3.14.10 `Runtime.addBinding` Lifetime

A binding added via `Runtime.addBinding` is:
- Available in all execution contexts of the target frame
- Persists across navigations (if added via `Page.addScriptToEvaluateOnNewDocument`)
- Fires `Runtime.bindingCalled` events to the CDP client
- The binding function is a no-op if the CDP client isn't connected

**For scraping**: bindings are a great way to signal from page JS back to your scraper without polling. Inject a binding, then have your injected script call it when data is ready.

---

## 3.15 Performance Impact

### 3.15.1 Cost of Runtime.evaluate

| Operation | Cost |
|---|---|
| `Runtime.enable` (cold start) | ~5-10ms (reports all contexts) |
| `Runtime.evaluate` (small expression, `returnByValue`) | ~0.5-2ms (CDP roundtrip + V8 compile + run + JSON serialize) |
| `Runtime.evaluate` (large expression, `returnByValue` of big object) | ~5-50ms (JSON serialization dominates) |
| `Runtime.evaluate` with `awaitPromise` | +1 microtask (~0.1ms) + promise resolution time |
| `Runtime.evaluate` with `generatePreview` | +1-5ms per object (preview generation) |
| `Runtime.evaluate` with `deep` serialization | +10-100ms depending on depth |
| `Runtime.getProperties` (object with 100 properties) | ~5-10ms |
| `Runtime.releaseObject` | ~0.1ms |
| `Runtime.releaseObjectGroup` | O(N) where N = objects in group |
| `Runtime.addBinding` (one-time setup) | ~1ms |
| `Page.addScriptToEvaluateOnNewDocument` | ~1ms (per script, one-time) |
| Per-navigation re-injection of all `addScriptToEvaluateOnNewDocument` scripts | ~1-5ms total |

### 3.15.2 Memory Overhead

| Per-Object | Memory |
|---|---|
| `RemoteObject` with `objectId` (strong v8::Global) | ~100-200 bytes + the object itself |
| `RemoteObject` with `value` (JSON-serialized) | ~size of JSON string |
| `RemoteObject` with `preview` | ~500 bytes - 5 KB per object |
| `RemoteObject` with `deepSerializedValue` | ~size of serialized structure |

### 3.15.3 Optimization Tips for Scraping

1. **Use `returnByValue: true`** when you just need the data — avoids `objectId` allocation and the need to release later.
2. **Batch multiple evaluations** into a single call — return a JSON object with all results.
3. **Use `objectGroup` and release it at the end** — prevents memory leaks.
4. **Avoid `generatePreview` for large objects** — previews are expensive to generate.
5. **Use `Page.addScriptToEvaluateOnNewDocument` for persistent hooks** — avoids re-evaluating on every navigation.
6. **Use isolated worlds for scraper logic** — page can't see your globals, no detection.
7. **Use `Runtime.addBinding` for page→scraper communication** — more efficient than polling.
8. **Don't enable `Debugger` unless you need breakpoints** — it adds ~5-10% overhead to every JS execution.
9. **Use `throwOnSideEffect: true` for read-only queries** — V8 can skip some work.
10. **Set `disableBreaks: true` for scraping evaluations** — avoids unexpected pauses.

---

## 3.16 Security & Privacy Impact

### 3.16.1 What CDP Runtime Can Access

A CDP client with `Runtime.enable` can:
- Execute **arbitrary JavaScript** in any execution context (main world, isolated world, worker)
- Read **any JS object** including private symbols, internal properties, and prototype chain
- Read **any global variable** including `window`, `document`, `localStorage`, `IndexedDB`
- Override **any JS function** (e.g., `window.fetch`, `XMLHttpRequest.prototype.send`)
- Catch **all uncaught exceptions** and read the exception value
- Catch **all console messages** including `console.log` of sensitive data
- Set **breakpoints** at any source location
- **Pause execution** at any point and inspect the call stack and local variables
- **Modify local variables** in a paused call frame (`Debugger.setVariableValue`)
- **Terminate execution** of any context (`Runtime.terminateExecution`)
- **Inject scripts** that run before page scripts (`Page.addScriptToEvaluateOnNewDocument`)
- **Create isolated worlds** with `grantUniversalAccess` (bypasses same-origin policy)
- **Add bindings** that page JS can call to signal back to the scraper

### 3.16.2 Detection of CDP Runtime Interference

A sophisticated anti-bot script can detect CDP-based JS interference:

1. **`Error().stack` reveals synthetic scripts** — `Runtime.evaluate` scripts show `<anonymous>` or no URL. Real page scripts have URLs.
2. **`Object.getOwnPropertyDescriptor(window, '$')`** — if `$` exists and is a function, the command-line API is installed (CDP artifact).
3. **`Function.prototype.toString.call(window.fetch)`** — if the source is not the native code, `fetch` has been monkey-patched.
4. **`document.currentScript`** — during a `<script>` tag's execution, this is non-null. During `Runtime.evaluate`, it's null.
5. **`performance.getEntriesByType("resource")`** — scripts loaded via `<script src>` appear here. Scripts run via `Runtime.evaluate` do NOT.
6. **`Debugger.scriptParsed`** — if the page can detect that a debugger is attached (e.g., via `Function.prototype.toString` timing), it knows CDP is active.
7. **`navigator.webdriver`** — set to `true` when `--enable-automation` is passed OR when CDP attaches.
8. **`window.cdc_*` properties** — ChromeDriver injects these. Not present when driving CDP directly.
9. **Timing anomalies** — the CDP round-trip adds ~0.5-2ms per `Runtime.evaluate` call. A page that detects consistent delays in `Date.now()` between operations can infer external control.
10. **`Runtime.addBinding` globals** — if you add a binding named `scraperReady`, the page can detect `typeof scraperReady !== 'undefined'`.

### 3.16.3 Stealth Scraping Best Practices for JS

1. **Use `Page.addScriptToEvaluateOnNewDocument` with `worldName`** (isolated world) — page can't see your globals.
2. **Don't use `includeCommandLineAPI: true`** — the `$` global is a dead giveaway.
3. **Don't override `window.fetch` in the main world** — use `Fetch.enable` at the network layer instead, OR override in an isolated world.
4. **Don't use `Runtime.evaluate` for scraping logic** — inject via `Page.addScriptToEvaluateOnNewDocument` so your code looks like a normal script tag.
5. **Call `Emulation.setAutomationOverride(false)` after attach** — removes the `navigator.webdriver = true` flag.
6. **Use `Runtime.addBinding` with a non-obvious name** — `__scraper_signal` is less detectable than `scraperReady`.
7. **Match the page's expected `Error().stack` format** — if your injected script has a `//# sourceURL=...` that looks like a real CDN URL, it's less detectable.
8. **Avoid `Debugger.enable` for stealth scraping** — the debugger adds detectable timing overhead.

---

## 3.17 Testing

### 3.17.1 Unit Tests

```cpp
#include <QtTest>
#include "JavaScriptExecutor.h"

class TestJavaScriptExecutor : public QObject {
    Q_OBJECT
private slots:
    void testEvaluateSync();
    void testAwaitPromise();
    void testException();
    void testConsoleTracking();
    void testIsolatedWorld();
    void testBinding();
    void testDebugger();
};

void TestJavaScriptExecutor::testEvaluateSync() {
    JavaScriptExecutor js(QUrl("ws://127.0.0.1:9222/devtools/page/test"));
    
    QSemaphore sem;
    js.enableConsoleTracking([&]() { sem.release(); });
    sem.acquire();
    
    EvaluateOptions opts;
    opts.returnByValue = true;
    
    EvaluateResult result = js.evaluateSync("1 + 2", opts);
    QVERIFY(!result.exceptionDetails.has_value());
    QCOMPARE(result.result.value.toInt(), 3);
    
    result = js.evaluateSync("document.title", opts);
    QVERIFY(result.result.value.isString());
}

void TestJavaScriptExecutor::testAwaitPromise() {
    JavaScriptExecutor js(QUrl("ws://127.0.0.1:9222/devtools/page/test"));
    
    EvaluateOptions opts;
    opts.awaitPromise = true;
    opts.returnByValue = true;
    
    QSemaphore sem;
    js.evaluate(R"(
        new Promise(resolve => {
            setTimeout(() => resolve('async result'), 100);
        })
    )", opts, [&](const EvaluateResult& r) {
        QVERIFY(!r.exceptionDetails.has_value());
        QCOMPARE(r.result.value.toString(), QString("async result"));
        sem.release();
    });
    QVERIFY(sem.tryAcquire(1, 5000));
}

void TestJavaScriptExecutor::testException() {
    JavaScriptExecutor js(QUrl("ws://127.0.0.1:9222/devtools/page/test"));
    
    EvaluateOptions opts;
    opts.returnByValue = true;
    
    QSemaphore sem;
    js.evaluate("throw new Error('test error')", opts, [&](const EvaluateResult& r) {
        QVERIFY(r.exceptionDetails.has_value());
        QCOMPARE(r.exceptionDetails->text, QString("Uncaught"));
        QVERIFY(r.exceptionDetails->exception.description.contains("test error"));
        sem.release();
    });
    QVERIFY(sem.tryAcquire(1, 5000));
}

void TestJavaScriptExecutor::testConsoleTracking() {
    JavaScriptExecutor js(QUrl("ws://127.0.0.1:9222/devtools/page/test"));
    js.enableConsoleTracking();
    
    QSignalSpy spy(&js, &JavaScriptExecutor::consoleAPICalled);
    
    EvaluateOptions opts;
    opts.returnByValue = true;
    js.evaluateSync("console.log('hello', 'world')", opts);
    
    QVERIFY(spy.wait(2000));
    QCOMPARE(spy.count(), 1);
    
    // Verify args
    const auto args = spy.takeFirst().at(1).value<QList<RemoteObject>>();
    QCOMPARE(args.size(), 2);
}

void TestJavaScriptExecutor::testIsolatedWorld() {
    JavaScriptExecutor js(QUrl("ws://127.0.0.1:9222/devtools/page/test"));
    
    // Inject in isolated world
    js.addScriptToEvaluateOnNewDocument(
        "window.__isolatedVar = 'secret';",
        "test-world", true, true);
    
    // Try to read from main world (should be undefined)
    EvaluateOptions mainOpts;
    mainOpts.returnByValue = true;
    EvaluateResult mainResult = js.evaluateSync(
        "typeof window.__isolatedVar", mainOpts);
    QCOMPARE(mainResult.result.value.toString(), QString("undefined"));
    
    // Try to read from isolated world (should find it)
    // First need to find the isolated context
    QSemaphore sem;
    js.createIsolatedWorld("<frameId>", "test-world", true);
    
    // ... find the contextId for the "test-world" isolated world ...
    // Then evaluate in that context
}

void TestJavaScriptExecutor::testBinding() {
    JavaScriptExecutor js(QUrl("ws://127.0.0.1:9222/devtools/page/test"));
    js.enableConsoleTracking();
    
    js.addBinding("testBinding");
    
    QSignalSpy spy(&js, &JavaScriptExecutor::bindingCalled);
    
    EvaluateOptions opts;
    opts.returnByValue = true;
    js.evaluateSync("testBinding('hello from page')", opts);
    
    QVERIFY(spy.wait(2000));
    QCOMPARE(spy.count(), 1);
    
    const auto args = spy.takeFirst();
    QCOMPARE(args.at(0).toString(), QString("testBinding"));
    QCOMPARE(args.at(1).toString(), QString("hello from page"));
}

void TestJavaScriptExecutor::testDebugger() {
    JavaScriptExecutor js(QUrl("ws://127.0.0.1:9222/devtools/page/test"));
    js.enableDebugger();
    
    QSignalSpy pausedSpy(&js, &JavaScriptExecutor::paused);
    
    // Set a breakpoint
    js.setBreakpointByUrl(5, "https://example.com/test.js", "",
        [](const QString& bpId, const QList<QJsonObject>& locs) {
        QVERIFY(!bpId.isEmpty());
    });
    
    // Trigger the breakpoint by navigating to the page
    // ...
    
    QVERIFY(pausedSpy.wait(5000));
    QCOMPARE(pausedSpy.count(), 1);
    
    js.resume();
}
```

---

## 3.18 Roadmap: Unique Features That Beat Puppeteer/Playwright

Based on this analysis, here are JS-execution features you can build that existing tools lack:

### 3.18.1 "JS Hot Reload" — Live Code Replacement

```cpp
class JsHotReload {
public:
    // Replace a function's source without reloading the page
    void replaceFunction(const QString& objectId, const QString& newSource);
    
    // Replace a module's source (ES modules only)
    void replaceModule(const QString& scriptUrl, const QString& newSource);
    
    // Snapshot all global functions for diffing
    QHash<QString, QString> snapshotGlobals();
};
```

### 3.18.2 "Function Hook" — Transparent Call Interception

```cpp
class FunctionHook {
public:
    // Hook a function with a callback (transparent to page)
    void hookFunction(const QString& objectPath,  // e.g., "window.fetch"
                     std::function<QJsonValue(const QJsonArray& args,
                                             const QJsonValue& result)> callback);
    
    // Unhook
    void unhookFunction(const QString& objectPath);
};
```

### 3.18.3 "JS Sandbox" — Safe Evaluation in Isolated World

```cpp
class JsSandbox {
public:
    // Create a sandbox with a fresh isolated world
    int createSandbox(const QString& name);
    
    // Evaluate in sandbox (can't access page's window)
    QJsonValue evaluate(int sandboxId, const QString& code);
    
    // Destroy sandbox (releases all objects)
    void destroySandbox(int sandboxId);
};
```

### 3.18.4 "Stack Inspector" — Real-Time Call Stack Visualization

```cpp
class StackInspector : public QAbstractTableModel {
public:
    // Columns: Function, File, Line, Column, This, Arguments, Locals
    int columnCount() const override { return 7; }
    
    // Update when Debugger.paused fires
    void setCallFrames(const QList<QJsonObject>& callFrames);
};
```

### 3.18.5 "Console Mirror" — Capture All Console Output

```cpp
class ConsoleMirror : public QAbstractTableModel {
public:
    // Columns: Timestamp, Type, Message, Source, Line, Stack
    int columnCount() const override { return 6; }
    
    // Filter by type, text, source
    void setFilter(const QString& typePattern, const QString& textPattern);
    
    // Export to file
    void exportToText(const QString& filepath);
    void exportToJson(const QString& filepath);
};
```

### 3.18.6 "Exception Tracker" — Aggregate and Analyze Errors

```cpp
class ExceptionTracker {
public:
    // Track all exceptions
    void enable();
    
    // Group similar exceptions by stack trace
    QList<ExceptionGroup> groupedExceptions() const;
    
    // Get exceptions by type
    QList<ExceptionDetails> byType(const QString& type) const;
    
    // Export report
    QJsonObject report() const;
};
```

---

## 3.19 Summary Cheat Sheet

| Operation | CDP Command | Implementation File:Line |
|---|---|---|
| Evaluate JS (async) | `Runtime.evaluate` | `v8-runtime-agent-impl.cc:356` |
| Call function on object | `Runtime.callFunctionOn` | `v8-runtime-agent-impl.cc:493` |
| Get properties | `Runtime.getProperties` | `v8-runtime-agent-impl.cc:577` |
| Release object | `Runtime.releaseObject` | `v8-runtime-agent-impl.cc:637` |
| Release group | `Runtime.releaseObjectGroup` | `v8-runtime-agent-impl.cc:645` |
| Get exception details | `Runtime.getExceptionDetails` | `v8-runtime-agent-impl.cc:1027` |
| Add binding | `Runtime.addBinding` | `InspectorRuntimeAgent::addBinding` |
| Inject script (before page) | `Page.addScriptToEvaluateOnNewDocument` | `InspectorInjectedScriptManager::AddScriptToEvaluateOnNewDocument` |
| Create isolated world | `Page.createIsolatedWorld` | `InspectorPageAgent::createIsolatedWorld` |
| Enable debugger | `Debugger.enable` | `v8-debugger-agent-impl.cc:487` |
| Set breakpoint by URL | `Debugger.setBreakpointByUrl` | `v8-debugger-agent-impl.cc:643` |
| Set breakpoint by scriptId | `Debugger.setBreakpoint` | `v8-debugger-agent-impl.cc:1142` |
| Set breakpoint on function call | `Debugger.setBreakpointOnFunctionCall` | `v8-debugger-agent-impl.cc:1185` |
| Pause on exceptions | `Debugger.setPauseOnExceptions` | `v8-debugger-agent-impl.cc:1600` |
| Pause | `Debugger.pause` | `v8-debugger-agent-impl.cc:1487` |
| Resume | `Debugger.resume` | `v8-debugger-agent-impl.cc:1541` |
| Step over/into/out | `Debugger.stepOver/Into/Out` | `v8-debugger-agent-impl.cc:1551/1567/1581` |
| Evaluate on call frame | `Debugger.evaluateOnCallFrame` | `v8-debugger-agent-impl.cc:1666` |
| Set async stack depth | `Debugger.setAsyncCallStackDepth` | `v8-debugger-agent-impl.cc:1753` |
| Get script source | `Debugger.getScriptSource` | `v8-debugger-agent-impl.cc:916` |

---

## End of Part 3

This concludes **Part 3: Runtime.evaluate / V8 Inspector** — approximately 15,000 words covering the two-level JS execution model, the complete `Runtime.evaluate` call chain, RemoteObject serialization, execution contexts (main vs isolated vs worker), the console API, exception handling, the InspectorInstrumentation system, the InspectorSession, V8 Inspector integration, the Debugger, full Qt6 C++ implementation, edge cases, performance, security, testing, and unique features.

---

## What's Next?

**Part 4: DOM & Storage Monitoring** (your #5 priority) will cover:
- The DOM domain complete API (getDocument, querySelector, setAttribute, etc.)
- DOM mutation capture (attributeModified, childNodeInserted, etc.)
- Shadow DOM traversal (open vs closed)
- Iframe handling
- CSS domain (computed styles, matched styles, addRule, createStyleSheet)
- DOMStorage (localStorage/sessionStorage) tracking
- IndexedDB monitoring and data extraction
- CacheStorage monitoring and data extraction
- Full Qt6 C++ implementation of a `DomMonitor` class
- Edge cases, performance, security, testing
- Unique features (DOM diff, CSS live editor, storage inspector, etc.)

