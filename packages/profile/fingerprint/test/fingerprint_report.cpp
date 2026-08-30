#include "navigator_profile.h"
#include "screen_profile.h"
#include "webgl_profile.h"
#include "audio_profile.h"
#include "font_visibility.h"
#include "generated_fonts_direct.h"
#include "canvas_interceptor.h"
#include "audio_interceptor.h"
#include "site_key.h"

#include <cstdio>
#include <cstring>
#include <sstream>
#include <vector>

namespace {

std::string JsonEscape(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    return out;
}

std::string HexKey(const Key32& k) {
    static const char* hex = "0123456789abcdef";
    std::string out;
    out.reserve(64);
    for (uint8_t b : k) {
        out += hex[b >> 4];
        out += hex[b & 0xF];
    }
    return out;
}

std::string ArrToJson(const std::vector<std::string>& v) {
    std::ostringstream o;
    o << "[";
    for (size_t i = 0; i < v.size(); ++i) {
        o << "\"" << JsonEscape(v[i]) << "\"";
        if (i + 1 < v.size()) o << ",";
    }
    o << "]";
    return o.str();
}

std::string UADataToJson(const std::optional<UserAgentData>& ua) {
    if (!ua) return "null";
    std::ostringstream o;
    o << "{\"brands\":[";
    for (size_t i = 0; i < ua->brands.size(); ++i) {
        o << "{\"brand\":\"" << JsonEscape(ua->brands[i].first)
          << "\",\"version\":\"" << JsonEscape(ua->brands[i].second) << "\"}";
        if (i + 1 < ua->brands.size()) o << ",";
    }
    o << "],\"mobile\":" << (ua->mobile ? "true" : "false")
      << ",\"platform\":\"" << JsonEscape(ua->platform) << "\"}";
    return o.str();
}

} // namespace

