# packages/storage

Dual-mode storage module for the Nothing Browser ecosystem. Provides persistent (disk) and volatile (in-memory) storage backends, selected per-call or per-context.

## Architecture

```
┌──────────────────────────────────────────────────────┐
│  Browser App                                          │
│                                                       │
│  piggy JS API                                        │
│  storage.set("tabs", "tab-1", data, {storage:"volatile"})
│       │                                               │
│       ▼ (piggy IPC)                                  │
│  ┌──────────────────────────────────────────────┐   │
│  │  StorageManager                               │   │
│  │  (per-context routing)                        │   │
│  │                                                │   │
│  │  ┌─────────────┐    ┌─────────────────┐     │   │
│  │  │  "default"  │    │  <incognito-uuid>│     │   │
│  │  │  context    │    │  context          │     │   │
│  │  │  (persist)  │    │  (volatile)      │     │   │
│  │  └──────┬──────┘    └────────┬──────────┘     │   │
│  │         │                    │                  │   │
│  │         ▼                    ▼                  │   │
│  │  ┌──────────────┐    ┌──────────────┐         │   │
│  │  │ Persistent   │    │ Volatile     │         │   │
│  │  │ Backend      │    │ Backend      │         │   │
│  │  │ (JSON files) │    │ (QHash RAM)  │         │   │
│  │  └──────┬───────┘    └──────────────┘         │   │
│  │         │                                     │   │
│  └─────────┼─────────────────────────────────────┘   │
│            │                                          │
│            ▼                                          │
│  <dataDir>/nothing-storage/                          │
│    default/                                           │
│      tabs.json                                        │
│      settings.json                                    │
│      downloads.json                                   │
│    <context-uuid>/                                    │
│      tabs.json                                        │
└──────────────────────────────────────────────────────┘
```

### Two Storage Modes

| Mode | Backend | Survives Restart | Written to Disk | Use Case |
|---|---|---|---|---|
| `persistent` | `PersistentBackend` | ✅ Yes | JSON files | Sabre Browser (daily driver) — tabs, settings, history, downloads |
| `volatile` | `VolatileBackend` | ❌ No | Never (RAM only) | Nothing Browser (scraper) + Nothing Private Browser — incognito session data, temp state |

### Context Routing

Each browsing context gets a `StorageContext`:

| Context | Default Mode | When Created | When Destroyed |
|---|---|---|---|
| `"default"` | Persistent | On `StorageManager` construction | Never (persists across browser restarts) |
| Incognito UUID | Volatile | On `IncognitoSession::Start()` | On `IncognitoSession::End()` — wipes all volatile data |

The JS API overrides the mode per-call:
```typescript
// Default: uses context's default mode (persistent for normal, volatile for incognito)
await storage.set("tabs", "tab-1", data);

// Explicit volatile: forces in-memory (even in normal browsing)
await storage.set("cache", "temp-data", value, { storage: "volatile" });

// Explicit persistent: forces disk (even in incognito)
await storage.set("bookmarks", "fav-1", url, { storage: "persistent" });
```

## Files

| File | Responsibility |
|---|---|
| `include/nothing/storage/StorageModels.h` | Enums (`StorageMode`), structs (`StorageEntry`, `StorageFilter`, `StorageStats`) |
| `include/nothing/storage/IStorageBackend.h` | Abstract `IStorageBackend` interface + `PersistentBackend` (JSON on disk) + `VolatileBackend` (in-memory QHash) |
| `include/nothing/storage/StorageContext.h` | Per-context routing — decides which backend to use based on mode |
| `include/nothing/storage/StorageManager.h` | Top-level entry point — manages backends and contexts |
| `src/*.cpp` | Implementations |
| `js/storage.js` | JS/TS API wrapper for piggy |
| `CMakeLists.txt` | Build config (Qt6, C++17) |

## Usage

### C++ — In the browser

```cpp
#include <nothing/storage/StorageManager.h>

// Create the storage manager (do this once at startup)
QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
auto* storage = new nothing::storage::StorageManager(dataDir, this);

// Normal browsing (Sabre Browser) — defaults to persistent
storage->set("default", "settings", "homepage", "https://example.com");
storage->set("default", "settings", "theme", "dark");
storage->set("default", "tabs", "tab-1", QJsonObject{
    {"url", "https://example.com"},
    {"title", "Example"},
    {"active", true}
});

// Read back
QJsonValue homepage = storage->get("default", "settings", "homepage");
// → "https://example.com"

// Explicit volatile (e.g. temp cache in normal browsing)
storage->set("default", "cache", "temp-image", imageData, "volatile");

// Incognito session
QString ctxId = storage->createContext("", nothing::storage::StorageMode::Volatile);
// ctxId is now a UUID like "a1b2c3d4-..."
storage->set(ctxId, "tabs", "tab-1", tabData);  // defaults to volatile (RAM)
storage->set(ctxId, "tabs", "tab-2", tabData2);

// ... incognito session ends ...
storage->destroyContext(ctxId);  // wipes ALL volatile data for this context
```

### JS/TS — In piggy

