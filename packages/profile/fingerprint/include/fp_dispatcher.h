#pragma once
#include "fp_target.h"
#include <bitset>
#include <optional>
#include <string>
#include <unordered_map>

enum class ProtectionMode { None, Baseline, FPP, RFP };

// scope: "" = global, "site" = first-party only, "site,*" = all under top-level,
// "*,third" = third-party under any top, "site,third" = specific pair
struct DomainOverride {
    std::string firstParty;   // "" or "*"
    std::string thirdParty;   // "" or "*"
    std::bitset<static_cast<size_t>(FPTarget::kCount)> add;
    std::bitset<static_cast<size_t>(FPTarget::kCount)> remove;
};

class FPDispatcher {
public:
    explicit FPDispatcher(ProtectionMode mode) : mode_(mode) {}

    void SetOverridesForOrigin(const std::string& origin, DomainOverride ov) {
        overrides_[origin] = std::move(ov);
    }

    bool ShouldResist(FPTarget target, const std::string& firstPartyOrigin,
                       const std::string& thirdPartyOrigin = "") const {
        if (mode_ == ProtectionMode::None) return false;

        auto it = overrides_.find(firstPartyOrigin);
        if (it != overrides_.end()) {
            size_t idx = static_cast<size_t>(target);
            if (it->second.remove.test(idx)) return false;
            if (it->second.add.test(idx)) return true;
        }

        if (mode_ == ProtectionMode::RFP) return true;
        return defaultActive_.test(static_cast<size_t>(target));
    }

    void SetDefaultActive(FPTarget t, bool active) {
        defaultActive_.set(static_cast<size_t>(t), active);
    }

private:
    ProtectionMode mode_;
    std::bitset<static_cast<size_t>(FPTarget::kCount)> defaultActive_;
    std::unordered_map<std::string, DomainOverride> overrides_;
};