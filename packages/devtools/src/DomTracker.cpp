#include "nothing/devtools/DomTracker.h"
#include <QJsonArray>

namespace nothing {
namespace devtools {

DomTracker::DomTracker(CdpClient* cdp, QObject* parent)
    : QObject(parent)
    , m_cdp(cdp)
{
    connect(m_cdp, &CdpClient::eventReceived,
            this, &DomTracker::handleEvent);
}

void DomTracker::enable(bool includeWhitespace)
{
    if (m_domEnabled) return;
    m_domEnabled = true;
    QJsonObject params;
    params["includeWhitespace"] = includeWhitespace ? "all" : "none";
    m_cdp->sendCommand("DOM.enable", params);
}

void DomTracker::disable()
{
    if (!m_domEnabled) return;
    m_domEnabled = false;
    m_cdp->sendCommand("DOM.disable");
}

void DomTracker::getDocument(int depth, bool pierce,
                             std::function<void(const DomNode&)> callback)
{
    QJsonObject params;
    params["depth"] = depth;
    params["pierce"] = pierce;
    m_cdp->sendCommand("DOM.getDocument", params, [callback](const QJsonObject& result) {
        if (callback) callback(parseNode(result.value("root").toObject()));
    });
}

void DomTracker::querySelector(int nodeId, const QString& selector,
                               std::function<void(int)> callback)
{
    QJsonObject params;
    params["nodeId"] = nodeId;
    params["selector"] = selector;
    m_cdp->sendCommand("DOM.querySelector", params, [callback](const QJsonObject& result) {
        if (callback) callback(result.value("nodeId").toInt(0));
    });
}

void DomTracker::querySelectorAll(int nodeId, const QString& selector,
                                  std::function<void(const QList<int>&)> callback)
{
    QJsonObject params;
    params["nodeId"] = nodeId;
    params["selector"] = selector;
    m_cdp->sendCommand("DOM.querySelectorAll", params, [callback](const QJsonObject& result) {
        QList<int> ids;
        const QJsonArray arr = result.value("nodeIds").toArray();
        for (const QJsonValue& v : arr) ids.append(v.toInt());
        if (callback) callback(ids);
    });
}

void DomTracker::getOuterHTML(int nodeId, std::function<void(const QString&)> callback)
{
    QJsonObject params;
    params["nodeId"] = nodeId;
    m_cdp->sendCommand("DOM.getOuterHTML", params, [callback](const QJsonObject& result) {
        if (callback) callback(result.value("outerHTML").toString());
    });
}

void DomTracker::setOuterHTML(int nodeId, const QString& html)
{
    QJsonObject params;
    params["nodeId"] = nodeId;
    params["outerHTML"] = html;
    m_cdp->sendCommand("DOM.setOuterHTML", params);
}

void DomTracker::setAttributeValue(int nodeId, const QString& name, const QString& value)
{
    QJsonObject params;
    params["nodeId"] = nodeId;
    params["name"] = name;
    params["value"] = value;
    m_cdp->sendCommand("DOM.setAttributeValue", params);
}

void DomTracker::removeAttribute(int nodeId, const QString& name)
{
    QJsonObject params;
    params["nodeId"] = nodeId;
    params["name"] = name;
    m_cdp->sendCommand("DOM.removeAttribute", params);
}

void DomTracker::removeNode(int nodeId)
{
    QJsonObject params;
    params["nodeId"] = nodeId;
    m_cdp->sendCommand("DOM.removeNode", params);
}

void DomTracker::focus(int nodeId)
{
    QJsonObject params;
    params["nodeId"] = nodeId;
    m_cdp->sendCommand("DOM.focus", params);
}

void DomTracker::scrollIntoViewIfNeeded(int nodeId)
{
    QJsonObject params;
    params["nodeId"] = nodeId;
    m_cdp->sendCommand("DOM.scrollIntoViewIfNeeded", params);
}

void DomTracker::resolveNode(int nodeId, const QString& objectGroup,
                             std::function<void(const QJsonObject&)> callback)
{
    QJsonObject params;
    params["nodeId"] = nodeId;
    params["objectGroup"] = objectGroup;
    m_cdp->sendCommand("DOM.resolveNode", params, [callback](const QJsonObject& result) {
        if (callback) callback(result.value("object").toObject());
    });
}

void DomTracker::getBoxModel(int nodeId, std::function<void(const BoxModel&)> callback)
{
    QJsonObject params;
    params["nodeId"] = nodeId;
    m_cdp->sendCommand("DOM.getBoxModel", params, [callback](const QJsonObject& result) {
        if (callback) callback(parseBoxModel(result.value("model").toObject()));
    });
}

void DomTracker::setFileInputFiles(int nodeId, const QStringList& filePaths)
{
    QJsonObject params;
    params["nodeId"] = nodeId;
    QJsonArray files;
    for (const QString& f : filePaths) files.append(f);
    params["files"] = files;
    m_cdp->sendCommand("DOM.setFileInputFiles", params);
}

void DomTracker::getComputedStyle(int nodeId,
                                  std::function<void(const QList<ComputedStyleProperty>&)> callback)
{
    QJsonObject params;
    params["nodeId"] = nodeId;
    m_cdp->sendCommand("CSS.getComputedStyleForNode", params, [callback](const QJsonObject& result) {
        QList<ComputedStyleProperty> props;
        const QJsonArray arr = result.value("computedStyle").toArray();
        for (const QJsonValue& v : arr) {
            const QJsonObject o = v.toObject();
            ComputedStyleProperty p;
            p.name = o.value("name").toString();
            p.value = o.value("value").toString();
            props.append(p);
        }
        if (callback) callback(props);
    });
}

void DomTracker::getMatchedStyles(int nodeId, std::function<void(const QJsonObject&)> callback)
{
    QJsonObject params;
    params["nodeId"] = nodeId;
    m_cdp->sendCommand("CSS.getMatchedStylesForNode", params, [callback](const QJsonObject& result) {
        if (callback) callback(result);
    });
}

void DomTracker::addCssRule(const QString& frameId, const QString& ruleText)
{
    // First create a stylesheet
    QJsonObject createParams;
    createParams["frameId"] = frameId;
    m_cdp->sendCommand("CSS.createStyleSheet", createParams, [this, ruleText](const QJsonObject& result) {
        const QString sheetId = result.value("styleSheetId").toString();
        if (sheetId.isEmpty()) return;

        QJsonObject addParams;
        addParams["styleSheetId"] = sheetId;
        addParams["ruleText"] = ruleText;
        m_cdp->sendCommand("CSS.addRule", addParams);
    });
}

void DomTracker::enableCss()
{
    if (m_cssEnabled) return;
    m_cssEnabled = true;
    m_cdp->sendCommand("CSS.enable");
}

void DomTracker::disableCss()
{
    if (!m_cssEnabled) return;
    m_cssEnabled = false;
    m_cdp->sendCommand("CSS.disable");
}

void DomTracker::requestChildNodes(int nodeId, int depth, bool pierce)
{
    QJsonObject params;
    params["nodeId"] = nodeId;
    params["depth"] = depth;
    params["pierce"] = pierce;
    m_cdp->sendCommand("DOM.requestChildNodes", params);
}

void DomTracker::handleEvent(const QString& method, const QJsonObject& params, const QString&)
{
    if (method == "DOM.setChildNodes") {
        const int parentId = params.value("parentId").toInt();
        QList<DomNode> nodes;
        const QJsonArray arr = params.value("nodes").toArray();
        for (const QJsonValue& v : arr) {
            nodes.append(parseNode(v.toObject()));
        }
        emit setChildNodes(parentId, nodes);
    }
    else if (method == "DOM.attributeModified") {
        emit attributeModified(params.value("nodeId").toInt(),
                              params.value("name").toString(),
                              params.value("value").toString());
    }
    else if (method == "DOM.attributeRemoved") {
        emit attributeRemoved(params.value("nodeId").toInt(),
                             params.value("name").toString());
    }
    else if (method == "DOM.characterDataModified") {
        emit characterDataModified(params.value("nodeId").toInt(),
                                   params.value("characterData").toString());
    }
    else if (method == "DOM.childNodeCountUpdated") {
        emit childNodeCountUpdated(params.value("nodeId").toInt(),
                                   params.value("childNodeCount").toInt());
    }
    else if (method == "DOM.childNodeInserted") {
        emit childNodeInserted(params.value("parentNodeId").toInt(),
                               params.value("previousNodeId").toInt(),
                               parseNode(params.value("node").toObject()));
    }
    else if (method == "DOM.childNodeRemoved") {
        emit childNodeRemoved(params.value("parentNodeId").toInt(),
                              params.value("nodeId").toInt());
    }
    else if (method == "DOM.shadowRootPushed") {
        emit shadowRootPushed(params.value("hostId").toInt(),
                             parseNode(params.value("root").toObject()));
    }
    else if (method == "DOM.shadowRootPopped") {
        emit shadowRootPopped(params.value("hostId").toInt(),
                             params.value("rootId").toInt());
    }
    else if (method == "DOM.documentUpdated") {
        emit documentUpdated();
    }
}

DomNode DomTracker::parseNode(const QJsonObject& obj)
{
    DomNode node;
    node.nodeId = obj.value("nodeId").toInt();
    node.parentId = obj.value("parentId").toInt(0);
    node.backendNodeId = obj.value("backendNodeId").toInt(0);
    node.nodeType = obj.value("nodeType").toInt();
    node.nodeName = obj.value("nodeName").toString();
    node.localName = obj.value("localName").toString();
    node.nodeValue = obj.value("nodeValue").toString();
    node.childNodeCount = obj.value("childNodeCount").toInt(0);

    const QJsonArray attrs = obj.value("attributes").toArray();
    for (const QJsonValue& a : attrs) {
        node.attributes.append(a.toString());
    }

    if (obj.contains("children")) {
        const QJsonArray children = obj.value("children").toArray();
        for (const QJsonValue& c : children) {
            node.children.append(parseNode(c.toObject()));
        }
    }

    node.frameId = obj.value("frameId").toString();
    node.shadowRootType = obj.value("shadowRootType").toString();
    node.pseudoType = obj.value("pseudoType").toString();
    node.documentUrl = obj.value("documentURL").toString();
    node.baseUrl = obj.value("baseURL").toString();
    return node;
}

BoxModel DomTracker::parseBoxModel(const QJsonObject& obj)
{
    BoxModel model;
    auto parseQuad = [](const QJsonArray& arr) -> QList<double> {
        QList<double> result;
        for (const QJsonValue& v : arr) result.append(v.toDouble());
        return result;
    };
    if (obj.contains("content")) model.content = parseQuad(obj.value("content").toArray());
    if (obj.contains("padding")) model.padding = parseQuad(obj.value("padding").toArray());
    if (obj.contains("border"))  model.border = parseQuad(obj.value("border").toArray());
    if (obj.contains("margin"))  model.margin = parseQuad(obj.value("margin").toArray());
    model.width = obj.value("width").toInt();
    model.height = obj.value("height").toInt();
    return model;
}

} // namespace devtools
} // namespace nothing
