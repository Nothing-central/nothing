#pragma once
#include "navigator_profile.h"
#include "screen_profile.h"
#include "webgl_profile.h"
#include "audio_profile.h"
#include "site_key.h"
#include <QString>

// Takes already-generated identity data + a SessionKeyStore (from your
// existing site_key.h) and produces the JS to inject for one navigation.
// No identity generation happens here — that's your existing structs' job.
class FingerprintSpoofer {
public:
    FingerprintSpoofer(const NavigatorProfile& nav, const ScreenProfile& scr,
                        const WebGLProfile& gl, const AudioProfile& audio,
                        SessionKeyStore& keys)
        : nav_(nav), scr_(scr), gl_(gl), audio_(audio), keys_(keys) {}

    QString injectionScript(const std::string& origin) const;

private:
    const NavigatorProfile& nav_;
    const ScreenProfile& scr_;
    const WebGLProfile& gl_;
    const AudioProfile& audio_;
    SessionKeyStore& keys_;
};