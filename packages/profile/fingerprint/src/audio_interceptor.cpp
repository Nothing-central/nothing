#include "audio_interceptor.h"
#include <cstring>

void AudioInterceptor::FlipMantissaBits(float* data, size_t count, const Key32& callKey) {
    auto rng1 = XorShift128Plus::FromBytes(callKey.data());       // bin/sample select
    auto rng2 = XorShift128Plus::FromBytes(callKey.data() + 16);  // mantissa bit select

    uint8_t numNoises = static_cast<uint8_t>(callKey[31] % 16 + 5); // clamp [5,20]

    for (uint8_t i = 0; i < numNoises; ++i) {
        uint32_t idx = static_cast<uint32_t>(rng1.Next() % count);
        uint8_t bit = static_cast<uint8_t>(rng2.Next() % 23); // mantissa bits 1..23, never exponent

        uint32_t bits;
        std::memcpy(&bits, &data[idx], sizeof(float));
        bits ^= (1u << bit);
        std::memcpy(&data[idx], &bits, sizeof(float));
    }
}

void AudioInterceptor::PerturbFFT(float* bins, size_t binCount, const std::string& origin) {
    if (binCount == 0) return;
    Key32 callKey = keys_.PerCallKey(origin,
        reinterpret_cast<const uint8_t*>(bins), binCount * sizeof(float));
    FlipMantissaBits(bins, binCount, callKey);
}

void AudioInterceptor::PerturbOfflineBuffer(float* samples, size_t sampleCount,
                                             const std::string& origin) {
    if (sampleCount == 0) return;
    Key32 callKey = keys_.PerCallKey(origin,
        reinterpret_cast<const uint8_t*>(samples), sampleCount * sizeof(float));
    FlipMantissaBits(samples, sampleCount, callKey);
}