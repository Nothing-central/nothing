#pragma once

#include <QObject>
#include <QHash>
#include "nothing/devtools/CdpClient.h"
#include "nothing/devtools/DevToolsModels.h"

namespace nothing {
namespace devtools {

/// DomTracker — Tracks the DOM tree, captures mutations, provides CSS inspection.
class DomTracker : public QObject {
    Q_OBJECT
public:
    explicit DomTracker(CdpClient* cdp, QObject* parent = nullptr);

    /// Enable DOM tracking.
    /// @param includeWhitespace  "all" to include whitespace-only text nodes
    void enable(bool includeWhitespace = false);

    /// Disable DOM tracking.
    void disable();

    /// Get the document root node.
    /// @param depth  -1 for full tree, 0 for just the root, 2 for default
    /// @param pierce  true to recurse into iframes and shadow roots
    void getDocument(int depth = 2, bool pierce = false,
                     std::function<void(const DomNode&)> callback = nullptr);

    /// Query a selector on a node.
    void querySelector(int nodeId, const QString& selector,
                       std::function<void(int)> callback);

    /// Query all matches.
    void querySelectorAll(int nodeId, const QString& selector,
                          std::function<void(const QList<int>&)> callback);

    /// Get outer HTML of a node.
    void getOuterHTML(int nodeId, std::function<void(const QString&)> callback);

    /// Set outer HTML of a node.
    void setOuterHTML(int nodeId, const QString& html);

    /// Set a single attribute value.
    void setAttributeValue(int nodeId, const QString& name, const QString& value);

    /// Remove an attribute.
    void removeAttribute(int nodeId, const QString& name);

    /// Remove a node from its parent.
    void removeNode(int nodeId);

    /// Focus an element.
    void focus(int nodeId);

    /// Scroll element into view if not visible.
    void scrollIntoViewIfNeeded(int nodeId);

    /// Resolve a nodeId to a Runtime.RemoteObject.
    void resolveNode(int nodeId, const QString& objectGroup,
                     std::function<void(const QJsonObject&)> callback);

    /// Get the box model for a node.
    void getBoxModel(int nodeId, std::function<void(const BoxModel&)> callback);

    /// Set files on an <input type="file"> element.
    void setFileInputFiles(int nodeId, const QStringList& filePaths);

    /// Get computed style for a node.
    void getComputedStyle(int nodeId,
                         std::function<void(const QList<ComputedStyleProperty>&)> callback);

    /// Get matched CSS rules for a node.
    void getMatchedStyles(int nodeId, std::function<void(const QJsonObject&)> callback);

    /// Create a new stylesheet and add a CSS rule.
    void addCssRule(const QString& frameId, const QString& ruleText);

    /// Enable CSS domain.
    void enableCss();

    /// Disable CSS domain.
    void disableCss();

    /// Request children of a node (lazy expansion).
    void requestChildNodes(int nodeId, int depth = 1, bool pierce = false);

signals:
    /// Emitted when children are first requested for a node.
    void setChildNodes(int parentId, const QList<DomNode>& nodes);

    /// Emitted when an attribute is modified.
    void attributeModified(int nodeId, const QString& name, const QString& value);

    /// Emitted when an attribute is removed.
    void attributeRemoved(int nodeId, const QString& name);

    /// Emitted when text content changes.
    void characterDataModified(int nodeId, const QString& newValue);

    /// Emitted when child count changes (for collapsed nodes).
    void childNodeCountUpdated(int nodeId, int count);

    /// Emitted when a child is inserted (for expanded nodes).
    void childNodeInserted(int parentNodeId, int previousNodeId, const DomNode& node);

    /// Emitted when a child is removed.
    void childNodeRemoved(int parentNodeId, int nodeId);

    /// Emitted when a shadow root is pushed.
    void shadowRootPushed(int hostId, const DomNode& root);

    /// Emitted when a shadow root is popped.
    void shadowRootPopped(int hostId, int rootId);

    /// Emitted when the entire document is replaced (navigation).
    void documentUpdated();

private:
    void handleEvent(const QString& method, const QJsonObject& params, const QString& sessionId);

    static DomNode parseNode(const QJsonObject& obj);
    static BoxModel parseBoxModel(const QJsonObject& obj);

    CdpClient* m_cdp;
    bool m_domEnabled = false;
    bool m_cssEnabled = false;
};

} // namespace devtools
} // namespace nothing
