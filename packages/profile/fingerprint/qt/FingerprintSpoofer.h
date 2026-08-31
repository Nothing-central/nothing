#pragma once
#include "navigator_profile.h"
#include "screen_profile.h"
#include "webgl_profile.h"
#include "audio_profile.h"
#include "site_key.h"
#include <QString>

// Takes already-generated identity data + a SessionKeyStore and produces
// ONE script for the whole session. No origin is baked in — the script
// derives its per-origin noise key itself, at runtime, from location.origin.
// Set this once on a WebEngineProfile before any tab loads.
class FingerprintSpoofer {
public:
    FingerprintSpoofer(const NavigatorProfile& nav, const ScreenProfile& scr,
                        const WebGLProfile& gl, const AudioProfile& audio,
                        SessionKeyStore& keys)
        : nav_(nav), scr_(scr), gl_(gl), audio_(audio), keys_(keys) {}

    QString injectionScript() const;

private:
    const NavigatorProfile& nav_;
    const ScreenProfile& scr_;
    const WebGLProfile& gl_;
    const AudioProfile& audio_;
    SessionKeyStore& keys_;
};