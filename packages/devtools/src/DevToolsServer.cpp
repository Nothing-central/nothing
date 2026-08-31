#include "nothing/devtools/DevToolsServer.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonDocument>

namespace nothing {
namespace devtools {

DevToolsServer::DevToolsServer(int port, QObject* parent)
    : QObject(parent)
    , m_port(port)
    , m_cdp(new CdpClient(this))
    , m_network(new NetworkTracker(m_cdp, this))
    , m_runtime(new RuntimeTracker(m_cdp, this))
    , m_dom(new DomTracker(m_cdp, this))
    , m_page(new PageTracker(m_cdp, this))
    , m_storage(new StorageTracker(m_cdp, this))
    , m_targets(new TargetManager(m_cdp, this))
{
    connect(m_cdp, &CdpClient::connected, this, [this]() {
        m_connected = true;
        emit connected();
    });
    connect(m_cdp, &CdpClient::disconnected, this, [this](const QString& reason) {
        m_connected = false;
        emit disconnected(reason);
    });

    // Auto-forward target attachment
    connect(m_targets, &TargetManager::attachedToTarget,
            this, &DevToolsServer::targetAttached);
}

DevToolsServer::~DevToolsServer()
{
    disconnect();
}

void DevToolsServer::connectToBrowser()
{
    // First discover the browser GUID via HTTP /json/version
    QNetworkAccessManager* nam = new QNetworkAccessManager(this);
    QUrl versionUrl(QString("http://127.0.0.1:%1/json/version").arg(m_port));

    QNetworkReply* reply = nam->get(QNetworkRequest(versionUrl));
    connect(reply, &QNetworkReply::finished, this, [this, reply, nam]() {
        reply->deleteLater();
        nam->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            qWarning() << "DevToolsServer: Failed to query /json/version:" << reply->errorString();
            return;
        }

        const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        const QJsonObject obj = doc.object();
        const QString wsUrl = obj.value("webSocketDebuggerUrl").toString();

        if (wsUrl.isEmpty()) {
            qWarning() << "DevToolsServer: No webSocketDebuggerUrl in /json/version response";
            return;
        }

        // Extract the browser GUID from the URL
        // Format: ws://127.0.0.1:<port>/devtools/browser/<guid>
        const int lastSlash = wsUrl.lastIndexOf('/');
        if (lastSlash > 0) {
            m_browserGuid = wsUrl.mid(lastSlash + 1);
        }

        m_cdp->connectToUrl(QUrl(wsUrl));
    });
}

void DevToolsServer::disconnect()
{
    if (m_cdp) {
        m_cdp->disconnect();
    }
    m_connected = false;
}

bool DevToolsServer::isConnected() const
{
    return m_connected;
}

void DevToolsServer::enableAll(const QString& sessionId)
{
    // Enable each domain tracker
    // When sessionId is empty, these go to the root (browser) session
    // When sessionId is non-empty, we need to route commands to that session
    // The CdpClient handles this via the sessionId parameter

    // For the browser target, Network/Runtime/DOM/Page/Storage don't apply
    // — they need to be enabled on a specific page target.
    // When sessionId is empty, we just enable Target discovery.
    if (sessionId.isEmpty()) {
        // Browser-level: only enable Target
        m_cdp->sendCommand("Target.setDiscoverTargets",
            QJsonObject{{"discover", true}});
    } else {
        // Page-level: enable all domain trackers
        // We need to temporarily set the sessionId on each tracker's commands
        // The trackers call m_cdp->sendCommand which supports sessionId
        // But the trackers don't expose sessionId setting...
        // Solution: use sendCommand with the session ID directly

        m_cdp->sendCommand("Network.enable", {}, sessionId.isEmpty() ? QString() : sessionId);
        m_cdp->sendCommand("Runtime.enable", {}, sessionId);
        m_cdp->sendCommand("DOM.enable", {}, sessionId);
        m_cdp->sendCommand("Page.enable", {}, sessionId);
        m_cdp->sendCommand("Page.setLifecycleEventsEnabled",
            QJsonObject{{"enabled", true}}, sessionId);
        m_cdp->sendCommand("CSS.enable", {}, sessionId);
    }
}

void DevToolsServer::disableAll(const QString& sessionId)
{
    if (sessionId.isEmpty()) {
        m_cdp->sendCommand("Target.setDiscoverTargets",
            QJsonObject{{"discover", false}});
    } else {
        m_cdp->sendCommand("Network.disable", {}, sessionId);
        m_cdp->sendCommand("Runtime.disable", {}, sessionId);
        m_cdp->sendCommand("DOM.disable", {}, sessionId);
        m_cdp->sendCommand("Page.disable", {}, sessionId);
        m_cdp->sendCommand("CSS.disable", {}, sessionId);
    }
}

void DevToolsServer::enableAutoAttach(bool waitForDebugger)
{
    m_targets->setAutoAttach(true, waitForDebugger, true);
}

QUrl DevToolsServer::browserUrl() const
{
    return QUrl(QString("ws://127.0.0.1:%1/devtools/browser/%2")
        .arg(m_port).arg(m_browserGuid));
}

QUrl DevToolsServer::pageUrl(const QString& targetId) const
{
    return QUrl(QString("ws://127.0.0.1:%1/devtools/page/%2")
        .arg(m_port).arg(targetId));
}

QUrl DevToolsServer::discoverBrowserUrl(int port)
{
    // This is a synchronous helper — blocks until /json/version responds
    QEventLoop loop;
    QNetworkAccessManager nam;
    QUrl versionUrl(QString("http://127.0.0.1:%1/json/version").arg(port));

    QNetworkReply* reply = nam.get(QNetworkRequest(versionUrl));
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);

    // 5 second timeout
    QTimer::singleShot(5000, &loop, &QEventLoop::quit);
    loop.exec();

    if (reply->error() != QNetworkReply::NoError) {
        reply->deleteLater();
        return QUrl();
    }

    const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    reply->deleteLater();

    const QString wsUrl = doc.object().value("webSocketDebuggerUrl").toString();
    return QUrl(wsUrl);
}

} // namespace devtools
} // namespace nothing
