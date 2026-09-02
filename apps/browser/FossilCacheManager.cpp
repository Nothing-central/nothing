// ═══════════════════════════════════════════ FossilCacheManager.cpp ═══════════════════════════════════════════
#include "FossilCacheManager.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QStandardPaths>
#include <QDir>
#include <QDebug>
#include <QCoreApplication> // ← ADDED: Required for the 'qApp' macro

FossilCacheManager* FossilCacheManager::s_instance = nullptr;

FossilCacheManager* FossilCacheManager::instance() {
    if (!s_instance) {
        s_instance = new FossilCacheManager(qApp);
    }
    return s_instance;
}

FossilCacheManager::FossilCacheManager(QObject* parent) : QObject(parent) {
    QString dbPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/fossils";
    QDir().mkpath(dbPath);
    QString dbFile = dbPath + "/fossil_cache.db";

    m_db = QSqlDatabase::addDatabase("QSQLITE", "fossil_conn");
    m_db.setDatabaseName(dbFile);

    if (!m_db.open()) {
        qCritical() << "Failed to open Fossil DB:" << m_db.lastError().text();
        return;
    }

    QSqlQuery q(m_db);
    q.exec("CREATE TABLE IF NOT EXISTS fossils (id TEXT PRIMARY KEY, original_url TEXT, html TEXT)");
    q.exec("CREATE TABLE IF NOT EXISTS assets (fossil_id TEXT, asset_url TEXT, mime_type TEXT, data BLOB, PRIMARY KEY(fossil_id, asset_url))");
}

void FossilCacheManager::saveAsset(const QString& fossilId, const QUrl& assetUrl, const QByteArray& data, const QString& mimeType) {
    QSqlQuery q(m_db);
    q.prepare("INSERT OR REPLACE INTO assets (fossil_id, asset_url, mime_type, data) VALUES (?, ?, ?, ?)");
    q.addBindValue(fossilId);
    q.addBindValue(assetUrl.toString());
    q.addBindValue(mimeType);
    q.addBindValue(data);
    q.exec();
}

void FossilCacheManager::saveHtmlSnapshot(const QString& fossilId, const QUrl& originalUrl, const QString& html) {
    QSqlQuery q(m_db);
    q.prepare("INSERT OR REPLACE INTO fossils (id, original_url, html) VALUES (?, ?, ?)");
    q.addBindValue(fossilId);
    q.addBindValue(originalUrl.toString());
    q.addBindValue(html);
    q.exec();
}

QByteArray FossilCacheManager::getAssetData(const QString& fossilId, const QUrl& assetUrl) {
    QSqlQuery q(m_db);
    q.prepare("SELECT data FROM assets WHERE fossil_id = ? AND asset_url = ?");
    q.addBindValue(fossilId);
    q.addBindValue(assetUrl.toString());
    if (q.exec() && q.next()) return q.value(0).toByteArray();
    return {};
}

QString FossilCacheManager::getAssetMime(const QString& fossilId, const QUrl& assetUrl) {
    QSqlQuery q(m_db);
    q.prepare("SELECT mime_type FROM assets WHERE fossil_id = ? AND asset_url = ?");
    q.addBindValue(fossilId);
    q.addBindValue(assetUrl.toString());
    if (q.exec() && q.next()) return q.value(0).toString();
    return "application/octet-stream";
}

QByteArray FossilCacheManager::getHtmlData(const QString& fossilId) {
    QSqlQuery q(m_db);
    q.prepare("SELECT html FROM fossils WHERE id = ?");
    q.addBindValue(fossilId);
    if (q.exec() && q.next()) return q.value(0).toString().toUtf8();
    return {};
}
