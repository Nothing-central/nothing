<div align="center">
  <img src="apps/browser/assets/icons/mainlogo-nobackground.png" width="120" alt="Sabre Browser" />
  <h1>Sabre Browser</h1>
  <p><strong>Fast. Private. Clean. No noise, no tracking, no DevTools.</strong></p>
</div>

---

Sabre is a Qt6-based web browser built from the ground up with one goal: get out of your way. No telemetry. No ads. No bloat. Just a browser that loads pages fast, protects your fingerprint, and doesn't report home to anyone.

Sabre ships with built-in ad blocking (EasyList + EasyPrivacy), fingerprint spoofing, incognito mode with full session isolation, and a clean minimal UI that doesn't try to be clever. It's a browser that respects you enough to just be a browser.

---

## What Sabre does not have

**Sabre does not ship with a DevTools panel.** There is no inspect element, no console, no network tab — nothing. Right-clicking a page will not open any developer tools. This is intentional. Sabre is built for browsing, not development. If you need DevTools, use a different browser for that.

---

## Install

### Linux — via apt (recommended)

Works on Ubuntu, Debian, and any apt-based distro. Keeps itself updated automatically.

```sh
curl -fsSL https://pub-35ea7aea1ae846babd7e2f81528d5c94.r2.dev/sabre-browser-key.gpg \
  | sudo gpg --dearmor -o /usr/share/keyrings/sabre-browser.gpg

echo 'deb [signed-by=/usr/share/keyrings/sabre-browser.gpg] https://pub-35ea7aea1ae846babd7e2f81528d5c94.r2.dev stable main' \
  | sudo tee /etc/apt/sources.list.d/sabre-browser.list

sudo apt update && sudo apt install sabre-browser
```

After that just launch it from your app menu or run `sabre-browser` in a terminal.

---

### Linux — manual .deb

Download the `.deb` for your architecture from the [latest release](../../releases/latest):

```sh
# amd64 (most desktops and laptops)
sudo dpkg -i sabre-browser_*_amd64.deb

# arm64 (Raspberry Pi, Apple Silicon Linux, etc)
sudo dpkg -i sabre-browser_*_arm64.deb
```

If you get missing dependency errors:
```sh
sudo apt-get install -f
```

---

### Linux — tar.gz (no install)

If you just want to run it without installing anything:

```sh
tar -xzf sabre-browser-*-linux-amd64.tar.gz
cd sabre-browser-*
./sabre-browser
```

Note: this version requires Qt6 WebEngine to already be on your system. If it isn't:
```sh
sudo apt install libqt6webenginewidgets6 libqt6webenginecore6
```

---

### Windows

Download `sabre-browser-*-windows-x64.zip` from the [latest release](../../releases/latest), extract it, and run `sabre-browser.exe`. No installer — just unzip and go. Qt is bundled inside the zip so nothing extra is needed.

---

## Versioning

| Tag | Meaning |
|---|---|
| `v0.1.0-alpha` | Early testing, expect rough edges |
| `v0.1.0-beta` | Feature complete, being stabilized |
| `v0.1.0-rc` | Release candidate, nearly final |
| `v0.1.0` | Stable release |
| `v0.2.0` | New features added |
| `v1.0.0` | Major milestone |

---

## Bug reports

Sabre does not have an issue tracker. If you find a bug, email **ernesttechhouse@gmail.com** with:

- What you were doing when it happened
- What you expected to happen
- What actually happened
- Your OS and version (e.g. Ubuntu 24.04, Windows 11)

Please do not report bugs asking for DevTools or inspect element — that feature does not exist in Sabre and will not be added.

---

## Built by

Ernest Tech House — building things that matter.
