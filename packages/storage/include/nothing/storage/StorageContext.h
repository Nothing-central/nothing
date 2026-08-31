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

namespace nothing {
namespace storage {

/// StorageContext — One per browsing context (normal or incognito).
///
/// Each contextId ("default" for normal, UUID for incognito) gets a
/// StorageContext that routes operations to the appropriate backend:
/// - Normal contexts → PersistentBackend (JSON files on disk)
/// - Incognito contexts → VolatileBackend (in-memory, wiped on session end)
///
/// The JS API selects the mode per-call:
///   storage.set("key", value, { storage: "volatile" })
///   storage.set("key", value)  // defaults to context's default mode
///
/// Incognito contexts default to volatile. Normal contexts default to persistent.
class StorageContext {
public:
    /// Constructor.
    /// @param contextId   "default" for normal browsing, UUID for incognito
    /// @param persistent  Pointer to the shared PersistentBackend
    /// @param volatile_   Pointer to the shared VolatileBackend
    StorageContext(const QString& contextId,
                   PersistentBackend* persistent,
                   VolatileBackend* volatile_);

    // === Core operations ===

    /// Store a value.
    /// @param namespace_  Logical group: "tabs", "settings", "downloads", etc.
    /// @param key          The key within the namespace
    /// @param value        The JSON value to store
    /// @param mode         Storage mode (defaults to context's default)
    /// @return true on success
    bool set(const QString& namespace_,
             const QString& key,
             const QJsonValue& value,
             StorageMode mode = StorageMode::Persistent);

    /// Store a value with a string mode.
    /// @param modeStr  "persistent" or "volatile"
    bool set(const QString& namespace_,
             const QString& key,
             const QJsonValue& value,
             const QString& modeStr);

    /// Retrieve a value.
    /// Tries the specified mode first, then falls back to the other.
    /// @param mode  Which backend to read from (defaults to context's default)
    QJsonValue get(const QString& namespace_,
                   const QString& key,
                   StorageMode mode = StorageMode::Persistent) const;

    /// Retrieve a value with a string mode.
    QJsonValue get(const QString& namespace_,
                   const QString& key,
                   const QString& modeStr) const;

    /// Check if a key exists.
    bool has(const QString& namespace_,
             const QString& key,
             StorageMode mode = StorageMode::Persistent) const;

    /// Remove a key.
    /// @return true if the key existed
    bool remove(const QString& namespace_,
                const QString& key,
                StorageMode mode = StorageMode::Persistent);

    /// List all entries in a namespace.
    QList<StorageEntry> list(const QString& namespace_ = QString(),
                             const QString& keyPrefix = QString(),
                             StorageMode mode = StorageMode::Persistent) const;

    /// Clear all entries in a namespace.
    /// If namespace_ is empty, clears ALL namespaces for this context.
    /// @param mode  Which backend to clear
    /// @return Number of entries removed
    int clear(const QString& namespace_ = QString(),
              StorageMode mode = StorageMode::Persistent);

    /// Clear BOTH persistent and volatile storage for this context.
    /// @return Total entries removed
    int clearAll();

    /// Get statistics for this context.
    StorageStats stats() const;

    /// Get the contextId.
    QString contextId() const { return m_contextId; }

    /// Get the default mode for this context.
    /// Incognito contexts return Volatile, normal contexts return Persistent.
    StorageMode defaultMode() const { return m_defaultMode; }

    /// Set the default mode (override).
    void setDefaultMode(StorageMode mode) { m_defaultMode = mode; }

private:
    IStorageBackend* backend(StorageMode mode) const;

    QString m_contextId;
    PersistentBackend* m_persistent;
    VolatileBackend* m_volatile;
    StorageMode m_defaultMode;
};

} // namespace storage
} // namespace nothing
