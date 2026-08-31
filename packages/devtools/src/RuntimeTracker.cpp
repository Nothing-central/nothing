#include "nothing/devtools/RuntimeTracker.h"
#include <QEventLoop>
#include <QTimer>

namespace nothing {
namespace devtools {

RuntimeTracker::RuntimeTracker(CdpClient* cdp, QObject* parent)
    : QObject(parent)
    , m_cdp(cdp)
{
    connect(m_cdp, &CdpClient::eventReceived,
            this, &RuntimeTracker::handleEvent);
}

void RuntimeTracker::enable()
{
    m_cdp->sendCommand("Runtime.enable");
}

void RuntimeTracker::disable()
{
    m_cdp->sendCommand("Runtime.disable");
}

void RuntimeTracker::evaluate(const QString& expression,
                              const EvaluateOptions& opts,
                              std::function<void(const EvaluateResult&)> callback)
{
    QJsonObject params;
    params["expression"] = expression;
    if (!opts.objectGroup.isEmpty()) params["objectGroup"] = opts.objectGroup;
    if (opts.includeCommandLineAPI) params["includeCommandLineAPI"] = true;
    if (opts.silent) params["silent"] = true;
    if (opts.executionContextId.has_value()) {
        params["executionContextId"] = opts.executionContextId.value();
    }
    if (opts.returnByValue) params["returnByValue"] = true;
    if (opts.generatePreview) params["generatePreview"] = true;
    if (opts.userGesture) params["userGesture"] = true;
    if (opts.awaitPromise) params["awaitPromise"] = true;
    if (opts.throwOnSideEffect) params["throwOnSideEffect"] = true;
    if (opts.timeout.has_value()) params["timeout"] = opts.timeout.value();
    if (opts.disableBreaks) params["disableBreaks"] = true;
    if (opts.replMode) params["replMode"] = true;
    if (opts.allowUnsafeEvalBlockedByCSP) params["allowUnsafeEvalBlockedByCSP"] = true;

    m_cdp->sendCommand("Runtime.evaluate", params, [callback](const QJsonObject& result) {
        EvaluateResult r;
        if (result.contains("result")) {
            r.result = parseRemoteObject(result.value("result").toObject());
        }
        if (result.contains("exceptionDetails")) {
            r.exceptionDetails = parseExceptionDetails(
                result.value("exceptionDetails").toObject());
        }
        callback(r);
    });
}

EvaluateResult RuntimeTracker::evaluateSync(const QString& expression,
                                           const EvaluateOptions& options)
{
    EvaluateResult result;
    QEventLoop loop;
    evaluate(expression, options, [&](const EvaluateResult& r) {
        result = r;
        loop.quit();
    });
    QTimer::singleShot(30000, &loop, &QEventLoop::quit);
    loop.exec();
    return result;
}

void RuntimeTracker::callFunctionOn(const QString& objectId,
                                    const QString& functionDeclaration,
                                    const QJsonArray& arguments,
                                    const EvaluateOptions& opts,
                                    std::function<void(const EvaluateResult&)> callback)
{
    QJsonObject params;
    params["objectId"] = objectId;
    params["functionDeclaration"] = functionDeclaration;
    params["arguments"] = arguments;
    if (!opts.objectGroup.isEmpty()) params["objectGroup"] = opts.objectGroup;
    if (opts.silent) params["silent"] = true;
    if (opts.returnByValue) params["returnByValue"] = true;
    if (opts.generatePreview) params["generatePreview"] = true;
    if (opts.userGesture) params["userGesture"] = true;
    if (opts.awaitPromise) params["awaitPromise"] = true;
    if (opts.throwOnSideEffect) params["throwOnSideEffect"] = true;

    m_cdp->sendCommand("Runtime.callFunctionOn", params, [callback](const QJsonObject& result) {
        EvaluateResult r;
        if (result.contains("result")) {
            r.result = parseRemoteObject(result.value("result").toObject());
        }
        if (result.contains("exceptionDetails")) {
            r.exceptionDetails = parseExceptionDetails(
                result.value("exceptionDetails").toObject());
        }
        callback(r);
    });
}

void RuntimeTracker::getProperties(const QString& objectId,
                                  bool ownProperties,
                                  bool accessorPropertiesOnly,
                                  bool generatePreview,
                                  std::function<void(const QJsonArray&,
                                                     const QJsonArray&,
                                                     const QJsonArray&,
                                                     const std::optional<ExceptionDetails>&)> callback)
{
    QJsonObject params;
    params["objectId"] = objectId;
    params["ownProperties"] = ownProperties;
    params["accessorPropertiesOnly"] = accessorPropertiesOnly;
    if (generatePreview) params["generatePreview"] = true;

    m_cdp->sendCommand("Runtime.getProperties", params, [callback](const QJsonObject& result) {
        QJsonArray props = result.value("result").toArray();
        QJsonArray internalProps = result.value("internalProperties").toArray();
        QJsonArray privateProps = result.value("privateProperties").toArray();
        std::optional<ExceptionDetails> exc;
        if (result.contains("exceptionDetails")) {
            exc = parseExceptionDetails(result.value("exceptionDetails").toObject());
        }
        callback(props, internalProps, privateProps, exc);
    });
}

void RuntimeTracker::releaseObject(const QString& objectId)
{
    QJsonObject params;
    params["objectId"] = objectId;
    m_cdp->sendCommand("Runtime.releaseObject", params);
}

void RuntimeTracker::releaseObjectGroup(const QString& objectGroup)
{
    QJsonObject params;
    params["objectGroup"] = objectGroup;
    m_cdp->sendCommand("Runtime.releaseObjectGroup", params);
}

QList<ExecutionContext> RuntimeTracker::executionContexts() const
{
    return m_contexts.values();
}

std::optional<int> RuntimeTracker::defaultExecutionContextId() const
{
    for (const ExecutionContext& ctx : m_contexts) {
        const QJsonObject aux = ctx.auxData;
        if (aux.value("isDefault").toBool(false)) {
            return ctx.id;
        }
    }
    return std::nullopt;
}

std::optional<int> RuntimeTracker::contextIdForFrame(const QString& frameId, bool mainWorld) const
{
    for (const ExecutionContext& ctx : m_contexts) {
        const QJsonObject aux = ctx.auxData;
        if (aux.value("frameId").toString() == frameId) {
            if (mainWorld && aux.value("isDefault").toBool(false)) {
                return ctx.id;
            }
            if (!mainWorld && aux.value("type").toString() == "isolated") {
                return ctx.id;
            }
        }
    }
    return std::nullopt;
}

void RuntimeTracker::addBinding(const QString& name)
{
    QJsonObject params;
    params["name"] = name;
    m_cdp->sendCommand("Runtime.addBinding", params);
    m_bindings.insert(name);
}

void RuntimeTracker::removeBinding(const QString& name)
{
    QJsonObject params;
    params["name"] = name;
    m_cdp->sendCommand("Runtime.removeBinding", params);
    m_bindings.remove(name);
}

void RuntimeTracker::addScriptToEvaluateOnNewDocument(const QString& source,
                                                       const QString& worldName,
                                                       bool grantUniversalAccess,
                                                       bool runImmediately,
                                                       std::function<void(const QString&)> callback)
{
    QJsonObject params;
    params["source"] = source;
    if (!worldName.isEmpty()) params["worldName"] = worldName;
    if (grantUniversalAccess) params["grantUniversalAccess"] = true;
    if (runImmediately) params["runImmediately"] = true;

    m_cdp->sendCommand("Page.addScriptToEvaluateOnNewDocument", params,
        [callback](const QJsonObject& result) {
            if (callback) callback(result.value("identifier").toString());
        });
}

void RuntimeTracker::removeScriptToEvaluateOnNewDocument(const QString& identifier)
{
    QJsonObject params;
    params["identifier"] = identifier;
    m_cdp->sendCommand("Page.removeScriptToEvaluateOnNewDocument", params);
}

void RuntimeTracker::createIsolatedWorld(const QString& frameId,
                                        const QString& worldName,
                                        bool grantUniversalAccess,
                                        std::function<void(int)> callback)
{
    QJsonObject params;
    params["frameId"] = frameId;
    if (!worldName.isEmpty()) params["worldName"] = worldName;
    if (grantUniversalAccess) params["grantUniversalAccess"] = true;

    m_cdp->sendCommand("Page.createIsolatedWorld", params, [callback](const QJsonObject& result) {
        if (callback) callback(result.value("executionContextId").toInt());
    });
}

// === Private ===

void RuntimeTracker::handleEvent(const QString& method, const QJsonObject& params, const QString&)
{
    if (method == "Runtime.executionContextCreated") {
        const QJsonObject ctx = params.value("context").toObject();
        ExecutionContext ec;
        ec.id = ctx.value("id").toInt();
        ec.origin = ctx.value("origin").toString();
        ec.name = ctx.value("name").toString();
        ec.uniqueId = ctx.value("uniqueId").toString();
        ec.auxData = ctx.value("auxData").toObject();
        m_contexts[ec.id] = ec;
        emit executionContextCreated(ec);
    }
    else if (method == "Runtime.executionContextDestroyed") {
        const int id = params.value("executionContextId").toInt();
        m_contexts.remove(id);
        emit executionContextDestroyed(id);
    }
    else if (method == "Runtime.executionContextsCleared") {
        m_contexts.clear();
        emit executionContextsCleared();
    }
    else if (method == "Runtime.consoleAPICalled") {
        ConsoleMessage msg;
        msg.type = parseConsoleType(params.value("type").toString());
        msg.executionContextId = params.value("executionContextId").toInt();
        msg.timestamp = params.value("timestamp").toDouble();

        const QJsonArray args = params.value("args").toArray();
        for (const QJsonValue& a : args) {
            msg.args.append(parseRemoteObject(a.toObject()));
        }

        if (params.contains("stackTrace")) {
            msg.stackTrace = parseStackTrace(params.value("stackTrace").toObject());
        }

        emit consoleApiCalled(msg);
    }
    else if (method == "Runtime.exceptionThrown") {
        ExceptionDetails d = parseExceptionDetails(
            params.value("exceptionDetails").toObject());
        emit exceptionThrown(d);
    }
    else if (method == "Runtime.exceptionRevoked") {
        emit exceptionRevoked(params.value("reason").toString(),
                              params.value("exceptionId").toInt());
    }
    else if (method == "Runtime.bindingCalled") {
        emit bindingCalled(params.value("name").toString(),
                         params.value("payload").toString(),
                         params.value("executionContextId").toInt());
    }
}

RemoteObject RuntimeTracker::parseRemoteObject(const QJsonObject& obj)
{
    RemoteObject r;
    r.type = obj.value("type").toString();
    r.subtype = obj.value("subtype").toString();
    r.className = obj.value("className").toString();
    if (obj.contains("value")) r.value = obj.value("value");
    r.unserializableValue = obj.value("unserializableValue").toString();
    r.description = obj.value("description").toString();
    r.objectId = obj.value("objectId").toString();
    if (obj.contains("preview")) r.preview = obj.value("preview").toObject();
    return r;
}

ExceptionDetails RuntimeTracker::parseExceptionDetails(const QJsonObject& obj)
{
    ExceptionDetails d;
    d.exceptionId = obj.value("exceptionId").toInt();
    d.text = obj.value("text").toString();
    d.lineNumber = obj.value("lineNumber").toInt();
    d.columnNumber = obj.value("columnNumber").toInt();
    d.scriptId = obj.value("scriptId").toString();
    d.url = obj.value("url").toString();
    d.executionContextId = obj.value("executionContextId").toInt();
    if (obj.contains("stackTrace")) {
        d.stackTrace = parseStackTrace(obj.value("stackTrace").toObject());
    }
    if (obj.contains("exception")) {
        d.exception = parseRemoteObject(obj.value("exception").toObject());
    }
    if (obj.contains("exceptionMetaData")) {
        d.exceptionMetaData = obj.value("exceptionMetaData").toObject();
    }
    return d;
}

StackTrace RuntimeTracker::parseStackTrace(const QJsonObject& obj)
{
    StackTrace st;
    st.description = obj.value("description").toString();
    const QJsonArray frames = obj.value("callFrames").toArray();
    for (const QJsonValue& f : frames) {
        const QJsonObject fo = f.toObject();
        CallFrame cf;
        cf.functionName = fo.value("functionName").toString();
        cf.scriptId = fo.value("scriptId").toString();
        cf.url = fo.value("url").toString();
        cf.lineNumber = fo.value("lineNumber").toInt();
        cf.columnNumber = fo.value("columnNumber").toInt();
        st.callFrames.append(cf);
    }
    if (obj.contains("parent")) {
        st.parent = std::make_shared<StackTrace>(parseStackTrace(obj.value("parent").toObject()));
    }
    return st;
}

ConsoleMessage::Type RuntimeTracker::parseConsoleType(const QString& type)
{
    if (type == "log") return ConsoleMessage::Log;
    if (type == "debug") return ConsoleMessage::Debug;
    if (type == "info") return ConsoleMessage::Info;
    if (type == "warning") return ConsoleMessage::Warning;
    if (type == "error") return ConsoleMessage::Error;
    if (type == "dir") return ConsoleMessage::Dir;
    if (type == "dirxml") return ConsoleMessage::DirXml;
    if (type == "table") return ConsoleMessage::Table;
    if (type == "trace") return ConsoleMessage::Trace;
    if (type == "startGroup") return ConsoleMessage::StartGroup;
    if (type == "startGroupCollapsed") return ConsoleMessage::StartGroupCollapsed;
    if (type == "endGroup") return ConsoleMessage::EndGroup;
    if (type == "clear") return ConsoleMessage::Clear;
    if (type == "assert") return ConsoleMessage::Assert;
    if (type == "timeEnd") return ConsoleMessage::TimeEnd;
    if (type == "count") return ConsoleMessage::Count;
    return ConsoleMessage::Log;
}

} // namespace devtools
} // namespace nothing
