#include "font_visibility.h"
#include "generated_fonts_direct.h"

const std::unordered_set<std::string>& LoadBaseFontsForPlatform(const std::string& platform) {
    if (platform == "Win32") return BaseFontsWindows();
    if (platform == "MacIntel") return BaseFontsMacOS();
    if (platform.find("Android") != std::string::npos) return BaseFontsAndroid();
    return BaseFontsLinux();
}

std::unordered_set<std::string> LoadLangPackFontsForLocale(const std::string& locale) {
    // Windows has a real kLangPackFonts array — use it as the base pool,
    // then narrow later if you port the FONT_RULE locale-matching too.
    return LangPackFontsWindows();
}