// ═══════════════════════════════════════════ FossilSchemeHandler.cpp ═══════════════════════════════════════════
#include "FossilSchemeHandler.h"
#include <QWebEngineUrlRequestJob>
#include <QBuffer>
#include <QUrl>

FossilSchemeHandler::FossilSchemeHandler(FossilCacheManager* manager, QObject* parent)
    : QWebEngineUrlSchemeHandler(parent), m_manager(manager) {}

void FossilSchemeHandler::requestStarted(QWebEngineUrlRequestJob* job) {
    QUrl url = job->requestUrl();
    QString fossilId = url.host();
    QString assetUrlEncoded = url.path().mid(1); // Remove leading '/'

    // FIX: Make 'job' the parent of 'buffer'.
    // Qt's object tree will automatically delete the buffer when the job is destroyed,
    // preventing memory leaks without needing the 'finished' signal.
    QBuffer* buffer = new QBuffer(job);

    if (assetUrlEncoded.isEmpty()) {
        // Request for the main HTML snapshot
        QByteArray html = m_manager->getHtmlData(fossilId);
        if (html.isEmpty()) {
            job->fail(QWebEngineUrlRequestJob::UrlNotFound);
            return;
        }
        buffer->setData(html);
        job->reply("text/html;charset=utf-8", buffer);
    } else {
        // Request for a subresource (image, css, js)
        QString assetUrlStr = QUrl::fromPercentEncoding(assetUrlEncoded.toUtf8());
        QUrl assetUrl(assetUrlStr);

        QByteArray data = m_manager->getAssetData(fossilId, assetUrl);
        if (data.isEmpty()) {
            job->fail(QWebEngineUrlRequestJob::UrlNotFound);
            return;
        }
        QString mime = m_manager->getAssetMime(fossilId, assetUrl);
        buffer->setData(data);
        job->reply(mime.toUtf8(), buffer);
    }
}
