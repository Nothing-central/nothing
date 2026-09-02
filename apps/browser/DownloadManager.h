#pragma once
#include <QObject>
#include <QList>
#include <QSqlDatabase>
#include <QWebEngineDownloadRequest>
#include <QUrl>

struct DownloadRecord {
    QString id;
    QString url;
    QString filename;
    QString path;
    qint64 totalBytes;
    qint64 receivedBytes;
    QString status; // "downloading", "paused", "completed", "failed"
    qint64 timestamp;
    QString mimeType;
};

class DownloadManager : public QObject {
    Q_OBJECT
public:
    static DownloadManager* instance();
    void initialize();
    void handleDownload(QWebEngineDownloadRequest* request);
    QList<DownloadRecord> getHistory() const;
    void clearHistory();

signals:
    void downloadStarted(const DownloadRecord& record);
    void downloadProgress(const QString& id, qint64 received, qint64 total, int speed);
    void downloadFinished(const QString& id, bool success);
    void historyUpdated();

private slots:
    void onDownloadStateChanged();

private:
    explicit DownloadManager(QObject* parent = nullptr);
    void setupDatabase();
    void insertRecord(const DownloadRecord& record);
    void updateRecord(const QString& id, const DownloadRecord& record);

    QSqlDatabase m_db;
    QMap<QString, QWebEngineDownloadRequest*> m_activeDownloads;
    QMap<QString, DownloadRecord> m_records;
};