int main(int argc, char** argv) {
    // ---- pick identity ----
    BrowserFamily family = BrowserFamily::Chrome;
    OSFamily os = OSFamily::Windows;
    std::string originForTest = "https://example.com";

    // ---- session key (deterministic in this run for reproducibility) ----
    Key32 sessionUuid{};
    for (size_t i = 0; i < sessionUuid.size(); ++i) sessionUuid[i] = static_cast<uint8_t>(i * 7 + 1);
    SessionKeyStore keys(sessionUuid);
    Key32 perSiteKey = keys.PerSiteKey(originForTest);

    // ---- seed derived from session key, drives randomized-but-plausible navigator fields ----
    uint64_t seed;
    std::memcpy(&seed, sessionUuid.data(), sizeof(seed));

    NavigatorProfile nav = BuildNavigatorProfile(family, os, seed);
    ScreenProfile scr;
    scr.DeriveFrom(1920, 1080);
    WebGLProfile gl;
    AudioProfile audio;

    const std::unordered_set<std::string>& baseFonts = LoadBaseFontsForPlatform(nav.platform);
    std::unordered_set<std::string> langFonts = LoadLangPackFontsForLocale(nav.intl_locale);
    FontVisibilityProvider fontProvider(FontVisibility::Base, baseFonts, langFonts);

    // ---- canvas noise sample ----
    const size_t cw = 16, ch = 4;
    std::vector<uint8_t> canvas(cw * ch * 4, 0);
    for (size_t i = 0; i < cw * ch; ++i) {
        canvas[i * 4 + 0] = static_cast<uint8_t>(i * 3);
        canvas[i * 4 + 1] = static_cast<uint8_t>(i * 5);
        canvas[i * 4 + 2] = static_cast<uint8_t>(i * 7);
        canvas[i * 4 + 3] = 255;
    }
    CanvasInterceptor canvasFx(keys);
    canvasFx.Perturb(canvas.data(), cw, ch, originForTest);
    Key32 canvasHash = HmacSha256(perSiteKey, canvas.data(), canvas.size());

    // ---- audio FFT noise sample ----
    std::vector<float> fftBins(64);
    for (size_t i = 0; i < fftBins.size(); ++i) fftBins[i] = -100.0f + static_cast<float>(i) * 0.5f;
    AudioInterceptor audioFx(keys);
    audioFx.PerturbFFT(fftBins.data(), fftBins.size(), originForTest);
    Key32 audioHash = HmacSha256(perSiteKey, reinterpret_cast<const uint8_t*>(fftBins.data()),
                                  fftBins.size() * sizeof(float));

    // ---- font check sample ----
    bool arialVisible = fontProvider.IsVisible("Arial");
    bool randomUserFontVisible = fontProvider.IsVisible("Some Totally Custom Font XYZ");

    // ---- emit JSON ----
    std::ostringstream j;
    j << "{\n";
    j << "  \"navigator\": {\n";
    j << "    \"userAgent\": \"" << JsonEscape(nav.user_agent) << "\",\n";
    j << "    \"platform\": \"" << JsonEscape(nav.platform) << "\",\n";
    j << "    \"appVersion\": \"" << JsonEscape(nav.app_version) << "\",\n";
    j << "    \"vendor\": \"" << JsonEscape(nav.vendor) << "\",\n";
    j << "    \"productSub\": \"" << JsonEscape(nav.product_sub) << "\",\n";
    j << "    \"hardwareConcurrency\": " << nav.hardware_concurrency << ",\n";
    j << "    \"deviceMemory\": " << (nav.device_memory ? std::to_string(*nav.device_memory) : "null") << ",\n";
    j << "    \"maxTouchPoints\": " << nav.max_touch_points << ",\n";
    j << "    \"languages\": " << ArrToJson(nav.languages) << ",\n";
    j << "    \"timezone\": \"" << JsonEscape(nav.timezone) << "\",\n";
    j << "    \"timezoneOffsetMinutes\": " << nav.timezone_offset_minutes << ",\n";
    j << "    \"webdriver\": " << (nav.webdriver ? "true" : "false") << ",\n";
    j << "    \"userAgentData\": " << UADataToJson(nav.user_agent_data) << "\n";
    j << "  },\n";
    j << "  \"screen\": {\n";
    j << "    \"width\": " << scr.width << ",\n";
    j << "    \"height\": " << scr.height << ",\n";
    j << "    \"colorDepth\": " << scr.color_depth << ",\n";
    j << "    \"devicePixelRatio\": " << scr.device_pixel_ratio << ",\n";
    j << "    \"colorGamut\": \"" << scr.color_gamut << "\",\n";
    j << "    \"orientationType\": \"" << scr.orientation_type << "\"\n";
    j << "  },\n";
    j << "  \"webgl\": {\n";
    j << "    \"vendor\": \"" << JsonEscape(gl.vendor) << "\",\n";
    j << "    \"renderer\": \"" << JsonEscape(gl.renderer) << "\",\n";
    j << "    \"maxTextureSize\": " << gl.max_texture_size << ",\n";
    j << "    \"extensionCount\": " << gl.supported_extensions.size() << "\n";
    j << "  },\n";
    j << "  \"audio\": {\n";
    j << "    \"sampleRate\": " << audio.sample_rate << ",\n";
    j << "    \"maxChannelCount\": " << audio.max_channel_count << ",\n";
    j << "    \"baseLatency\": " << audio.base_latency << ",\n";
    j << "    \"fftNoiseHash\": \"" << HexKey(audioHash) << "\"\n";
    j << "  },\n";
    j << "  \"fonts\": {\n";
    j << "    \"visibilityLevel\": " << static_cast<int>(fontProvider.Level()) << ",\n";
    j << "    \"baseFontCount\": " << baseFonts.size() << ",\n";
    j << "    \"arialVisible\": " << (arialVisible ? "true" : "false") << ",\n";
    j << "    \"unknownUserFontVisible\": " << (randomUserFontVisible ? "true" : "false") << "\n";
    j << "  },\n";
    j << "  \"canvas\": {\n";
    j << "    \"noiseHash\": \"" << HexKey(canvasHash) << "\"\n";
    j << "  },\n";
    j << "  \"session\": {\n";
    j << "    \"perSiteKey\": \"" << HexKey(perSiteKey) << "\",\n";
    j << "    \"origin\": \"" << JsonEscape(originForTest) << "\"\n";
    j << "  }\n";
    j << "}\n";

    std::printf("%s", j.str().c_str());
    return 0;
}