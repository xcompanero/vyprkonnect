import QtQuick
import QtQuick.Controls.Basic

// Dark Vypr-style shell. Phase 1: server picker + connect + status.
ApplicationWindow {
    id: root
    width: 400
    height: 600
    visible: true
    title: qsTr("VyprKonnect")

    color: "#16181d"

    // Map backend states to labels/colors.
    function statusColor() {
        switch (VpnBackend.state) {
        case VpnBackend.Connected: return "#2bbfa4"
        case VpnBackend.Connecting: return "#f0b429"
        case VpnBackend.Disconnecting: return "#f0b429"
        case VpnBackend.Failed: return "#e05252"
        default: return "#9aa0a6"
        }
    }
    function statusText() {
        if (VpnBackend.statusMessage !== "") return VpnBackend.statusMessage
        switch (VpnBackend.state) {
        case VpnBackend.Connected: return qsTr("Connected to %1").arg(VpnBackend.activeServer)
        case VpnBackend.Connecting: return qsTr("Connecting…")
        case VpnBackend.Disconnecting: return qsTr("Disconnecting…")
        default: return qsTr("Not Connected")
        }
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
            enabled: VpnBackend.state === VpnBackend.Disconnected
                     || VpnBackend.state === VpnBackend.Failed
            background: Rectangle {
                color: "#22252c"
                border.color: "#3a3f4a"
                radius: 6
            }
        }

        ConnectButton {
            id: connectButton
            anchors.horizontalCenter: parent.horizontalCenter
            connected: VpnBackend.state === VpnBackend.Connected
            busy: VpnBackend.state === VpnBackend.Connecting
                  || VpnBackend.state === VpnBackend.Disconnecting
            onToggled: {
                if (VpnBackend.state === VpnBackend.Connected
                        || VpnBackend.state === VpnBackend.Disconnecting) {
                    VpnBackend.disconnect()
                } else if (VpnBackend.state === VpnBackend.Disconnected
                           || VpnBackend.state === VpnBackend.Failed) {
                    VpnBackend.connectToServer(Servers.get(serverPicker.currentIndex).name)
                }
            }
        }

        Label {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: statusText()
            color: statusColor()
            font.family: "Open Sans"
            font.pixelSize: 14
            wrapMode: Text.Wrap
        }

        // Login section (simple dev form until full settings UI).
        Column {
            width: parent.width
            spacing: 8
            visible: Credentials.username === "" || Credentials.password === ""

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
