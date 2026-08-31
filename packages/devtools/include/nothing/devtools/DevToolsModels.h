#pragma once

#include <QObject>
#include <QJsonObject>
#include <QJsonArray>
#include <QString>
#include <QList>
#include <QHash>
#include <QSet>
#include <QDateTime>
#include <QByteArray>
#include <QUrl>
#include <QPair>
#include <optional>
#include <functional>
#include <memory>

namespace nothing {
namespace devtools {

// ============================================================================
// Network Models
// ============================================================================

struct HttpHeader {
    QString name;
    QString value;
};

struct RequestInfo {
    QString requestId;
    QString loaderId;
    QString frameId;
    QString documentUrl;
    QUrl url;
    QString method;
    QList<HttpHeader> headers;
    QString postData;
    bool hasPostData = false;
    QString resourceType;       // Document, Script, XHR, Fetch, Image, etc.
    QString initiatorType;     // parser, script, preload, etc.
    QString initiatorUrl;
    int initiatorLine = 0;
    QString referrerPolicy;
    QString mixedContentType;
    QString initialPriority;
    double timestamp = 0;
    double wallTime = 0;
    bool hasUserGesture = false;
};

struct ResponseInfo {
    QString requestId;
    QUrl url;
    int status = 0;
    QString statusText;
    QList<HttpHeader> headers;
    QString headersText;
    QString mimeType;
    QString charset;
    QString protocol;          // h2, h3, http/1.1
    QString remoteIPAddress;
    int remotePort = 0;
    bool fromDiskCache = false;
    bool fromServiceWorker = false;
    bool fromPrefetchCache = false;
    qint64 encodedDataLength = 0;
    QString securityState;     // secure, insecure, neutral, unknown
    QString securityProtocol;  // TLSv1.3, etc.
    QString securityCipher;
    QString securitySubjectName;
    QString securityIssuer;
    double securityValidFrom = 0;
    double securityValidTo = 0;
    double responseTime = 0;
    QString serviceWorkerResponseSource;
};

struct ResourceTiming {
    double requestTime = 0;
    double proxyStart = -1;
    double proxyEnd = -1;
    double dnsStart = -1;
    double dnsEnd = -1;
    double connectStart = -1;
    double connectEnd = -1;
    double sslStart = -1;
    double sslEnd = -1;
    double workerStart = -1;
    double workerReady = -1;
    double sendStart = -1;
    double sendEnd = -1;
    double receiveHeadersStart = -1;
    double receiveHeadersEnd = -1;
};

struct WebSocketFrame {
    enum Direction { Sent, Received };
    enum Opcode { Text = 1, Binary = 2, Close = 8, Ping = 9, Pong = 10 };
    
