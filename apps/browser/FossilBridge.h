#pragma once
#include <QObject>
#include <QString>
#include <QUrl>
#include "FossilCacheManager.h"

// The QWebChannel bridge. JS calls these slots to send Base64 assets and HTML.
class FossilBridge : public QObject {
    Q_OBJECT
public:
    explicit FossilBridge(FossilCacheManager* manager, QObject* parent = nullptr);
    void setCurrentFossil(const QString& id, const QUrl& originalUrl);

public slots:
    void saveAsset(const QString& url, const QString& base64Data, const QString& mimeType);
    void saveHtml(const QString& html);
    void reportHeuristic(const QString& status);

signals:
    void assetCollectionFinished();
    void heuristicReported(const QString& status);

private:
    FossilCacheManager* m_manager;
    QString m_currentFossilId;
    QUrl m_originalUrl;
};
