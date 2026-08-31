#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QtWebEngineQuick/QtWebEngineQuick>
#include <QUrl>

int main(int argc, char *argv[]) {
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    // Must be called before QGuiApplication
    QtWebEngineQuick::initialize();

    QGuiApplication app(argc, argv);
    app.setApplicationName("Sabre Browser");
    app.setOrganizationName("Ernest Tech House");
    app.setApplicationVersion("0.1.0.0");

    QQmlApplicationEngine engine;

    engine.rootContext()->setContextProperty("BUILD_MODE", "SABRE");
    engine.rootContext()->setContextProperty("APP_VERSION", "0.1.0.0");

    engine.load(QUrl::fromLocalFile(
        QString(SOURCE_DIR) + "/ui/pages/SplashScreen.qml"
    ));

    if (engine.rootObjects().isEmpty())
        return -1;

    return app.exec();
}