    Direction direction;
    int opcode = 0;
    bool masked = false;
    QString payloadData;       // UTF-8 for text, base64 for binary
    double timestamp = 0;
};

struct WebSocketInfo {
    QString requestId;
    QUrl url;
    QString initiatorType;
    QList<HttpHeader> handshakeRequestHeaders;
    int handshakeResponseStatus = 0;
    QString handshakeResponseStatusText;
    QList<HttpHeader> handshakeResponseHeaders;
    QList<WebSocketFrame> frames;
    bool closed = false;
    double createdTimestamp = 0;
};

struct NetworkRequest {
    QString requestId;
    RequestInfo request;
    ResponseInfo response;
    ResourceTiming timing;
    qint64 dataLength = 0;
    qint64 encodedDataLength = 0;
    qint64 decodedBodySize = 0;
    bool finished = false;
    bool failed = false;
    bool canceled = false;
    bool isRedirect = false;
    QString errorText;
    QString blockedReason;
    QString corsError;
    QByteArray responseBody;
    bool responseBodyBase64Encoded = false;
    QByteArray requestBody;
    bool requestBodyBase64Encoded = false;
    bool isWebSocket = false;
    bool isEventSource = false;
};

struct SseEvent {
    QString requestId;
    double timestamp;
    QString eventName;
    QString eventId;
    QString data;
};

// ============================================================================
// Runtime Models
// ============================================================================

struct RemoteObject {
    QString type;           // object, function, undefined, string, number, boolean, symbol, bigint
    QString subtype;       // array, null, error, proxy, promise, typedarray, regexp
    QString className;
    QJsonValue value;
    QString unserializableValue;
    QString description;
    QString objectId;
    QJsonObject preview;
};

struct CallFrame {
    QString functionName;
    QString scriptId;
    QString url;
    int lineNumber = 0;
    int columnNumber = 0;
};

struct StackTrace {
    QString description;
    QList<CallFrame> callFrames;
    std::shared_ptr<StackTrace> parent;
};

struct ExceptionDetails {
    int exceptionId = 0;
    QString text;
    int lineNumber = 0;
    int columnNumber = 0;
    QString scriptId;
    QString url;
    StackTrace stackTrace;
    int executionContextId = 0;
    RemoteObject exception;
    QJsonObject exceptionMetaData;
};

struct ConsoleMessage {
    enum Type {
        Log, Debug, Info, Warning, Error,
        Dir, DirXml, Table, Trace,
        StartGroup, StartGroupCollapsed, EndGroup,
        Clear, Assert, TimeEnd, Count
    };
    
    Type type = Log;
    QList<RemoteObject> args;
    int executionContextId = 0;
    double timestamp = 0;
    StackTrace stackTrace;
    QString consoleContext;
};

struct ExecutionContext {
    int id = 0;
    QString origin;
    QString name;
    QString uniqueId;
    QJsonObject auxData;     // {isDefault, type, frameId}
};

// ============================================================================
// DOM Models
// ============================================================================

struct DomNode {
    int nodeId = 0;
    int parentId = 0;
    int backendNodeId = 0;
    int nodeType = 0;        // 1=Element, 3=Text, 8=Comment, 9=Document
    QString nodeName;
    QString localName;
    QString nodeValue;
    QStringList attributes;  // flat: [name1, value1, name2, value2, ...]
    QList<DomNode> children;
    int childNodeCount = 0;
    QString frameId;
    QString shadowRootType;  // open, closed, user-agent
    QString pseudoType;
    QString documentUrl;
    QString baseUrl;
    
