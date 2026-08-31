#pragma once
#include <QObject>
#include <QString>
#include "identity_manager.h"

// QML-facing bridge over IdentityManager/FingerprintSpoofer/IncognitoSession.
// Owns the one IdentityManager for this process. Nothing in packages/profile
// knows QML exists — this is app-layer glue only.
class FingerprintController : public QObject {
    Q_OBJECT
public:
    explicit FingerprintController(QObject* parent = nullptr);

    // One script for the whole session/window — call once, set it on the
    // WebEngineProfile before any tab loads. Creates the identity on first
    // use if it doesn't exist yet.
    Q_INVOKABLE QString sessionScript(const QString& contextId);

    Q_INVOKABLE QString startIncognito();
    Q_INVOKABLE void endIncognito(const QString& contextId);

private:
    IdentityManager manager_;
    void ensureIdentity(const std::string& contextId);
};