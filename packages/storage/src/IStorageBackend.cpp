#include "nothing/storage/IStorageBackend.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDebug>

namespace nothing {
namespace storage {

// ============================================================================
// PersistentBackend
// ============================================================================

PersistentBackend::PersistentBackend(const QString& dataDir)
    : m_dataDir(dataDir)
{
    // Ensure the storage root exists
    QDir dir(m_dataDir + "/nothing-storage");
    if (!dir.exists()) {
        dir.mkpath(".");
    }
}

PersistentBackend::~PersistentBackend()
{
    flush();
}

QString PersistentBackend::contextDir(const QString& contextId) const
{
    return m_dataDir + "/nothing-storage/" + contextId;
}

QString PersistentBackend::filePath(const QString& contextId, const QString& namespace_) const
{
    // Sanitize namespace_ for filesystem (replace / with _)
    QString safeNs = namespace_;
    safeNs.replace('/', '_');
    return contextDir(contextId) + "/" + safeNs + ".json";
}

QString PersistentBackend::dirtyKey(const QString& contextId, const QString& namespace_) const
{
    return contextId + "/" + namespace_;
}

void PersistentBackend::loadFromDisk(const QString& contextId, const QString& namespace_) const
{
    // const_cast because IStorageBackend::get is const but we need to cache
    auto* self = const_cast<PersistentBackend*>(this);
    QRecursiveMutexLocker lock(&self->m_mutex);

    QString key = dirtyKey(contextId, namespace_);
    if (self->m_data.contains(contextId) &&
        self->m_data[contextId].contains(namespace_)) {
        return;  // already loaded
    }

    QString path = filePath(contextId, namespace_);
    QFile f(path);
    if (!f.exists()) {
        // No file yet — initialize empty
        self->m_data[contextId][namespace_] = QJsonObject();
        return;
    }

    if (!f.open(QIODevice::ReadOnly)) {
        qWarning() << "PersistentBackend: Failed to open" << path;
        self->m_data[contextId][namespace_] = QJsonObject();
        return;
    }

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &parseError);
    f.close();

    if (parseError.error != QJsonParseError::NoError) {
        qWarning() << "PersistentBackend: Parse error in" << path << ":" << parseError.errorString();
        self->m_data[contextId][namespace_] = QJsonObject();
        return;
    }

    self->m_data[contextId][namespace_] = doc.object();
}

void PersistentBackend::saveToDisk(const QString& contextId, const QString& namespace_)
{
    QRecursiveMutexLocker lock(&m_mutex);

    QString dirPath = contextDir(contextId);
    QDir dir(dirPath);
    if (!dir.exists()) {
        dir.mkpath(".");
    }

    QString path = filePath(contextId, namespace_);
    QJsonObject obj = m_data[contextId][namespace_];
    QJsonDocument doc(obj);

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning() << "PersistentBackend: Failed to write" << path;
        return;
    }
    f.write(doc.toJson(QJsonDocument::Compact));
    f.close();

    m_dirty.remove(dirtyKey(contextId, namespace_));
}

qint64 PersistentBackend::estimateSize(const QJsonValue& value) const
{
    return QJsonDocument(QJsonObject{{"v", value}}).toJson(QJsonDocument::Compact).size();
}

bool PersistentBackend::set(const QString& contextId,
                            const QString& namespace_,
                            const QString& key,
                            const QJsonValue& value)
{
    QRecursiveMutexLocker lock(&m_mutex);

    loadFromDisk(contextId, namespace_);

    m_data[contextId][namespace_][key] = value;
    m_dirty.insert(dirtyKey(contextId, namespace_));
    return true;
}

QJsonValue PersistentBackend::get(const QString& contextId,
                                  const QString& namespace_,
                                  const QString& key) const
{
    QRecursiveMutexLocker lock(&m_mutex);
    loadFromDisk(contextId, namespace_);

    const QJsonObject& obj = m_data[contextId][namespace_];
    if (obj.contains(key)) {
        return obj.value(key);
    }
    return QJsonValue();
}

bool PersistentBackend::has(const QString& contextId,
                            const QString& namespace_,
                            const QString& key) const
{
    QRecursiveMutexLocker lock(&m_mutex);
    loadFromDisk(contextId, namespace_);
    return m_data[contextId][namespace_].contains(key);
}

