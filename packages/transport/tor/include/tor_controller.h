#pragma once
#include <string>
#include <vector>

// Talks to Tor's control port (default 9051) using the Tor control protocol
// (not SOCKS — that's separate, see tor_config.h). Handles auth, circuit
// rotation (NEWNYM), and status checks. Assumes a `tor` binary/daemon is
// already running — this does not launch or manage the Tor process itself.
struct TorControlConfig {
    std::string host = "127.0.0.1";
    int controlPort = 9051;
    std::string controlPassword; // "" if using cookie auth instead
    std::string cookieAuthFilePath; // used if controlPassword is empty
};

class TorController {
public:
    explicit TorController(TorControlConfig config) : config_(std::move(config)) {}

    // Opens the control connection and authenticates. Returns false on
    // failure (Tor not running, wrong password, cookie file unreadable).
    bool Connect();

    void Disconnect();

    bool IsConnected() const { return connected_; }

    // Requests a new circuit (new exit node/identity) for future connections.
    // Existing open connections keep their current circuit until they close.
    bool RequestNewIdentity();

    // GETINFO circuit-status — returns raw circuit info lines, caller parses
    // as needed. Useful for a UI "current exit node" display.
    std::vector<std::string> GetCircuitStatus();

    // GETINFO status/bootstrap-phase — returns true once Tor reports
    // "PROGRESS=100" i.e. fully bootstrapped and usable.
    bool IsBootstrapped();

private:
    TorControlConfig config_;
    bool connected_ = false;
    int socketFd_ = -1; // platform socket handle, set by Connect()

    bool Authenticate();
    std::string SendCommand(const std::string& command);
    std::string ReadCookieAuthHex();
};