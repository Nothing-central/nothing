#include "canvas_interceptor.h"
#include <cstring>

bool CanvasInterceptor::IsUniform(const uint8_t* rgba, size_t pixelCount) const {
    if (pixelCount == 0) return true;
    for (size_t i = 1; i < pixelCount; ++i) {
        if (std::memcmp(rgba, rgba + i * 4, 4) != 0) return false;
    }
    return true;
}

void CanvasInterceptor::Perturb(uint8_t* rgba, size_t width, size_t height,
                                 const std::string& origin) {
    const size_t pixelCount = width * height;
    if (pixelCount == 0) return;
    if (IsUniform(rgba, pixelCount)) return; // matches Firefox: no tell on blank canvas

    Key32 callKey = keys_.PerCallKey(origin, rgba, pixelCount * 4);

    auto rng1 = XorShift128Plus::FromBytes(callKey.data());       // pixel + channel select
    auto rng2 = XorShift128Plus::FromBytes(callKey.data() + 16);  // bit select
    uint8_t numNoises = callKey[31];
    numNoises = numNoises < 20 ? 20 : numNoises; // clamp [20,255]

    // IMPROVEMENT over Firefox: include alpha channel (channel % 4, not % 3)
    for (uint8_t i = 0; i < numNoises; ++i) {
        uint32_t pixel = static_cast<uint32_t>(rng1.Next() % pixelCount);
        uint8_t channel = static_cast<uint8_t>(rng1.Next() % 4);
        uint8_t bit = static_cast<uint8_t>(rng2.Next() & 1);
        rgba[pixel * 4 + channel] ^= (0x2 >> bit);
    }
}

double CanvasInterceptor::PerturbTextWidth(double realWidth, const std::string& origin,
                                            const std::string& text, const std::string& font) {
    std::string blob = text + "|" + font;
    Key32 key = keys_.PerCallKey(origin,
        reinterpret_cast<const uint8_t*>(blob.data()), blob.size());
    // ±1 LSB in the fractional part — invisible to layout, breaks width-hash fingerprinting
    int8_t sign = (key[0] & 1) ? 1 : -1;
    return realWidth + sign * 0.001;
}