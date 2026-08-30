#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

enum class BrowserFamily { Firefox, Chrome };
enum class OSFamily { Windows, macOS, Linux, Android };

struct UserAgentData {
    std::vector<std::pair<std::string, std::string>> brands; // {"Chromium","124"}
    bool mobile = false;
    std::string platform;
};

struct NavigatorProfile {
    BrowserFamily family = BrowserFamily::Chrome;
    OSFamily os = OSFamily::Windows;

    std::string user_agent;
    std::string platform;
    std::string oscpu;              // Firefox only, empty otherwise
    std::string app_version;
    std::string app_name = "Netscape";
    std::string app_code_name = "Mozilla";
    std::string product = "Gecko";
    std::string product_sub;        // "20100101" FF / "20030107" Chrome
    std::string vendor;             // "" FF / "Google Inc." Chrome
    std::string vendor_sub;
    std::string build_id;           // FF only

    uint32_t hardware_concurrency = 4;
    std::optional<double> device_memory; // Chrome only
    uint32_t max_touch_points = 0;

    std::vector<std::string> languages = {"en-US", "en"};
    std::string intl_locale = "en-US";
    std::string accept_language_header = "en-US,en;q=0.5";

    std::string timezone = "Atlantic/Reykjavik";
    int32_t timezone_offset_minutes = 0;

    bool pdf_viewer_enabled = true;
    bool webdriver = false;
    bool cookie_enabled = true;
    bool online = true;
    std::string do_not_track = "unspecified";
    bool global_privacy_control = false;

    std::optional<UserAgentData> user_agent_data; // Chrome only
};

// Common real-world tiers — pick one, don't invent arbitrary numbers.
inline uint32_t PickHardwareConcurrency(uint64_t seed) {
    static const uint32_t tiers[] = {2, 4, 6, 8, 12, 16};
    return tiers[seed % (sizeof(tiers) / sizeof(tiers[0]))];
}

inline double PickDeviceMemory(uint64_t seed) {
    static const double tiers[] = {2.0, 4.0, 8.0, 16.0};
    return tiers[seed % (sizeof(tiers) / sizeof(tiers[0]))];
}

// Builds a fully coherent profile from one seed decision (family+os).
// seed drives randomized-but-plausible fields (hardwareConcurrency, deviceMemory)
// so repeated calls with the same seed are stable, different seeds vary.
// Every field is derived here — never set independently elsewhere.
NavigatorProfile BuildNavigatorProfile(BrowserFamily family, OSFamily os, uint64_t seed);