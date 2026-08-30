#include "screen_profile.h"

int32_t StepDimension(int32_t dim, bool isWidth) {
    if (dim <= 50) return dim;
    int32_t step;
    if (dim <= 500) step = 50;
    else if (dim <= 1600) step = isWidth ? 200 : 100;
    else step = 200;
    return dim - (dim % step);
}

void ScreenProfile::DeriveFrom(int32_t realWidth, int32_t realHeight) {
    width = StepDimension(realWidth, true);
    height = StepDimension(realHeight, false);
    color_depth = pixel_depth = 24;
    device_pixel_ratio = 2.0;
    screen_x = screen_y = 0;
    avail_left = avail_top = 0;
    color_gamut = "srgb";

    if (width >= height) {
        orientation_type = "landscape-primary";
        orientation_angle = 0;
    } else {
        orientation_type = "portrait-primary";
        orientation_angle = 90;
    }
}