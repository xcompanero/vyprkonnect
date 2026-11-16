import QtQuick
import QtQuick.Controls.Basic

// Dark Vypr-style shell. Phase 1: server picker + connect + status.
ApplicationWindow {
    id: root
    width: 400
    height: 620
    visible: true
    title: qsTr("VyprKonnect")

    color: "#16181d"

    function statusColor() {
        if (VpnBackend.stateLabel === VpnBackend.statusMessage
                && VpnBackend.statusMessage !== "")
            return "#e05252" // a reason string is showing — treat as the error channel
        if (VpnBackend.connected) return "#2bbfa4"
        if (VpnBackend.busy) return "#f0b429"
        return "#9aa0a6"
    }

    Column {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 20

        Label {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: qsTr("VyprKonnect")
            color: "#f0b429"
            font.family: "Rubik"
            font.pixelSize: 22
            font.bold: true
        }

        ComboBox {
            id: serverPicker
            width: parent.width
            model: Servers
            textRole: "name"
            enabled: VpnBackend.canConnect
            background: Rectangle {
                color: "#22252c"
                border.color: "#3a3f4a"
                radius: 6
            }
        }

        ConnectButton {
            id: connectButton
            anchors.horizontalCenter: parent.horizontalCenter
            connected: VpnBackend.connected
            busy: VpnBackend.busy
            onToggled: {
                if (VpnBackend.canDisconnect) {
                    VpnBackend.disconnect()
                } else if (VpnBackend.canConnect) {
                    VpnBackend.connectToServer(serverPicker.currentText)
                }
            }
        }

        Label {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: VpnBackend.stateLabel
            color: statusColor()
            font.family: "Open Sans"
            font.pixelSize: 14
            wrapMode: Text.Wrap
        }

        // Account section (always available; pre-seeded credentials are shown).
        Column {
            width: parent.width
            spacing: 8

            Label {
                text: qsTr("VyprVPN account")
                color: "#9aa0a6"
                font.pixelSize: 12
            }
            TextField {
                id: userField
                width: parent.width
                placeholderText: qsTr("Account email")
                text: Credentials.username
                onEditingFinished: Credentials.username = text
                background: Rectangle { color: "#22252c"; border.color: "#3a3f4a"; radius: 6 }
            }
            TextField {
                id: passField
                width: parent.width
                placeholderText: qsTr("Password")
                echoMode: TextInput.Password
                text: Credentials.password
                onEditingFinished: Credentials.password = text
                background: Rectangle { color: "#22252c"; border.color: "#3a3f4a"; radius: 6 }
            }
            Label {
                width: parent.width
                visible: !Credentials.secure
                text: qsTr("⚠ Storage backend is not encrypted yet (KWallet module not compiled in).")
                color: "#e0a452"
                font.pixelSize: 11
                wrapMode: Text.Wrap
            }
        }
    }
}
