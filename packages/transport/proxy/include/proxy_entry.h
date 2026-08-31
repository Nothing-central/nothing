#pragma once
#include <string>

enum class ProxyScheme { Http, Https, Socks4, Socks5 };

struct ProxyEntry {
    ProxyScheme scheme = ProxyScheme::Http;
    std::string host;
    uint16_t port = 0;
    std::string username; // "" if no auth
    std::string password;

    // Parses "scheme://[user:pass@]host:port" or bare "host:port" (defaults
    // to Http). Returns false on malformed input — caller should skip that
    // line rather than crash on a bad proxy list entry.
    static bool Parse(const std::string& raw, ProxyEntry& outEntry);

    std::string ToUri() const;
};