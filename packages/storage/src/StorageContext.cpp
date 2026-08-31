#include "nothing/storage/StorageContext.h"

namespace nothing {
namespace storage {

StorageContext::StorageContext(const QString& contextId,
                               PersistentBackend* persistent,
                               VolatileBackend* volatile_)
    : m_contextId(contextId)
    , m_persistent(persistent)
    , m_volatile(volatile_)
    , m_defaultMode(contextId == "default" ? StorageMode::Persistent
                                            : StorageMode::Volatile)
{
}

IStorageBackend* StorageContext::backend(StorageMode mode) const
{
    if (mode == StorageMode::Volatile) return m_volatile;
    return m_persistent;
}

bool StorageContext::set(const QString& namespace_,
                         const QString& key,
                         const QJsonValue& value,
                         StorageMode mode)
{
    return backend(mode)->set(m_contextId, namespace_, key, value);
}

bool StorageContext::set(const QString& namespace_,
                         const QString& key,
                         const QJsonValue& value,
                         const QString& modeStr)
{
    return set(namespace_, key, value, stringToMode(modeStr));
}

QJsonValue StorageContext::get(const QString& namespace_,
                               const QString& key,
                               StorageMode mode) const
{
    // Try the specified mode first
    QJsonValue val = backend(mode)->get(m_contextId, namespace_, key);
    if (!val.isUndefined() && !val.isNull()) {
        return val;
    }

    // Fall back to the other mode
    StorageMode otherMode = (mode == StorageMode::Persistent)
                            ? StorageMode::Volatile
                            : StorageMode::Persistent;
    return backend(otherMode)->get(m_contextId, namespace_, key);
}

QJsonValue StorageContext::get(const QString& namespace_,
                               const QString& key,
                               const QString& modeStr) const
{
    return get(namespace_, key, stringToMode(modeStr));
}

bool StorageContext::has(const QString& namespace_,
                         const QString& key,
                         StorageMode mode) const
{
    if (backend(mode)->has(m_contextId, namespace_, key)) return true;
    // Fall back to the other mode
    StorageMode otherMode = (mode == StorageMode::Persistent)
                            ? StorageMode::Volatile
                            : StorageMode::Persistent;
    return backend(otherMode)->has(m_contextId, namespace_, key);
}

bool StorageContext::remove(const QString& namespace_,
                            const QString& key,
                            StorageMode mode)
{
    bool removed = backend(mode)->remove(m_contextId, namespace_, key);
    // Also try the other mode
    StorageMode otherMode = (mode == StorageMode::Persistent)
                            ? StorageMode::Volatile
                            : StorageMode::Persistent;
    removed = backend(otherMode)->remove(m_contextId, namespace_, key) || removed;
    return removed;
}

QList<StorageEntry> StorageContext::list(const QString& namespace_,
                                         const QString& keyPrefix,
                                         StorageMode mode) const
{
    QList<StorageEntry> result;

    // Get from the specified mode
    result.append(backend(mode)->list(m_contextId, namespace_, keyPrefix));

    // Also get from the other mode (and deduplicate by key)
    StorageMode otherMode = (mode == StorageMode::Persistent)
                            ? StorageMode::Volatile
                            : StorageMode::Persistent;
    QList<StorageEntry> other = backend(otherMode)->list(m_contextId, namespace_, keyPrefix);

    QSet<QString> seenKeys;
    for (const StorageEntry& e : result) {
        seenKeys.insert(e.namespace_ + "/" + e.key);
    }
    for (const StorageEntry& e : other) {
        QString compositeKey = e.namespace_ + "/" + e.key;
        if (!seenKeys.contains(compositeKey)) {
            result.append(e);
            seenKeys.insert(compositeKey);
        }
    }

    return result;
}

int StorageContext::clear(const QString& namespace_, StorageMode mode)
{
    return backend(mode)->clearContext(m_contextId, namespace_);
}

int StorageContext::clearAll()
{
    int cleared = 0;
    cleared += m_persistent->clearContext(m_contextId);
    cleared += m_volatile->clearContext(m_contextId);
    return cleared;
}

StorageStats StorageContext::stats() const
{
    StorageStats s;
    s.persistentEntries = m_persistent->count(m_contextId);
    s.persistentBytes = m_persistent->sizeBytes(m_contextId);
    s.volatileEntries = m_volatile->count(m_contextId);
    s.volatileBytes = m_volatile->sizeBytes(m_contextId);
    s.entryCount = s.persistentEntries + s.volatileEntries;
    s.totalBytes = s.persistentBytes + s.volatileBytes;
    return s;
}

} // namespace storage
} // namespace nothing
