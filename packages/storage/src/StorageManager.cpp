#include "nothing/storage/StorageManager.h"
#include <QUuid>
#include <QDebug>

namespace nothing {
namespace storage {

StorageManager::StorageManager(const QString& dataDir, QObject* parent)
    : QObject(parent)
    , m_dataDir(dataDir)
{
    m_persistent = std::make_unique<PersistentBackend>(dataDir);
    m_volatile = std::make_unique<VolatileBackend>();

    // Always create the "default" context (normal browsing, persistent)
    createContext("default", StorageMode::Persistent);
}

StorageManager::~StorageManager()
{
    // Flush persistent storage before destruction
    if (m_persistent) {
        m_persistent->flush();
    }
    // Volatile backend destructor will clean up in-memory data
}

QString StorageManager::createContext(const QString& contextId, StorageMode defaultMode)
{
    QString id = contextId;
    if (id.isEmpty()) {
        id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }

    if (m_contexts.contains(id)) {
        // Context already exists — return it
        return id;
    }

    auto ctx = std::make_unique<StorageContext>(id, m_persistent.get(), m_volatile.get());
    ctx->setDefaultMode(defaultMode);
    m_contexts[id] = std::move(ctx);

    emit contextCreated(id, defaultMode);
    return id;
}

int StorageManager::destroyContext(const QString& contextId)
{
    if (contextId == "default") {
        // Don't destroy the default context — just clear its volatile data
        return m_volatile->clearContext(contextId);
    }

    auto it = m_contexts.find(contextId);
    if (it == m_contexts.end()) {
        return 0;
    }

    // Clear both persistent and volatile data for this context
    int cleared = it.value()->clearAll();

    // Also clear any persistent data that might be on disk
    m_persistent->clearContext(contextId);

    m_contexts.erase(it);
    emit contextDestroyed(contextId, cleared);
    return cleared;
}

StorageContext* StorageManager::context(const QString& contextId)
{
    auto it = m_contexts.find(contextId);
    if (it != m_contexts.end()) {
        return it.value().get();
    }

    // Auto-create with volatile mode (safer default for unknown contexts)
    createContext(contextId, StorageMode::Volatile);
    return m_contexts[contextId].get();
}

bool StorageManager::hasContext(const QString& contextId) const
{
    return m_contexts.contains(contextId);
}

QStringList StorageManager::contexts() const
{
    return m_contexts.keys();
}

bool StorageManager::set(const QString& contextId,
                         const QString& namespace_,
                         const QString& key,
                         const QJsonValue& value,
                         const QString& modeStr)
{
    StorageContext* ctx = context(contextId);

    StorageMode mode;
    if (modeStr.isEmpty()) {
        mode = ctx->defaultMode();
    } else {
        mode = stringToMode(modeStr);
    }

    bool success = ctx->set(namespace_, key, value, mode);
    if (success) {
        emit valueSet(contextId, namespace_, key, value, mode);
    }
    return success;
}

QJsonValue StorageManager::get(const QString& contextId,
                               const QString& namespace_,
                               const QString& key,
                               const QString& modeStr) const
{
    // For const access, we need to find the context without auto-creating
    auto it = m_contexts.find(contextId);
    if (it == m_contexts.end()) {
        // Context doesn't exist — try persistent backend directly
        if (modeStr.isEmpty() || modeStr.toLower() == "persistent") {
            return m_persistent->get(contextId, namespace_, key);
        }
        if (modeStr.toLower() == "volatile") {
            return m_volatile->get(contextId, namespace_, key);
        }
        // Try both
        QJsonValue val = m_persistent->get(contextId, namespace_, key);
        if (!val.isUndefined() && !val.isNull()) return val;
        return m_volatile->get(contextId, namespace_, key);
    }

    const StorageContext* ctx = it.value().get();

    if (modeStr.isEmpty()) {
        // Try context's default mode first, then fall back
        return ctx->get(namespace_, key, ctx->defaultMode());
    }

    return ctx->get(namespace_, key, stringToMode(modeStr));
}

bool StorageManager::has(const QString& contextId,
                         const QString& namespace_,
                         const QString& key,
                         const QString& modeStr) const
{
    auto it = m_contexts.find(contextId);
    if (it == m_contexts.end()) {
        if (modeStr.isEmpty() || modeStr.toLower() == "persistent") {
            return m_persistent->has(contextId, namespace_, key);
        }
        if (modeStr.toLower() == "volatile") {
            return m_volatile->has(contextId, namespace_, key);
        }
        return m_persistent->has(contextId, namespace_, key) ||
               m_volatile->has(contextId, namespace_, key);
    }

    const StorageContext* ctx = it.value().get();
    if (modeStr.isEmpty()) {
        return ctx->has(namespace_, key, ctx->defaultMode());
    }
    return ctx->has(namespace_, key, stringToMode(modeStr));
}

bool StorageManager::remove(const QString& contextId,
                            const QString& namespace_,
                            const QString& key,
                            const QString& modeStr)
{
    StorageContext* ctx = context(contextId);

    StorageMode mode;
    if (modeStr.isEmpty()) {
        mode = ctx->defaultMode();
    } else {
        mode = stringToMode(modeStr);
    }

    bool removed = ctx->remove(namespace_, key, mode);
    if (removed) {
        emit valueRemoved(contextId, namespace_, key, mode);
    }
    return removed;
}

QList<StorageEntry> StorageManager::list(const QString& contextId,
                                         const QString& namespace_,
                                         const QString& keyPrefix,
                                         const QString& modeStr) const
{
    auto it = m_contexts.find(contextId);
    if (it == m_contexts.end()) {
        // Context doesn't exist — check both backends
        QList<StorageEntry> result;
        if (modeStr.isEmpty() || modeStr.toLower() == "persistent") {
            result.append(m_persistent->list(contextId, namespace_, keyPrefix));
        }
        if (modeStr.isEmpty() || modeStr.toLower() == "volatile") {
            result.append(m_volatile->list(contextId, namespace_, keyPrefix));
        }
        return result;
    }

    const StorageContext* ctx = it.value().get();
    if (modeStr.isEmpty()) {
        return ctx->list(namespace_, keyPrefix, ctx->defaultMode());
    }
    return ctx->list(namespace_, keyPrefix, stringToMode(modeStr));
}

int StorageManager::clear(const QString& contextId,
                          const QString& namespace_,
                          const QString& modeStr)
{
    StorageContext* ctx = context(contextId);

    int cleared = 0;

    if (modeStr.isEmpty()) {
        // Clear both modes
        cleared = ctx->clearAll();
    } else {
        cleared = ctx->clear(namespace_, stringToMode(modeStr));
    }

    return cleared;
}

StorageStats StorageManager::stats(const QString& contextId) const
{
    auto it = m_contexts.find(contextId);
    if (it == m_contexts.end()) {
        StorageStats s;
        s.persistentEntries = m_persistent->count(contextId);
        s.persistentBytes = m_persistent->sizeBytes(contextId);
        s.volatileEntries = m_volatile->count(contextId);
        s.volatileBytes = m_volatile->sizeBytes(contextId);
        s.entryCount = s.persistentEntries + s.volatileEntries;
        s.totalBytes = s.persistentBytes + s.volatileBytes;
        return s;
    }
    return it.value()->stats();
}

void StorageManager::flush()
{
    m_persistent->flush();
}

void StorageManager::wipeVolatile()
{
    m_volatile->wipeAll();
}

} // namespace storage
} // namespace nothing
