// ═══════════════════════════════════════════ main.cpp ═════════════════════════════════════════════════
#include <QApplication>
#include <QtWebEngineCore/QWebEngineProfile>
#include <QtWebEngineCore/QWebEngineScript>
#include <QtWebEngineCore/QWebEngineScriptCollection>
#include <QtWebEngineWidgets/QWebEngineView>
#include "LangManager.h"
#include "FingerprintController.h"
#include "SearchController.h"
#include "NormalWindow.h"

int main(int argc, char *argv[]) {
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    QApplication app(argc, argv);
    app.setApplicationName("Sabre Browser");
    app.setOrganizationName("Ernest Tech House");
    app.setApplicationVersion("0.1.0.0");

    LangManager langManager;
    SearchController searchController;
    FingerprintController fingerprintController;

    QWebEngineProfile* sessionProfile = new QWebEngineProfile("default", &app);

    QWebEngineScript fpScript;
    fpScript.setName("sabre-fingerprint");
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