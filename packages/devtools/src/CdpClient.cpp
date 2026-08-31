#include "nothing/devtools/CdpClient.h"
#include <QJsonDocument>
#include <QEventLoop>
#include <QTimer>

namespace nothing {
namespace devtools {

CdpClient::CdpClient(QObject* parent)
    : QObject(parent)
    , m_socket(new QWebSocket)
{
    connect(m_socket, &QWebSocket::textMessageReceived,
            this, &CdpClient::onTextMessageReceived);
    connect(m_socket, &QWebSocket::connected,
            this, &CdpClient::onConnected);
    connect(m_socket, &QWebSocket::disconnected,
            this, &CdpClient::onDisconnected);
}

CdpClient::~CdpClient()
{
    if (m_socket) {
        m_socket->close();
    }
}

void CdpClient::connectToUrl(const QUrl& url)
{
    m_url = url;
    m_socket->open(url);
}

void CdpClient::disconnect()
{
    if (m_socket) {
        m_socket->close();
    }
}

bool CdpClient::isConnected() const
{
    return m_socket && m_socket->state() == QAbstractSocket::ConnectedState;
}

int CdpClient::sendCommand(const QString& method,
                           const QJsonObject& params,
                           std::function<void(const QJsonObject&)> callback,
                           const QString& sessionId)
{
    const int id = m_nextId++;
    if (callback) {
        m_callbacks[id] = callback;
    }

    QJsonObject msg;
    msg["id"] = id;
    msg["method"] = method;
    if (!params.isEmpty()) {
        msg["params"] = params;
    }
    if (!sessionId.isEmpty()) {
        msg["sessionId"] = sessionId;
    }

    const QByteArray data = QJsonDocument(msg).toJson(QJsonDocument::Compact);
    m_socket->sendTextMessage(QString::fromUtf8(data));
    return id;
}

int CdpClient::sendCommand(const QString& method,
                           const QJsonObject& params,
                           const QString& sessionId)
{
    return sendCommand(method, params, nullptr, sessionId);
}

QJsonObject CdpClient::sendCommandSync(const QString& method,
                                       const QJsonObject& params,
                                       const QString& sessionId,
                                       int timeoutMs)
{
    QJsonObject result;
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);

    int id = sendCommand(method, params, [&](const QJsonObject& res) {
        result = res;
        loop.quit();
    }, sessionId);

    connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start(timeoutMs);
    loop.exec();

    m_callbacks.remove(id);
    return result;
}

void CdpClient::onTextMessageReceived(const QString& message)
{
    const QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8());
    if (doc.isNull() || !doc.isObject()) {
        return;
    }

    const QJsonObject obj = doc.object();

    // Response to a command we sent
    if (obj.contains("id")) {
        const int id = obj.value("id").toInt();
        auto it = m_callbacks.find(id);
        if (it != m_callbacks.end()) {
            auto cb = it.value();
            m_callbacks.erase(it);

            if (obj.contains("error")) {
                const QJsonObject error = obj.value("error").toObject();
                emit commandError(id, error);
                // Still call the callback with empty result so callers don't hang
                if (cb) cb(QJsonObject());
            } else if (obj.contains("result")) {
                const QJsonObject result = obj.value("result").toObject();
                emit commandResponse(id, result);
                if (cb) cb(result);
            } else {
                if (cb) cb(QJsonObject());
            }
        }
        return;
    }

    // Event from the browser
    if (obj.contains("method")) {
        const QString method = obj.value("method").toString();
        const QJsonObject params = obj.value("params").toObject();
        QString sessionId;
        if (obj.contains("sessionId")) {
            sessionId = obj.value("sessionId").toString();
        }
        emit eventReceived(method, params, sessionId);
    }
}

void CdpClient::onConnected()
{
    emit connected();
}

void CdpClient::onDisconnected()
{
    // Cancel all pending callbacks
    m_callbacks.clear();
    emit disconnected(m_socket->closeReason());
}

} // namespace devtools
} // namespace nothing
