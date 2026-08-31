import QtQuick 2.15
import QtWebEngine 1.15
import "."

Item {
    id: tabView
    anchors.fill: parent

    property string initialUrl: "about://home"
    property bool showHome: true
    property var webProfile: null

    property string url: ""
    property string title: ""
    property bool canGoBack: false
    property bool canGoForward: false
    property int loadProgress: 0
    property bool loading: false

    function goBack()    { webView.goBack() }
    function goForward() { webView.goForward() }
    function reload()    { webView.reload() }
    function stop()      { webView.stop() }
    function navigate(u) {
        if (u === "about://home") {
            showHome = true
        } else {
            showHome = false
            webView.url = u
        }
        tabView.url = u
    }

    Loader {
        anchors.fill: parent
        active: showHome
        visible: showHome
        z: 1

        sourceComponent: Component {
            NewTabPage {
                anchors.fill: parent
                onNewTabSearched: function(u) {
                    showHome = false
                    webView.url = u
                    tabView.url = u
                }
            }
        }
    }

    WebEngineView {
        id: webView
        anchors.fill: parent
        visible: !showHome
        z: 0
        backgroundColor: "#0a0a0a"
        profile: tabView.webProfile

        onTitleChanged:        { tabView.title = webView.title }
        onUrlChanged:          {
            tabView.url = webView.url.toString()
            if (webView.url.toString() !== "about:blank") showHome = false
        }
        onCanGoBackChanged:    { tabView.canGoBack = webView.canGoBack }
        onCanGoForwardChanged: { tabView.canGoForward = webView.canGoForward }
        onLoadProgressChanged: { tabView.loadProgress = webView.loadProgress }
        onLoadingChanged: function(info) {
            tabView.loading = (info.status === WebEngineLoadingInfo.LoadStartedStatus ||
                               info.status === WebEngineLoadingInfo.LoadInProgressStatus)
        }
    }

    Component.onCompleted: {
        if (initialUrl !== "about://home") {
            showHome = false
            webView.url = initialUrl
            tabView.url = initialUrl
        }
    }
}