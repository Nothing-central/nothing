#include "proxy_source.h"
#include <fstream>
#include <sstream>

ProxySourceKind ProxySource::DetectKind(const std::string& input) {
    if (input.rfind("http://", 0) == 0 || input.rfind("https://", 0) == 0) {
        // Could still be a single socks/http proxy URI OR a remote list URL.
        // Heuristic: if it ends in .txt or a common list extension, treat as
        // a remote list; otherwise treat as a single proxy entry with an
        // http(s) scheme. Caller can always bypass this by calling
        // FromSingleEntry/FetchFromUrl directly if the heuristic guesses wrong.
        if (input.size() >= 4 && input.substr(input.size() - 4) == ".txt") {
            return ProxySourceKind::RemoteUrl;
        }
        return ProxySourceKind::SingleEntry;
    }

    if (input.find("://") == std::string::npos && input.find(':') != std::string::npos
        && input.find('/') == std::string::npos) {
        // "host:port" or "scheme://host:port" without a URL path — single entry
        return ProxySourceKind::SingleEntry;
    }

    return ProxySourceKind::LocalFile;
}

bool ProxySource::LoadFromFile(const std::string& path, std::vector<std::string>& outLines) {
    std::ifstream file(path);
    if (!file) return false;

    std::string line;
    while (std::getline(file, line)) {
        if (!line.empty()) outLines.push_back(line);
    }
    return true;
}

bool ProxySource::FetchFromUrl(const std::string& url, std::vector<std::string>& outLines) {
    // NOTE: placeholder — wire to Qt's QNetworkAccessManager (synchronous
    // blocking GET via QEventLoop) or piggycpp's existing HTTP fetch utility,
    // whichever the rest of the browser already uses for non-page requests.
    (void)url;
    (void)outLines;
    return false;
}

void ProxySource::FromSingleEntry(const std::string& entry, std::vector<std::string>& outLines) {
    outLines.push_back(entry);
}

bool ProxySource::Load(const std::string& input, std::vector<std::string>& outLines) {
    switch (DetectKind(input)) {
        case ProxySourceKind::LocalFile:
            return LoadFromFile(input, outLines);
        case ProxySourceKind::RemoteUrl:
            return FetchFromUrl(input, outLines);
        case ProxySourceKind::SingleEntry:
            FromSingleEntry(input, outLines);
            return true;
    }
    return false;
}