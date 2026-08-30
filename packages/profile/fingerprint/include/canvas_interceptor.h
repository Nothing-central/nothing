#pragma once
#include "site_key.h"
#include <cstdint>
#include <string>

class CanvasInterceptor {
public:
    explicit CanvasInterceptor(SessionKeyStore& keys) : keys_(keys) {}

    // rgba: BGRA-premultiplied buffer, exactly as Firefox perturbs before unpremultiply.
    // Call on every getImageData/toDataURL/toBlob/readPixels/measureText-width readback.
    void Perturb(uint8_t* rgba, size_t width, size_t height, const std::string& origin);

    // measureText width jitter — ±1 LSB, deterministic per (origin, text, font).
    double PerturbTextWidth(double realWidth, const std::string& origin,
                             const std::string& text, const std::string& font);

private:
    SessionKeyStore& keys_;
    bool IsUniform(const uint8_t* rgba, size_t pixelCount) const;
};