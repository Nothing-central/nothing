#pragma once
#include <cstdint>
#include <string>

struct AudioProfile {
    double sample_rate = 44100.0;
    uint32_t max_channel_count = 2;
    double base_latency = 0.0;
    double output_latency = 0.025;
    double current_time_resolution = 16.667e-3;

    bool inject_fft_noise = true;
    bool inject_offline_render_noise = true;
    bool force_scalar_audio_path = true;
};