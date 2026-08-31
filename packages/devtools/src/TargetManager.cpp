#include "nothing/devtools/TargetManager.h"
#include <QJsonArray>

namespace nothing {
namespace devtools {

TargetManager::TargetManager(CdpClient* cdp, QObject* parent)
    : QObject(parent)
    , m_cdp(cdp)
{
    connect(m_cdp, &CdpClient::eventReceived,
            this, &TargetManager::handleEvent);
}

QList<TargetInfo> TargetManager::allTargets() const
{
    return m_knownTargets.values();
}

QList<TargetInfo> TargetManager::targetsByType(const QString& type) const
{
    QList<TargetInfo> result;
    for (const TargetInfo& t : m_knownTargets) {
        if (t.type == type) result.append(t);
    }
    return result;
}

void TargetManager::attachToTarget(const QString& targetId, bool flatten,
                                 std::function<void(const QString&)> callback)
{
    QJsonObject params;
    params["targetId"] = targetId;
    params["flatten"] = flatten;
    m_cdp->sendCommand("Target.attachToTarget", params, [callback](const QJsonObject& result) {
        if (callback) callback(result.value("sessionId").toString());
    });
}

void TargetManager::setAutoAttach(bool autoAttach, bool waitForDebugger, bool flatten,
                                 std::function<void()> callback)
{
    QJsonObject params;
    params["autoAttach"] = autoAttach;
    params["waitForDebuggerOnStart"] = waitForDebugger;
    params["flatten"] = flatten;
    m_cdp->sendCommand("Target.setAutoAttach", params, [callback](const QJsonObject&) {
        if (callback) callback();
    });
}

void TargetManager::detachFromTarget(const QString& sessionId)
{
    QJsonObject params;
    params["sessionId"] = sessionId;
    m_cdp->sendCommand("Target.detachFromTarget", params);
}

void TargetManager::createTarget(const QUrl& url, bool newWindow, bool forTab,
                                std::function<void(const QString&)> callback)
{
    QJsonObject params;
    params["url"] = url.toString();
    if (newWindow) params["newWindow"] = true;
    if (forTab) params["forTab"] = true;
    m_cdp->sendCommand("Target.createTarget", params, [callback](const QJsonObject& result) {
        if (callback) callback(result.value("targetId").toString());
    });
}

void TargetManager::closeTarget(const QString& targetId)
{
    QJsonObject params;
    params["targetId"] = targetId;
    m_cdp->sendCommand("Target.closeTarget", params);
}

void TargetManager::activateTarget(const QString& targetId)
{
    QJsonObject params;
    params["targetId"] = targetId;
    m_cdp->sendCommand("Target.activateTarget", params);
}

void TargetManager::createBrowserContext(std::function<void(const QString&)> callback)
{
    m_cdp->sendCommand("Target.createBrowserContext", {}, [callback](const QJsonObject& result) {
        if (callback) callback(result.value("browserContextId").toString());
    });
}

void TargetManager::disposeBrowserContext(const QString& contextId)
{
    QJsonObject params;
    params["browserContextId"] = contextId;
    m_cdp->sendCommand("Target.disposeBrowserContext", params);
}

void TargetManager::getBrowserContexts(std::function<void(const QStringList&)> callback)
{
    m_cdp->sendCommand("Target.getBrowserContexts", {}, [callback](const QJsonObject& result) {
        QStringList contexts;
        const QJsonArray arr = result.value("browserContextIds").toArray();
        for (const QJsonValue& v : arr) contexts.append(v.toString());
        if (callback) callback(contexts);
    });
}

void TargetManager::handleEvent(const QString& method, const QJsonObject& params, const QString&)
{
    if (method == "Target.targetCreated") {
        const TargetInfo info = parseTargetInfo(params.value("targetInfo").toObject());
        m_knownTargets[info.targetId] = info;
        emit targetCreated(info);
    }
    else if (method == "Target.targetDestroyed") {
        const QString targetId = params.value("targetId").toString();
        m_knownTargets.remove(targetId);
        emit targetDestroyed(targetId);
    }
    else if (method == "Target.targetInfoChanged") {
        const TargetInfo info = parseTargetInfo(params.value("targetInfo").toObject());
        m_knownTargets[info.targetId] = info;
        emit targetInfoChanged(info);
    }
    else if (method == "Target.targetCrashed") {
        emit targetCrashed(params.value("targetId").toString(),
                          params.value("status").toString(),
                          params.value("errorCode").toInt());
    }
    else if (method == "Target.attachedToTarget") {
        const QString sessionId = params.value("sessionId").toString();
        const TargetInfo info = parseTargetInfo(params.value("targetInfo").toObject());
        const bool waiting = params.value("waitingForDebugger").toBool();
        m_sessionToTarget[sessionId] = info.targetId;
        m_knownTargets[info.targetId] = info;
        emit attachedToTarget(sessionId, info, waiting);
    }
    else if (method == "Target.detachedFromTarget") {
        const QString sessionId = params.value("sessionId").toString();
        const QString targetId = params.value("targetId").toString();
        m_sessionToTarget.remove(sessionId);
        emit detachedFromTarget(sessionId, targetId);
    }
}

TargetInfo TargetManager::parseTargetInfo(const QJsonObject& obj)
{
    TargetInfo info;
    info.targetId = obj.value("targetId").toString();
    info.type = obj.value("type").toString();
    info.title = obj.value("title").toString();
    info.url = QUrl(obj.value("url").toString());
    info.parentId = obj.value("parentId").toString();
    info.openerId = obj.value("openerId").toString();
    info.parentFrameId = obj.value("parentFrameId").toString();
    info.browserContextId = obj.value("browserContextId").toString();
    info.attached = obj.value("attached").toBool(false);
    info.canAccessOpener = obj.value("canAccessOpener").toBool(false);
    return info;
}

} // namespace devtools
} // namespace nothing
