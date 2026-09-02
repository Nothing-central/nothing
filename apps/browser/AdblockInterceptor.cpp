#include "AdblockInterceptor.h"
#include <QFile>
#include <QDebug>
#include <QStandardPaths>
#include <QDir>

AdblockInterceptor::AdblockInterceptor(QObject* parent)
    : QWebEngineUrlRequestInterceptor(parent)
{
    // ── ALWAYS ON: Load filter lists ───────────────────────────────────────
    // Prefer user-updated files in AppData, fallback to bundled resources
    QString appDataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QString localEasyList = appDataDir + "/filters/easylist.txt";
    QString localEasyPrivacy = appDataDir + "/filters/easyprivacy.txt";

    QStringList filesToLoad;

    if (QFile::exists(localEasyList)) {
        filesToLoad << localEasyList;
    } else {
        filesToLoad << ":/filters/easylist.txt";
    }

    if (QFile::exists(localEasyPrivacy)) {
        filesToLoad << localEasyPrivacy;
    } else {
        filesToLoad << ":/filters/easyprivacy.txt";
    }

    for (const QString& filePath : filesToLoad) {
        QFile file(filePath);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            m_blocker.load(file.readAll().toStdString());
            file.close();
            qDebug() << "[Adblock] Loaded:" << filePath;
        } else {
            qWarning() << "[Adblock] Failed to load:" << filePath;
        }
    }

    m_blocker.finalize();
    qDebug() << "[Adblock] Engine finalized. Always ON. No toggle.";
}

void AdblockInterceptor::interceptRequest(QWebEngineUrlRequestInfo& info) {
    std::string url = info.requestUrl().toString().toStdString();
    std::string firstPartyUrl = info.firstPartyUrl().toString().toStdString();

    adblock::RequestType type = mapResourceType(info.resourceType());
    auto req = adblock::Request::build(url, firstPartyUrl, type);
    auto result = m_blocker.check(req);

    if (result.should_block) {
        // Uncomment for debugging:
        // qDebug() << "[Adblock] Blocked:" << info.requestUrl().toString();
        info.block(true);
    }
}

adblock::RequestType AdblockInterceptor::mapResourceType(QWebEngineUrlRequestInfo::ResourceType type) const {
    switch (type) {
        case QWebEngineUrlRequestInfo::ResourceTypeMainFrame: return adblock::RequestType::Document;
        case QWebEngineUrlRequestInfo::ResourceTypeSubFrame: return adblock::RequestType::Subdocument;
        case QWebEngineUrlRequestInfo::ResourceTypeStylesheet: return adblock::RequestType::Stylesheet;
        case QWebEngineUrlRequestInfo::ResourceTypeScript: return adblock::RequestType::Script;
        case QWebEngineUrlRequestInfo::ResourceTypeImage: return adblock::RequestType::Image;
        case QWebEngineUrlRequestInfo::ResourceTypeFontResource: return adblock::RequestType::Font;
        case QWebEngineUrlRequestInfo::ResourceTypeMedia: return adblock::RequestType::Media;
        case QWebEngineUrlRequestInfo::ResourceTypeXhr: return adblock::RequestType::XmlHttpRequest;
        case QWebEngineUrlRequestInfo::ResourceTypeWorker:
        case QWebEngineUrlRequestInfo::ResourceTypeSharedWorker:
        case QWebEngineUrlRequestInfo::ResourceTypeServiceWorker: return adblock::RequestType::Script;
        default: return adblock::RequestType::Other;
    }
}
