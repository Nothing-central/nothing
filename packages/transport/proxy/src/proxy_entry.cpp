#include "proxy_entry.h"
#include <sstream>

namespace {
ProxyScheme SchemeFromString(const std::string& s) {
    if (s == "https") return ProxyScheme::Https;
    if (s == "socks4") return ProxyScheme::Socks4;
    if (s == "socks5") return ProxyScheme::Socks5;
    return ProxyScheme::Http;
}

std::string SchemeToString(ProxyScheme s) {
    switch (s) {
        case ProxyScheme::Https: return "https";
        case ProxyScheme::Socks4: return "socks4";
        case ProxyScheme::Socks5: return "socks5";
        default: return "http";
    }
}
}

bool ProxyEntry::Parse(const std::string& raw, ProxyEntry& outEntry) {
    std::string working = raw;

    // strip inline comments and whitespace-only lines — proxy list files
    // often have both
    size_t hashPos = working.find('#');
    if (hashPos != std::string::npos) working = working.substr(0, hashPos);
    while (!working.empty() && (working.back() == ' ' || working.back() == '\r' || working.back() == '\n'))
        working.pop_back();
    if (working.empty()) return false;

    std::string scheme = "http";
    std::string rest = working;

    size_t schemeSep = working.find("://");
    if (schemeSep != std::string::npos) {
        scheme = working.substr(0, schemeSep);
        rest = working.substr(schemeSep + 3);
    }

    std::string userPass, hostPort = rest;
    size_t atPos = rest.find('@');
    if (atPos != std::string::npos) {
        userPass = rest.substr(0, atPos);
        hostPort = rest.substr(atPos + 1);
    }

    size_t colonPos = hostPort.rfind(':');
    if (colonPos == std::string::npos) return false; // port is required

    std::string host = hostPort.substr(0, colonPos);
    std::string portStr = hostPort.substr(colonPos + 1);
    if (host.empty() || portStr.empty()) return false;

    int port = 0;
    try {
        port = std::stoi(portStr);
    } catch (...) {
        return false;
    }
    if (port <= 0 || port > 65535) return false;

    outEntry.scheme = SchemeFromString(scheme);
    outEntry.host = host;
    outEntry.port = static_cast<uint16_t>(port);

    if (!userPass.empty()) {
        size_t userColon = userPass.find(':');
        if (userColon != std::string::npos) {
            outEntry.username = userPass.substr(0, userColon);
            outEntry.password = userPass.substr(userColon + 1);
        } else {
            outEntry.username = userPass;
        }
    }

    return true;
}

std::string ProxyEntry::ToUri() const {
    std::ostringstream out;
    out << SchemeToString(scheme) << "://";
    if (!username.empty()) {
        out << username;
        if (!password.empty()) out << ":" << password;
        out << "@";
    }
    out << host << ":" << port;
    return out.str();
}