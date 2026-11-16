import QtQuick
import QtQuick.Controls.Basic

// Dark Vypr-style shell. Phase 1 will flesh out: login, server list, status view.
ApplicationWindow {
    id: root
    width: 380
    height: 560
    visible: true
    title: qsTr("VyprKonnect")

    color: "#16181d"

    Column {
        anchors.centerIn: parent
        spacing: 24

        ConnectButton {
            anchors.horizontalCenter: parent.horizontalCenter
            connected: false
            onToggled: connected = !connected // placeholder state flip
        }

        Label {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "Not Connected"
            color: "#9aa0a6"
            font.family: "Open Sans"
            font.pixelSize: 15
        }
    }
}
