#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QUrl>

int main(int argc, char *argv[]) {
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    QGuiApplication app(argc, argv);
    app.setApplicationName("Sabre Browser");
    app.setOrganizationName("Ernest Tech House");
    app.setApplicationVersion("0.1.0.0");

    QQmlApplicationEngine engine;

    // Expose build mode to QML
    // Later this will be set by CMake build flags
    engine.rootContext()->setContextProperty("BUILD_MODE", "SABRE");
    engine.rootContext()->setContextProperty("APP_VERSION", "0.1.0.0");

    // Start with splash screen
    engine.load(QUrl::fromLocalFile(
        QString(SOURCE_DIR) + "/ui/pages/SplashScreen.qml"
    ));

    if (engine.rootObjects().isEmpty())
        return -1;

    return app.exec();
}
