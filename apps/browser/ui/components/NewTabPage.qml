import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Item {
    id: newTabRoot
    anchors.fill: parent

    // ── Background image ──────────────────────────────────────
    Image {
        anchors.fill: parent
        source: "../../assets/images/mainlogo-rectangle.jpeg"
        fillMode: Image.PreserveAspectCrop
        opacity: 0.18
    }

    // Dark overlay
    Rectangle {
        anchors.fill: parent
        color: "#0a0a0a"
        opacity: 0.82
    }

    // ── Red dot ───────────────────────────────────────────────
    Rectangle {
        width: 6; height: 6; radius: 3
        color: "#ff2d2d"
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.topMargin: 32
    }

    // ── Clock ─────────────────────────────────────────────────
    Column {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: 60
        spacing: 4

        Text {
            id: clockText
            font.family: geistMono.name
            font.pixelSize: 72
            font.weight: Font.Light
            color: "#ffffff"
            font.letterSpacing: -3
            anchors.horizontalCenter: parent.horizontalCenter
        }

        Text {
            id: dateText
            font.family: geistMono.name
            font.pixelSize: 11
            color: "#333333"
            font.letterSpacing: 4
            anchors.horizontalCenter: parent.horizontalCenter
        }

        Text {
            id: tzText
            font.family: geistMono.name
            font.pixelSize: 10
            color: "#222222"
            font.letterSpacing: 3
            anchors.horizontalCenter: parent.horizontalCenter
        }
    }

    Timer {
        interval: 1000
        running: true
        repeat: true
        triggeredOnStart: true
        onTriggered: {
            var now = new Date()
            var h = String(now.getHours()).padStart(2, "0")
            var m = String(now.getMinutes()).padStart(2, "0")
            var s = String(now.getSeconds()).padStart(2, "0")
            clockText.text = h + ":" + m + ":" + s

            var days = ["SUN","MON","TUE","WED","THU","FRI","SAT"]
            var months = ["JAN","FEB","MAR","APR","MAY","JUN",
                          "JUL","AUG","SEP","OCT","NOV","DEC"]
            dateText.text = days[now.getDay()] + "  ·  " +
                            now.getDate() + "  " +
                            months[now.getMonth()] + "  " +
                            now.getFullYear()

            // Timezone abbreviation
            var tz = now.toLocaleTimeString("en", {timeZoneName: "short"})
            var parts = tz.split(" ")
            tzText.text = parts[parts.length - 1]
        }
    }

    // ── Search bar ────────────────────────────────────────────
    Column {
        anchors.centerIn: parent
        anchors.verticalCenterOffset: 20
        spacing: 12
        width: Math.min(parent.width * 0.55, 620)

        // Engine label
        Text {
            id: engineLabel
            text: "BRAVE SEARCH  ·  CHANGE IN SETTINGS"
            font.family: geistMono.name
            font.pixelSize: 9
            color: "#222222"
            font.letterSpacing: 3
            anchors.horizontalCenter: parent.horizontalCenter
        }

        // Search input
        Rectangle {
            width: parent.width
            height: 52
            radius: 2
            color: "#0f0f0f"
            border.color: searchInput.activeFocus ? "#333333" : "#1a1a1a"
            border.width: 1

            Behavior on border.color { ColorAnimation { duration: 150 } }

            Row {
                anchors.fill: parent
                anchors.leftMargin: 18
                anchors.rightMargin: 18
                spacing: 12

                Text {
                    text: "/"
                    font.family: geistMono.name
                    font.pixelSize: 18
                    color: "#ff2d2d"
                    anchors.verticalCenter: parent.verticalCenter
                }

                TextInput {
                    id: searchInput
                    width: parent.width - 60
                    anchors.verticalCenter: parent.verticalCenter
                    font.family: geistMono.name
                    font.pixelSize: 14
                    color: "#ffffff"
                    clip: true
                    focus: true

                    // Placeholder
                    Text {
                        visible: !searchInput.text && !searchInput.activeFocus
                        text: "SEARCH_"
                        font.family: geistMono.name
                        font.pixelSize: 14
                        color: "#222222"
                        font.letterSpacing: 2
                        anchors.verticalCenter: parent.verticalCenter
                    }

                    onAccepted: {
                        if (text.trim() === "") return
                        var query = text.trim()
                        var isUrl = query.indexOf(".") !== -1 && query.indexOf(" ") === -1

                        var url
                        if (isUrl) {
                            url = query.startsWith("http") ? query : "https://" + query
                        } else {
                            // Default: Brave Search. Replace with SearchEngineManager later
                            url = "https://search.brave.com/search?q=" + encodeURIComponent(query)
                        }
                        newTabSearched(url)
                    }
                }
            }
        }
    }

    // ── Favourites ────────────────────────────────────────────
    Column {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 48
        spacing: 16
        width: Math.min(parent.width * 0.7, 700)

        Text {
            text: "FAVOURITES"
            font.family: geistMono.name
            font.pixelSize: 9
            color: "#1a1a1a"
            font.letterSpacing: 4
            anchors.horizontalCenter: parent.horizontalCenter
        }

        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 12

            Repeater {
                model: favouriteModel

                delegate: Rectangle {
                    width: 100
                    height: 60
                    radius: 2
                    color: "#0d0d0d"
                    border.color: "#1a1a1a"
                    border.width: 1

                    Column {
                        anchors.centerIn: parent
                        spacing: 6

                        Text {
                            text: model.icon
                            font.pixelSize: 18
                            anchors.horizontalCenter: parent.horizontalCenter
                        }

                        Text {
                            text: model.label
                            font.family: geistMono.name
                            font.pixelSize: 9
                            color: "#333333"
                            font.letterSpacing: 2
                            anchors.horizontalCenter: parent.horizontalCenter
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: newTabSearched(model.url)
                    }
                }
            }

            // Add button
            Rectangle {
                width: 100
                height: 60
                radius: 2
                color: "transparent"
                border.color: "#1a1a1a"
                border.width: 1

                Text {
                    text: "+"
                    font.family: geistMono.name
                    font.pixelSize: 20
                    color: "#222222"
                    anchors.centerIn: parent
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor

                    // Short press — add current page
                    onClicked: addCurrentPage()

                    // Long press — open modal to type URL
                    onPressAndHold: addModal.open()
                }
            }
        }
    }

    // ── Add favourite modal ───────────────────────────────────
    Popup {
        id: addModal
        anchors.centerIn: parent
        width: 420
        height: 160
        modal: true
        background: Rectangle {
            color: "#0f0f0f"
            border.color: "#1f1f1f"
            border.width: 1
            radius: 2
        }

        Column {
            anchors.fill: parent
            anchors.margins: 24
            spacing: 16

            Text {
                text: "ADD FAVOURITE"
                font.family: geistMono.name
                font.pixelSize: 10
                color: "#444444"
                font.letterSpacing: 3
            }

            Rectangle {
                width: parent.width
                height: 36
                radius: 2
                color: "#111111"
                border.color: modalInput.activeFocus ? "#333333" : "#1a1a1a"
                border.width: 1

                // Placeholder text as sibling before TextInput
                Text {
                    visible: !modalInput.text
                    text: "https://"
                    font.family: geistMono.name
                    font.pixelSize: 12
                    color: "#222222"
                    anchors.left: parent.left
                    anchors.leftMargin: 12
                    anchors.verticalCenter: parent.verticalCenter
                }

                TextInput {
                    id: modalInput
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    anchors.rightMargin: 12
                    verticalAlignment: TextInput.AlignVCenter
                    font.family: geistMono.name
                    font.pixelSize: 12
                    color: "#cccccc"
                    clip: true

                    onAccepted: {
                        if (text.trim() !== "") {
                            var u = text.trim()
                            var label = u.replace("https://","").replace("http://","").split("/")[0]
                            favouriteModel.append({ label: label, url: u, icon: "🔗" })
                            modalInput.text = ""
                            addModal.close()
                        }
                    }
                }
            }
        }
    }

    // ── Favourites model ──────────────────────────────────────
    ListModel {
        id: favouriteModel
    }

    // ── Signals ───────────────────────────────────────────────
    signal newTabSearched(string url)
    signal addCurrentPage()
}