#pragma once
#include "search_engine.h"
#include <string>
#include <unordered_map>
#include <vector>

// Built-in engine list + user-added custom engines (e.g. a self-hosted
// SearXNG instance). This is pure data lookup — no notion of "which engine
// is currently active," that's SearchEngineManager's job.
class SearchEngineRegistry {
public:
    SearchEngineRegistry() { RegisterBuiltins(); }

    const SearchEngine* Get(const std::string& id) const {
        auto it = engines_.find(id);
        return it != engines_.end() ? &it->second : nullptr;
    }

    // For a custom SearXNG-style instance: caller supplies the base URL,
    // this fills in the query/suggest templates and registers it under
    // whatever id the caller picks (e.g. "searxng-myinstance").
    void RegisterCustom(const std::string& id, const std::string& displayName,
                         const std::string& baseUrl) {
        SearchEngine e;
        e.id = id;
        e.displayName = displayName;
        e.queryUrlTemplate = baseUrl + "/search?q={q}";
        e.suggestUrlTemplate = baseUrl + "/autocompleter?q={q}";
        e.homeUrl = baseUrl;
        e.isCustom = true;
        engines_[id] = std::move(e);
    }

    std::vector<std::string> ListIds() const {
        std::vector<std::string> ids;
        ids.reserve(engines_.size());
        for (const auto& [id, _] : engines_) ids.push_back(id);
        return ids;
    }

private:
    std::unordered_map<std::string, SearchEngine> engines_;

    void RegisterBuiltins() {
        engines_["duckduckgo"] = SearchEngine{
            "duckduckgo", "DuckDuckGo",
            "https://duckduckgo.com/html/?q={q}",
            "https://duckduckgo.com/ac/?q={q}&type=list",
            "https://duckduckgo.com/",
            "https://duckduckgo.com/favicon.ico",
            false, false
        };

        engines_["google"] = SearchEngine{
            "google", "Google",
            "https://www.google.com/search?q={q}",
            "https://suggestqueries.google.com/complete/search?client=firefox&q={q}",
            "https://www.google.com/",
            "https://www.google.com/favicon.ico",
            false, false
        };

        engines_["bing"] = SearchEngine{
            "bing", "Bing",
            "https://www.bing.com/search?q={q}",
            "https://www.bing.com/osjson.aspx?query={q}",
            "https://www.bing.com/",
            "https://www.bing.com/favicon.ico",
            false, false
        };

        engines_["brave"] = SearchEngine{
            "brave", "Brave Search",
            "https://search.brave.com/search?q={q}",
            "", // no public suggest API
            "https://search.brave.com/",
            "https://search.brave.com/favicon.ico",
            false, false
        };

        engines_["searxng"] = SearchEngine{
            "searxng", "SearXNG (searx.be)",
            "https://searx.be/search?q={q}",
            "https://searx.be/autocompleter?q={q}",
            "https://searx.be/",
            "https://searx.be/favicon.ico",
            false, false
        };

        // Onion-network search engine — only meaningful with Tor routing
        // active. requiresTor=true is a signal for the UI/manager to warn
        // or auto-enable Tor, not an enforced technical block here.
        engines_["ahmia"] = SearchEngine{
            "ahmia", "Ahmia",
            "https://ahmia.fi/search/?q={q}",
            "",
            "https://ahmia.fi/",
            "https://ahmia.fi/favicon.ico",
            true, false
        };
    }
};