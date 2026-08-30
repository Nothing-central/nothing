#pragma once
#include "search_engine_registry.h"
#include <string>
#include <unordered_map>

// Mirrors identity_manager.h's contextId pattern: "default" for normal
// browsing, an incognito contextId for a private session, or any id the
// JS side (piggy) wants to use for a scraper instance. Each context has
// exactly one active search engine at a time, switchable independently.
class SearchEngineManager {
public:
    explicit SearchEngineManager(const SearchEngineRegistry& registry)
        : registry_(registry) {}

    // Sets the active engine id for a context. Returns false if the id
    // doesn't exist in the registry — caller should keep the previous
    // engine active in that case, not silently clear it.
    bool SetActiveEngine(const std::string& contextId, const std::string& engineId) {
        if (!registry_.Get(engineId)) return false;
        activeEngineByContext_[contextId] = engineId;
        return true;
    }

    // Falls back to "duckduckgo" if the context never set one explicitly —
    // every context always resolves to *some* engine, never null.
    const SearchEngine& GetActiveEngine(const std::string& contextId) const {
        auto it = activeEngineByContext_.find(contextId);
        std::string engineId = (it != activeEngineByContext_.end()) ? it->second : "duckduckgo";
        const SearchEngine* engine = registry_.Get(engineId);
        return engine ? *engine : *registry_.Get("duckduckgo");
    }

    // Called when an incognito session ends — same lifecycle as
    // IdentityManager::DestroyIdentity, keeps the two in step.
    void ClearContext(const std::string& contextId) {
        activeEngineByContext_.erase(contextId);
    }

    const SearchEngineRegistry& Registry() const { return registry_; }

private:
    const SearchEngineRegistry& registry_;
    std::unordered_map<std::string, std::string> activeEngineByContext_;
};