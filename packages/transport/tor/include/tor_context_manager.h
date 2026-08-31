#pragma once
#include "tor_controller.h"
#include "tor_config.h"
#include <string>
#include <unordered_map>

// Mirrors identity_manager.h's contextId pattern: per-context Tor on/off,
// so normal browsing can be direct while incognito or a specific scraper
// run routes through Tor, independently.
class TorContextManager {
public:
    TorContextManager(TorControlConfig controlConfig, TorSocksConfig socksConfig)
        : controller_(std::move(controlConfig)), socksConfig_(socksConfig) {}

    // Enables/disables Tor routing for a given context. Does not itself
    // start the Tor daemon — Connect() must succeed at least once (lazily,
    // on first SetEnabled(true) call) or this fails.
    bool SetEnabled(const std::string& contextId, bool enabled) {
        if (enabled && !controller_.IsConnected()) {
            if (!controller_.Connect()) return false;
        }
        enabledContexts_[contextId] = enabled;
        return true;
    }

    bool IsEnabled(const std::string& contextId) const {
        auto it = enabledContexts_.find(contextId);
        return it != enabledContexts_.end() && it->second;
    }

    // What to hand to Qt's proxy setup for this context — empty/default
    // TorSocksConfig semantics aren't checked here, caller should call
    // IsEnabled() first and only apply the proxy if true.
    const TorSocksConfig& SocksConfig() const { return socksConfig_; }

    // New circuit for ALL contexts currently routed through Tor — Tor
    // doesn't support per-context circuits without separate SOCKS ports
    // (see docs.md note on that limitation).
    bool RequestNewIdentity() { return controller_.RequestNewIdentity(); }

    bool IsBootstrapped() { return controller_.IsBootstrapped(); }

    // Same lifecycle hook as IdentityManager::DestroyIdentity /
    // SearchEngineManager::ClearContext — call when a context (e.g. an
    // incognito session) ends.
    void ClearContext(const std::string& contextId) {
        enabledContexts_.erase(contextId);
    }

private:
    TorController controller_;
    TorSocksConfig socksConfig_;
    std::unordered_map<std::string, bool> enabledContexts_;
};