# VyprKonnect

A Qt6/QML GUI client for **VyprVPN** on Linux, built for KDE Plasma (Kubuntu 26.04)
but not tied to it. Not affiliated with Golden Frog / VyprVPN / Certida.

## Approach

- **OpenVPN first**, driven through **NetworkManager** over D-Bus — no root daemon,
  connections live alongside your other NM VPN profiles.
- Server data derived from Vypr's official OpenVPN config downloads; bundled snapshot
  works offline, refresh optional.
- WireGuard planned via Vypr's official `vyprvpn-wireguard-go` client (Phase 2).
- Design language mirrors Vypr's apps: dark UI, gold accent, Rubik / Open Sans.

## Status

Phase 1 skeleton — UI shell only. See `../` planning notes for the phased plan.

## Build

```sh
cmake -B build -S .
cmake --build build
./build/vyprkonnect
```

Requires: Qt 6 (Quick, Network, DBus), CMake ≥ 3.24, NetworkManager + network-manager-openvpn.
