#pragma once
#include "proxy_pool.h"
#include "proxy_source.h"
#include <string>
#include <unordered_map>

// Same contextId pattern as tor_context_manager.h / identity_manager.h —
// each context has its own proxy pool + rotation mode, independent of
// whether Tor is also enabled for that context (see docs.md on precedence
// when both are set).
class ProxyContextManager {
public:
    bool LoadForContext(const std::string& contextId, const std::string& sourceInput,
                         ProxyRotationMode mode = ProxyRotationMode::Sequential) {
        std::vector<std::string> rawLines;
        if (!ProxySource::Load(sourceInput, rawLines)) return false;

        std::vector<ProxyEntry> entries;
        for (const auto& line : rawLines) {
            ProxyEntry entry;
            if (ProxyEntry::Parse(line, entry)) entries.push_back(entry);
        }
        if (entries.empty()) return false;

        ProxyPool pool;
        pool.SetEntries(std::move(entries));
        pools_[contextId] = std::move(pool);
        modes_[contextId] = mode;
        return true;
    }

    bool IsEnabled(const std::string& contextId) const {
        auto it = pools_.find(contextId);
        return it != pools_.end() && !it->second.IsEmpty();
    }

    const ProxyEntry* GetProxy(const std::string& contextId, const std::string& origin = "") {
        auto poolIt = pools_.find(contextId);
        if (poolIt == pools_.end()) return nullptr;

        auto modeIt = modes_.find(contextId);
        ProxyRotationMode mode = modeIt != modes_.end() ? modeIt->second : ProxyRotationMode::Sequential;

        if (mode == ProxyRotationMode::StickyPerOrigin && !origin.empty()) {
            return poolIt->second.GetForOrigin(origin);
        }
        return poolIt->second.Next(mode);
    }

    void ClearContext(const std::string& contextId) {
        pools_.erase(contextId);
        modes_.erase(contextId);
    }

private:
    std::unordered_map<std::string, ProxyPool> pools_;
    std::unordered_map<std::string, ProxyRotationMode> modes_;
};