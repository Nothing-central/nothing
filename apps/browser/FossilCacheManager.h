// ═══════════════════════════════════════════ FossilCacheManager.h ═══════════════════════════════════════════
#pragma once
#include <QObject>
#include <QSqlDatabase>
#include <QByteArray>
#include <QUrl>

class FossilCacheManager : public QObject {
    Q_OBJECT
public:
    // Singleton accessor
    static FossilCacheManager* instance();

    void saveAsset(const QString& fossilId, const QUrl& assetUrl, const QByteArray& data, const QString& mimeType);
    void saveHtmlSnapshot(const QString& fossilId, const QUrl& originalUrl, const QString& html);

    QByteArray getAssetData(const QString& fossilId, const QUrl& assetUrl);
    QString getAssetMime(const QString& fossilId, const QUrl& assetUrl);
    QByteArray getHtmlData(const QString& fossilId);

private:
    explicit FossilCacheManager(QObject* parent = nullptr);
    static FossilCacheManager* s_instance;
    QSqlDatabase m_db;
};
