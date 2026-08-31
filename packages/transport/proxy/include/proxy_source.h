#pragma once
#include "proxy_entry.h"
#include <string>
#include <vector>

enum class ProxySourceKind { LocalFile, RemoteUrl, SingleEntry };

// Matches the three input shapes  described: a local .txt path, an
// http(s) URL serving a plain-text list, or one proxy string inline.
// This module only knows how to FETCH raw lines — parsing each line into
// a ProxyEntry is proxy_entry.h's job, pooling/rotation is proxy_pool.h's.
class ProxySource {
public:
    static ProxySourceKind DetectKind(const std::string& input);

    // Loads raw proxy lines from a local file path.
    static bool LoadFromFile(const std::string& path, std::vector<std::string>& outLines);

    // Fetches raw proxy lines from a remote URL. Actual HTTP fetch is left
    // to whatever HTTP client the browser already links (Qt's QNetworkAccessManager
    // or piggycpp's existing fetch path) — this function signature is the
    // contract, body wired in once that dependency is confirmed.
    static bool FetchFromUrl(const std::string& url, std::vector<std::string>& outLines);

    // Single entry passed directly — wraps it as a one-line "list" so
    // callers can treat all three input kinds uniformly.
    static void FromSingleEntry(const std::string& entry, std::vector<std::string>& outLines);

    // One entry point matching all three of your input shapes — detects
    // kind automatically and dispatches to the right loader above.
    static bool Load(const std::string& input, std::vector<std::string>& outLines);
};