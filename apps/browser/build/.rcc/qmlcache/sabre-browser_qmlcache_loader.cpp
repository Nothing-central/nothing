#include <QtQml/qqmlprivate.h>
#include <QtCore/qdir.h>
#include <QtCore/qurl.h>
#include <QtCore/qhash.h>
#include <QtCore/qstring.h>

namespace QmlCacheGeneratedCode {
namespace _0x5f_SabreBrowser_ui_pages_SplashScreen_qml { 
    extern const unsigned char qmlData[];
    extern const QQmlPrivate::AOTCompiledFunction aotBuiltFunctions[];
    const QQmlPrivate::CachedQmlUnit unit = {
        reinterpret_cast<const QV4::CompiledData::Unit*>(&qmlData), &aotBuiltFunctions[0], nullptr
    };
}
namespace _0x5f_SabreBrowser_ui_pages_NormalWindow_qml { 
    extern const unsigned char qmlData[];
    extern const QQmlPrivate::AOTCompiledFunction aotBuiltFunctions[];
    const QQmlPrivate::CachedQmlUnit unit = {
        reinterpret_cast<const QV4::CompiledData::Unit*>(&qmlData), &aotBuiltFunctions[0], nullptr
    };
}
namespace _0x5f_SabreBrowser_ui_pages_IncognitoWindow_qml { 
    extern const unsigned char qmlData[];
    extern const QQmlPrivate::AOTCompiledFunction aotBuiltFunctions[];
    const QQmlPrivate::CachedQmlUnit unit = {
        reinterpret_cast<const QV4::CompiledData::Unit*>(&qmlData), &aotBuiltFunctions[0], nullptr
    };
}
namespace _0x5f_SabreBrowser_ui_components_Toolbar_qml { 
    extern const unsigned char qmlData[];
    extern const QQmlPrivate::AOTCompiledFunction aotBuiltFunctions[];
    const QQmlPrivate::CachedQmlUnit unit = {
        reinterpret_cast<const QV4::CompiledData::Unit*>(&qmlData), &aotBuiltFunctions[0], nullptr
    };
}
namespace _0x5f_SabreBrowser_ui_components_TabBar_qml { 
    extern const unsigned char qmlData[];
    extern const QQmlPrivate::AOTCompiledFunction aotBuiltFunctions[];
    const QQmlPrivate::CachedQmlUnit unit = {
        reinterpret_cast<const QV4::CompiledData::Unit*>(&qmlData), &aotBuiltFunctions[0], nullptr
    };
}
namespace _0x5f_SabreBrowser_ui_components_AddressBar_qml { 
    extern const unsigned char qmlData[];
    extern const QQmlPrivate::AOTCompiledFunction aotBuiltFunctions[];
    const QQmlPrivate::CachedQmlUnit unit = {
        reinterpret_cast<const QV4::CompiledData::Unit*>(&qmlData), &aotBuiltFunctions[0], nullptr
    };
}
namespace _0x5f_SabreBrowser_ui_components_NewTabPage_qml { 
    extern const unsigned char qmlData[];
    extern const QQmlPrivate::AOTCompiledFunction aotBuiltFunctions[];
    const QQmlPrivate::CachedQmlUnit unit = {
        reinterpret_cast<const QV4::CompiledData::Unit*>(&qmlData), &aotBuiltFunctions[0], nullptr
    };
}

}
namespace {
struct Registry {
    Registry();
    ~Registry();
    QHash<QString, const QQmlPrivate::CachedQmlUnit*> resourcePathToCachedUnit;
    static const QQmlPrivate::CachedQmlUnit *lookupCachedUnit(const QUrl &url);
};

Q_GLOBAL_STATIC(Registry, unitRegistry)


Registry::Registry() {
    resourcePathToCachedUnit.insert(QStringLiteral("/SabreBrowser/ui/pages/SplashScreen.qml"), &QmlCacheGeneratedCode::_0x5f_SabreBrowser_ui_pages_SplashScreen_qml::unit);
    resourcePathToCachedUnit.insert(QStringLiteral("/SabreBrowser/ui/pages/NormalWindow.qml"), &QmlCacheGeneratedCode::_0x5f_SabreBrowser_ui_pages_NormalWindow_qml::unit);
    resourcePathToCachedUnit.insert(QStringLiteral("/SabreBrowser/ui/pages/IncognitoWindow.qml"), &QmlCacheGeneratedCode::_0x5f_SabreBrowser_ui_pages_IncognitoWindow_qml::unit);
    resourcePathToCachedUnit.insert(QStringLiteral("/SabreBrowser/ui/components/Toolbar.qml"), &QmlCacheGeneratedCode::_0x5f_SabreBrowser_ui_components_Toolbar_qml::unit);
    resourcePathToCachedUnit.insert(QStringLiteral("/SabreBrowser/ui/components/TabBar.qml"), &QmlCacheGeneratedCode::_0x5f_SabreBrowser_ui_components_TabBar_qml::unit);
    resourcePathToCachedUnit.insert(QStringLiteral("/SabreBrowser/ui/components/AddressBar.qml"), &QmlCacheGeneratedCode::_0x5f_SabreBrowser_ui_components_AddressBar_qml::unit);
    resourcePathToCachedUnit.insert(QStringLiteral("/SabreBrowser/ui/components/NewTabPage.qml"), &QmlCacheGeneratedCode::_0x5f_SabreBrowser_ui_components_NewTabPage_qml::unit);
    QQmlPrivate::RegisterQmlUnitCacheHook registration;
    registration.structVersion = 0;
    registration.lookupCachedQmlUnit = &lookupCachedUnit;
    QQmlPrivate::qmlregister(QQmlPrivate::QmlUnitCacheHookRegistration, &registration);
}

Registry::~Registry() {
    QQmlPrivate::qmlunregister(QQmlPrivate::QmlUnitCacheHookRegistration, quintptr(&lookupCachedUnit));
}

const QQmlPrivate::CachedQmlUnit *Registry::lookupCachedUnit(const QUrl &url) {
    if (url.scheme() != QLatin1String("qrc"))
        return nullptr;
    QString resourcePath = QDir::cleanPath(url.path());
    if (resourcePath.isEmpty())
        return nullptr;
    if (!resourcePath.startsWith(QLatin1Char('/')))
        resourcePath.prepend(QLatin1Char('/'));
    return unitRegistry()->resourcePathToCachedUnit.value(resourcePath, nullptr);
}
}
int QT_MANGLE_NAMESPACE(qInitResources_qmlcache_sabre_browser)() {
    ::unitRegistry();
    return 1;
}
Q_CONSTRUCTOR_FUNCTION(QT_MANGLE_NAMESPACE(qInitResources_qmlcache_sabre_browser))
int QT_MANGLE_NAMESPACE(qCleanupResources_qmlcache_sabre_browser)() {
    return 1;
}
