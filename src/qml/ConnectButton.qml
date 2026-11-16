import QtQuick
import QtQuick.Controls.Basic

// Large circular connect button — Vypr family-resemblance motif.
// Gold when disconnected, teal/green when connected, pulsing when busy.
Item {
    id: root
    property bool connected: false
    property bool busy: false
    signal toggled()

    width: 160
    height: 160

    Rectangle {
        id: ring
        anchors.fill: parent
        radius: width / 2
        color: {
            if (root.busy) return "#2a2418"
            return root.connected ? "#1d3a35" : "#2a2013"
        }
        border.color: {
            if (root.busy) return "#c7a044"
            return root.connected ? "#2bbfa4" : "#f0b429"
        }
        border.width: 4

        SequentialAnimation on border.color {
            running: root.busy
            loops: Animation.Infinite
            ColorAnimation { from: "#c7a044"; to: "#f0b429"; duration: 700 }
            ColorAnimation { from: "#f0b429"; to: "#c7a044"; duration: 700 }
        }

        Behavior on border.color { ColorAnimation { duration: 250 } }
        Behavior on color { ColorAnimation { duration: 250 } }
    }

    Text {
        anchors.centerIn: parent
        text: {
            if (root.busy) return qsTr("…")
            return root.connected ? qsTr("DISCONNECT") : qsTr("CONNECT")
        }
        color: ring.border.color
        font.family: "Rubik"
        font.pixelSize: 16
        font.letterSpacing: 1.5
        font.bold: true
    }

    scale: pressArea.pressed && !root.busy ? 0.96 : 1.0
    Behavior on scale { NumberAnimation { duration: 80 } }

    MouseArea {
        id: pressArea
        anchors.fill: parent
        enabled: !root.busy
        cursorShape: root.busy ? Qt.BusyCursor : Qt.PointingHandCursor
        onClicked: root.toggled()
    }
}
