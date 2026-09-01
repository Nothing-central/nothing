#pragma once
#include <QObject>
#include <QString>
#include "identity_manager.h"

QT_BEGIN_NAMESPACE
class QWebEngineProfile;
QT_END_NAMESPACE

class FingerprintController : public QObject {
    Q_OBJECT
public:
    explicit FingerprintController(QObject* parent = nullptr);

    Q_INVOKABLE QString sessionScript(const QString& contextId);
    Q_INVOKABLE QString identityJson(const QString& contextId);
    Q_INVOKABLE QString startIncognito();
    Q_INVOKABLE void endIncognito(const QString& contextId);

    Q_INVOKABLE QString newProfile();
    Q_INVOKABLE QString currentContextId() const;
    void applyToWebProfile(QWebEngineProfile* profile);

signals:
    void profileChanged(const QString& contextId);

private:
    IdentityManager manager_;
    QString m_currentContextId = "default";
    void ensureIdentity(const std::string& contextId);
};