bool PersistentBackend::remove(const QString& contextId,
                                const QString& namespace_,
                                const QString& key)
{
    QRecursiveMutexLocker lock(&m_mutex);
    loadFromDisk(contextId, namespace_);

    QJsonObject& obj = m_data[contextId][namespace_];
    if (!obj.contains(key)) return false;
    obj.remove(key);
    m_dirty.insert(dirtyKey(contextId, namespace_));
    return true;
}

QList<StorageEntry> PersistentBackend::list(const QString& contextId,
                                             const QString& namespace_,
                                             const QString& keyPrefix) const
{
    QRecursiveMutexLocker lock(&m_mutex);
    QList<StorageEntry> result;

    if (!namespace_.isEmpty()) {
        loadFromDisk(contextId, namespace_);
        const QJsonObject& obj = m_data[contextId][namespace_];
        for (auto it = obj.begin(); it != obj.end(); ++it) {
            if (!keyPrefix.isEmpty() && !it.key().startsWith(keyPrefix)) continue;
            StorageEntry entry;
            entry.key = it.key();
            entry.value = it.value();
            entry.mode = StorageMode::Persistent;
            entry.contextId = contextId;
            entry.namespace_ = namespace_;
            entry.sizeBytes = estimateSize(it.value());
            result.append(entry);
        }
    } else {
        // List all namespaces for this context
        if (m_data.contains(contextId)) {
            const ContextMap& ctxMap = m_data[contextId];
            for (auto nsIt = ctxMap.begin(); nsIt != ctxMap.end(); ++nsIt) {
                const QJsonObject& obj = nsIt.value();
                for (auto it = obj.begin(); it != obj.end(); ++it) {
                    if (!keyPrefix.isEmpty() && !it.key().startsWith(keyPrefix)) continue;
                    StorageEntry entry;
                    entry.key = it.key();
                    entry.value = it.value();
                    entry.mode = StorageMode::Persistent;
                    entry.contextId = contextId;
                    entry.namespace_ = nsIt.key();
                    entry.sizeBytes = estimateSize(it.value());
                    result.append(entry);
                }
            }
        }

        // Also check disk for namespaces we haven't loaded yet
        QDir dir(contextDir(contextId));
        if (dir.exists()) {
            QStringList filters;
            filters << "*.json";
            dir.setNameFilters(filters);
            for (const QFileInfo& fi : dir.entryInfoList(QDir::Files)) {
                QString ns = fi.baseName().replace('_', '/');
                if (m_data.contains(contextId) && m_data[contextId].contains(ns)) continue;
                loadFromDisk(contextId, ns);
                const QJsonObject& obj = m_data[contextId][ns];
                for (auto it = obj.begin(); it != obj.end(); ++it) {
                    if (!keyPrefix.isEmpty() && !it.key().startsWith(keyPrefix)) continue;
                    StorageEntry entry;
                    entry.key = it.key();
                    entry.value = it.value();
                    entry.mode = StorageMode::Persistent;
                    entry.contextId = contextId;
                    entry.namespace_ = ns;
                    entry.sizeBytes = estimateSize(it.value());
                    result.append(entry);
                }
            }
        }
    }

    return result;
}

int PersistentBackend::clearContext(const QString& contextId, const QString& namespace_)
{
    QRecursiveMutexLocker lock(&m_mutex);
    int cleared = 0;

    if (!namespace_.isEmpty()) {
        // Clear one namespace
        loadFromDisk(contextId, namespace_);
        cleared = m_data[contextId][namespace_].size();
        m_data[contextId][namespace_] = QJsonObject();
        m_dirty.insert(dirtyKey(contextId, namespace_));
        saveToDisk(contextId, namespace_);
    } else {
        // Clear all namespaces for this context
        if (m_data.contains(contextId)) {
            for (auto it = m_data[contextId].begin(); it != m_data[contextId].end(); ++it) {
                cleared += it.value().size();
                it.value() = QJsonObject();
                m_dirty.insert(dirtyKey(contextId, it.key()));
                saveToDisk(contextId, it.key());
            }
        }
        // Also delete any .json files on disk
        QDir dir(contextDir(contextId));
        if (dir.exists()) {
            dir.removeRecursively();
        }
    }

    return cleared;
}

