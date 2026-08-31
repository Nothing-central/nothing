#pragma once

#include <QObject>
#include <QWebSocket>
#include <QHash>
#include <QString>
#include <QJsonObject>
#include <QJsonArray>
#include <functional>
#include < QTimer>

namespace nothing {
namespace devtools {

/// CdpClient — Low-level Chrome DevTools Protocol client.
///
/// Connects to the browser's --remote-debugging-port WebSocket endpoint
/// and provides:
///   - sendCommand(method, params) → response via callback
///   - eventReceived signal for all CDP events
///   - sessionId support for flattened multi-target sessions
///
/// The browser must be launched with QWebEngineProfile::setRemoteDebuggingPort(port)
/// (which internally passes --remote-debugging-port=port to the Chromium engine).
///
/// Connection URL patterns:
///   Browser target:  ws://127.0.0.1:<port>/devtools/browser
///   Page target:     ws://127.0.0.1:<port>/devtools/page/<targetId>
///
/// For flattened sessions (recommended), connect to the browser target and
/// use Target.setAutoAttach + Target.attachToTarget with flatten=true.
/// All child-target messages carry a "sessionId" field.
class CdpClient : public QObject {
    Q_OBJECT
public:
    explicit CdpClient(QObject* parent = nullptr);
    ~CdpClient();

    /// Connect to a CDP WebSocket endpoint.
    /// For browser target: ws://127.0.0.1:<port>/devtools/browser
    /// For page target:   ws://127.0.0.1:<port>/devtools/page/<id>
    void connectToUrl(const QUrl& url);

    /// Disconnect from the CDP endpoint.
    void disconnect();

    /// Check if the WebSocket is connected.
    bool isConnected() const;

    /// Send a CDP command with a callback for the response.
    /// @param method  e.g. "Network.enable", "Runtime.evaluate"
    /// @param params  JSON parameters (can be empty)
    /// @param callback Called with the "result" object from the response
    /// @param sessionId For flattened sessions, the child session ID
    /// @return The command ID (for tracking)
    int sendCommand(const QString& method,
                    const QJsonObject& params,
                    std::function<void(const QJsonObject&)> callback,
                    const QString& sessionId = QString());

    /// Send a CDP command without waiting for a response (fire-and-forget).
    int sendCommand(const QString& method,
                    const QJsonObject& params = {},
                    const QString& sessionId = QString());

    /// Wait for a command to complete (blocking, with timeout).
    /// Returns the result object, or empty if timeout/error.
    QJsonObject sendCommandSync(const QString& method,
                                const QJsonObject& params = {},
                                const QString& sessionId = QString(),
                                int timeoutMs = 10000);

signals:
    /// Emitted when the WebSocket connection is established.
    void connected();

    /// Emitted when the WebSocket disconnects.
    void disconnected(const QString& reason);

    /// Emitted for every CDP event.
    /// @param method   e.g. "Network.requestWillBeSent"
    /// @param params   The event parameters
    /// @param sessionId The session that received this event (empty for root)
    void eventReceived(const QString& method,
                       const QJsonObject& params,
                       const QString& sessionId);

    /// Emitted when a command response arrives (in addition to the callback).
    void commandResponse(int id, const QJsonObject& result);

    /// Emitted when a command fails.
    void commandError(int id, const QJsonObject& error);

private slots:
    void onTextMessageReceived(const QString& message);
    void onConnected();
    void onDisconnected();

private:
    QWebSocket* m_socket;
    int m_nextId = 1;
    QHash<int, std::function<void(const QJsonObject&)>> m_callbacks;
    QUrl m_url;
};

} // namespace devtools
} // namespace nothing
