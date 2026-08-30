#pragma once
#include <string>
#include <unordered_set>

#define StandardFonts 1

namespace fonts_win {
#include "../vendor/StandardFonts-win10.inc"
}
namespace fonts_mac {
#include "../vendor/StandardFonts-macos.inc"
}
namespace fonts_linux {
#include "../vendor/StandardFonts-linux.inc"
}
namespace fonts_android {
#include "../vendor/StandardFonts-android.inc"
}

#undef StandardFonts

inline std::unordered_set<std::string> ToSet(const char* const* arr, size_t n) {
    return std::unordered_set<std::string>(arr, arr + n);
}

inline const std::unordered_set<std::string>& BaseFontsWindows() {
    static const auto s = ToSet(fonts_win::kBaseFonts,
        sizeof(fonts_win::kBaseFonts) / sizeof(fonts_win::kBaseFonts[0]));
    return s;
}
inline const std::unordered_set<std::string>& LangPackFontsWindows() {
    static const auto s = ToSet(fonts_win::kLangPackFonts,
        sizeof(fonts_win::kLangPackFonts) / sizeof(fonts_win::kLangPackFonts[0]));
    return s;
}
inline const std::unordered_set<std::string>& BaseFontsMacOS() {
    static const auto s = ToSet(fonts_mac::kBaseFonts,
        sizeof(fonts_mac::kBaseFonts) / sizeof(fonts_mac::kBaseFonts[0]));
    return s;
}
inline const std::unordered_set<std::string>& BaseFontsLinux() {
    static const auto s = ToSet(fonts_linux::kBaseFonts_Ubuntu_22_04,
        sizeof(fonts_linux::kBaseFonts_Ubuntu_22_04) / sizeof(fonts_linux::kBaseFonts_Ubuntu_22_04[0]));
    return s;
}
inline const std::unordered_set<std::string>& BaseFontsAndroid() {
    static const auto s = ToSet(fonts_android::kBaseFonts_Android,
        sizeof(fonts_android::kBaseFonts_Android) / sizeof(fonts_android::kBaseFonts_Android[0]));
    return s;
}