int PersistentBackend::clearNamespace(const QString& namespace_)
{
    QRecursiveMutexLocker lock(&m_mutex);
    int cleared = 0;

    // Clear from memory
    for (auto ctxIt = m_data.begin(); ctxIt != m_data.end(); ++ctxIt) {
        if (ctxIt.value().contains(namespace_)) {
            cleared += ctxIt.value()[namespace_].size();
            ctxIt.value()[namespace_] = QJsonObject();
            m_dirty.insert(dirtyKey(ctxIt.key(), namespace_));
            saveToDisk(ctxIt.key(), namespace_);
        }
    }

    // Also scan disk for contexts we haven't loaded
    QDir rootDir(m_dataDir + "/nothing-storage");
    if (rootDir.exists()) {
        for (const QFileInfo& ctxInfo : rootDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            QString ctxId = ctxInfo.fileName();
            if (m_data.contains(ctxId) && m_data[ctxId].contains(namespace_)) continue;
            loadFromDisk(ctxId, namespace_);
            if (m_data.contains(ctxId) && m_data[ctxId].contains(namespace_)) {
                cleared += m_data[ctxId][namespace_].size();
                m_data[ctxId][namespace_] = QJsonObject();
                m_dirty.insert(dirtyKey(ctxId, namespace_));
                saveToDisk(ctxId, namespace_);
            }
        }
    }

    return cleared;
}

qint64 PersistentBackend::sizeBytes(const QString& contextId) const
{
    QRecursiveMutexLocker lock(&m_mutex);
    qint64 total = 0;

    if (m_data.contains(contextId)) {
        for (auto it = m_data[contextId].begin(); it != m_data[contextId].end(); ++it) {
            for (auto kvIt = it.value().begin(); kvIt != it.value().end(); ++kvIt) {
                total += estimateSize(kvIt.value());
            }
        }
    }

    return total;
}

int PersistentBackend::count(const QString& contextId) const
{
    QRecursiveMutexLocker lock(&m_mutex);
    int total = 0;

    if (m_data.contains(contextId)) {
        for (auto it = m_data[contextId].begin(); it != m_data[contextId].end(); ++it) {
            total += it.value().size();
        }
    }

    return total;
}

void PersistentBackend::flush()
{
    QRecursiveMutexLocker lock(&m_mutex);
    for (const QString& key : m_dirty) {
        int slashPos = key.indexOf('/');
        QString ctxId = key.left(slashPos);
        QString ns = key.mid(slashPos + 1);
        saveToDisk(ctxId, ns);
    }
    m_dirty.clear();
}

// ============================================================================
// VolatileBackend
// ============================================================================

qint64 VolatileBackend::estimateSize(const QJsonValue& value) const
{
    return QJsonDocument(QJsonObject{{"v", value}}).toJson(QJsonDocument::Compact).size();
}

bool VolatileBackend::set(const QString& contextId,
                          const QString& namespace_,
                          const QString& key,
                          const QJsonValue& value)
{
    QRecursiveMutexLocker lock(&m_mutex);
    m_data[contextId][namespace_][key] = value;
    return true;
}

QJsonValue VolatileBackend::get(const QString& contextId,
                                 const QString& namespace_,
                                 const QString& key) const
{
    QRecursiveMutexLocker lock(&m_mutex);
    auto ctxIt = m_data.find(contextId);
    if (ctxIt == m_data.end()) return QJsonValue();
    auto nsIt = ctxIt.value().find(namespace_);
    if (nsIt == ctxIt.value().end()) return QJsonValue();
    if (!nsIt.value().contains(key)) return QJsonValue();
    return nsIt.value().value(key);
}

bool VolatileBackend::has(const QString& contextId,
                          const QString& namespace_,
                          const QString& key) const
{
    QRecursiveMutexLocker lock(&m_mutex);
    auto ctxIt = m_data.find(contextId);
    if (ctxIt == m_data.end()) return false;
    auto nsIt = ctxIt.value().find(namespace_);
    if (nsIt == ctxIt.value().end()) return false;
    return nsIt.value().contains(key);
}

bool VolatileBackend::remove(const QString& contextId,
                              const QString& namespace_,
                              const QString& key)
{
    QRecursiveMutexLocker lock(&m_mutex);
    auto ctxIt = m_data.find(contextId);
    if (ctxIt == m_data.end()) return false;
    auto nsIt = ctxIt.value().find(namespace_);
    if (nsIt == ctxIt.value().end()) return false;
    if (!nsIt.value().contains(key)) return false;
    nsIt.value().remove(key);
    return true;
}

