#pragma once

#include <QObject>
#include <QHash>
#include <QList>
#include "nothing/devtools/CdpClient.h"
#include "nothing/devtools/DevToolsModels.h"

namespace nothing {
namespace devtools {

/// PageTracker — Tracks page lifecycle, handles navigation, screenshots,
/// PDF generation, and the "Find File" feature (resource tree).
class PageTracker : public QObject {
    Q_OBJECT
public:
    explicit PageTracker(CdpClient* cdp, QObject* parent = nullptr);

    /// Enable page tracking.
    /// @param enableFileChooserOpenedEvent  Emit events when file inputs are clicked
    void enable(bool enableFileChooserOpenedEvent = false);

    /// Disable page tracking.
    void disable();

    /// Enable lifecycle events (DOMContentLoaded, load, networkIdle, etc.).
    /// Replays past events for the current page.
    void setLifecycleEventsEnabled(bool enabled);

    /// Navigate to a URL.
    void navigate(const QUrl& url, const QString& referrer = QString(),
                  std::function<void(const QString& frameId,
                                   const QString& loaderId,
                                   const QString& errorText)> callback = nullptr);

    /// Reload the page.
    /// @param ignoreCache  Bypass cache on reload
    void reload(bool ignoreCache = false);

    /// Stop all loading.
    void stopLoading();

    /// Close the page.
    void close();

    /// Capture a screenshot.
    void captureScreenshot(const ScreenshotOptions& opts,
                          std::function<void(const QByteArray&)> callback);

    /// Capture a full-page screenshot.
    void captureFullPage(std::function<void(const QByteArray&)> callback,
                         const QString& format = "png", int quality = 80);

    /// Generate a PDF.
    void printToPdf(const PdfOptions& opts,
                    std::function<void(const QByteArray&)> callback);

    /// Handle a JavaScript dialog (alert/confirm/prompt).
    void handleJavaScriptDialog(bool accept, const QString& promptText = QString());

    /// Bypass CSP for all future script evaluations.
    void setBypassCSP(bool bypass);

    /// Set the web lifecycle state.
    /// @param state "frozen" or "active"
    void setWebLifecycleState(const QString& state);

    /// Get the full resource tree (for "Find File" feature).
    void getResourceTree(std::function<void(const FrameResourceTree&)> callback);

    /// Get the content of a specific resource by URL.
    void getResourceContent(const QString& frameId, const QString& url,
                           std::function<void(const QByteArray&, bool)> callback);

    /// Find all loaded resources matching a filename (substring match).
    void findFilesByName(const QString& filename,
                        std::function<void(const QList<QPair<ResourceEntry, QString>>)> callback);

    /// Download a specific resource to disk.
    void downloadResource(const QString& frameId, const QString& url,
                         const QString& filepath,
                         std::function<void(bool success)> callback);

    /// Get the frame tree (without resources).
    void getFrameTree(std::function<void(const QList<FrameInfo>&)> callback);

    /// Wait for a specific lifecycle event.
    void waitForLifecycle(const QString& lifecycleName,
                          std::function<void()> callback,
                          int timeoutMs = 30000);

    /// Wait for page load event.
    void waitForLoad(std::function<void()> callback, int timeoutMs = 30000);

    /// Wait for network idle.
    void waitForNetworkIdle(std::function<void()> callback, int timeoutMs = 30000);

    /// Current page URL.
    QUrl currentUrl() const { return m_currentUrl; }

    /// Current frame ID.
    QString currentFrameId() const { return m_currentFrameId; }

    /// Lifecycle event history.
    QList<LifecycleEvent> lifecycleHistory() const { return m_lifecycleHistory; }

signals:
    void frameStartedNavigating(const QString& frameId, const QUrl& url, const QString& navType);
    void frameAttached(const QString& frameId, const QString& parentFrameId);
    void frameStartedLoading(const QString& frameId);
    void frameNavigated(const FrameInfo& frame, const QString& navigationType);
    void frameStoppedLoading(const QString& frameId);
    void frameDetached(const QString& frameId, const QString& reason);
    void navigatedWithinDocument(const QString& frameId, const QUrl& url, const QString& navType);
    void lifecycleEvent(const LifecycleEvent& event);
    void domContentEventFired(double timestamp);
    void loadEventFired(double timestamp);
    void dialogOpening(const QUrl& url, const QString& message, const QString& type);
    void dialogClosed(bool result, bool userDismissed);
    void fileChooserOpened(const QString& frameId, const QString& mode, int backendNodeId);
    void windowOpen(const QUrl& url, const QString& windowName, const QString& windowFeatures, bool userGesture);

private:
    void handleEvent(const QString& method, const QJsonObject& params, const QString& sessionId);

    static FrameInfo parseFrame(const QJsonObject& obj);
    static FrameResourceTree parseResourceTree(const QJsonObject& obj);
    static void collectAllResources(const FrameResourceTree& tree,
                                   QList<QPair<ResourceEntry, QString>>& result,
                                   const QString& frameId = QString());
    static ResourceEntry parseResource(const QJsonObject& obj);

    CdpClient* m_cdp;
    QUrl m_currentUrl;
    QString m_currentFrameId;
    QString m_currentLoaderId;
    QList<LifecycleEvent> m_lifecycleHistory;
    QHash<QString, FrameInfo> m_frames;

    // Wait helpers
    std::function<void()> m_waitCallback;
    QString m_waitLifecycleName;
    QTimer* m_waitTimer;
};

} // namespace devtools
} // namespace nothing
