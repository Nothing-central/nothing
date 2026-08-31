#pragma once

#include <QObject>
#include <QHash>
#include <QSet>
#include <QString>
#include <QJsonObject>
#include <QList>
#include <memory>
#include "nothing/storage/StorageModels.h"
#include "nothing/storage/IStorageBackend.h"
#include "nothing/storage/StorageContext.h"

namespace nothing {
namespace storage {

/// StorageManager — Top-level entry point for the storage module.
///
/// Manages two shared backends (persistent + volatile) and per-context
/// StorageContext instances. This is the class the browser creates and
/// the JS API talks to via piggy IPC.
///
/// Usage:
///   auto* mgr = new StorageManager(dataDir, this);
///   mgr->set("default", "settings", "homepage", "https://example.com");
///   auto val = mgr->get("default", "settings", "homepage");
///
///   // Incognito session:
///   QString ctxId = mgr->createContext();  // generates UUID, defaults to volatile
///   mgr->set(ctxId, "tabs", "tab-1", {...});
///   // ... session ends ...
///   mgr->destroyContext(ctxId);  // wipes all volatile data for this context
///
/// The JS API:
///   storage.set("settings", "homepage", "https://example.com")
///   storage.set("tabs", "tab-1", {...}, { storage: "volatile" })
///   storage.get("settings", "homepage")
///   storage.clear("tabs", { storage: "volatile" })
class StorageManager : public QObject {
    Q_OBJECT
public:
    /// Constructor.
    /// @param dataDir  Directory for persistent storage (e.g. QStandardPaths::writableLocation)
    /// @param parent   Parent QObject
    explicit StorageManager(const QString& dataDir, QObject* parent = nullptr);
    ~StorageManager();

    // === Context management ===

    /// Create a new storage context.
    /// @param contextId  "default" for normal browsing, or a generated UUID for incognito.
    ///                   If empty, generates a UUID.
    /// @param defaultMode  Storage mode for this context (Volatile for incognito, Persistent for normal)
    /// @return The contextId
    QString createContext(const QString& contextId = QString(),
                          StorageMode defaultMode = StorageMode::Volatile);

    /// Destroy a storage context.
    /// Clears all volatile data for the context.
    /// Does NOT clear persistent data (that survives for "default" context).
    /// For non-default contexts, also clears persistent data.
    /// @return Number of entries removed
    int destroyContext(const QString& contextId);

    /// Get a StorageContext for a contextId.
    /// Creates one if it doesn't exist (defaulting to Volatile).
    StorageContext* context(const QString& contextId);

    /// Check if a context exists.
    bool hasContext(const QString& contextId) const;

    /// List all active context IDs.
    QStringList contexts() const;

    // === Convenience operations (operate on the context's default mode) ===

    /// Store a value.
    /// @param contextId  "default" for normal, UUID for incognito
    /// @param namespace_  Logical group ("tabs", "settings", "downloads", etc.)
    /// @param key          The key
    /// @param value        The JSON value
    /// @param modeStr      "persistent" or "volatile" (empty = context default)
    /// @return true on success
    bool set(const QString& contextId,
             const QString& namespace_,
             const QString& key,
             const QJsonValue& value,
             const QString& modeStr = QString());

    /// Retrieve a value.
    /// @param modeStr  "persistent" or "volatile" (empty = tries both)
    QJsonValue get(const QString& contextId,
                   const QString& namespace_,
                   const QString& key,
                   const QString& modeStr = QString()) const;

    /// Check if a key exists.
    bool has(const QString& contextId,
             const QString& namespace_,
             const QString& key,
             const QString& modeStr = QString()) const;

    /// Remove a key.
    bool remove(const QString& contextId,
                const QString& namespace_,
                const QString& key,
                const QString& modeStr = QString());

    /// List entries.
    QList<StorageEntry> list(const QString& contextId,
                             const QString& namespace_ = QString(),
                             const QString& keyPrefix = QString(),
                             const QString& modeStr = QString()) const;

    /// Clear entries for a context.
    /// @param namespace_  Empty = clear ALL namespaces
    /// @param modeStr     "persistent" or "volatile" (empty = both)
    /// @return Number of entries removed
    int clear(const QString& contextId,
              const QString& namespace_ = QString(),
              const QString& modeStr = QString());

    /// Get statistics for a context.
    StorageStats stats(const QString& contextId) const;

    /// Flush persistent storage to disk.
    void flush();

    /// Wipe ALL volatile data (called when last incognito session ends).
    void wipeVolatile();

signals:
    /// Emitted when a value is set.
    void valueSet(const QString& contextId, const QString& namespace_,
                  const QString& key, const QJsonValue& value, StorageMode mode);

    /// Emitted when a value is removed.
    void valueRemoved(const QString& contextId, const QString& namespace_,
                      const QString& key, StorageMode mode);

    /// Emitted when a context is created.
    void contextCreated(const QString& contextId, StorageMode defaultMode);

    /// Emitted when a context is destroyed.
    void contextDestroyed(const QString& contextId, int entriesRemoved);

private:
    std::unique_ptr<PersistentBackend> m_persistent;
    std::unique_ptr<VolatileBackend> m_volatile;
    QHash<QString, std::unique_ptr<StorageContext>> m_contexts;
    QString m_dataDir;
};

} // namespace storage
} // namespace nothing
