#pragma once

#include <QObject>
#include <QHash>
#include <QJsonObject>
#include <QJsonDocument>
#include <QString>
#include <QVariant>
#include <QDir>
#include <QFile>
#include <QMutex>
#include "nothing/storage/StorageModels.h"

namespace nothing {
namespace storage {

/// IStorageBackend — Abstract interface for storage backends.
///
/// Two implementations:
/// - PersistentBackend: writes JSON files to disk
/// - VolatileBackend: keeps everything in a QHash in memory
class IStorageBackend {
public:
    virtual ~IStorageBackend() = default;

    /// Store a value under (contextId, namespace_, key).
    virtual bool set(const QString& contextId,
                     const QString& namespace_,
                     const QString& key,
                     const QJsonValue& value) = 0;

    /// Retrieve a value. Returns empty QJsonValue if not found.
    virtual QJsonValue get(const QString& contextId,
                           const QString& namespace_,
                           const QString& key) const = 0;

    /// Check if a key exists.
    virtual bool has(const QString& contextId,
                     const QString& namespace_,
                     const QString& key) const = 0;

    /// Remove a key. Returns true if the key existed.
    virtual bool remove(const QString& contextId,
                        const QString& namespace_,
                        const QString& key) = 0;

    /// List all keys matching the filter.
    virtual QList<StorageEntry> list(const QString& contextId,
                                     const QString& namespace_,
                                     const QString& keyPrefix = QString()) const = 0;

    /// Clear all entries for a contextId.
    /// If namespace_ is non-empty, only clear that namespace.
    virtual int clearContext(const QString& contextId,
                             const QString& namespace_ = QString()) = 0;

    /// Clear all entries for a namespace across all contexts.
    virtual int clearNamespace(const QString& namespace_) = 0;

    /// Get approximate byte count for a context.
    virtual qint64 sizeBytes(const QString& contextId) const = 0;

    /// Get entry count for a context.
    virtual int count(const QString& contextId) const = 0;

    /// Flush to disk (no-op for VolatileBackend).
    virtual void flush() = 0;

    /// Get the storage mode.
    virtual StorageMode mode() const = 0;
};

// ============================================================================
// PersistentBackend — JSON files on disk
// ============================================================================

/// PersistentBackend — Stores data as JSON files on disk.
///
/// Layout:
///   <dataDir>/nothing-storage/
///     default/
///       tabs.json          ← namespace "tabs"
///       settings.json      ← namespace "settings"
///       downloads.json
///     <contextId>/
///       tabs.json
///       ...
///
/// Each JSON file is a flat { "key": value, ... } object.
/// Writes are lazy — data is kept in memory and flushed to disk
/// on flush() or on destruction.
class PersistentBackend : public IStorageBackend {
public:
    explicit PersistentBackend(const QString& dataDir);
    ~PersistentBackend() override;

    bool set(const QString& contextId,
             const QString& namespace_,
             const QString& key,
             const QJsonValue& value) override;

    QJsonValue get(const QString& contextId,
                   const QString& namespace_,
                   const QString& key) const override;

    bool has(const QString& contextId,
             const QString& namespace_,
             const QString& key) const override;

    bool remove(const QString& contextId,
                const QString& namespace_,
                const QString& key) override;

    QList<StorageEntry> list(const QString& contextId,
                             const QString& namespace_,
                             const QString& keyPrefix = QString()) const override;

    int clearContext(const QString& contextId,
                     const QString& namespace_ = QString()) override;

    int clearNamespace(const QString& namespace_) override;

    qint64 sizeBytes(const QString& contextId) const override;
    int count(const QString& contextId) const override;
    void flush() override;
    StorageMode mode() const override { return StorageMode::Persistent; }

private:
    // Internal: (contextId, namespace_) → QJsonObject of key→value
    using ContextMap = QHash<QString, QJsonObject>;
    QHash<QString, ContextMap> m_data;

    // Track which (contextId, namespace_) pairs are dirty (need flush)
    QSet<QString> m_dirty;  // format: "contextId/namespace_"

    QString m_dataDir;
    mutable QRecursiveMutex m_mutex;

    // Helpers
    QString contextDir(const QString& contextId) const;
    QString filePath(const QString& contextId, const QString& namespace_) const;
    void loadFromDisk(const QString& contextId, const QString& namespace_) const;
    void saveToDisk(const QString& contextId, const QString& namespace_);
    qint64 estimateSize(const QJsonValue& value) const;
    QString dirtyKey(const QString& contextId, const QString& namespace_) const;
};

// ============================================================================
// VolatileBackend — In-memory only, cleared on session end
// ============================================================================

/// VolatileBackend — Stores data in a QHash in memory.
///
/// All data is lost when the process exits or when clearContext() is called.
/// Used for incognito / private browsing where zero persistence is required.
///
/// The data is also never written to swap — the QHash lives entirely in
/// the process's heap. (Note: the OS may still swap the process's memory
/// to disk under memory pressure; for true anti-forensic guarantees you'd
/// need mlock() or equivalent, which is out of scope for this module.)
class VolatileBackend : public IStorageBackend {
public:
    VolatileBackend() = default;
    ~VolatileBackend() override = default;

    bool set(const QString& contextId,
             const QString& namespace_,
             const QString& key,
             const QJsonValue& value) override;

    QJsonValue get(const QString& contextId,
                   const QString& namespace_,
                   const QString& key) const override;

    bool has(const QString& contextId,
             const QString& namespace_,
             const QString& key) const override;

    bool remove(const QString& contextId,
                const QString& namespace_,
                const QString& key) override;

    QList<StorageEntry> list(const QString& contextId,
                             const QString& namespace_,
                             const QString& keyPrefix = QString()) const override;

    int clearContext(const QString& contextId,
                     const QString& namespace_ = QString()) override;

    int clearNamespace(const QString& namespace_) override;

    qint64 sizeBytes(const QString& contextId) const override;
    int count(const QString& contextId) const override;
    void flush() override {}  // no-op — everything is in memory
    StorageMode mode() const override { return StorageMode::Volatile; }

    /// Securely overwrite and clear ALL volatile data.
    /// Called when the last incognito session ends.
    void wipeAll();

private:
    // (contextId, namespace_) → QJsonObject of key→value
    using ContextMap = QHash<QString, QJsonObject>;
    QHash<QString, ContextMap> m_data;
    mutable QRecursiveMutex m_mutex;

    qint64 estimateSize(const QJsonValue& value) const;
};

} // namespace storage
} // namespace nothing
