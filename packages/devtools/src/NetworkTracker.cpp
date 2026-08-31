#include "nothing/devtools/NetworkTracker.h"
#include <QJsonArray>

namespace nothing {
namespace devtools {

NetworkTracker::NetworkTracker(CdpClient* cdp, QObject* parent)
    : QObject(parent)
    , m_cdp(cdp)
{
    connect(m_cdp, &CdpClient::eventReceived,
            this, &NetworkTracker::handleEvent);
}

void NetworkTracker::enable(int maxTotalBufferSize, int maxResourceBufferSize,
                           int maxPostDataSize, bool enableDurableMessages)
{
    if (m_enabled) return;
    m_enabled = true;

    QJsonObject params;
    params["maxTotalBufferSize"] = maxTotalBufferSize;
    params["maxResourceBufferSize"] = maxResourceBufferSize;
    params["maxPostDataSize"] = maxPostDataSize;
    if (enableDurableMessages) {
        params["enableDurableMessages"] = true;
    }
    m_cdp->sendCommand("Network.enable", params);
}

void NetworkTracker::disable()
{
    if (!m_enabled) return;
    m_enabled = false;
    m_cdp->sendCommand("Network.disable");
}

QList<NetworkRequest> NetworkTracker::allRequests() const
{
    return m_requests.values();
}

std::optional<NetworkRequest> NetworkTracker::request(const QString& requestId) const
{
    auto it = m_requests.find(requestId);
    if (it != m_requests.end()) {
        return it.value();
    }
    return std::nullopt;
}

QList<NetworkRequest> NetworkTracker::requestsForUrl(const QString& urlSubstring) const
{
    QList<NetworkRequest> result;
    for (const NetworkRequest& r : m_requests) {
        if (r.request.url.toString().contains(urlSubstring, Qt::CaseInsensitive)) {
            result.append(r);
        }
    }
    return result;
}

QList<NetworkRequest> NetworkTracker::webSockets() const
{
    QList<NetworkRequest> result;
    for (const NetworkRequest& r : m_requests) {
        if (r.isWebSocket) result.append(r);
    }
    return result;
}

QList<NetworkRequest> NetworkTracker::requestsByType(const QString& resourceType) const
{
    QList<NetworkRequest> result;
    for (const NetworkRequest& r : m_requests) {
        if (r.request.resourceType == resourceType) result.append(r);
    }
    return result;
}

void NetworkTracker::getResponseBody(const QString& requestId,
                                    std::function<void(const QByteArray&, bool)> callback)
{
    QJsonObject params;
    params["requestId"] = requestId;
    m_cdp->sendCommand("Network.getResponseBody", params, [callback](const QJsonObject& result) {
        const QString body = result.value("body").toString();
        const bool base64 = result.value("base64Encoded").toBool(false);
        if (base64) {
            callback(QByteArray::fromBase64(body.toUtf8()), true);
        } else {
            callback(body.toUtf8(), false);
        }
    });
}

void NetworkTracker::getRequestPostData(const QString& requestId,
                                       std::function<void(const QByteArray&, bool)> callback)
{
    QJsonObject params;
    params["requestId"] = requestId;
    m_cdp->sendCommand("Network.getRequestPostData", params, [callback](const QJsonObject& result) {
        const QString body = result.value("postData").toString();
        const bool base64 = result.value("base64Encoded").toBool(false);
        if (base64) {
            callback(QByteArray::fromBase64(body.toUtf8()), true);
        } else {
            callback(body.toUtf8(), false);
        }
    });
}

void NetworkTracker::setExtraHTTPHeaders(const QHash<QString, QString>& headers)
{
    QJsonObject headersObj;
    for (auto it = headers.begin(); it != headers.end(); ++it) {
        headersObj[it.key()] = it.value();
    }
    QJsonObject params;
    params["headers"] = headersObj;
    m_cdp->sendCommand("Network.setExtraHTTPHeaders", params);
}

void NetworkTracker::clearExtraHTTPHeaders()
{
    setExtraHTTPHeaders({});
}

void NetworkTracker::setCacheDisabled(bool disabled)
{
    QJsonObject params;
    params["cacheDisabled"] = disabled;
    m_cdp->sendCommand("Network.setCacheDisabled", params);
}

void NetworkTracker::setBypassServiceWorker(bool bypass)
{
    QJsonObject params;
    params["bypass"] = bypass;
    m_cdp->sendCommand("Network.setBypassServiceWorker", params);
}

void NetworkTracker::setBlockedUrls(const QStringList& patterns)
{
    // Build block patterns array
    QJsonArray arr;
    for (const QString& p : patterns) {
        QJsonObject pattern;
        pattern["urlPattern"] = p;
        pattern["block"] = true;
        arr.append(pattern);
    }
    QJsonObject params;
    params["urlPatterns"] = arr;
    m_cdp->sendCommand("Network.setBlockedURLs", params);
    m_blockedPatterns = QSet<QString>(patterns.begin(), patterns.end());
}

void NetworkTracker::blockUrl(const QString& pattern)
{
    m_blockedPatterns.insert(pattern);
    setBlockedUrls(m_blockedPatterns.values());
}

void NetworkTracker::unblockUrl(const QString& pattern)
{
    m_blockedPatterns.remove(pattern);
    setBlockedUrls(m_blockedPatterns.values());
}

void NetworkTracker::emulateNetworkConditions(bool offline, double latencyMs,
                                              double downloadThroughputBps,
                                              double uploadThroughputBps)
{
    QJsonObject params;
    params["offline"] = offline;
    params["latency"] = latencyMs;
    params["downloadThroughput"] = downloadThroughputBps;
    params["uploadThroughput"] = uploadThroughputBps;
    m_cdp->sendCommand("Network.emulateNetworkConditions", params);
}

void NetworkTracker::clearNetworkConditions()
{
    emulateNetworkConditions(false, 0, -1, -1);
}

void NetworkTracker::clearAll()
{
    m_requests.clear();
    m_sseEvents.clear();
}

QList<SseEvent> NetworkTracker::sseEvents(const QString& requestId) const
{
    return m_sseEvents.value(requestId);
}

// === Private: Event handling ===

void NetworkTracker::handleEvent(const QString& method, const QJsonObject& params, const QString&)
{
    if (method == "Network.requestWillBeSent") {
        const QString reqId = params.value("requestId").toString();
        NetworkRequest& nr = m_requests[reqId];
        nr.requestId = reqId;
        nr.request.requestId = reqId;
        nr.request.loaderId = params.value("loaderId").toString();
        nr.request.frameId = params.value("frameId").toString();
        nr.request.documentUrl = params.value("documentURL").toString();

        const QJsonObject reqObj = params.value("request").toObject();
        nr.request.url = QUrl(reqObj.value("url").toString());
        nr.request.method = reqObj.value("method").toString();
        nr.request.headers = parseHeaders(reqObj, "headers");
        nr.request.timestamp = params.value("timestamp").toDouble();
        nr.request.wallTime = params.value("wallTime").toDouble();
        nr.request.resourceType = resourceTypeFromCdp(params.value("type").toString());
        nr.request.hasUserGesture = params.value("hasUserGesture").toBool(false);
        nr.request.referrerPolicy = reqObj.value("referrerPolicy").toString();
        nr.request.initialPriority = reqObj.value("initialPriority").toString();
        nr.request.mixedContentType = reqObj.value("mixedContentType").toString();

        if (reqObj.contains("postData")) {
            nr.request.postData = reqObj.value("postData").toString();
            nr.request.hasPostData = true;
        }
        if (reqObj.value("hasPostData").toBool(false)) {
            nr.request.hasPostData = true;
        }

        // Initiator
        const QJsonObject init = params.value("initiator").toObject();
        nr.request.initiatorType = init.value("type").toString();
        nr.request.initiatorUrl = init.value("url").toString();
        nr.request.initiatorLine = init.value("lineNumber").toInt();

        // redirectResponse (if this is a redirect)
        if (params.contains("redirectResponse")) {
            nr.isRedirect = true;
            // The previous hop's response is in redirectResponse
            // The requestId is reused for the new URL
        }

        // Check if this is a WebSocket or EventSource
        if (nr.request.resourceType == "WebSocket") nr.isWebSocket = true;
        if (nr.request.resourceType == "EventSource") nr.isEventSource = true;

        emit requestWillBeSent(nr);
    }
    else if (method == "Network.responseReceived") {
        const QString reqId = params.value("requestId").toString();
        NetworkRequest& nr = m_requests[reqId];
        nr.response.requestId = reqId;

        const QJsonObject resp = params.value("response").toObject();
        nr.response.url = QUrl(resp.value("url").toString());
        nr.response.status = resp.value("status").toInt();
        nr.response.statusText = resp.value("statusText").toString();
        nr.response.headers = parseHeaders(resp, "headers");
        nr.response.mimeType = resp.value("mimeType").toString();
        nr.response.protocol = resp.value("protocol").toString();
        nr.response.remoteIPAddress = resp.value("remoteIPAddress").toString();
        nr.response.remotePort = resp.value("remotePort").toInt();
        nr.response.fromDiskCache = resp.value("fromDiskCache").toBool(false);
        nr.response.fromServiceWorker = resp.value("fromServiceWorker").toBool(false);
        nr.response.fromPrefetchCache = resp.value("fromPrefetchCache").toBool(false);
        nr.response.encodedDataLength = static_cast<qint64>(resp.value("encodedDataLength").toDouble());
        nr.response.securityState = resp.value("securityState").toString();
        nr.response.responseTime = resp.value("responseTime").toDouble();

        // Security details
        if (resp.contains("securityDetails")) {
            const QJsonObject sd = resp.value("securityDetails").toObject();
            nr.response.securityProtocol = sd.value("protocol").toString();
            nr.response.securityCipher = sd.value("cipher").toString();
            nr.response.securitySubjectName = sd.value("subjectName").toString();
            nr.response.securityIssuer = sd.value("issuer").toString();
            nr.response.securityValidFrom = sd.value("validFrom").toDouble();
            nr.response.securityValidTo = sd.value("validTo").toDouble();
        }

        // Timing
        if (resp.contains("timing")) {
            const QJsonObject t = resp.value("timing").toObject();
            nr.timing.requestTime = t.value("requestTime").toDouble();
            nr.timing.proxyStart = t.value("proxyStart").toDouble(-1);
            nr.timing.proxyEnd = t.value("proxyEnd").toDouble(-1);
            nr.timing.dnsStart = t.value("dnsStart").toDouble(-1);
            nr.timing.dnsEnd = t.value("dnsEnd").toDouble(-1);
            nr.timing.connectStart = t.value("connectStart").toDouble(-1);
            nr.timing.connectEnd = t.value("connectEnd").toDouble(-1);
            nr.timing.sslStart = t.value("sslStart").toDouble(-1);
            nr.timing.sslEnd = t.value("sslEnd").toDouble(-1);
            nr.timing.sendStart = t.value("sendStart").toDouble(-1);
            nr.timing.sendEnd = t.value("sendEnd").toDouble(-1);
            nr.timing.receiveHeadersStart = t.value("receiveHeadersStart").toDouble(-1);
            nr.timing.receiveHeadersEnd = t.value("receiveHeadersEnd").toDouble(-1);
        }

        // Service worker response source
        if (resp.contains("serviceWorkerResponseSource")) {
            nr.response.serviceWorkerResponseSource = resp.value("serviceWorkerResponseSource").toString();
        }

        emit responseReceived(nr);
    }
    else if (method == "Network.dataReceived") {
        const QString reqId = params.value("requestId").toString();
        NetworkRequest& nr = m_requests[reqId];
        nr.dataLength += params.value("dataLength").toInt();
        nr.encodedDataLength += params.value("encodedDataLength").toInt();
        nr.decodedBodySize += params.value("dataLength").toInt();
        emit dataReceived(reqId, params.value("dataLength").toInt(),
                         params.value("encodedDataLength").toInt());
    }
    else if (method == "Network.loadingFinished") {
        const QString reqId = params.value("requestId").toString();
        NetworkRequest& nr = m_requests[reqId];
        nr.finished = true;
        nr.encodedDataLength = static_cast<qint64>(params.value("encodedDataLength").toDouble());
        emit loadingFinished(reqId, nr.encodedDataLength);
    }
    else if (method == "Network.loadingFailed") {
        const QString reqId = params.value("requestId").toString();
        NetworkRequest& nr = m_requests[reqId];
        nr.failed = true;
        nr.errorText = params.value("errorText").toString();
        nr.canceled = params.value("canceled").toBool(false);
        if (params.contains("blockedReason")) {
            nr.blockedReason = params.value("blockedReason").toString();
        }
        if (params.contains("corsErrorStatus")) {
            nr.corsError = params.value("corsErrorStatus").toObject().value("corsError").toString();
        }
        emit loadingFailed(reqId, nr.errorText, nr.canceled, nr.blockedReason, nr.corsError);
    }
    else if (method == "Network.requestServedFromCache") {
        const QString reqId = params.value("requestId").toString();
        NetworkRequest& nr = m_requests[reqId];
        nr.response.fromDiskCache = true;
        emit requestServedFromCache(reqId);
    }
    // === WebSocket events ===
    else if (method == "Network.webSocketCreated") {
        const QString reqId = params.value("requestId").toString();
        NetworkRequest& nr = m_requests[reqId];
        nr.isWebSocket = true;
        nr.request.url = QUrl(params.value("url").toString());
        emit webSocketCreated(reqId, nr.request.url);
    }
    else if (method == "Network.webSocketWillSendHandshakeRequest") {
        const QString reqId = params.value("requestId").toString();
        NetworkRequest& nr = m_requests[reqId];
        nr.request.timestamp = params.value("timestamp").toDouble();
        nr.request.wallTime = params.value("wallTime").toDouble();
        const QJsonObject reqObj = params.value("request").toObject();
        nr.request.headers = parseHeaders(reqObj, "headers");
    }
    else if (method == "Network.webSocketHandshakeResponseReceived") {
        const QString reqId = params.value("requestId").toString();
        NetworkRequest& nr = m_requests[reqId];
        const QJsonObject resp = params.value("response").toObject();
        nr.response.status = resp.value("status").toInt();
        nr.response.statusText = resp.value("statusText").toString();
        nr.response.headers = parseHeaders(resp, "headers");
    }
    else if (method == "Network.webSocketFrameSent") {
        const QString reqId = params.value("requestId").toString();
        NetworkRequest& nr = m_requests[reqId];
        const QJsonObject frame = params.value("response").toObject();
        WebSocketFrame wf;
        wf.direction = WebSocketFrame::Sent;
        wf.opcode = frame.value("opcode").toInt();
        wf.masked = frame.value("mask").toBool();
        wf.payloadData = frame.value("payloadData").toString();
        wf.timestamp = params.value("timestamp").toDouble();
        emit webSocketFrameSent(reqId, wf);
    }
    else if (method == "Network.webSocketFrameReceived") {
        const QString reqId = params.value("requestId").toString();
        NetworkRequest& nr = m_requests[reqId];
        const QJsonObject frame = params.value("response").toObject();
        WebSocketFrame wf;
        wf.direction = WebSocketFrame::Received;
        wf.opcode = frame.value("opcode").toInt();
        wf.masked = frame.value("mask").toBool();
        wf.payloadData = frame.value("payloadData").toString();
        wf.timestamp = params.value("timestamp").toDouble();
        emit webSocketFrameReceived(reqId, wf);
    }
    else if (method == "Network.webSocketClosed") {
        const QString reqId = params.value("requestId").toString();
        NetworkRequest& nr = m_requests[reqId];
        nr.isWebSocket = false; // connection ended
        emit webSocketClosed(reqId);
    }
    // === SSE events ===
    else if (method == "Network.eventSourceMessageReceived") {
        const QString reqId = params.value("requestId").toString();
        SseEvent ev;
        ev.requestId = reqId;
        ev.timestamp = params.value("timestamp").toDouble();
        ev.eventName = params.value("eventName").toString();
        ev.eventId = params.value("eventId").toString();
        ev.data = params.value("data").toString();
        m_sseEvents[reqId].append(ev);
        emit sseMessageReceived(ev);
    }
}

QList<HttpHeader> NetworkTracker::parseHeaders(const QJsonObject& obj, const QString& key)
{
    QList<HttpHeader> result;
    const QJsonObject headers = obj.value(key).toObject();
    for (auto it = headers.begin(); it != headers.end(); ++it) {
        HttpHeader h;
        h.name = it.key();
        h.value = it.value().toString();
        result.append(h);
    }
    return result;
}

QString NetworkTracker::resourceTypeFromCdp(const QString& type)
{
    if (type.isEmpty()) return "Other";
    return type;
}

} // namespace devtools
} // namespace nothing
