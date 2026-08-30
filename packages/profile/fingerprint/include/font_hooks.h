#pragma once
#include "font_visibility.h"
#include <optional>
#include <string>

class FontHooks {
public:
    explicit FontHooks(FontVisibilityProvider provider) : provider_(std::move(provider)) {}

    // Gate for CSS font-family resolution / measureText fallback selection.
    bool CanUseFamily(const std::string& familyName) const {
        return provider_.IsVisible(familyName);
    }

    // document.fonts.check('1px SomeFont') — closes Firefox's gap (nullptr provider bug).
    bool CheckFontLoaded(const std::string& familyName, bool actuallyInstalled) const {
        return actuallyInstalled && provider_.IsVisible(familyName);
    }

    // @font-face { src: local("Name") } resolution — return nullopt to force url() fallback.
    std::optional<std::string> ResolveLocalSrc(const std::string& familyName,
                                                bool actuallyInstalled) const {
        if (actuallyInstalled && provider_.IsVisible(familyName)) return familyName;
        return std::nullopt;
    }

private:
    FontVisibilityProvider provider_;
};