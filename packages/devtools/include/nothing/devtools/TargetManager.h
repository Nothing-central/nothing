#pragma once

#include <QObject>
#include <QHash>
#include <QSet>
#include "nothing/devtools/CdpClient.h"
#include "nothing/devtools/DevToolsModels.h"

namespace nothing {
namespace devtools {

/// TargetManager — Manages multiple browser targets (pages, iframes, workers).
/// Uses the Target CDP domain with flattened sessions.
class TargetManager : public QObject {
    Q_OBJECT
public:
    explicit TargetManager(CdpClient* cdp, QObject* parent = nullptr);

    /// Get all known targets.
    QList<TargetInfo> allTargets() const;

    /// Get targets filtered by type.
    QList<TargetInfo> targetsByType(const QString& type) const;

    /// Attach to a specific target by ID.
    void attachToTarget(const QString& targetId, bool flatten = true,
                       std::function<void(const QString& sessionId)> callback = nullptr);

    /// Enable auto-attach to new targets.
    /// @param autoAttach        Enable auto-attach
    /// @param waitForDebugger   Pause new targets until Runtime.runIfWaitingForDebugger
    /// @param flatten           Use flattened sessions (recommended)
    void setAutoAttach(bool autoAttach, bool waitForDebugger, bool flatten,
                      std::function<void()> callback = nullptr);

    /// Detach from a target.
    void detachFromTarget(const QString& sessionId);

    /// Create a new tab/page.
    void createTarget(const QUrl& url, bool newWindow = false, bool forTab = false,
                     std::function<void(const QString& targetId)> callback = nullptr);

    /// Close a target.
    void closeTarget(const QString& targetId);

    /// Activate (focus) a target.
    void activateTarget(const QString& targetId);

    /// Create an isolated browser context (for fingerprint isolation).
    void createBrowserContext(std::function<void(const QString& contextId)> callback);

    /// Dispose a browser context.
    void disposeBrowserContext(const QString& contextId);

    /// Get all browser contexts.
    void getBrowserContexts(std::function<void(const QStringList&)> callback);

    /// Get the list of sessions (sessionId → targetId).
    QHash<QString, QString> sessions() const { return m_sessionToTarget; }

signals:
    void targetCreated(const TargetInfo& info);
    void targetDestroyed(const QString& targetId);
    void targetInfoChanged(const TargetInfo& info);
    void targetCrashed(const QString& targetId, const QString& status, int errorCode);
    void attachedToTarget(const QString& sessionId, const TargetInfo& info, bool waitingForDebugger);
    void detachedFromTarget(const QString& sessionId, const QString& targetId);

private:
    void handleEvent(const QString& method, const QJsonObject& params, const QString& sessionId);

    static TargetInfo parseTargetInfo(const QJsonObject& obj);

    CdpClient* m_cdp;
    QHash<QString, TargetInfo> m_knownTargets;
    QHash<QString, QString> m_sessionToTarget;  // sessionId → targetId
};

} // namespace devtools
} // namespace nothing
