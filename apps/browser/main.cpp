// ═══════════════════════════════════════════ main.cpp ═════════════════════════════════════════════════
#include <QApplication>
#include <QWebEngineUrlScheme>
#include <QtWebEngineCore/QWebEngineProfile>
#include <QtWebEngineCore/QWebEngineScript>
#include <QtWebEngineCore/QWebEngineScriptCollection>
#include <QtWebEngineWidgets/QWebEngineView>
#include <QFile>
#include <QDir>
#include <QDebug>
#include "LangManager.h"
#include "FingerprintController.h"
#include "SearchController.h"
#include "NormalWindow.h"
#include "FossilCacheManager.h"
#include "FossilSchemeHandler.h"
#include "DownloadManager.h"
#include "AdblockInterceptor.h"

int main(int argc, char *argv[]) {
    QApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    QWebEngineUrlScheme fossilScheme("sabre-fossil");
    fossilScheme.setFlags(QWebEngineUrlScheme::SecureScheme |
                          QWebEngineUrlScheme::LocalScheme |
                          QWebEngineUrlScheme::ViewSourceAllowed);
    QWebEngineUrlScheme::registerScheme(fossilScheme);

    QApplication app(argc, argv);
    app.setApplicationName("Sabre Browser");
    app.setOrganizationName("Ernest Tech House");
    app.setApplicationVersion("0.1.0.0");
    app.setWindowIcon(QIcon(":/sabre/icons/mainlogo-nobackground.png"));

    // ── DEBUG: Check embedded resources ──────────────────────────────────────
    qDebug() << "=== RESOURCE DEBUG ===";
    qDebug() << "back.svg exists?" << QFile::exists(":/sabre/icons/back.svg");
    qDebug() << "easylist exists?" << QFile::exists(":/sabre/filters/easylist.txt");
    qDebug() << "Root resource entries:" << QDir(":/").entryList();
    qDebug() << "Icons entries:" << QDir(":/icons").entryList();
    qDebug() << "=== END DEBUG ===";

    LangManager langManager;
    SearchController searchController;
    FingerprintController fingerprintController;

    QWebEngineProfile* sessionProfile = new QWebEngineProfile("default", &app);
    sessionProfile->setHttpUserAgent(
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
        "(KHTML, like Gecko) Chrome/147.0.0.0 Safari/537.36"
    );
    AdblockInterceptor* adblock = new AdblockInterceptor(&app);
    sessionProfile->setUrlRequestInterceptor(adblock);
    QObject::connect(sessionProfile, &QWebEngineProfile::downloadRequested,
                     DownloadManager::instance(), &DownloadManager::handleDownload);

    FossilSchemeHandler* fossilHandler = new FossilSchemeHandler(FossilCacheManager::instance(), &app);
    sessionProfile->installUrlSchemeHandler("sabre-fossil", fossilHandler);

    QWebEngineScript fpScript;
    fpScript.setName("sabre");
    fpScript.setInjectionPoint(QWebEngineScript::DocumentCreation);
    fpScript.setWorldId(QWebEngineScript::MainWorld);
    fpScript.setRunsOnSubFrames(true);
    fpScript.setSourceCode(fingerprintController.sessionScript("default"));
    sessionProfile->scripts()->insert(fpScript);

    NormalWindow* window = new NormalWindow(sessionProfile, &fingerprintController,
                                            &langManager, &searchController);
    window->show();
    return app.exec();
}
