#include "FingerprintController.h"
#include "FingerprintSpoofer.h"
#include "profile_to_json.h"
#include "incognito_session.h"
#include <QJsonObject>
#include <QJsonDocument>
#include <QUuid>
#include <QtWebEngineCore/QWebEngineProfile>
#include <QtWebEngineCore/QWebEngineScript>
#include <QtWebEngineCore/QWebEngineScriptCollection>

namespace {
constexpr int32_t kDefaultScreenW = 1920;
constexpr int32_t kDefaultScreenH = 1080;
}

FingerprintController::FingerprintController(QObject* parent)
    : QObject(parent) {
    manager_.CreateIdentity("default", BrowserFamily::Chrome, OSFamily::Windows,
                             kDefaultScreenW, kDefaultScreenH);
    m_currentContextId = "default";
}

void FingerprintController::ensureIdentity(const std::string& contextId) {
    if (!manager_.HasIdentity(contextId)) {
        manager_.CreateIdentity(contextId, BrowserFamily::Chrome, OSFamily::Windows,
                                 kDefaultScreenW, kDefaultScreenH);
    }
}

QString FingerprintController::sessionScript(const QString& contextId) {
    std::string ctx = contextId.toStdString();
    ensureIdentity(ctx);

    IdentityBundle* b = manager_.GetIdentity(ctx);
    if (!b) return QString();

    FingerprintSpoofer spoofer(b->nav, b->screen, b->webgl, b->audio, b->keys);
    return spoofer.injectionScript();
}

QString FingerprintController::identityJson(const QString& contextId) {
    std::string ctx = contextId.toStdString();
    ensureIdentity(ctx);

    IdentityBundle* b = manager_.GetIdentity(ctx);
    if (!b) return QString("{}");

    QJsonObject root;
    root["contextId"] = QString::fromStdString(b->contextId);
    root["navigator"]  = NavigatorProfileToJson(b->nav);
    root["screen"]     = ScreenProfileToJson(b->screen);
    root["webgl"]      = WebGLProfileToJson(b->webgl);
    root["audio"]      = AudioProfileToJson(b->audio);

    return QString::fromUtf8(
        QJsonDocument(root).toJson(QJsonDocument::Indented)
    );
}

QString FingerprintController::startIncognito() {
    std::string ctx = IncognitoSession::Start(manager_, BrowserFamily::Chrome, OSFamily::Windows,
                                               kDefaultScreenW, kDefaultScreenH);
    return QString::fromStdString(ctx);
}

void FingerprintController::endIncognito(const QString& contextId) {
    IncognitoSession::End(manager_, contextId.toStdString());
}

// ── New methods ──────────────────────────────────────────────────────────────

QString FingerprintController::newProfile() {
    QString newId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    std::string ctx = newId.toStdString();

    manager_.CreateIdentity(ctx, BrowserFamily::Chrome, OSFamily::Windows,
                             kDefaultScreenW, kDefaultScreenH);

    m_currentContextId = newId;
    emit profileChanged(m_currentContextId);
    return m_currentContextId;
}

QString FingerprintController::currentContextId() const {
    return m_currentContextId;
}

void FingerprintController::applyToWebProfile(QWebEngineProfile* profile) {
    if (!profile) return;

    profile->scripts()->clear();

    QWebEngineScript fpScript;
    fpScript.setName("sabre-fingerprint");
    fpScript.setInjectionPoint(QWebEngineScript::DocumentCreation);
    fpScript.setWorldId(QWebEngineScript::MainWorld);
    fpScript.setRunsOnSubFrames(true);
    fpScript.setSourceCode(sessionScript(m_currentContextId));
    profile->scripts()->insert(fpScript);
}