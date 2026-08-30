#pragma once
#include "site_key.h"
#include <cstdint>
#include <string>

class AudioInterceptor {
public:
    explicit AudioInterceptor(SessionKeyStore& keys) : keys_(keys) {}

    // getFloatFrequencyData / getByteFrequencyData output — call after the real FFT runs.
    void PerturbFFT(float* bins, size_t binCount, const std::string& origin);

    // OfflineAudioContext.startRendering() result — same treatment, closes Firefox's gap.
    void PerturbOfflineBuffer(float* samples, size_t sampleCount, const std::string& origin);

private:
    SessionKeyStore& keys_;
    void FlipMantissaBits(float* data, size_t count, const Key32& callKey);
};