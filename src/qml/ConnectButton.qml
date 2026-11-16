import QtQuick
import QtQuick.Controls.Basic

// Large circular connect button — Vypr family-resemblance motif.
// Gold ring when disconnected, teal/green when connected.
Item {
    id: root
    property bool connected: false
    signal toggled()

    width: 160
    height: 160

    Rectangle {
        id: ring
        anchors.fill: parent
        radius: width / 2
        color: root.connected ? "#1d3a35" : "#2a2013"
        border.color: root.connected ? "#2bbfa4" : "#f0b429"
        border.width: 4

        Behavior on border.color { ColorAnimation { duration: 250 } }
        Behavior on color { ColorAnimation { duration: 250 } }
    }

    Text {
        anchors.centerIn: parent
        text: root.connected ? qsTr("DISCONNECT") : qsTr("CONNECT")
        color: root.connected ? "#2bbfa4" : "#f0b429"
        font.family: "Rubik"
        font.pixelSize: 16
        font.letterSpacing: 1.5
        font.bold: true
    }

    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        onClicked: root.toggled()
    }

    // Press feedback
    scale: pressArea.pressed ? 0.96 : 1.0
    Behavior on scale { NumberAnimation { duration: 80 } }
    MouseArea {
        id: pressArea
        anchors.fill: parent
    }
}
