#pragma once
#include <cstdint>
#include <string>

struct ScreenProfile {
    int32_t width = 0, height = 0;
    int32_t avail_left = 0, avail_top = 0;
    int32_t color_depth = 24, pixel_depth = 24;
    double device_pixel_ratio = 2.0;
    int32_t screen_x = 0, screen_y = 0;
    std::string color_gamut = "srgb";
    std::string orientation_type = "landscape-primary";
    uint16_t orientation_angle = 0;

    void DeriveFrom(int32_t realWidth, int32_t realHeight);
};

int32_t StepDimension(int32_t dim, bool isWidth);