# Setting Up the Tor Service (for packages/transport/tor)

This pairs with the already-built `TorController`/`TorContextManager` code.
That code assumes a `tor` daemon is already running and reachable on a
control port (default 9051) and a SOCKS port (default 9050) — this doc is
just "how do I get that daemon running on Deepin/Debian."

## 1. Install Tor

```bash
sudo apt update
sudo apt install tor
```

This installs the `tor` service and a default `/etc/tor/torrc` config.
On Debian-based systems it's usually registered as a systemd service
automatically.

Check it installed and what version:
```bash
tor --version
```

## 2. Decide: system service vs. standalone process

**Option A — system service (simplest, good for "always available")**
```bash
sudo systemctl enable tor
sudo systemctl start tor
sudo systemctl status tor
```
Runs as the `debian-tor` user, config at `/etc/tor/torrc`, logs to
`journalctl -u tor` or `/var/log/tor/log` depending on config.

**Option B — standalone process (good for dev/testing, one-off, or
running a second isolated instance alongside the system one)**
```bash
tor -f ~/dev/nothing/tor-dev/torrc
```
Runs in the foreground in your terminal, uses whatever `torrc` you point
it at, easy to kill with Ctrl+C. Good while iterating on
`TorController`/`TorContextManager` code without touching system config.

For active development against `packages/transport/tor`, Option B is
recommended — you don't want a bug in your own control-port code
accidentally messing with a system service other things might rely on.

## 3. Minimal torrc for development

Create `~/dev/nothing/tor-dev/torrc`:
```
SocksPort 9050
ControlPort 9051
CookieAuthentication 1
DataDirectory ~/dev/nothing/tor-dev/data
```

This matches `TorSocksConfig`'s default (`127.0.0.1:9050`) and
`TorControlConfig`'s default (`127.0.0.1:9051`) exactly — no code changes
needed to point at this.

`CookieAuthentication 1` is the simpler auth path for local dev: Tor
writes an auth cookie file to `DataDirectory/control_auth_cookie` on
startup, and `TorController::Authenticate()` already knows how to read
and hex-encode that file via `ReadCookieAuthHex()` (that's what
`TorControlConfig::cookieAuthFilePath` is for).

Make the data directory:
```bash
mkdir -p ~/dev/nothing/tor-dev/data
```

Start it:
```bash
tor -f ~/dev/nothing/tor-dev/torrc
```

Watch the terminal for:
```
Bootstrapped 100% (done): Done
```
That's the same string `TorController::IsBootstrapped()` checks for via
`GETINFO status/bootstrap-phase` — don't try connecting your code before
you see this, connections will fail or hang.

## 4. Point your code at it

```cpp
TorControlConfig controlConfig;
controlConfig.host = "127.0.0.1";
controlConfig.controlPort = 9051;
controlConfig.cookieAuthFilePath = "/home/YOUR_USER/dev/nothing/tor-dev/data/control_auth_cookie";
// leave controlConfig.controlPassword empty — using cookie auth instead

TorSocksConfig socksConfig; // defaults already match: 127.0.0.1:9050

TorContextManager manager(controlConfig, socksConfig);
manager.SetEnabled("default", true); // connects + authenticates on first call
```

## 5. Password auth instead (alternative to cookie auth)

If you'd rather use a password instead of the cookie file:

Generate a hashed password:
```bash
tor --hash-password "your-chosen-password"
```
This prints a hash like `16:8C7...` — put that in torrc, NOT the plain
password:
```
ControlPort 9051
HashedControlPassword 16:8C7...ACTUAL_HASH_HERE
```

Then in code:
```cpp
controlConfig.controlPassword = "your-chosen-password"; // the PLAIN password, not the hash
controlConfig.cookieAuthFilePath = ""; // unused when controlPassword is set
```

Cookie auth is simpler for local dev (no hash management); password auth
is more portable if you're distributing a config where the local
filesystem path to the cookie file might differ per machine.

## 6. Verify SOCKS routing actually works (outside your own code first)

Before trusting `TorContextManager`, confirm Tor itself is routing
traffic correctly:
```bash
curl --socks5-hostname 127.0.0.1:9050 https://check.torproject.org/api/ip
```
Expected output includes `"IsTor":true`. If this fails, the problem is
in your Tor setup, not in `packages/transport` — fix this step first
before debugging any C++.

## 7. Testing NEWNYM (circuit rotation) manually

```bash
echo -e 'AUTHENTICATE\r\nSIGNAL NEWNYM\r\n' | nc 127.0.0.1 9051
```
(If using password auth instead of an empty/cookie-based `AUTHENTICATE`,
adjust the command to `AUTHENTICATE "your-password"`.)

Should return two `250 OK` lines. Run the `curl` command from step 6
again after this — the reported exit IP should change (may take a few
seconds for the new circuit to establish).

## 8. Common issues

| Symptom | Likely cause |
|---|---|
| `Connect()` returns false immediately | Tor isn't running, or wrong host/port in `TorControlConfig` |
| Connects but `Authenticate()` fails | Cookie file path wrong, or `CookieAuthentication 1` missing from torrc; or wrong password / bad hash in `HashedControlPassword` |
| `IsBootstrapped()` never returns true | Tor hasn't finished bootstrapping yet (slow network, or blocked) — check the terminal running `tor -f ...` directly for bootstrap progress/errors |
| SOCKS routing works via `curl` but not through Qt | Proxy isn't actually being applied to `QWebEngineProfile`/`QNetworkProxy` — this is downstream of `packages/transport/tor`, in whatever wiring code sets up the browser's network layer for a Tor-enabled context |
| `RequestNewIdentity()` seems to do nothing | Remember the known limitation: it rotates the circuit for ALL Tor-enabled contexts, and old already-open connections keep their existing circuit until they close — open a fresh connection/tab after calling it to see the new exit node |

## 9. When you're done developing against Option B

Just Ctrl+C the terminal running `tor -f ...`. Nothing persists outside
`~/dev/nothing/tor-dev/data` (delete that directory to fully reset,
including generating a new cookie file next run).