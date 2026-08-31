import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick.Window 2.15

Window {
    id: splashRoot
    width: 900
    height: 600
    visible: true
    title: "Sabre Browser"
    color: "#0a0a0a"

    // ── Font definitions ──────────────────────────────────────
    FontLoader { id: geistMono;    source: "../../assets/fonts/GeistMono-Regular.ttf" }
    FontLoader { id: geistLight;   source: "../../assets/fonts/Geist-Light.ttf" }
    FontLoader { id: geistPixel;   source: "../../assets/fonts/GeistPixel-Square.ttf" }
    FontLoader { id: notoSans;     source: "../../assets/fonts/NotoSans-Regular.ttf" }
    FontLoader { id: notoArabic;   source: "../../assets/fonts/NotoSansArabic-Regular.ttf" }
    FontLoader { id: notoCJK;      source: "../../assets/fonts/NotoSansCJK-Regular.ttc" }

    // ── State machine ─────────────────────────────────────────
    // 0 = language select
    // 1 = privacy + terms
    // 2 = clock + accept
    property int step: 0
    property string selectedLang: "en"
    property bool accepted: false

    // ── Nothing red dot ───────────────────────────────────────
    Rectangle {
        width: 7
        height: 7
        radius: 4
        color: "#ff2d2d"
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: 16
        z: 10
    }

    // ── STEP 0 — Language Select ──────────────────────────────
    Item {
        id: langPage
        anchors.fill: parent
        visible: step === 0
        opacity: step === 0 ? 1 : 0

        Behavior on opacity { NumberAnimation { duration: 300 } }

        // Logo
        Image {
            id: logo
            source: "../../assets/icons/mainlogo-nobackground.png"
            width: 120
            height: 120
            fillMode: Image.PreserveAspectFit
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            anchors.topMargin: 40
        }

        // Sabre Browser label
        Text {
            text: "SABRE BROWSER"
            font.family: geistPixel.name
            font.pixelSize: 13
            color: "#ffffff"
            font.letterSpacing: 4
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: logo.bottom
            anchors.topMargin: 12
        }

        // Select language label
        Text {
            id: langLabel
            text: "SELECT LANGUAGE"
            font.family: geistMono.name
            font.pixelSize: 11
            color: "#444444"
            font.letterSpacing: 3
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: logo.bottom
            anchors.topMargin: 50
        }

        // Language grid
        GridLayout {
            columns: 3
            columnSpacing: 12
            rowSpacing: 12
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: langLabel.bottom
            anchors.topMargin: 24

            Repeater {
                model: [
                    { code: "en", label: "English" },
                    { code: "sw", label: "Kiswahili" },
                    { code: "ko", label: "한국어" },
                    { code: "zh", label: "中文" },
                    { code: "tl", label: "Filipino" },
                    { code: "ar", label: "العربية" }
                ]

                delegate: Rectangle {
                    width: 160
                    height: 44
                    radius: 2
                    color: selectedLang === modelData.code ? "#1a1a1a" : "transparent"
                    border.color: selectedLang === modelData.code ? "#ffffff" : "#222222"
                    border.width: 1

                    Text {
                        text: modelData.label
                        font.family: modelData.code === "ar" ? notoArabic.name :
                                     (modelData.code === "ko" || modelData.code === "zh") ? notoCJK.name :
                                     geistLight.name
                        font.pixelSize: 13
                        color: selectedLang === modelData.code ? "#ffffff" : "#444444"
                        anchors.centerIn: parent
                    }

                    MouseArea {
                        anchors.fill: parent
                        onClicked: selectedLang = modelData.code
                        cursorShape: Qt.PointingHandCursor
                    }
                }
            }
        }

        // Continue button
        Rectangle {
            width: 160
            height: 40
            radius: 2
            color: "transparent"
            border.color: "#ffffff"
            border.width: 1
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 40

            Text {
                text: "CONTINUE →"
                font.family: geistMono.name
                font.pixelSize: 11
                color: "#ffffff"
                font.letterSpacing: 3
                anchors.centerIn: parent
            }

            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: step = 1
            }
        }
    }

    // ── STEP 1 — Privacy + Terms ──────────────────────────────
    Item {
        id: policyPage
        anchors.fill: parent
        visible: step === 1
        opacity: step === 1 ? 1 : 0

        Behavior on opacity { NumberAnimation { duration: 300 } }

        Text {
            id: policyTitle
            text: "PRIVACY & TERMS"
            font.family: geistPixel.name
            font.pixelSize: 13
            color: "#ffffff"
            font.letterSpacing: 4
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            anchors.topMargin: 50
        }

        Rectangle {
            id: policyBox
            anchors.top: policyTitle.bottom
            anchors.topMargin: 24
            anchors.horizontalCenter: parent.horizontalCenter
            width: parent.width - 120
            height: parent.height - 200
            color: "#0f0f0f"
            border.color: "#1a1a1a"
            border.width: 1
            radius: 2

            Flickable {
                anchors.fill: parent
                anchors.margins: 24
                contentHeight: policyText.height
                clip: true

                Text {
                    id: policyText
                    width: parent.width
                    wrapMode: Text.WordWrap
                    font.family: geistLight.name
                    font.pixelSize: 12
                    color: "#666666"
                    lineHeight: 1.6
                    text: "PRIVACY POLICY\n\n" +
                          "Sabre Browser is built to respect your privacy. We do not collect, " +
                          "store, or transmit any personal data. No telemetry. No analytics. " +
                          "No crash reports sent anywhere without your explicit consent.\n\n" +
                          "Your browsing history, cookies, and session data stay on your device. " +
                          "In incognito mode, all session data is cryptographically wiped the " +
                          "moment you close the window.\n\n" +
                          "Fingerprint spoofing is enabled by default to reduce your trackability " +
                          "across websites. This is done entirely on your device.\n\n" +
                          "────────────────────────────────────\n\n" +
                          "TERMS OF SERVICE\n\n" +
                          "Sabre Browser is provided as-is by Ernest Tech House Co-operation. " +
                          "By using this software you agree that:\n\n" +
                          "1. You will not use Sabre Browser for illegal activities.\n" +
                          "2. Ernest Tech House is not liable for any damages arising from use.\n" +
                          "3. Sabre Browser is open source software licensed under MIT.\n" +
                          "4. Features may change between versions.\n\n" +
                          "Built in Kenya 🇰🇪 — Nothing Central"
                }
            }
        }

        // Back + Continue
        Row {
            spacing: 16
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 40

            Rectangle {
                width: 120
                height: 40
                radius: 2
                color: "transparent"
                border.color: "#333333"
                border.width: 1

                Text {
                    text: "← BACK"
                    font.family: geistMono.name
                    font.pixelSize: 11
                    color: "#444444"
                    font.letterSpacing: 3
                    anchors.centerIn: parent
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: step = 0
                }
            }

            Rectangle {
                width: 120
                height: 40
                radius: 2
                color: "transparent"
                border.color: "#ffffff"
                border.width: 1

                Text {
                    text: "CONTINUE →"
                    font.family: geistMono.name
                    font.pixelSize: 11
                    color: "#ffffff"
                    font.letterSpacing: 3
                    anchors.centerIn: parent
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: step = 2
                }
            }
        }
    }

    // ── STEP 2 — Clock + Accept ───────────────────────────────
    Item {
        id: clockPage
        anchors.fill: parent
        visible: step === 2
        opacity: step === 2 ? 1 : 0

        Behavior on opacity { NumberAnimation { duration: 300 } }

        // Logo small
        Image {
            id: smallLogo
            source: "../../assets/icons/mainlogo-nobackground.png"
            width: 80
            height: 80
            fillMode: Image.PreserveAspectFit
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            anchors.topMargin: 40
        }

        // Big clock
        Text {
            id: clockText
            font.family: geistMono.name
            font.pixelSize: 86
            font.weight: Font.Light
            color: "#ffffff"
            font.letterSpacing: -4
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: smallLogo.bottom
            anchors.topMargin: 16
        }

        // Day Date Month Year
        Text {
            id: dateText
            font.family: geistMono.name
            font.pixelSize: 12
            color: "#333333"
            font.letterSpacing: 4
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: clockText.bottom
            anchors.topMargin: 8
        }

        // Clock timer
        Timer {
            interval: 1000
            running: step === 2
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
            }
        }

        // Accept checkbox row
        Row {
            id: acceptRow
            spacing: 12
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: launchBtn.top
            anchors.bottomMargin: 24

            Rectangle {
                width: 18
                height: 18
                radius: 2
                color: "transparent"
                border.color: accepted ? "#ffffff" : "#333333"
                border.width: 1

                Rectangle {
                    width: 10
                    height: 10
                    radius: 1
                    color: "#ff2d2d"
                    anchors.centerIn: parent
                    visible: accepted
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: accepted = !accepted
                }
            }

            Text {
                text: "I have read and accept the Privacy Policy and Terms of Service"
                font.family: geistLight.name
                font.pixelSize: 12
                color: "#444444"
                anchors.verticalCenter: parent.verticalCenter
            }
        }

        // Launch button
        Rectangle {
            id: launchBtn
            width: 200
            height: 44
            radius: 2
            color: accepted ? "#ffffff" : "transparent"
            border.color: accepted ? "#ffffff" : "#222222"
            border.width: 1
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 40

            Behavior on color { ColorAnimation { duration: 200 } }

            Text {
                text: "LAUNCH SABRE →"
                font.family: geistMono.name
                font.pixelSize: 11
                color: accepted ? "#000000" : "#333333"
                font.letterSpacing: 3
                anchors.centerIn: parent

                Behavior on color { ColorAnimation { duration: 200 } }
            }

            MouseArea {
                anchors.fill: parent
                cursorShape: accepted ? Qt.PointingHandCursor : Qt.ArrowCursor
                onClicked: {
                    if (accepted) {
                        // TODO: save selectedLang, close splash, open main browser
                        console.log("Launching Sabre with lang:", selectedLang)
                    }
                }
            }
        }
    }
}
