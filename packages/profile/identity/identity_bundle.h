#pragma once
#include "navigator_profile.h"
#include "screen_profile.h"
#include "webgl_profile.h"
#include "audio_profile.h"
#include "site_key.h"

// Everything that makes up "one identity" — normal browsing has one of these,
// each incognito window gets its own, thrown away on close.
struct IdentityBundle {
    std::string contextId;      // "default" or a per-incognito-window uuid
    NavigatorProfile nav;
    ScreenProfile screen;
    WebGLProfile webgl;
    AudioProfile audio;
    SessionKeyStore keys;       // owns the sessionUuid this identity's noise derives from

    IdentityBundle(std::string id, NavigatorProfile n, ScreenProfile s,
                   WebGLProfile g, AudioProfile a, Key32 sessionUuid)
        : contextId(std::move(id)), nav(std::move(n)), screen(std::move(s)),
          webgl(std::move(g)), audio(std::move(a)), keys(sessionUuid) {}
};