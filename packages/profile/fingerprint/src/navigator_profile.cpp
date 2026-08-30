#include "navigator_profile.h"

namespace {

void ApplyWindows(NavigatorProfile& p, BrowserFamily f) {
    p.platform = "Win32";
    p.max_touch_points = 10;
    if (f == BrowserFamily::Firefox) {
        p.oscpu = "Windows NT 10.0; Win64; x64";
        p.app_version = "5.0 (Windows)";
        p.user_agent = "Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:121.0) "
                        "Gecko/20100101 Firefox/121.0";
    } else {
        p.app_version = "5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
                         "(KHTML, like Gecko) Chrome/124.0.0.0 Safari/537.36";
        p.user_agent = "Mozilla/" + p.app_version;
        p.user_agent_data = UserAgentData{
            {{"Chromium", "124"}, {"Google Chrome", "124"}, {"Not-A.Brand", "99"}},
            false, "Windows"};
    }
}

void ApplyMac(NavigatorProfile& p, BrowserFamily f) {
    p.platform = "MacIntel";
    p.max_touch_points = 0;
    if (f == BrowserFamily::Firefox) {
        p.oscpu = "Intel Mac OS X 10.15";
        p.app_version = "5.0 (Macintosh)";
        p.user_agent = "Mozilla/5.0 (Macintosh; Intel Mac OS X 10.15; rv:121.0) "
                        "Gecko/20100101 Firefox/121.0";
    } else {
        p.app_version = "5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 "
                         "(KHTML, like Gecko) Chrome/124.0.0.0 Safari/537.36";
        p.user_agent = "Mozilla/" + p.app_version;
        p.user_agent_data = UserAgentData{
            {{"Chromium", "124"}, {"Google Chrome", "124"}, {"Not-A.Brand", "99"}},
            false, "macOS"};
    }
}

void ApplyLinux(NavigatorProfile& p, BrowserFamily f) {
    p.platform = "Linux x86_64";
    p.max_touch_points = 5;
    if (f == BrowserFamily::Firefox) {
        p.oscpu = "Linux x86_64";
        p.app_version = "5.0 (X11)";
        p.user_agent = "Mozilla/5.0 (X11; Linux x86_64; rv:121.0) "
                        "Gecko/20100101 Firefox/121.0";
    } else {
        p.app_version = "5.0 (X11; Linux x86_64) AppleWebKit/537.36 "
                         "(KHTML, like Gecko) Chrome/124.0.0.0 Safari/537.36";
        p.user_agent = "Mozilla/" + p.app_version;
        p.user_agent_data = UserAgentData{
            {{"Chromium", "124"}, {"Google Chrome", "124"}, {"Not-A.Brand", "99"}},
            false, "Linux"};
    }
}

} // namespace

NavigatorProfile BuildNavigatorProfile(BrowserFamily family, OSFamily os, uint64_t seed) {
    NavigatorProfile p;
    p.family = family;
    p.os = os;

    if (family == BrowserFamily::Firefox) {
        p.product_sub = "20100101";
        p.vendor = "";
        p.vendor_sub = "";
        p.build_id = "20181001000000";
        p.hardware_concurrency = (os == OSFamily::macOS) ? 8 : 4; // Firefox pins low — keep as-is
        p.device_memory.reset();
    } else {
        p.product_sub = "20030107";
        p.vendor = "Google Inc.";
        p.vendor_sub = "";
        p.build_id = "";
        p.hardware_concurrency = PickHardwareConcurrency(seed);
        p.device_memory = PickDeviceMemory(seed >> 8); // different bits, avoid correlated picks
    }

    switch (os) {
        case OSFamily::Windows: ApplyWindows(p, family); break;
        case OSFamily::macOS:   ApplyMac(p, family);     break;
        case OSFamily::Linux:   ApplyLinux(p, family);   break;
        case OSFamily::Android: ApplyLinux(p, family);   break; // TODO: real Android UA
    }

    return p;
}