```typescript
import { storage } from "nothing-browser";

// Normal browsing (defaults to persistent)
await storage.set("settings", "homepage", "https://example.com");
const homepage = await storage.get("settings", "homepage");

// Explicit volatile (in-memory, cleared on session end)
await storage.set("cache", "temp-data", { foo: 1 }, { storage: "volatile" });
const temp = await storage.getVolatile("cache", "temp-data");

// Incognito — piggy sets the context automatically
// When piggy.goto(url, { useIncognito: true }) is called,
// the context is switched to the incognito UUID (volatile by default)
await storage.set("tabs", "tab-1", { url, title });
// ↑ stored in volatile because incognito context defaults to volatile

// Persistent override (even in incognito — e.g. bookmarks should persist)
await storage.set("bookmarks", "fav-1", url, { storage: "persistent" });

// List entries
const allTabs = await storage.list("tabs");
const allVolatile = await storage.list("", "", { storage: "volatile" });

// Clear
await storage.clear("cache", { storage: "volatile" });  // clear volatile cache
await storage.clear();  // clear ALL for current context

// Stats
const stats = await storage.stats();
console.log(`Persistent: ${stats.persistentEntries} entries, ${stats.persistentBytes} bytes`);
console.log(`Volatile: ${stats.volatileEntries} entries, ${stats.volatileBytes} bytes`);

// Context management (piggy calls these internally for incognito)
const ctxId = await storage.createContext("", "volatile");
storage.setContext(ctxId);  // switch active context
// ... incognito session ...
const cleared = await storage.destroyContext(ctxId);  // wipes volatile
```

### Integration with packages/profile/incognito

The storage module integrates with the existing `contextId` pattern:

```cpp
// In IncognitoSession::Start():
QString contextId = generateUuid();
storage->createContext(contextId, StorageMode::Volatile);
identityManager->createIdentity(contextId);
return contextId;

// In IncognitoSession::End():
storage->destroyContext(contextId);  // wipes all volatile data
identityManager->destroyIdentity(contextId);
```

### Integration with packages/devtools

The DevTools module can use storage for:
- Saving HAR exports: `storage.set("default", "har-exports", timestamp, harJson, "persistent")`
- Saving DevTools settings: `storage.set("default", "devtools-settings", "theme", "dark")`
- Caching network capture: `storage.set(ctxId, "network-cache", requestId, data, "volatile")`

## On-Disk Layout

```
<dataDir>/nothing-storage/
  default/                          ← "default" context (normal browsing)
    tabs.json                        ← namespace "tabs"
    settings.json                    ← namespace "settings"
    downloads.json                   ← namespace "downloads"
    history.json                     ← namespace "history"
    devtools-settings.json           ← namespace "devtools-settings"
  <incognito-uuid>/                  ← incognito context (if persistent override used)
    bookmarks.json                   ← namespace "bookmarks" (explicitly persisted)
```

Each `.json` file is a flat JSON object:
```json
{
  "homepage": "https://example.com",
  "theme": "dark",
  "lastVisited": "https://news.example.com"
}
```

## Security & Privacy

### Volatile Storage (Incognito/Private)

- **No disk writes**: The `VolatileBackend` never writes to disk. All data lives in `QHash<QString, QJsonObject>` in the process heap.
- **Secure wipe**: On `destroyContext()`, each `QJsonObject` is swapped with an empty object before erasure (best-effort secure clear). This overwrites the internal buffer before freeing memory.
- **No swap guarantee**: The OS may still swap the process's memory to disk under memory pressure. For true anti-forensic guarantees, use `mlock()` on the process or run with swap disabled. This is documented as a known limitation.
- **Process crash**: If the browser crashes, all volatile data is lost immediately (the QHash is in the process heap).

### Persistent Storage (Normal Browsing)

- **Plain JSON**: Data is stored as unencrypted JSON on disk. If you need encryption at rest, wrap `PersistentBackend` or add an encryption layer in `StorageContext`.
- **No sensitive data**: Don't store passwords, credit cards, or auth tokens in `StorageManager` — use the browser's existing credential manager for that.

## IPC Handler Stubs (for piggycpp)

The JS API calls the following IPC commands. These need to be registered when piggycpp's command-registration API is available:

| IPC Command | Parameters | Returns | Notes |
|---|---|---|---|
| `storage.set` | `{namespace, key, value, storage, contextId}` | `bool` | `storage` = "persistent" \| "volatile" |
| `storage.get` | `{namespace, key, storage, contextId}` | `any` (JSON value) | `storage` = "" = try both |
| `storage.has` | `{namespace, key, storage, contextId}` | `bool` | |
| `storage.remove` | `{namespace, key, storage, contextId}` | `bool` | |
| `storage.list` | `{namespace, keyPrefix, storage, contextId}` | `StorageEntry[]` | |
| `storage.clear` | `{namespace, storage, contextId}` | `int` (count removed) | |
| `storage.stats` | `{contextId}` | `StorageStats` | |
| `storage.flush` | `{}` | `void` | Flush persistent to disk |
| `storage.createContext` | `{contextId, defaultMode}` | `string` (contextId) | |
| `storage.destroyContext` | `{contextId}` | `int` (count removed) | Wipes volatile |
| `storage.contexts` | `{}` | `string[]` | List all context IDs |

## Dependencies

- **Qt6 Core** — QObject, QJsonDocument, QFile, QDir, QMutex
- **C++17** — std::optional, std::unique_ptr

No other Qt modules required. No SQLite, no network, no WebEngine dependency.

## Future Improvements

- [ ] Encryption at rest for `PersistentBackend` (AES-256 via Qt's QCryptographicHash or libsodium)
- [ ] `mlock()` support for `VolatileBackend` to prevent swapping
- [ ] Size limits per namespace (e.g. max 10MB for "cache", max 100MB total)
- [ ] TTL (time-to-live) for entries — auto-expire after N seconds
- [ ] Compression for large persistent entries (gzip via qCompress)
- [ ] SQLite backend option for high-volume use cases (better than JSON for >10K entries)
- [ ] Change notifications via piggy events (emit when storage.set is called)
- [ ] Import/export entire context as ZIP (for backup/migration)
