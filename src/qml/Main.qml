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

    // Selected protocol backend.
    property bool useWireGuard: false
    property var backend: useWireGuard ? VpnWireGuard : VpnOpenVpn

    function statusColor() {
        if (backend.stateLabel === backend.statusMessage
                && backend.statusMessage !== "")
            return "#e05252" // a reason string is showing — treat as the error channel
        if (backend.connected) return "#2bbfa4"
        if (backend.busy) return "#f0b429"
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

        Row {
            width: parent.width
            spacing: 8

            Button {
                text: qsTr("OpenVPN")
                checkable: true
                checked: !root.useWireGuard
                enabled: !backend.busy
                onClicked: root.useWireGuard = false
            }
            Button {
                text: qsTr("WireGuard")
                checkable: true
                checked: root.useWireGuard
                enabled: !backend.busy
                onClicked: root.useWireGuard = true
            }
        }

        ComboBox {
            id: serverPicker
            width: parent.width
            model: Servers
            textRole: "name"
            enabled: backend.canConnect
            background: Rectangle {
                color: "#22252c"
                border.color: "#3a3f4a"
                radius: 6
            }
        }

        ConnectButton {
            id: connectButton
            anchors.horizontalCenter: parent.horizontalCenter
            connected: backend.connected
            busy: backend.busy
            onToggled: {
                if (backend.canDisconnect) {
                    backend.disconnect()
                } else if (backend.canConnect) {
                    backend.connectToServer(serverPicker.currentText)
                }
            }
        }

        Label {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: backend.stateLabel
            color: statusColor()
            font.family: "Open Sans"
            font.pixelSize: 14
            wrapMode: Text.Wrap
        }

        Label {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            visible: WanIp.address !== ""
            text: qsTr("WAN IP: ") + WanIp.address
            color: "#6d737b"
            font.family: "Open Sans"
            font.pixelSize: 12
        }

        // Account section: collapsed by default; password is never echoed back.
        Column {
            width: parent.width
            spacing: 8

            Button {
                id: accountToggle
                width: parent.width
                flat: true
                contentItem: Row {
                    spacing: 8
                    Text {
                        text: qsTr("Account")
                        color: "#c9ced6"
                        font.family: "Rubik"
                        font.pixelSize: 13
                        verticalAlignment: Text.AlignVCenter
                    }
                    Text {
                        text: Credentials.username !== ""
                              ? Credentials.username + "  " + (Credentials.secure ? "🔒" : "⚠ plaintext")
                              : qsTr("not configured")
                        color: Credentials.secure ? "#6d737b" : "#e0a452"
                        font.pixelSize: 12
                        elide: Text.ElideRight
                        verticalAlignment: Text.AlignVCenter
                        width: parent.width - 60
                    }
                    Text {
                        text: accountSection.visible ? "▾" : "▸"
                        color: "#6d737b"
                        verticalAlignment: Text.AlignVCenter
                    }
                }
                background: Rectangle { color: "transparent" }
                onClicked: accountSection.visible = !accountSection.visible
            }

            Column {
                id: accountSection
                visible: Credentials.username === "" || Credentials.password === ""
                width: parent.width
                spacing: 8

                TextField {
                    width: parent.width
                    placeholderText: qsTr("Account email")
                    text: Credentials.username
                    onEditingFinished: Credentials.username = text
                    background: Rectangle { color: "#22252c"; border.color: "#3a3f4a"; radius: 6 }
                }
                TextField {
                    width: parent.width
                    placeholderText: Credentials.password !== ""
                                     ? qsTr("•••••••• (stored — type to replace)")
                                     : qsTr("Password")
                    echoMode: TextInput.Password
                    // Intentionally NOT bound to the stored password: secrets
                    // should not be echoed into editable widgets.
                    onEditingFinished: if (text !== "") { Credentials.password = text; text = "" }
                    background: Rectangle { color: "#22252c"; border.color: "#3a3f4a"; radius: 6 }
                }
                Label {
                    width: parent.width
                    visible: Credentials.secure
                    text: qsTr("Credentials stored in KWallet (encrypted).")
                    color: "#4d8f7f"
                    font.pixelSize: 11
                    wrapMode: Text.Wrap
                }
                Label {
                    width: parent.width
                    visible: !Credentials.secure
                    text: qsTr("⚠ Storage backend is not encrypted (KWallet module not compiled in).")
                    color: "#e0a452"
                    font.pixelSize: 11
                    wrapMode: Text.Wrap
                }
            }
        }
    }
}