QList<StorageEntry> VolatileBackend::list(const QString& contextId,
                                           const QString& namespace_,
                                           const QString& keyPrefix) const
{
    QRecursiveMutexLocker lock(&m_mutex);
    QList<StorageEntry> result;

    auto ctxIt = m_data.find(contextId);
    if (ctxIt == m_data.end()) return result;

    if (!namespace_.isEmpty()) {
        auto nsIt = ctxIt.value().find(namespace_);
        if (nsIt == ctxIt.value().end()) return result;
        for (auto it = nsIt.value().begin(); it != nsIt.value().end(); ++it) {
            if (!keyPrefix.isEmpty() && !it.key().startsWith(keyPrefix)) continue;
            StorageEntry entry;
            entry.key = it.key();
            entry.value = it.value();
            entry.mode = StorageMode::Volatile;
            entry.contextId = contextId;
            entry.namespace_ = namespace_;
            entry.sizeBytes = estimateSize(it.value());
            result.append(entry);
        }
    } else {
        for (auto nsIt = ctxIt.value().begin(); nsIt != ctxIt.value().end(); ++nsIt) {
            for (auto it = nsIt.value().begin(); it != nsIt.value().end(); ++it) {
                if (!keyPrefix.isEmpty() && !it.key().startsWith(keyPrefix)) continue;
                StorageEntry entry;
                entry.key = it.key();
                entry.value = it.value();
                entry.mode = StorageMode::Volatile;
                entry.contextId = contextId;
                entry.namespace_ = nsIt.key();
                entry.sizeBytes = estimateSize(it.value());
                result.append(entry);
            }
        }
    }

    return result;
}

int VolatileBackend::clearContext(const QString& contextId, const QString& namespace_)
{
    QRecursiveMutexLocker lock(&m_mutex);
    auto ctxIt = m_data.find(contextId);
    if (ctxIt == m_data.end()) return 0;

    int cleared = 0;
    if (!namespace_.isEmpty()) {
        auto nsIt = ctxIt.value().find(namespace_);
        if (nsIt != ctxIt.value().end()) {
            cleared = nsIt.value().size();
            // Overwrite before removing (best-effort secure clear)
            QJsonObject empty;
            nsIt.value().swap(empty);
            ctxIt.value().erase(nsIt);
        }
    } else {
        for (auto nsIt = ctxIt.value().begin(); nsIt != ctxIt.value().end(); ++nsIt) {
            cleared += nsIt.value().size();
            QJsonObject empty;
            nsIt.value().swap(empty);
        }
        m_data.erase(ctxIt);
    }
    return cleared;
}

int VolatileBackend::clearNamespace(const QString& namespace_)
{
    QRecursiveMutexLocker lock(&m_mutex);
    int cleared = 0;

    for (auto ctxIt = m_data.begin(); ctxIt != m_data.end(); ++ctxIt) {
        auto nsIt = ctxIt.value().find(namespace_);
        if (nsIt != ctxIt.value().end()) {
            cleared += nsIt.value().size();
            QJsonObject empty;
            nsIt.value().swap(empty);
            ctxIt.value().erase(nsIt);
        }
    }

    return cleared;
}

qint64 VolatileBackend::sizeBytes(const QString& contextId) const
{
    QRecursiveMutexLocker lock(&m_mutex);
    qint64 total = 0;

    auto ctxIt = m_data.find(contextId);
    if (ctxIt == m_data.end()) return 0;

    for (auto nsIt = ctxIt.value().begin(); nsIt != ctxIt.value().end(); ++nsIt) {
        for (auto it = nsIt.value().begin(); it != nsIt.value().end(); ++it) {
            total += estimateSize(it.value());
        }
    }

    return total;
}

int VolatileBackend::count(const QString& contextId) const
{
    QRecursiveMutexLocker lock(&m_mutex);
    int total = 0;

    auto ctxIt = m_data.find(contextId);
    if (ctxIt == m_data.end()) return 0;

    for (auto nsIt = ctxIt.value().begin(); nsIt != ctxIt.value().end(); ++nsIt) {
        total += nsIt.value().size();
    }

    return total;
}

void VolatileBackend::wipeAll()
{
    QRecursiveMutexLocker lock(&m_mutex);
    // Best-effort secure wipe: swap each QJsonObject with empty before erasing
    for (auto ctxIt = m_data.begin(); ctxIt != m_data.end(); ++ctxIt) {
        for (auto nsIt = ctxIt.value().begin(); nsIt != ctxIt.value().end(); ++nsIt) {
            QJsonObject empty;
            nsIt.value().swap(empty);
        }
    }
    m_data.clear();
}

} // namespace storage
} // namespace nothing
