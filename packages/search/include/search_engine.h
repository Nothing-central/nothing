#pragma once
#include <string>

// A single search engine definition — no logic, just data. Every field
// filled here is what SearchEngineManager returns/uses when this engine
// is active for a given context.
struct SearchEngine {
    std::string id;              // "duckduckgo", "google", "bing", "brave", "searxng", "ahmia"
    std::string displayName;     // "DuckDuckGo"
    std::string queryUrlTemplate;    // "{q}" gets URL-encoded query substituted in
    std::string suggestUrlTemplate;  // "" if no autocomplete endpoint, else JSON API
    std::string homeUrl;          // landing page when no query yet
    std::string iconUrl;          // favicon/logo for UI
    bool requiresTor = false;     // true only for onion-only engines (ahmia)
    bool isCustom = false;        // true for user-added SearXNG instances etc.

    std::string BuildQueryUrl(const std::string& urlEncodedQuery) const;
    std::string BuildSuggestUrl(const std::string& urlEncodedQuery) const;
};