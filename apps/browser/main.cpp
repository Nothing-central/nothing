// ═══════════════════════════════════════════ main.cpp ═════════════════════════════════════════════════
#include <QApplication>
#include <QWebEngineUrlScheme>
#include <QtWebEngineCore/QWebEngineProfile>
#include <QtWebEngineCore/QWebEngineScript>
#include <QtWebEngineCore/QWebEngineScriptCollection>
#include <QtWebEngineWidgets/QWebEngineView>
#include "LangManager.h"
#include "FingerprintController.h"
#include "SearchController.h"
#include "NormalWindow.h"
#include "FossilCacheManager.h"
#include "FossilSchemeHandler.h"
#include "DownloadManager.h"

int main(int argc, char *argv[]) {
    // ── Fix: Must be called before QApplication ───────────────────────────────
    QApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    // ── CRITICAL: Register custom scheme BEFORE QApplication is created ───────
    QWebEngineUrlScheme fossilScheme("sabre-fossil");
    fossilScheme.setFlags(QWebEngineUrlScheme::SecureScheme |
                          QWebEngineUrlScheme::LocalScheme |
                          QWebEngineUrlScheme::ViewSourceAllowed);
    QWebEngineUrlScheme::registerScheme(fossilScheme);

    QApplication app(argc, argv);
    app.setApplicationName("Sabre Browser");
    app.setOrganizationName("Ernest Tech House");
    app.setApplicationVersion("0.1.0.0");

    LangManager langManager;
    SearchController searchController;
    FingerprintController fingerprintController;

    QWebEngineProfile* sessionProfile = new QWebEngineProfile("default", &app);

    // ── User Agent: Match our fingerprint version so sites don't block us ─────
    sessionProfile->setHttpUserAgent(
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
        "(KHTML, like Gecko) Chrome/147.0.0.0 Safari/537.36"
    );

    // ── SUPERCHARGED DOWNLOADS: Global single connection ──────────────────────
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
