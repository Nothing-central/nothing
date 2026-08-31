#include "nothing/devtools/PageTracker.h"
#include <QJsonArray>
#include <QTimer>
#include <QFile>
#include <QDir>

namespace nothing {
namespace devtools {

PageTracker::PageTracker(CdpClient* cdp, QObject* parent)
    : QObject(parent)
    , m_cdp(cdp)
    , m_waitTimer(new QTimer(this))
{
    m_waitTimer->setSingleShot(true);
    connect(m_cdp, &CdpClient::eventReceived,
            this, &PageTracker::handleEvent);
}

void PageTracker::enable(bool enableFileChooserOpenedEvent)
{
    QJsonObject params;
    if (enableFileChooserOpenedEvent) {
        params["enableFileChooserOpenedEvent"] = true;
    }
    m_cdp->sendCommand("Page.enable", params);
}

void PageTracker::disable()
{
    m_cdp->sendCommand("Page.disable");
}

void PageTracker::setLifecycleEventsEnabled(bool enabled)
{
    QJsonObject params;
    params["enabled"] = enabled;
    m_cdp->sendCommand("Page.setLifecycleEventsEnabled", params);
}

void PageTracker::navigate(const QUrl& url, const QString& referrer,
                           std::function<void(const QString&, const QString&, const QString&)> callback)
{
    QJsonObject params;
    params["url"] = url.toString();
    if (!referrer.isEmpty()) params["referrer"] = referrer;
    m_cdp->sendCommand("Page.navigate", params, [callback](const QJsonObject& result) {
        if (callback) callback(
            result.value("frameId").toString(),
            result.value("loaderId").toString(),
            result.value("errorText").toString()
        );
    });
}

void PageTracker::reload(bool ignoreCache)
{
    QJsonObject params;
    if (ignoreCache) params["ignoreCache"] = true;
    m_cdp->sendCommand("Page.reload", params);
}

void PageTracker::stopLoading()
{
    m_cdp->sendCommand("Page.stopLoading");
}

void PageTracker::close()
{
    m_cdp->sendCommand("Page.close");
}

void PageTracker::captureScreenshot(const ScreenshotOptions& opts,
                                    std::function<void(const QByteArray&)> callback)
{
    QJsonObject params;
    params["format"] = opts.format;
    if (opts.format != "png") params["quality"] = opts.quality;
    if (opts.hasClip) {
        QJsonObject clip;
        clip["x"] = opts.clipX;
        clip["y"] = opts.clipY;
        clip["width"] = opts.clipWidth;
        clip["height"] = opts.clipHeight;
        clip["scale"] = 1.0;
        params["clip"] = clip;
    }
    params["fromSurface"] = opts.fromSurface;
    if (opts.captureBeyondViewport) params["captureBeyondViewport"] = true;
    if (opts.optimizeForSpeed) params["optimizeForSpeed"] = true;

    m_cdp->sendCommand("Page.captureScreenshot", params, [callback](const QJsonObject& result) {
        const QString data = result.value("data").toString();
        if (callback) callback(QByteArray::fromBase64(data.toUtf8()));
    });
}

void PageTracker::captureFullPage(std::function<void(const QByteArray&)> callback,
                                 const QString& format, int quality)
{
    ScreenshotOptions opts;
    opts.format = format;
    opts.quality = quality;
    opts.captureBeyondViewport = true;
    opts.fromSurface = true;
    captureScreenshot(opts, callback);
}

void PageTracker::printToPdf(const PdfOptions& opts,
                             std::function<void(const QByteArray&)> callback)
{
    QJsonObject params;
    params["landscape"] = opts.landscape;
    params["displayHeaderFooter"] = opts.displayHeaderFooter;
    params["printBackground"] = opts.printBackground;
    params["scale"] = opts.scale;
    params["paperWidth"] = opts.paperWidth;
    params["paperHeight"] = opts.paperHeight;
    params["marginTop"] = opts.marginTop;
    params["marginBottom"] = opts.marginBottom;
    params["marginLeft"] = opts.marginLeft;
    params["marginRight"] = opts.marginRight;
    if (!opts.pageRanges.isEmpty()) params["pageRanges"] = opts.pageRanges;
    if (opts.preferCssPageSize) params["preferCssPageSize"] = true;

    m_cdp->sendCommand("Page.printToPDF", params, [callback](const QJsonObject& result) {
        const QString data = result.value("data").toString();
        if (callback) callback(QByteArray::fromBase64(data.toUtf8()));
    });
}

void PageTracker::handleJavaScriptDialog(bool accept, const QString& promptText)
{
    QJsonObject params;
    params["accept"] = accept;
    if (!promptText.isEmpty()) params["promptText"] = promptText;
    m_cdp->sendCommand("Page.handleJavaScriptDialog", params);
}

void PageTracker::setBypassCSP(bool bypass)
{
    QJsonObject params;
    params["enabled"] = bypass;
    m_cdp->sendCommand("Page.setBypassCSP", params);
}

void PageTracker::setWebLifecycleState(const QString& state)
{
    QJsonObject params;
    params["state"] = state;
    m_cdp->sendCommand("Page.setWebLifecycleState", params);
}

void PageTracker::getResourceTree(std::function<void(const FrameResourceTree&)> callback)
{
    m_cdp->sendCommand("Page.getResourceTree", {}, [callback](const QJsonObject& result) {
        if (callback) callback(parseResourceTree(result.value("frameTree").toObject()));
    });
}

void PageTracker::getResourceContent(const QString& frameId, const QString& url,
                                    std::function<void(const QByteArray&, bool)> callback)
{
    QJsonObject params;
    params["frameId"] = frameId;
    params["url"] = url;
    m_cdp->sendCommand("Page.getResourceContent", params, [callback](const QJsonObject& result) {
        const bool base64 = result.value("base64Encoded").toBool(false);
        const QString content = result.value("content").toString();
        if (base64) {
            callback(QByteArray::fromBase64(content.toUtf8()), true);
        } else {
            callback(content.toUtf8(), false);
        }
    });
}

void PageTracker::findFilesByName(const QString& filename,
                                  std::function<void(const QList<QPair<ResourceEntry, QString>>)> callback)
{
    getResourceTree([callback, filename](const FrameResourceTree& tree) {
        QList<QPair<ResourceEntry, QString>> allResources;
        collectAllResources(tree, allResources);

        QList<QPair<ResourceEntry, QString>> matches;
        for (const auto& pair : allResources) {
            const QString url = pair.first.url.toString();
            const QString name = url.mid(url.lastIndexOf('/') + 1);
            if (name.contains(filename, Qt::CaseInsensitive)) {
                matches.append(pair);
            }
        }
        if (callback) callback(matches);
    });
}

void PageTracker::downloadResource(const QString& frameId, const QString& url,
                                   const QString& filepath,
                                   std::function<void(bool)> callback)
{
    getResourceContent(frameId, url, [filepath, callback](const QByteArray& data, bool) {
        QFile f(filepath);
        if (f.open(QIODevice::WriteOnly)) {
            f.write(data);
            f.close();
            if (callback) callback(true);
        } else {
            if (callback) callback(false);
        }
    });
}

void PageTracker::getFrameTree(std::function<void(const QList<FrameInfo>&)> callback)
{
    m_cdp->sendCommand("Page.getFrameTree", {}, [callback](const QJsonObject& result) {
        QList<FrameInfo> frames;
        std::function<void(const QJsonObject&)> collect = [&](const QJsonObject& treeObj) {
            frames.append(parseFrame(treeObj.value("frame").toObject()));
            if (treeObj.contains("childFrames")) {
                const QJsonArray children = treeObj.value("childFrames").toArray();
                for (const QJsonValue& v : children) {
                    collect(v.toObject());
                }
            }
        };
        collect(result.value("frameTree").toObject());
        if (callback) callback(frames);
    });
}

void PageTracker::waitForLifecycle(const QString& lifecycleName,
                                   std::function<void()> callback,
                                   int timeoutMs)
{
    m_waitLifecycleName = lifecycleName;
    m_waitCallback = callback;

    disconnect(m_waitTimer, nullptr, nullptr, nullptr);
    connect(m_waitTimer, &QTimer::timeout, this, [this]() {
        if (m_waitCallback) {
            m_waitCallback = nullptr;
            m_waitLifecycleName.clear();
        }
    });
    m_waitTimer->start(timeoutMs);
}

void PageTracker::waitForLoad(std::function<void()> callback, int timeoutMs)
{
    waitForLifecycle("load", callback, timeoutMs);
}

void PageTracker::waitForNetworkIdle(std::function<void()> callback, int timeoutMs)
{
    waitForLifecycle("networkIdle", callback, timeoutMs);
}

// === Private ===

void PageTracker::handleEvent(const QString& method, const QJsonObject& params, const QString&)
{
    if (method == "Page.frameStartedNavigating") {
        emit frameStartedNavigating(params.value("frameId").toString(),
                                    QUrl(params.value("url").toString()),
                                    params.value("navigationType").toString());
    }
    else if (method == "Page.frameAttached") {
        emit frameAttached(params.value("frameId").toString(),
                           params.value("parentFrameId").toString());
    }
    else if (method == "Page.frameStartedLoading") {
        emit frameStartedLoading(params.value("frameId").toString());
    }
    else if (method == "Page.frameNavigated") {
        const FrameInfo frame = parseFrame(params.value("frame").toObject());
        const QString navType = params.value("navigationType").toString();
        m_frames[frame.id] = frame;
        if (frame.parentId.isEmpty()) {
            m_currentUrl = frame.url;
            m_currentFrameId = frame.id;
            m_currentLoaderId = frame.loaderId;
        }
        emit frameNavigated(frame, navType);
    }
    else if (method == "Page.frameStoppedLoading") {
        emit frameStoppedLoading(params.value("frameId").toString());
    }
    else if (method == "Page.frameDetached") {
        emit frameDetached(params.value("frameId").toString(),
                          params.value("reason").toString());
    }
    else if (method == "Page.navigatedWithinDocument") {
        const QString fid = params.value("frameId").toString();
        const QUrl url(params.value("url").toString());
        if (m_frames.contains(fid)) {
            m_frames[fid].url = url;
            if (fid == m_currentFrameId) m_currentUrl = url;
        }
        emit navigatedWithinDocument(fid, url, params.value("navigationType").toString());
    }
    else if (method == "Page.lifecycleEvent") {
        LifecycleEvent event;
        event.name = params.value("name").toString();
        event.timestamp = params.value("timestamp").toDouble();
        event.frameId = params.value("frameId").toString();
        event.loaderId = params.value("loaderId").toString();
        m_lifecycleHistory.append(event);
        emit lifecycleEvent(event);

        // Check wait condition
        if (m_waitLifecycleName == event.name && event.frameId == m_currentFrameId) {
            if (m_waitCallback) {
                auto cb = m_waitCallback;
                m_waitCallback = nullptr;
                m_waitLifecycleName.clear();
                m_waitTimer->stop();
                cb();
            }
        }
    }
    else if (method == "Page.domContentEventFired") {
        emit domContentEventFired(params.value("timestamp").toDouble());
    }
    else if (method == "Page.loadEventFired") {
        emit loadEventFired(params.value("timestamp").toDouble());
        if (m_waitLifecycleName == "load" && m_waitCallback) {
            auto cb = m_waitCallback;
            m_waitCallback = nullptr;
            m_waitLifecycleName.clear();
            m_waitTimer->stop();
            cb();
        }
    }
    else if (method == "Page.javascriptDialogOpening") {
        emit dialogOpening(QUrl(params.value("url").toString()),
                          params.value("message").toString(),
                          params.value("type").toString());
    }
    else if (method == "Page.javascriptDialogClosed") {
        emit dialogClosed(params.value("result").toBool(),
                         params.value("userDismissed").toBool());
    }
    else if (method == "Page.fileChooserOpened") {
        emit fileChooserOpened(params.value("frameId").toString(),
                              params.value("mode").toString(),
                              params.value("backendNodeId").toInt());
    }
    else if (method == "Page.windowOpen") {
        emit windowOpen(QUrl(params.value("url").toString()),
                       params.value("windowName").toString(),
                       params.value("windowFeatures").toString(),
                       params.value("userGesture").toBool(false));
    }
}

FrameInfo PageTracker::parseFrame(const QJsonObject& obj)
{
    FrameInfo f;
    f.id = obj.value("id").toString();
    f.parentId = obj.value("parentId").toString();
    f.loaderId = obj.value("loaderId").toString();
    f.url = QUrl(obj.value("url").toString());
    f.domainAndRegistry = obj.value("domainAndRegistry").toString();
    f.securityOrigin = obj.value("securityOrigin").toString();
    f.mimeType = obj.value("mimeType").toString();
    f.secureContextType = obj.value("secureContextType").toString();
    f.crossOriginIsolatedContextType = obj.value("crossOriginIsolatedContextType").toString();
    f.name = obj.value("name").toString();
    f.unreachableUrl = obj.value("unreachableUrl").toString();

    if (obj.contains("gatedAPIFeatures")) {
        const QJsonArray arr = obj.value("gatedAPIFeatures").toArray();
        for (const QJsonValue& v : arr) f.gatedAPIFeatures.append(v.toString());
    }
    if (obj.contains("adFrameStatus")) {
        f.isAdFrame = obj.value("adFrameStatus").toObject().value("adFrame").toBool(false);
    }
    return f;
}

ResourceEntry PageTracker::parseResource(const QJsonObject& obj)
{
    ResourceEntry r;
    r.url = QUrl(obj.value("url").toString());
    r.type = obj.value("type").toString();
    r.mimeType = obj.value("mimeType").toString();
    r.contentSize = static_cast<qint64>(obj.value("contentSize").toDouble(0));
    r.lastModified = obj.value("lastModified").toDouble(0);
    r.canceled = obj.value("canceled").toBool(false);
    r.failed = obj.value("failed").toBool(false);
    return r;
}

FrameResourceTree PageTracker::parseResourceTree(const QJsonObject& obj)
{
    FrameResourceTree tree;
    tree.frame = parseFrame(obj.value("frame").toObject());

    const QJsonArray resources = obj.value("resources").toArray();
    for (const QJsonValue& v : resources) {
        tree.resources.append(parseResource(v.toObject()));
    }

    if (obj.contains("childFrames")) {
        const QJsonArray children = obj.value("childFrames").toArray();
        for (const QJsonValue& v : children) {
            tree.childFrames.append(parseResourceTree(v.toObject()));
        }
    }
    return tree;
}

void PageTracker::collectAllResources(const FrameResourceTree& tree,
                                      QList<QPair<ResourceEntry, QString>>& result,
                                      const QString& frameId)
{
    const QString fid = frameId.isEmpty() ? tree.frame.id : frameId;
    for (const ResourceEntry& r : tree.resources) {
        result.append({r, fid});
    }
    for (const FrameResourceTree& child : tree.childFrames) {
        collectAllResources(child, result, fid);
    }
}

} // namespace devtools
} // namespace nothing
