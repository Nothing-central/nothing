import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick.Window 2.15
import QtWebEngine 1.15

Window {
    id: browserWindow
    width: 1280
    height: 800
    visible: true
    title: "Sabre Browser"
    color: "#0a0a0a"

    FontLoader { id: geistMono;  source: "../../assets/fonts/GeistMono-Regular.ttf" }
    FontLoader { id: geistLight; source: "../../assets/fonts/Geist-Light.ttf" }
    FontLoader { id: geistPixel; source: "../../assets/fonts/GeistPixel-Square.ttf" }

    property bool isLoading: false
    property bool sidebarOpen: false

    WebEngineScript {
        id: fpScript
        injectionPoint: WebEngineScript.DocumentCreation
        worldId: WebEngineScript.MainWorld
        runOnSubframes: true
    }

    WebEngineProfile {
        id: sessionProfile
        storageName: "default"
        offTheRecord: false
        userScripts: [fpScript]

        Component.onCompleted: {
            fpScript.sourceCode = Fingerprint.sessionScript("default")
        }
    }

    Rectangle {
        width: 7; height: 7; radius: 4
        color: "#ff2d2d"
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: 10
        z: 10
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            id: sidebar
            width: sidebarOpen ? 220 : 0
            Layout.fillHeight: true
            color: "#080808"
            clip: true
            Behavior on width { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }

            Column {
                anchors.fill: parent
                anchors.margins: 16
                spacing: 0
                visible: sidebarOpen

                Text {
                    text: "BOOKMARKS"
                    font.family: geistMono.name
                    font.pixelSize: 9
                    color: "#333333"
                    font.letterSpacing: 3
                    bottomPadding: 16
                }

                Repeater {
                    model: bookmarkModel
                    delegate: Rectangle {
                        width: parent.width
                        height: 36
                        color: "transparent"
                        radius: 2

                        Row {
                            anchors.fill: parent
                            anchors.leftMargin: 4
                            spacing: 8
                            Text {
                                text: model.icon
                                font.pixelSize: 12
                                anchors.verticalCenter: parent.verticalCenter
                            }
                            Text {
                                text: model.label
                                font.family: geistLight.name
                                font.pixelSize: 11
                                color: "#555555"
                                elide: Text.ElideRight
                                width: parent.width - 40
                                anchors.verticalCenter: parent.verticalCenter
                            }
                        }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            hoverEnabled: true
                            onEntered: parent.color = "#111111"
                            onExited:  parent.color = "transparent"
                            onClicked: navigateTo(model.url)
                        }
                    }
                }

                Rectangle {
                    width: parent.width
                    height: 36
                    color: "transparent"
                    radius: 2

                    Row {
                        anchors.fill: parent
                        anchors.leftMargin: 4
                        spacing: 8
                        Text {
                            text: "+"
                            font.family: geistMono.name
                            font.pixelSize: 14
                            color: "#222222"
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        Text {
                            text: "ADD CURRENT"
                            font.family: geistMono.name
                            font.pixelSize: 9
                            color: "#222222"
                            font.letterSpacing: 2
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        hoverEnabled: true
                        onEntered: parent.color = "#111111"
                        onExited:  parent.color = "transparent"
                        onClicked: {
                            var wv = currentWebView()
                            if (wv && wv.url !== "" && wv.url !== "about://home") {
                                bookmarkModel.append({
                                    label: wv.title !== "" ? wv.title : wv.url,
                                    url: wv.url,
                                    icon: "🔗"
                                })
                            }
                        }
                    }
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            Rectangle {
                Layout.fillWidth: true
                height: 38
                color: "#080808"

                Row {
                    anchors.left: parent.left
                    anchors.leftMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 2

                    Repeater {
                        model: tabModel
                        delegate: Rectangle {
                            width: 180; height: 28
                            radius: 2
                            color: model.active ? "#141414" : "transparent"
                            border.color: model.active ? "#1f1f1f" : "transparent"
                            border.width: 1

                            Row {
                                anchors.fill: parent
                                anchors.leftMargin: 10
                                anchors.rightMargin: 6
                                spacing: 6

                                Text {
                                    text: model.title
                                    font.family: geistLight.name
                                    font.pixelSize: 11
                                    color: model.active ? "#ffffff" : "#444444"
                                    elide: Text.ElideRight
                                    width: parent.width - 24
                                    anchors.verticalCenter: parent.verticalCenter
                                }

                                Text {
                                    text: "×"
                                    font.family: geistMono.name
                                    font.pixelSize: 13
                                    color: "#2a2a2a"
                                    anchors.verticalCenter: parent.verticalCenter

                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: {
                                            if (tabModel.count === 1) {
                                                browserWindow.close()
                                                return
                                            }
                                            var item = webStack.children[index]
                                            if (item) item.destroy()
                                            tabModel.remove(index)
                                            var next = Math.min(index, tabModel.count - 1)
                                            for (var i = 0; i < tabModel.count; i++)
                                                tabModel.setProperty(i, "active", i === next)
                                            webStack.currentIndex = next
                                        }
                                    }
                                }
                            }

                            MouseArea {
                                anchors.fill: parent
                                onClicked: {
                                    for (var i = 0; i < tabModel.count; i++)
                                        tabModel.setProperty(i, "active", i === index)
                                    webStack.currentIndex = index
                                    var wv = currentWebView()
                                    if (wv) addressInput.text = wv.url
                                }
                            }
                        }
                    }

                    Rectangle {
                        width: 28; height: 28
                        radius: 2; color: "transparent"
                        Text {
                            text: "+"
                            font.family: geistMono.name
                            font.pixelSize: 16
                            color: "#2a2a2a"
                            anchors.centerIn: parent
                        }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: addTab("New Tab", "about://home")
                        }
                    }
                }
            }

            Rectangle {
                id: toolbar
                Layout.fillWidth: true
                height: 46
                color: "#0d0d0d"

                SequentialAnimation on color {
                    running: isLoading
                    loops: Animation.Infinite
                    ColorAnimation { to: "#111111"; duration: 600 }
                    ColorAnimation { to: "#0d0d0d"; duration: 600 }
                }

                Row {
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    spacing: 4

                    ToolBtn {
                        icon: "../../assets/icons/menu.svg"
                        onClicked: sidebarOpen = !sidebarOpen
                        active: sidebarOpen
                    }

                    ToolBtn {
                        icon: "../../assets/icons/back.svg"
                        enabled: {
                            var wv = currentWebView()
                            return wv ? wv.canGoBack : false
                        }
                        onClicked: {
                            var wv = currentWebView()
                            if (wv && wv.canGoBack) wv.goBack()
                        }
                    }

                    ToolBtn {
                        icon: "../../assets/icons/forward.svg"
                        enabled: {
                            var wv = currentWebView()
                            return wv ? wv.canGoForward : false
                        }
                        onClicked: {
                            var wv = currentWebView()
                            if (wv && wv.canGoForward) wv.goForward()
                        }
                    }

                    ToolBtn {
                        icon: isLoading ? "../../assets/icons/close.svg" : "../../assets/icons/reload.svg"
                        spinning: isLoading
                        onClicked: {
                            var wv = currentWebView()
                            if (!wv) return
                            isLoading ? wv.stop() : wv.reload()
                        }
                    }

                    Rectangle {
                        height: 32
                        width: parent.width - 200
                        radius: 2
                        color: "#111111"
                        border.color: addressInput.activeFocus ? "#2a2a2a" : "#161616"
                        border.width: 1
                        anchors.verticalCenter: parent.verticalCenter
                        Behavior on border.color { ColorAnimation { duration: 150 } }

                        Row {
                            anchors.fill: parent
                            anchors.leftMargin: 10
                            anchors.rightMargin: 10
                            spacing: 8

                            Text {
                                text: {
                                    var wv = currentWebView()
                                    if (!wv || wv.url === "" || wv.url === "about://home") return "◈"
                                    return wv.url.startsWith("https") ? "●" : "○"
                                }
                                font.family: geistMono.name
                                font.pixelSize: 9
                                color: {
                                    var wv = currentWebView()
                                    if (!wv || wv.url === "" || wv.url === "about://home") return "#ff2d2d"
                                    return wv.url.startsWith("https") ? "#2a6e2a" : "#444444"
                                }
                                anchors.verticalCenter: parent.verticalCenter
                            }

                            TextInput {
                                id: addressInput
                                width: parent.width - 30
                                anchors.verticalCenter: parent.verticalCenter
                                font.family: geistMono.name
                                font.pixelSize: 11
                                color: "#aaaaaa"
                                clip: true

                                onAccepted: {
                                    var raw = text.trim()
                                    if (raw === "") return
                                    var isUrl = raw.indexOf(".") !== -1 &&
                                                raw.indexOf(" ") === -1 &&
                                                !raw.startsWith("about://")
                                    var url = isUrl
                                        ? (raw.startsWith("http") ? raw : "https://" + raw)
                                        : "https://search.brave.com/search?q=" + encodeURIComponent(raw)
                                    navigateTo(url)
                                }
                            }
                        }
                    }

                    ToolBtn {
                        icon: "../../assets/icons/incognito.svg"
                        onClicked: {
                            var comp = Qt.createComponent(Qt.resolvedUrl("IncognitoWindow.qml"))
                            if (comp.status === Component.Ready)
                                comp.createObject(null).show()
                        }
                    }

                    ToolBtn {
                        icon: "../../assets/icons/bookmark.svg"
                        onClicked: {
                            var wv = currentWebView()
                            if (!wv || wv.url === "" || wv.url === "about://home") return
                            bookmarkModel.append({
                                label: wv.title !== "" ? wv.title : wv.url,
                                url: wv.url,
                                icon: "🔗"
                            })
                        }
                    }

                    ToolBtn {
                        icon: "../../assets/icons/settings.svg"
                        onClicked: navigateTo("about://settings")
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: "#0d0d0d"
                Rectangle {
                    id: progressBar
                    width: 0; height: 1
                    color: "#ff2d2d"
                    Behavior on width { NumberAnimation { duration: 150 } }
                }
            }

            StackLayout {
                id: webStack
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: 0
            }
        }
    }

    component ToolBtn: Rectangle {
        id: btnRoot
        property string icon: ""
        property bool active: false
        property bool spinning: false
        property bool enabled: true

        signal clicked()

        width: 32; height: 32
        radius: 2
        color: active ? "#141414" : "transparent"
        anchors.verticalCenter: parent ? parent.verticalCenter : undefined

        Image {
            id: btnIcon
            anchors.centerIn: parent
            source: btnRoot.icon
            width: 18; height: 18
            smooth: true
            fillMode: Image.PreserveAspectFit
            opacity: btnRoot.enabled ? (btnRoot.active ? 1.0 : 0.7) : 0.25

            RotationAnimation {
                target: btnIcon
                property: "rotation"
                running: btnRoot.spinning && btnRoot.enabled
                loops: Animation.Infinite
                from: 0; to: 360
                duration: 800
            }
        }

        MouseArea {
            anchors.fill: parent
            cursorShape: btnRoot.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
            onClicked: if (btnRoot.enabled) btnRoot.clicked()
        }
    }

    ListModel { id: tabModel }
    ListModel { id: bookmarkModel }

    function currentWebView() {
        var idx = webStack.currentIndex
        if (idx < 0 || idx >= webStack.children.length) return null
        return webStack.children[idx]
    }

    function navigateTo(url) {
        var wv = currentWebView()
        if (!wv) return
        wv.navigate(url)
        addressInput.text = url
    }

    function addTab(title, url) {
        for (var i = 0; i < tabModel.count; i++)
            tabModel.setProperty(i, "active", false)
        tabModel.append({ title: title, url: url, active: true })

        var comp = Qt.createComponent(Qt.resolvedUrl("../components/TabWebView.qml"))
        if (comp.status !== Component.Ready) {
            console.error("TabWebView failed:", comp.errorString())
            return
        }
        var wv = comp.createObject(webStack, { initialUrl: url, webProfile: sessionProfile })
        if (!wv) { console.error("createObject failed"); return }

        var tabIndex = tabModel.count - 1

        wv.titleChanged.connect(function() {
            tabModel.setProperty(tabIndex, "title",
                wv.title.length > 22 ? wv.title.substring(0, 22) + "…" : wv.title)
        })
        wv.urlChanged.connect(function() {
            tabModel.setProperty(tabIndex, "url", wv.url)
            if (webStack.currentIndex === tabIndex)
                addressInput.text = wv.url
            progressBar.width = 0
        })
        wv.loadingChanged.connect(function() {
            isLoading = wv.loading
            if (!wv.loading) progressBar.width = 0
        })
        wv.loadProgressChanged.connect(function() {
            if (webStack.currentIndex === tabIndex)
                progressBar.width = wv.loadProgress < 100
                    ? (toolbar.width * wv.loadProgress / 100) : 0
        })

        webStack.currentIndex = tabIndex
    }

    Component.onCompleted: {
        addTab("New Tab", "about://home")
    }
}