#include "FingerprintController.h"
#include "FingerprintSpoofer.h"
#include "incognito_session.h"

namespace {
constexpr int32_t kDefaultScreenW = 1920;
constexpr int32_t kDefaultScreenH = 1080;
}

FingerprintController::FingerprintController(QObject* parent)
    : QObject(parent) {
    manager_.CreateIdentity("default", BrowserFamily::Chrome, OSFamily::Windows,
                             kDefaultScreenW, kDefaultScreenH);
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

QString FingerprintController::startIncognito() {
    std::string ctx = IncognitoSession::Start(manager_, BrowserFamily::Chrome, OSFamily::Windows,
                                               kDefaultScreenW, kDefaultScreenH);
    return QString::fromStdString(ctx);
}

void FingerprintController::endIncognito(const QString& contextId) {
    IncognitoSession::End(manager_, contextId.toStdString());
}