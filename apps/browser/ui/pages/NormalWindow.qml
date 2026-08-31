import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtWebEngine 1.15

Window {
    id: browserWindow
    width: 1280
    height: 800
    visible: true
    title: "Sabre Browser"
    color: "#0a0a0a"

    // ── Fonts ─────────────────────────────────────────────────
    FontLoader { id: geistMono;  source: "../../assets/fonts/GeistMono-Regular.ttf" }
    FontLoader { id: geistLight; source: "../../assets/fonts/Geist-Light.ttf" }

    // ── Nothing red dot ───────────────────────────────────────
    Rectangle {
        width: 7; height: 7; radius: 4
        color: "#ff2d2d"
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: 10
        z: 10
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ── Tab bar ───────────────────────────────────────────
        Rectangle {
            Layout.fillWidth: true
            height: 38
            color: "#080808"

            Row {
                id: tabRow
                anchors.left: parent.left
                anchors.leftMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                spacing: 2

                Repeater {
                    model: tabModel
                    delegate: Rectangle {
                        width: 180
                        height: 28
                        radius: 2
                        color: model.active ? "#141414" : "transparent"
                        border.color: model.active ? "#1f1f1f" : "transparent"
                        border.width: 1

                        Row {
                            anchors.fill: parent
                            anchors.margins: 8
                            spacing: 6

                            Text {
                                text: model.title
                                font.family: geistLight.name
                                font.pixelSize: 11
                                color: model.active ? "#ffffff" : "#444444"
                                elide: Text.ElideRight
                                width: parent.width - 20
                                anchors.verticalCenter: parent.verticalCenter
                            }

                            Text {
                                text: "×"
                                font.family: geistMono.name
                                font.pixelSize: 12
                                color: "#333333"
                                anchors.verticalCenter: parent.verticalCenter

                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: tabModel.remove(index)
                                }
                            }
                        }

                        MouseArea {
                            anchors.fill: parent
                            onClicked: {
                                for (var i = 0; i < tabModel.count; i++)
                                    tabModel.setProperty(i, "active", false)
                                tabModel.setProperty(index, "active", true)
                                webView.url = model.url
                            }
                        }
                    }
                }

                // New tab button
                Rectangle {
                    width: 28; height: 28
                    radius: 2
                    color: "transparent"

                    Text {
                        text: "+"
                        font.family: geistMono.name
                        font.pixelSize: 16
                        color: "#333333"
                        anchors.centerIn: parent
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            for (var i = 0; i < tabModel.count; i++)
                                tabModel.setProperty(i, "active", false)
                            tabModel.append({
                                title: "New Tab",
                                url: "about:blank",
                                active: true
                            })
                            webView.url = "about:blank"
                        }
                    }
                }
            }
        }

        // ── Toolbar ───────────────────────────────────────────
        Rectangle {
            Layout.fillWidth: true
            height: 44
            color: "#0d0d0d"

            Row {
                anchors.fill: parent
                anchors.margins: 8
                spacing: 6

                // Back
                Rectangle {
                    width: 28; height: 28
                    radius: 2; color: "transparent"
                    anchors.verticalCenter: parent.verticalCenter

                    Text {
                        text: "←"
                        font.family: geistMono.name
                        font.pixelSize: 14
                        color: webView.canGoBack ? "#888888" : "#2a2a2a"
                        anchors.centerIn: parent
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: if (webView.canGoBack) webView.goBack()
                    }
                }

                // Forward
                Rectangle {
                    width: 28; height: 28
                    radius: 2; color: "transparent"
                    anchors.verticalCenter: parent.verticalCenter

                    Text {
                        text: "→"
                        font.family: geistMono.name
                        font.pixelSize: 14
                        color: webView.canGoForward ? "#888888" : "#2a2a2a"
                        anchors.centerIn: parent
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: if (webView.canGoForward) webView.goForward()
                    }
                }

                // Reload
                Rectangle {
                    width: 28; height: 28
                    radius: 2; color: "transparent"
                    anchors.verticalCenter: parent.verticalCenter

                    Text {
                        text: webView.loading ? "×" : "↻"
                        font.family: geistMono.name
                        font.pixelSize: 14
                        color: "#888888"
                        anchors.centerIn: parent
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: webView.loading ? webView.stop() : webView.reload()
                    }
                }

                // Address bar
                Rectangle {
                    height: 30
                    width: parent.width - 120
                    radius: 2
                    color: "#111111"
                    border.color: addressInput.activeFocus ? "#333333" : "#1a1a1a"
                    border.width: 1
                    anchors.verticalCenter: parent.verticalCenter

                    // Lock icon
                    Text {
                        text: webView.url.toString().startsWith("https") ? "🔒" : "🔓"
                        font.pixelSize: 10
                        anchors.left: parent.left
                        anchors.leftMargin: 8
                        anchors.verticalCenter: parent.verticalCenter
                        color: "#444444"
                    }

                    TextInput {
                        id: addressInput
                        anchors.fill: parent
                        anchors.leftMargin: 28
                        anchors.rightMargin: 8
                        anchors.verticalCenter: parent.verticalCenter
                        font.family: geistMono.name
                        font.pixelSize: 11
                        color: "#cccccc"
                        clip: true
                        verticalAlignment: TextInput.AlignVCenter
                        text: webView.url.toString()

                        onAccepted: {
                            var url = text
                            if (!url.startsWith("http://") && !url.startsWith("https://"))
                                url = "https://" + url
                            webView.url = url
                        }
                    }
                }

                // Incognito toggle
                Rectangle {
                    width: 28; height: 28
                    radius: 2
                    color: "transparent"
                    anchors.verticalCenter: parent.verticalCenter

                    Text {
                        text: "🕶"
                        font.pixelSize: 14
                        anchors.centerIn: parent
                        color: "#444444"
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: console.log("incognito")
                    }
                }
            }
        }

        // ── Loading bar ───────────────────────────────────────
        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: "#111111"

            Rectangle {
                width: webView.loadProgress < 100 ?
                       parent.width * webView.loadProgress / 100 : 0
                height: 1
                color: "#ff2d2d"

                Behavior on width { NumberAnimation { duration: 200 } }
            }
        }

        // ── Web view ──────────────────────────────────────────
        WebEngineView {
            id: webView
            Layout.fillWidth: true
            Layout.fillHeight: true
            url: "https://search.brave.com"
            backgroundColor: "#0a0a0a"

            onTitleChanged: {
                for (var i = 0; i < tabModel.count; i++) {
                    if (tabModel.get(i).active) {
                        tabModel.setProperty(i, "title",
                            title.length > 20 ? title.substring(0, 20) + "…" : title)
                        break
                    }
                }
            }

            onUrlChanged: {
                addressInput.text = url.toString()
                for (var i = 0; i < tabModel.count; i++) {
                    if (tabModel.get(i).active) {
                        tabModel.setProperty(i, "url", url.toString())
                        break
                    }
                }
            }
        }
    }

    // ── Tab model ─────────────────────────────────────────────
    ListModel {
        id: tabModel
        ListElement { title: "Brave Search"; url: "https://search.brave.com"; active: true }
    }
}
