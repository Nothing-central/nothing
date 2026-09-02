#include "DownloadManager.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QDir>
#include <QStandardPaths>
#include <QDateTime>
#include <QUuid>
#include <QFileInfo>
#include <QDebug>

DownloadManager* DownloadManager::instance() {
    static DownloadManager inst;
    return &inst;
}

DownloadManager::DownloadManager(QObject* parent) : QObject(parent) {
    initialize();
}

void DownloadManager::initialize() {
    setupDatabase();
}

void DownloadManager::setupDatabase() {
    m_db = QSqlDatabase::addDatabase("QSQLITE", "sabre_downloads");
    QString dbPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/sabre_downloads.db";
    QDir().mkpath(QFileInfo(dbPath).absolutePath());
    m_db.setDatabaseName(dbPath);

    if (!m_db.open()) {
        qWarning() << "Failed to open download database:" << m_db.lastError();
        return;
    }

    QSqlQuery q(m_db);
    q.exec("CREATE TABLE IF NOT EXISTS downloads ("
           "id TEXT PRIMARY KEY, url TEXT, filename TEXT, path TEXT, "
           "total_bytes INTEGER, received_bytes INTEGER, status TEXT, "
           "timestamp INTEGER, mime_type TEXT)");
}

void DownloadManager::handleDownload(QWebEngineDownloadRequest* request) {
    if (!request) return;

    // We accept the download to let WebEngine handle the complex cookie/auth logic
    request->accept();

    DownloadRecord rec;
    rec.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    rec.url = request->url().toString();
    rec.filename = request->downloadFileName();
    rec.path = request->downloadDirectory();
    rec.totalBytes = request->totalBytes();
    rec.receivedBytes = 0;
    rec.status = "downloading";
    rec.timestamp = QDateTime::currentMSecsSinceEpoch();
    rec.mimeType = request->mimeType();

    m_activeDownloads[rec.id] = request;
    m_records[rec.id] = rec;

    insertRecord(rec);
    emit downloadStarted(rec);

    // ── FIX 1: Correct Qt6 signal name ──
    connect(request, &QWebEngineDownloadRequest::stateChanged, this, &DownloadManager::onDownloadStateChanged);
}

void DownloadManager::onDownloadStateChanged() {
    auto* request = qobject_cast<QWebEngineDownloadRequest*>(sender());
    if (!request) return;

    QString id = m_activeDownloads.key(request);
    if (id.isEmpty()) return;

    DownloadRecord& rec = m_records[id];
    rec.receivedBytes = request->receivedBytes();
    rec.totalBytes = request->totalBytes();

    // ── FIX 2: Correct Qt6 method name ──
    using State = QWebEngineDownloadRequest::DownloadState;
    State state = request->state();

    if (state == State::DownloadInProgress) {
        rec.status = "downloading";
        // Calculate speed (simplified)
        emit downloadProgress(id, rec.receivedBytes, rec.totalBytes, 0);
    }
    else if (state == State::DownloadCompleted) {
        rec.status = "completed";
        updateRecord(id, rec);
        m_activeDownloads.remove(id);
        emit downloadFinished(id, true);
        emit historyUpdated();
    }
    else if (state == State::DownloadCancelled || state == State::DownloadInterrupted) {
        rec.status = (state == State::DownloadCancelled) ? "cancelled" : "failed";
        updateRecord(id, rec);
        m_activeDownloads.remove(id);
        emit downloadFinished(id, false);
        emit historyUpdated();
    }
}

void DownloadManager::insertRecord(const DownloadRecord& rec) {
    QSqlQuery q(m_db);
    q.prepare("INSERT INTO downloads (id, url, filename, path, total_bytes, received_bytes, status, timestamp, mime_type) "
              "VALUES (:id, :url, :filename, :path, :total, :received, :status, :ts, :mime)");
    q.bindValue(":id", rec.id);
    q.bindValue(":url", rec.url);
    q.bindValue(":filename", rec.filename);
    q.bindValue(":path", rec.path);
    q.bindValue(":total", rec.totalBytes);
    q.bindValue(":received", rec.receivedBytes);
    q.bindValue(":status", rec.status);
    q.bindValue(":ts", rec.timestamp);
    q.bindValue(":mime", rec.mimeType);
    q.exec();
}

void DownloadManager::updateRecord(const QString& id, const DownloadRecord& rec) {
    QSqlQuery q(m_db);
    q.prepare("UPDATE downloads SET received_bytes=:received, status=:status WHERE id=:id");
    q.bindValue(":received", rec.receivedBytes);
    q.bindValue(":status", rec.status);
    q.bindValue(":id", id);
    q.exec();
}

QList<DownloadRecord> DownloadManager::getHistory() const {
    QList<DownloadRecord> list;
    QSqlQuery q(m_db);
    q.exec("SELECT * FROM downloads ORDER BY timestamp DESC");
    while (q.next()) {
        DownloadRecord rec;
        rec.id = q.value("id").toString();
        rec.url = q.value("url").toString();
        rec.filename = q.value("filename").toString();
        rec.path = q.value("path").toString();
        rec.totalBytes = q.value("total_bytes").toLongLong();
        rec.receivedBytes = q.value("received_bytes").toLongLong();
        rec.status = q.value("status").toString();
        rec.timestamp = q.value("timestamp").toLongLong();
        rec.mimeType = q.value("mime_type").toString();
        list.append(rec);
    }
    return list;
}

void DownloadManager::clearHistory() {
    QSqlQuery q(m_db);
    q.exec("DELETE FROM downloads WHERE status != 'downloading'");
    emit historyUpdated();
}
