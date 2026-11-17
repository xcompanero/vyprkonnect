# VyprKonnect

A Qt6/QML GUI client for **VyprVPN** on Linux, built for KDE Plasma but not tied
to it. Uses NetworkManager for all tunnel management — no root daemon — and
supports **OpenVPN** and **WireGuard**.

**Not affiliated with, endorsed by, or connected to Golden Frog GmbH, VyprVPN,
or Certida LLC.** "VyprVPN" is their trademark; this project simply manages
connections to their service on behalf of its own users.

![status](https://img.shields.io/badge/status-usable%20on%20Kubuntu%2026.04-green)

## Features

- **OpenVPN and WireGuard** via NetworkManager over D-Bus (no privileged daemon;
  connections are ordinary NM profiles, visible and manageable from any NM tool)
- Server browser with 70+ bundled locations (from Vypr's official config set)
- WireGuard sessions provisioned through Vypr's official
  [`vyprvpn-wireguard-go`](https://support.vyprvpn.com/hc/en-us/articles/43750934530317-VyprVPN-WireGuard-Go-Client-Setup)
  CLI; sessions auto-renew when they expire server-side
- Tunnels are verified live before "Connected" is shown (WireGuard activation
  alone proves nothing — we check real traffic)
- Already-running tunnels are detected and adopted on app relaunch
- Credentials stored in **KWallet** (encrypted); falls back to a local file with
  a visible warning if KWallet is unavailable
- Live WAN IP display (via [ifconfig.io](https://ifconfig.io))
- System tray icon with status tooltip and disconnect menu
- Endpoint encapsulation-loop protection (see *How it works*)

## Requirements

- Linux with **NetworkManager** (tested on Kubuntu 26.04, NM 1.54)
- `network-manager-openvpn`
- `kwalletd` / `kwallet6` for credential storage
- For **WireGuard**: `wireguard-tools`, plus Vypr's official CLI binary
  installed as `/usr/local/bin/vyprvpn-wireguard-go` (download from the
  VyprVPN support article linked above; it is Vypr's closed binary and is
  **not** distributed with this project)

## Install (Kubuntu 26.04)

```sh
sudo apt install wireguard-tools          # WG support (skip for OpenVPN-only)
sudo install -m 755 vyprvpn-wireguard-go-x64 /usr/local/bin/vyprvpn-wireguard-go
sudo dpkg -i vyprkonnect_<version>_amd64.deb
sudo apt-get install -f                   # only if dpkg reports missing deps
```

Launch **VyprKonnect** from the application menu (Network category) or run
`vyprkonnect`. On first run, expand *Account* and enter your VyprVPN email and
password; they are stored in KWallet.

## Build from source

```sh
sudo apt install qt6-base-dev qt6-declarative-dev qt6-quickcontrols2-dev \
                 libkf6wallet-dev cmake g++ make
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/vyprkonnect
```

KWallet support is enabled automatically when `libkf6wallet-dev` is present
(and reported at configure time). Without it, credentials fall back to a
`0600` plaintext file with a visible warning in the UI.

## How it works

All tunnels are ordinary NetworkManager connections:

- **OpenVPN** profiles are imported from the bundled `.ovpn` set (with two
  adjustments: Vypr's `compress stub-v2` directive is rewritten to a form NM's
  plugin accepts, and compression is set to *asymmetric* per Vypr's Ubuntu guide).
- **WireGuard** profiles are created via NM's D-Bus API (nmcli cannot script
  WG peers); VyprDNS is set as the link resolver with `dns-priority -50` so it
  wins the global slot, while **Tailscale's MagicDNS keeps resolving tailnet
  names** via per-domain routing. LAN and Tailscale IPs never enter the tunnel
  (longest-prefix routing).

A subtle failure mode deserves mention: a WireGuard tunnel whose AllowedIPs
are `0.0.0.0/1 + 128.0.0.0/1` will route **its own endpoint packets back into
itself** (an encapsulation loop — symptom: massive outbound traffic, total
network standstill). VyprKonnect prevents this by pre-installing a host route
for the endpoint via the physical uplink before every WG activation
(`nmcli device reapply`), with an NM dispatcher hook
(`/etc/NetworkManager/dispatcher.d/90-vyprkonnect-wg`) as a safety net.

Closing the window hides it to the tray; the tunnel keeps running
(it belongs to NetworkManager, not the app). Relaunching adopts the running
session. *Quit* from the tray menu exits the app (the tunnel survives that too,
by design).

## Known limitations

- **No Kill Switch** yet (traffic is not blocked if the tunnel dies mid-session;
  WireGuard sessions auto-renew, but there is a brief gap during renewal)
- No per-app split tunneling, no Trusted-Networks auto-connect (planned)
- WireGuard requires the separate Vypr CLI binary (see Requirements)
- Bundled server list is a snapshot (dated in `data/servers.json`); Vypr
  occasionally adds locations — reconnect always uses the bundled set's current
  state, refresh tooling is planned
- Developed and tested on Kubuntu 26.04 + Tailscale; other setups may differ
  (feedback welcome)

## Screenshots

See [docs/screenshots/](docs/screenshots/).

## DNS behaviour

OpenVPN and WireGuard both resolve names through the tunnel via VyprDNS by
default. On machines running Tailscale with MagicDNS, the two coexist via
systemd-resolved's per-domain routing: MagicDNS keeps handling tailnet names
(`*.ts.net`, `100.x` reverse lookups) while VyprDNS handles the wider internet.
LAN and Tailscale IP addresses never enter the tunnel.

Real-world DNS-leak test results from an Austria connection (both protocols,
before/after the VyprDNS fix) are in [docs/dns-tests/](docs/dns-tests/).

## License

GPL-3.0-or-later. See [LICENSE](LICENSE).
Copyright © 2026 xcompanero.

Bundled `.ovpn` configs and server metadata originate from VyprVPN's official
public downloads and remain the property of Golden Frog GmbH / Certida LLC.
Fonts: Rubik and Open Sans (SIL Open Font License).
