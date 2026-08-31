#pragma once

#include <QObject>
#include <QHash>
#include <memory>
#include "nothing/devtools/CdpClient.h"
#include "nothing/devtools/NetworkTracker.h"
#include "nothing/devtools/RuntimeTracker.h"
#include "nothing/devtools/DomTracker.h"
#include "nothing/devtools/PageTracker.h"
#include "nothing/devtools/StorageTracker.h"
#include "nothing/devtools/TargetManager.h"

namespace nothing {
namespace devtools {

/// DevToolsServer — The main entry point for the DevTools module.
///
/// Usage:
///   1. Enable remote debugging on your QWebEngineProfile:
///        profile->setRemoteDebuggingPort(9222);
///   2. Create a DevToolsServer:
///        auto* dt = new DevToolsServer(9222, this);
///   3. Connect:
///        dt->connectToBrowser();
///   4. Enable all domains:
///        dt->enableAll();
///   5. Access domain trackers:
///        dt->network()->...
///        dt->runtime()->...
///        dt->dom()->...
///        dt->page()->...
///        dt->storage()->...
///        dt->targets()->...
///
/// The DevToolsServer connects to Chrome's --remote-debugging-port via WebSocket
/// and speaks the Chrome DevTools Protocol (CDP) directly. This gives full
/// access to network capture, DOM inspection, JS runtime, page lifecycle,
/// storage, and target management — the same power as Chrome DevTools.
class DevToolsServer : public QObject {
    Q_OBJECT
public:
    /// Constructor.
    /// @param port  The remote debugging port (set via QWebEngineProfile::setRemoteDebuggingPort)
    /// @param parent  Parent QObject
    explicit DevToolsServer(int port = 9222, QObject* parent = nullptr);
    ~DevToolsServer();

    /// Connect to the browser-level CDP endpoint.
    /// Emits connected() when the WebSocket is established.
    void connectToBrowser();

    /// Disconnect from the CDP endpoint.
    void disconnect();

    /// Check if connected.
    bool isConnected() const;

    /// Enable all domain trackers (Network, Runtime, DOM, Page, Storage).
    /// Call after connected() signal.
    /// @param sessionId  If non-empty, enable for a specific target session
    void enableAll(const QString& sessionId = QString());

    /// Disable all domain trackers.
    /// @param sessionId  If non-empty, disable for a specific target session
    void disableAll(const QString& sessionId = QString());

    /// Enable auto-attach to all new targets with flattened sessions.
    /// Call after connectToBrowser() to automatically attach to all pages,
    /// iframes, and workers. New targets get a sessionId that can be passed
    /// to enableAll(sessionId).
    void enableAutoAttach(bool waitForDebugger = false);

    // === Domain accessors ===

    /// Network tracker — captures all HTTP requests, responses, WebSocket frames.
    NetworkTracker* network() const { return m_network; }

    /// Runtime tracker — JS console, exceptions, evaluate.
    RuntimeTracker* runtime() const { return m_runtime; }

    /// DOM tracker — DOM tree, mutations, CSS inspection.
    DomTracker* dom() const { return m_dom; }

    /// Page tracker — lifecycle events, screenshots, navigation, Find File.
    PageTracker* page() const { return m_page; }

    /// Storage tracker — cookies, localStorage, IndexedDB, CacheStorage.
    StorageTracker* storage() const { return m_storage; }

    /// Target manager — multi-tab, auto-attach, browser contexts.
    TargetManager* targets() const { return m_targets; }

    /// Raw CDP client — for sending custom commands.
    CdpClient* cdp() const { return m_cdp; }

    /// Get the browser target's WebSocket URL.
    QUrl browserUrl() const;

    /// Get the page target's WebSocket URL for a specific targetId.
    QUrl pageUrl(const QString& targetId) const;

    /// Discover the browser WebSocket URL by querying /json/version.
    /// This is the recommended way to find the browser GUID.
    static QUrl discoverBrowserUrl(int port);

signals:
    /// Emitted when the CDP WebSocket connection is established.
    void connected();

    /// Emitted when the CDP WebSocket disconnects.
    void disconnected(const QString& reason);

    /// Emitted when a new target is auto-attached.
    /// Connect to this to call enableAll(sessionId) for each new page.
    void targetAttached(const QString& sessionId, const TargetInfo& info);

private:
    int m_port;
    CdpClient* m_cdp;
    NetworkTracker* m_network;
    RuntimeTracker* m_runtime;
    DomTracker* m_dom;
    PageTracker* m_page;
    StorageTracker* m_storage;
    TargetManager* m_targets;
    QString m_browserGuid;
    bool m_connected = false;
};

} // namespace devtools
} // namespace nothing
