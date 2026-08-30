#pragma once
#include <cstdint>
#include <string_view>
#include <unordered_map>

#define FP_TARGET_LIST(X) \
    X(CanvasImageExtraction) \
    X(CanvasRandomization) \
    X(WebGLRenderInfo) \
    X(WebGLRendererConstant) \
    X(WebGLRandomization) \
    X(NavigatorHWConcurrency) \
    X(NavigatorLanguage) \
    X(NavigatorUserAgent) \
    X(NavigatorPlatform) \
    X(NavigatorDeviceMemory) \
    X(ScreenSize) \
    X(ScreenDepth) \
    X(WindowDevicePixelRatio) \
    X(AudioSampleRate) \
    X(AudioNoise) \
    X(FontVisibilityBaseSystem) \
    X(FontVisibilityLangPack) \
    X(TimezoneOffset)

enum class FPTarget : uint64_t {
#define X(name) name,
    FP_TARGET_LIST(X)
#undef X
    kCount
};

inline const std::unordered_map<std::string_view, FPTarget>& FPTargetNameMap() {
    static const std::unordered_map<std::string_view, FPTarget> map = {
#define X(name) {#name, FPTarget::name},
        FP_TARGET_LIST(X)
#undef X
    };
    return map;
}