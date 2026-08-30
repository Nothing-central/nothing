#include "identity_manager.h"
#include <random>

Key32 GenerateRandomKey32() {
    Key32 k;
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<uint32_t> dist(0, 255);
    for (auto& b : k) b = static_cast<uint8_t>(dist(gen));
    return k;
}