#pragma once
#include "identity_bundle.h"
#include <memory>
#include <string>
#include <unordered_map>

Key32 GenerateRandomKey32();

// Tracks the current identity per context ("default" for normal browsing,
// a fresh uuid per incognito window). Nothing here knows about Qt or the
// injection layer — this is purely "who are we pretending to be, right now,
// in this context."
class IdentityManager {
public:
    // Creates and stores a brand-new identity for contextId, replacing any
    // existing one silently — caller decides whether that's a fresh browser
    // launch or a deliberate incognito reset.
    IdentityBundle& CreateIdentity(const std::string& contextId,
                                    BrowserFamily family, OSFamily os,
                                    int32_t screenWidth, int32_t screenHeight) {
        Key32 sessionUuid = GenerateRandomKey32();
        uint64_t seed;
        std::memcpy(&seed, sessionUuid.data(), sizeof(seed));

        NavigatorProfile nav = BuildNavigatorProfile(family, os, seed);
        ScreenProfile screen;
        screen.DeriveFrom(screenWidth, screenHeight);
        WebGLProfile webgl;
        AudioProfile audio;

        auto bundle = std::make_unique<IdentityBundle>(
            contextId, std::move(nav), std::move(screen), std::move(webgl),
            std::move(audio), sessionUuid);

        auto& ref = *bundle;
        identities_[contextId] = std::move(bundle);
        return ref;
    }

    // Returns nullptr if the context was never created or was destroyed —
    // caller (Qt layer) should treat that as "needs CreateIdentity first."
    IdentityBundle* GetIdentity(const std::string& contextId) {
        auto it = identities_.find(contextId);
        return it != identities_.end() ? it->second.get() : nullptr;
    }

    // Called when an incognito window closes — wipes the identity so nothing
    // about it persists or can be correlated with a future incognito session.
    void DestroyIdentity(const std::string& contextId) {
        identities_.erase(contextId);
    }

    bool HasIdentity(const std::string& contextId) const {
        return identities_.count(contextId) > 0;
    }

private:
    std::unordered_map<std::string, std::unique_ptr<IdentityBundle>> identities_;
};