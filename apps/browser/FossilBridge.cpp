#include "FossilBridge.h"
#include <QByteArray>

FossilBridge::FossilBridge(FossilCacheManager* manager, QObject* parent)
    : QObject(parent), m_manager(manager) {}

void FossilBridge::setCurrentFossil(const QString& id, const QUrl& originalUrl) {
    m_currentFossilId = id;
    m_originalUrl = originalUrl;
}

void FossilBridge::saveAsset(const QString& url, const QString& base64Data, const QString& mimeType) {
    QByteArray data = QByteArray::fromBase64(base64Data.toUtf8());
    m_manager->saveAsset(m_currentFossilId, QUrl(url), data, mimeType);
}

void FossilBridge::saveHtml(const QString& html) {
    m_manager->saveHtmlSnapshot(m_currentFossilId, m_originalUrl, html);
    emit assetCollectionFinished();
}

void FossilBridge::reportHeuristic(const QString& status) {
    emit heuristicReported(status);
}
