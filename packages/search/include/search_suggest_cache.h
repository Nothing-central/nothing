#pragma once
#include <chrono>
#include <string>
#include <unordered_map>
#include <vector>

// Caches autocomplete responses per (engineId, query prefix) so repeated
// keystrokes on a slowly-typed query don't refire network requests. Not
// a correctness layer — purely a latency optimization for the omnibox/
// scraper autocomplete path.
class SearchSuggestCache {
public:
    explicit SearchSuggestCache(std::chrono::seconds ttl = std::chrono::seconds(300))
        : ttl_(ttl) {}

    // Returns true and fills outSuggestions if a fresh cache entry exists.
    bool TryGet(const std::string& engineId, const std::string& queryPrefix,
                std::vector<std::string>& outSuggestions) const {
        auto key = MakeKey(engineId, queryPrefix);
        auto it = cache_.find(key);
        if (it == cache_.end()) return false;

        auto age = std::chrono::steady_clock::now() - it->second.storedAt;
        if (age > ttl_) return false;

        outSuggestions = it->second.suggestions;
        return true;
    }

    void Put(const std::string& engineId, const std::string& queryPrefix,
             std::vector<std::string> suggestions) {
        auto key = MakeKey(engineId, queryPrefix);
        cache_[key] = Entry{std::move(suggestions), std::chrono::steady_clock::now()};
    }

    void Clear() { cache_.clear(); }

    // Drops entries older than ttl_ — call periodically (e.g. on navigation)
    // rather than on every lookup, to keep TryGet cheap.
    void PruneExpired() {
        auto now = std::chrono::steady_clock::now();
        for (auto it = cache_.begin(); it != cache_.end();) {
            if (now - it->second.storedAt > ttl_) it = cache_.erase(it);
            else ++it;
        }
    }

private:
    struct Entry {
        std::vector<std::string> suggestions;
        std::chrono::steady_clock::time_point storedAt;
    };

    std::string MakeKey(const std::string& engineId, const std::string& queryPrefix) const {
        return engineId + "|" + queryPrefix;
    }

    std::chrono::seconds ttl_;
    std::unordered_map<std::string, Entry> cache_;
};