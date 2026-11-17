# DNS leak tests

Results from [dnsleaktest.org](https://dnsleaktest.org) full-result exports,
captured on a Kubuntu 26.04 machine with Tailscale (MagicDNS) running
throughout. Each test was performed while connected to the same VyprVPN
location (Austria) via VyprKonnect.

| File | Protocol | Tunnel exit | DNS resolvers seen | Meaning |
|---|---|---|---|---|
| `DNS-Test-Austria-OVPN-...json` | OpenVPN | 128.90.148.15 (Vypr, Vienna) | AMAZON-02, DIGITALOCEAN (VyprDNS infrastructure) | DNS resolved inside the tunnel |
| `DNS-Test-Austria-WG-...json` | WireGuard | 128.90.148.14 (Vypr, Vienna) | CLOUDFLARENET (system resolver chain) | Pre-fix behaviour: DNS via system resolver while traffic used the tunnel |
| `DNS-Test2-For-Austria-WG-...json` | WireGuard | 128.90.148.19 (Vypr, Vienna) | AMAZON-02, DIGITALOCEAN (VyprDNS infrastructure) | After the VyprDNS fix: identical DNS behaviour to OpenVPN |

All three: no resolver belonging to the author's ISP (`isYourIsp: false`
throughout). The `warn` verdict from the tool reflects the resolvers being
hosted by cloud providers (Amazon/DigitalOcean) rather than the VPN
provider's own AS — the resolvers are nonetheless Vypr's infrastructure,
reached through the tunnel.

Note the second file: WireGuard profiles originally carried no tunnel DNS
(mirroring the `-ignoredns` behaviour of Vypr's own CLI). Systemd-resolved
then left the system resolver chain in charge of global names — a "leak" in
the loose sense: the resolver geography (here: via Cloudflare) no longer
matched the tunnel exit, and DNS resolution metadata remained visible to
whoever operated that resolver path. The current WireGuard backend resolves
via VyprDNS by default; see the main README for the details and the
encapsulation-loop fix that made this safe.
