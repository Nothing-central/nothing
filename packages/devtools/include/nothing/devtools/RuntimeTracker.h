#pragma once

#include <QObject>
#include <QHash>
#include "nothing/devtools/CdpClient.h"
#include "nothing/devtools/DevToolsModels.h"

namespace nothing {
namespace devtools {

/// RuntimeTracker — Captures console messages, exceptions, and provides
/// JavaScript evaluation via Runtime.evaluate and Runtime.callFunctionOn.
class RuntimeTracker : public QObject {
    Q_OBJECT
public:
    explicit RuntimeTracker(CdpClient* cdp, QObject* parent = nullptr);

    /// Enable runtime tracking. Call after CDP is connected.
    void enable();

    /// Disable runtime tracking.
    void disable();

    /// Evaluate a JavaScript expression.
    /// @param expression  The JS code to run
    /// @param options     Evaluation options (returnByValue, awaitPromise, etc.)
    /// @param callback    Called with the result (RemoteObject + optional ExceptionDetails)
    void evaluate(const QString& expression,
                  const EvaluateOptions& options,
                  std::function<void(const EvaluateResult&)> callback);

    /// Synchronous evaluate (blocks with QEventLoop).
    EvaluateResult evaluateSync(const QString& expression,
                                const EvaluateOptions& options = EvaluateOptions{});

    /// Call a function on a RemoteObject.
    /// @param objectId          The object to call on
    /// @param functionDeclaration A function expression like "(function(arg) { ... })"
    /// @param arguments          Arguments to pass (as JSON values or objectIds)
    /// @param options            Evaluation options
    /// @param callback           Called with the result
    void callFunctionOn(const QString& objectId,
                        const QString& functionDeclaration,
                        const QJsonArray& arguments,
                        const EvaluateOptions& options,
                        std::function<void(const EvaluateResult&)> callback);

    /// Get properties of a RemoteObject.
    /// @param objectId   The object to inspect
    /// @param callback   Called with the properties as a JSON array
    void getProperties(const QString& objectId,
                       bool ownProperties,
                       bool accessorPropertiesOnly,
                       bool generatePreview,
                       std::function<void(const QJsonArray&,
                                          const QJsonArray&,
                                          const QJsonArray&,
                                          const std::optional<ExceptionDetails>&)> callback);

    /// Release a RemoteObject (free its V8 handle).
    void releaseObject(const QString& objectId);

    /// Release all objects in an object group.
    void releaseObjectGroup(const QString& objectGroup);

    /// Get all execution contexts.
    QList<ExecutionContext> executionContexts() const;

    /// Get the default (main world) execution context ID.
    std::optional<int> defaultExecutionContextId() const;

    /// Get execution context ID for a specific frame.
    std::optional<int> contextIdForFrame(const QString& frameId, bool mainWorld = true) const;

    /// Add a binding — a JS function that calls back to CDP.
    /// When the page calls the binding, bindingCalled signal fires.
    void addBinding(const QString& name);

    /// Remove a binding.
    void removeBinding(const QString& name);

    /// Inject a script that runs before page scripts on every navigation.
    /// @param source       The JS source code
    /// @param worldName    Empty = main world; non-empty = isolated world
    /// @param grantUniversalAccess  Bypass same-origin policy (for isolated worlds)
    /// @param runImmediately  Also run in the current page immediately
    /// @param callback     Called with the script identifier
    void addScriptToEvaluateOnNewDocument(const QString& source,
                                          const QString& worldName = QString(),
                                          bool grantUniversalAccess = false,
                                          bool runImmediately = true,
                                          std::function<void(const QString&)> callback = nullptr);

    /// Remove an injected script.
    void removeScriptToEvaluateOnNewDocument(const QString& identifier);

    /// Create an isolated world for a frame.
    void createIsolatedWorld(const QString& frameId,
                            const QString& worldName,
                            bool grantUniversalAccess,
                            std::function<void(int)> callback);

signals:
    /// Emitted when a console.log/info/warn/error/etc. is called.
    void consoleApiCalled(const ConsoleMessage& message);

    /// Emitted when an uncaught exception occurs.
    void exceptionThrown(const ExceptionDetails& details);

    /// Emitted when an exception is revoked (e.g. promise rejection handled).
    void exceptionRevoked(const QString& reason, int exceptionId);

    /// Emitted when a new execution context is created.
    void executionContextCreated(const ExecutionContext& context);

    /// Emitted when an execution context is destroyed.
    void executionContextDestroyed(int contextId);

    /// Emitted when all execution contexts are cleared (navigation).
    void executionContextsCleared();

    /// Emitted when a binding is called from the page.
    void bindingCalled(const QString& name, const QString& payload, int executionContextId);

private:
    void handleEvent(const QString& method, const QJsonObject& params, const QString& sessionId);

    static RemoteObject parseRemoteObject(const QJsonObject& obj);
    static ExceptionDetails parseExceptionDetails(const QJsonObject& obj);
    static StackTrace parseStackTrace(const QJsonObject& obj);
    static ConsoleMessage::Type parseConsoleType(const QString& type);

    CdpClient* m_cdp;
    QHash<int, ExecutionContext> m_contexts;
    QSet<QString> m_bindings;
};

} // namespace devtools
} // namespace nothing
