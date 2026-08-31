#pragma once

#include <QObject>
#include <QHash>
#include <QList>
#include "nothing/devtools/CdpClient.h"
#include "nothing/devtools/DevToolsModels.h"

namespace nothing {
namespace devtools {

/// NetworkTracker — Captures all network activity for the DevTools Network tab.
///
/// Subscribes to CDP Network domain events and maintains a live list of
/// all requests, responses, WebSocket connections, and SSE events.
///
/// Enable by calling enable() after the CDP connection is established.
class NetworkTracker : public QObject {
    Q_OBJECT
public:
    explicit NetworkTracker(CdpClient* cdp, QObject* parent = nullptr);

    /// Enable network tracking. Call after CDP is connected.
    /// @param maxTotalBufferSize    Max total bytes for response bodies (default 200MB)
    /// @param maxResourceBufferSize Max bytes per response body (default 20MB)
    /// @param maxPostDataSize       Max bytes for POST body inline (default 1MB)
    /// @param enableDurableMessages Offload body storage to network service
    void enable(int maxTotalBufferSize = 200 * 1024 * 1024,
               int maxResourceBufferSize = 20 * 1024 * 1024,
               int maxPostDataSize = 1024 * 1024,
               bool enableDurableMessages = true);

    /// Disable network tracking.
    void disable();

    /// Get all captured requests (in chronological order).
    QList<NetworkRequest> allRequests() const;

    /// Get a specific request by requestId.
    std::optional<NetworkRequest> request(const QString& requestId) const;

    /// Get all requests matching a URL pattern (substring match).
    QList<NetworkRequest> requestsForUrl(const QString& urlSubstring) const;

    /// Get all WebSocket connections.
    QList<NetworkRequest> webSockets() const;

    /// Get all requests of a specific type (Document, Script, XHR, etc.).
    QList<NetworkRequest> requestsByType(const QString& resourceType) const;

    /// Fetch the response body for a specific request.
    /// @param callback Called with the body bytes and whether it was base64-encoded.
    void getResponseBody(const QString& requestId,
                        std::function<void(const QByteArray&, bool)> callback);

    /// Fetch the POST body for a specific request.
    void getRequestPostData(const QString& requestId,
                           std::function<void(const QByteArray&, bool)> callback);

    /// Set extra HTTP headers to be added to all outgoing requests.
    void setExtraHTTPHeaders(const QHash<QString, QString>& headers);

    /// Clear all extra headers.
    void clearExtraHTTPHeaders();

    /// Disable the HTTP cache for all requests.
    void setCacheDisabled(bool disabled);

    /// Bypass the service worker for all requests.
    void setBypassServiceWorker(bool bypass);

    /// Block URLs matching any of the given patterns (glob-style: * and ?).
    void setBlockedUrls(const QStringList& patterns);

    /// Block a single URL pattern.
    void blockUrl(const QString& pattern);

    /// Unblock a single URL pattern.
    void unblockUrl(const QString& pattern);

    /// Emulate network conditions (throttling).
    /// @param offline              Simulate offline
    /// @param latencyMs            Added latency in milliseconds
    /// @param downloadThroughputBps Download speed in bytes/sec (-1 = no limit)
    /// @param uploadThroughputBps   Upload speed in bytes/sec (-1 = no limit)
    void emulateNetworkConditions(bool offline, double latencyMs,
                                  double downloadThroughputBps,
                                  double uploadThroughputBps);

    /// Clear throttling.
    void clearNetworkConditions();

    /// Clear all captured requests.
    void clearAll();

    /// Get SSE events for a specific request.
    QList<SseEvent> sseEvents(const QString& requestId) const;

signals:
    /// Emitted when a new request is sent.
    void requestWillBeSent(const NetworkRequest& request);

    /// Emitted when response headers are received.
    void responseReceived(const NetworkRequest& request);

    /// Emitted when a chunk of response data arrives.
    void dataReceived(const QString& requestId, qint64 dataLength, qint64 encodedDataLength);

    /// Emitted when a request finishes loading successfully.
    void loadingFinished(const QString& requestId, qint64 encodedDataLength);

    /// Emitted when a request fails.
    void loadingFailed(const QString& requestId, const QString& errorText,
                       bool canceled, const QString& blockedReason,
                       const QString& corsError);

    /// Emitted when a request is served from cache.
    void requestServedFromCache(const QString& requestId);

    /// Emitted when a WebSocket is created.
    void webSocketCreated(const QString& requestId, const QUrl& url);

    /// Emitted when a WebSocket frame is sent.
    void webSocketFrameSent(const QString& requestId, const WebSocketFrame& frame);

    /// Emitted when a WebSocket frame is received.
    void webSocketFrameReceived(const QString& requestId, const WebSocketFrame& frame);

    /// Emitted when a WebSocket is closed.
    void webSocketClosed(const QString& requestId);

    /// Emitted when an SSE message is received.
    void sseMessageReceived(const SseEvent& event);

private:
    void handleEvent(const QString& method, const QJsonObject& params, const QString& sessionId);

    static QList<HttpHeader> parseHeaders(const QJsonObject& obj, const QString& key);
    static QString resourceTypeFromCdp(const QString& type);

    CdpClient* m_cdp;
    QHash<QString, NetworkRequest> m_requests;
    QHash<QString, QList<SseEvent>> m_sseEvents;
    QSet<QString> m_blockedPatterns;
    bool m_enabled = false;
};

} // namespace devtools
} // namespace nothing
