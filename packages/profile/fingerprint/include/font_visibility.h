#pragma once
#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

enum class FontVisibility : uint8_t {
    Unknown = 0,
    Base = 1,
    LangPack = 2,
    User = 3,
    Hidden = 4,
    Webfont = 5,
};

class FontVisibilityProvider {
public:
    FontVisibilityProvider(FontVisibility level, const std::unordered_set<std::string>& baseFonts,
                            std::unordered_set<std::string> langPackFonts)
        : level_(level), base_(baseFonts), langPack_(std::move(langPackFonts)) {}

    // The single gatekeeping predicate — every font lookup (CSS resolution,
    // measureText, document.fonts.check, @font-face local() resolution) goes through this.
    bool IsVisible(const std::string& familyName) const {
        FontVisibility v = ClassifyFamily(familyName);
        return static_cast<uint8_t>(v) <= static_cast<uint8_t>(level_);
    }

    FontVisibility ClassifyFamily(const std::string& familyName) const {
        if (!familyName.empty() && familyName[0] == '.') return FontVisibility::Hidden;
        if (base_.count(familyName)) return FontVisibility::Base;
        if (langPack_.count(familyName)) return FontVisibility::LangPack;
        return FontVisibility::User;
    }

    FontVisibility Level() const { return level_; }

private:
    FontVisibility level_;
    const std::unordered_set<std::string>& base_;   // ref into generated_fonts_direct.h — no copy
    std::unordered_set<std::string> langPack_;        // small, per-locale, fine to own
};

// Port of StandardFonts-{platform}.inc — returns a ref into generated_fonts_direct.h.
const std::unordered_set<std::string>& LoadBaseFontsForPlatform(const std::string& platform);
std::unordered_set<std::string> LoadLangPackFontsForLocale(const std::string& locale);