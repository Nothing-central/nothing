#pragma once
#include <cstdint>
#include <string>

// The SOCKS5 proxy endpoint Tor exposes for actual traffic routing —
// separate from the control port. This is what gets handed to Qt's
// QNetworkProxy / QWebEngineProfile setup, not the control connection.
struct TorSocksConfig {
    std::string host = "127.0.0.1";
    uint16_t port = 9050;

    // Builds a proxy URI string, e.g. "socks5://127.0.0.1:9050",
    // convenient for logging or IPC responses.
    std::string ToUri() const {
        return "socks5://" + host + ":" + std::to_string(port);
    }
};