#pragma once

#include <QObject>
#include <QHash>
#include <QJsonObject>
#include <QJsonDocument>
#include <QString>
#include <QVariant>
#include <QList>
#include <QPair>
#include <QDir>
#include <QFile>
#include <QDateTime>
#include <QMutex>
#include <QRecursiveMutex>
#include <memory>
#include <functional>

namespace nothing {
namespace storage {

/// StorageMode — determines whether data persists to disk or lives in RAM only.
///
/// - Persistent: Data is written to JSON files on disk. Survives browser restart.
///   Used by Sabre Browser (daily driver) for normal browsing.
///
/// - Volatile: Data lives in memory only. Cleared when the context (session) ends.
///   Used by Nothing Browser (scraper) and Nothing Private Browser for
///   incognito / private sessions where zero persistence is required.
///
/// The JS API selects the mode per-call:
///   storage.set("key", value, { storage: "volatile" })
///   storage.set("key", value, { storage: "persistent" })  // default
///
/// Or per-context: incognito contexts default to volatile, normal contexts
/// default to persistent.
enum class StorageMode {
    Persistent,   // JSON file on disk, survives restart
    Volatile,     // In-memory QHash, cleared on session end
};

/// Convert mode to string for JSON serialization.
inline QString modeToString(StorageMode mode) {
    return mode == StorageMode::Persistent ? "persistent" : "volatile";
}

/// Convert string to mode. Defaults to Persistent for unknown values.
inline StorageMode stringToMode(const QString& str) {
    return str.toLower() == "volatile" ? StorageMode::Volatile : StorageMode::Persistent;
}

/// A single storage entry with metadata.
struct StorageEntry {
    QString key;
    QJsonValue value;
    StorageMode mode;
    QString contextId;        // "default" for normal, UUID for incognito
    QString namespace_;       // logical grouping: "tabs", "downloads", "settings", etc.
    QDateTime createdAt;
    QDateTime updatedAt;
    qint64 sizeBytes = 0;     // approximate size of the value
};

/// Query filter for listing entries.
struct StorageFilter {
    QString contextId;        // empty = all contexts
    QString namespace_;       // empty = all namespaces
    StorageMode mode;         // both modes if not set
    bool hasMode = false;
    QString keyPrefix;        // empty = all keys
};

/// Statistics for a storage context.
struct StorageStats {
    int entryCount = 0;
    qint64 totalBytes = 0;
    int persistentEntries = 0;
    int volatileEntries = 0;
    qint64 persistentBytes = 0;
    qint64 volatileBytes = 0;
};

} // namespace storage
} // namespace nothing

Q_DECLARE_METATYPE(nothing::storage::StorageEntry)
Q_DECLARE_METATYPE(nothing::storage::StorageStats)
