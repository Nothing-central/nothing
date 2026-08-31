#pragma once
#include "proxy_entry.h"
#include <vector>
#include <cstddef>

enum class ProxyRotationMode { Sequential, Random, StickyPerOrigin };

// Holds a loaded, parsed set of proxies for one context and decides which
// one to hand out per request/navigation, according to the rotation mode.
class ProxyPool {
public:
    void SetEntries(std::vector<ProxyEntry> entries) { entries_ = std::move(entries); cursor_ = 0; }

    size_t Count() const { return entries_.size(); }
    bool IsEmpty() const { return entries_.empty(); }

    // Returns nullptr if the pool is empty — caller should fall back to
    // direct connection (or Tor, if both are somehow requested) in that case.
    const ProxyEntry* Next(ProxyRotationMode mode) {
        if (entries_.empty()) return nullptr;

        switch (mode) {
            case ProxyRotationMode::Sequential: {
                const ProxyEntry* e = &entries_[cursor_];
                cursor_ = (cursor_ + 1) % entries_.size();
                return e;
            }
            case ProxyRotationMode::Random: {
                size_t idx = static_cast<size_t>(rand()) % entries_.size();
                return &entries_[idx];
            }
            case ProxyRotationMode::StickyPerOrigin:
                // Caller should use GetForOrigin instead — falls through to
                // sequential here only as a safe default if misused.
                return Next(ProxyRotationMode::Sequential);
        }
        return nullptr;
    }

    // For StickyPerOrigin: same origin always gets the same proxy for the
    // life of the pool, different origins get spread across entries via a
    // simple hash — avoids an origin's fingerprint jumping IPs mid-session.
    const ProxyEntry* GetForOrigin(const std::string& origin) const {
        if (entries_.empty()) return nullptr;
        size_t hash = std::hash<std::string>{}(origin);
        return &entries_[hash % entries_.size()];
    }

private:
    std::vector<ProxyEntry> entries_;
    size_t cursor_ = 0;
};