#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QtWebEngineQuick/QtWebEngineQuick>
#include <QUrl>
#include "LangManager.h"
#include "FingerprintController.h"

int main(int argc, char *argv[]) {
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    QtWebEngineQuick::initialize();

    QGuiApplication app(argc, argv);
    app.setApplicationName("Sabre Browser");
    app.setOrganizationName("Ernest Tech House");
    app.setApplicationVersion("0.1.0.0");

    LangManager langManager;
    FingerprintController fingerprintController;

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("Lang", &langManager);
    engine.rootContext()->setContextProperty("Fingerprint", &fingerprintController);
    engine.rootContext()->setContextProperty("isFirstLaunch", langManager.isFirstLaunch());
    engine.rootContext()->setContextProperty("BUILD_MODE", "SABRE");
    engine.rootContext()->setContextProperty("APP_VERSION", "0.1.0.0");

    if (langManager.isFirstLaunch()) {
        engine.load(QUrl::fromLocalFile(
            QString(SOURCE_DIR) + "/ui/pages/SplashScreen.qml"
        ));
    } else {
        engine.load(QUrl::fromLocalFile(
            QString(SOURCE_DIR) + "/ui/pages/NormalWindow.qml"
        ));
    }

    if (engine.rootObjects().isEmpty())
        return -1;

    return app.exec();
}