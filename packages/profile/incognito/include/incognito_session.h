#pragma once
#include "identity_manager.h"
#include <string>

// The only thing incognito actually needs to do: tell IdentityManager
// "give this window a brand-new identity, unconnected to anything else."
// No access to the previous identity's data is needed or given — that's
// the whole point of incognito (no correlation), not a migration.
class IncognitoSession {
public:
    // Call when a new incognito window/profile is created.
    // Returns the contextId to associate with that window's QWebEngineProfile —
    // pass this same id into FingerprintSpoofer wiring for that window.
    static std::string Start(IdentityManager& manager,
                              BrowserFamily family, OSFamily os,
                              int32_t screenWidth, int32_t screenHeight) {
        std::string contextId = "incognito-" + GenerateContextSuffix();
        manager.CreateIdentity(contextId, family, os, screenWidth, screenHeight);
        return contextId;
    }

    // Call when the incognito window closes.
    static void End(IdentityManager& manager, const std::string& contextId) {
        manager.DestroyIdentity(contextId);
    }

private:
    static std::string GenerateContextSuffix() {
        Key32 k = GenerateRandomKey32();
        static const char* hex = "0123456789abcdef";
        std::string out;
        out.reserve(16);
        for (int i = 0; i < 8; ++i) {
            out += hex[k[i] >> 4];
            out += hex[k[i] & 0xF];
        }
        return out;
    }
};