    QString attribute(const QString& name) const {
        for (int i = 0; i < attributes.size() - 1; i += 2) {
            if (attributes[i].compare(name, Qt::CaseInsensitive) == 0)
                return attributes[i + 1];
        }
        return {};
    }
    bool isElement() const { return nodeType == 1; }
    bool isDocument() const { return nodeType == 9; }
};

struct ComputedStyleProperty {
    QString name;
    QString value;
};

struct BoxModel {
    QList<double> content;
    QList<double> padding;
    QList<double> border;
    QList<double> margin;
    int width = 0;
    int height = 0;
};

// ============================================================================
// Page Models
// ============================================================================

struct FrameInfo {
    QString id;
    QString parentId;
    QString loaderId;
    QUrl url;
    QString domainAndRegistry;
    QString securityOrigin;
    QString mimeType;
    QString secureContextType;
    QString crossOriginIsolatedContextType;
    QStringList gatedAPIFeatures;
    QString name;
    QString unreachableUrl;
    bool isAdFrame = false;
};

struct LifecycleEvent {
    QString name;            // commit, DOMContentLoaded, load, networkIdle, etc.
    double timestamp = 0;
    QString frameId;
    QString loaderId;
};

struct ResourceEntry {
    QUrl url;
    QString type;
    QString mimeType;
    qint64 contentSize = 0;
    double lastModified = 0;
    bool canceled = false;
    bool failed = false;
};

struct FrameResourceTree {
    FrameInfo frame;
    QList<ResourceEntry> resources;
    QList<FrameResourceTree> childFrames;
};

// ============================================================================
// Storage Models
// ============================================================================

struct CookieInfo {
    QString name;
    QString value;
    QString domain;
    QString path;
    double expires = -1;       // epoch seconds, -1 = session
    bool secure = false;
    bool httpOnly = false;
    QString sameSite;           // Strict, Lax, None, ""
    QString priority;           // Low, Medium, High
    QString sourceScheme;       // Secure, NonSecure, Unset
    int sourcePort = 0;
    bool session = false;
    int size = 0;
    QString partitionKey;
    bool partitionKeyOpaque = false;
};

struct StorageItem {
    QString key;
    QString value;
};

struct IndexedDBObjectStore {
    QString name;
    QJsonObject keyPath;
    bool autoIncrement = false;
    QJsonArray indexes;
};

struct IndexedDBDatabase {
    QString name;
    qint64 version = 0;
    QList<IndexedDBObjectStore> objectStores;
};

struct CacheEntry {
    QString requestURL;
    QString requestMethod;
    QList<HttpHeader> requestHeaders;
    QList<HttpHeader> responseHeaders;
    double responseTime = 0;
    int responseStatus = 0;
    QString responseStatusText;
    QString responseType;
};

// ============================================================================
// Target Models
// ============================================================================

struct TargetInfo {
    QString targetId;
    QString type;            // page, iframe, tab, browser, worker, service_worker
    QString title;
    QUrl url;
    QString parentId;
    QString openerId;
    QString parentFrameId;
    QString browserContextId;
    bool attached = false;
    bool canAccessOpener = false;
};

// ============================================================================
// Screenshot / PDF Options
// ============================================================================

struct ScreenshotOptions {
    QString format = "png";        // png, jpeg, webp
    int quality = 80;              // 0-100 for jpeg/webp
    qreal clipX = 0;
    qreal clipY = 0;
    qreal clipWidth = 0;
    qreal clipHeight = 0;
    bool hasClip = false;
    bool fromSurface = true;
    bool captureBeyondViewport = false;
    bool optimizeForSpeed = false;
};

struct PdfOptions {
    bool landscape = false;
    bool displayHeaderFooter = false;
    bool printBackground = false;
    double scale = 1.0;
    double paperWidth = 8.5;       // inches
    double paperHeight = 11.0;
    double marginTop = 0.4;
    double marginBottom = 0.4;
    double marginLeft = 0.4;
    double marginRight = 0.4;
    QString pageRanges;
    bool preferCssPageSize = false;
};

// ============================================================================
// Evaluate Options
// ============================================================================

struct EvaluateOptions {
    QString expression;
    QString objectGroup = "eval";
    bool includeCommandLineAPI = false;
    bool silent = false;
    std::optional<int> executionContextId;
    bool returnByValue = false;
    bool generatePreview = false;
    bool userGesture = false;
    bool awaitPromise = false;
    bool throwOnSideEffect = false;
    std::optional<double> timeout;
    bool disableBreaks = false;
    bool replMode = false;
    bool allowUnsafeEvalBlockedByCSP = true;
};

struct EvaluateResult {
    RemoteObject result;
    std::optional<ExceptionDetails> exceptionDetails;
};

} // namespace devtools
} // namespace nothing

Q_DECLARE_METATYPE(nothing::devtools::NetworkRequest)
Q_DECLARE_METATYPE(nothing::devtools::ConsoleMessage)
Q_DECLARE_METATYPE(nothing::devtools::DomNode)
Q_DECLARE_METATYPE(nothing::devtools::FrameInfo)
Q_DECLARE_METATYPE(nothing::devtools::LifecycleEvent)
Q_DECLARE_METATYPE(nothing::devtools::CookieInfo)
Q_DECLARE_METATYPE(nothing::devtools::TargetInfo)
Q_DECLARE_METATYPE(nothing::devtools::EvaluateResult)
Q_DECLARE_METATYPE(nothing::devtools::ExceptionDetails)
Q_DECLARE_METATYPE(nothing::devtools::WebSocketFrame)
