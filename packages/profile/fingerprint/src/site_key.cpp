#include "site_key.h"
#include "sha256.h"
#include <array>
#include <cstring>

Key32 HmacSha256(const uint8_t* key, size_t keyLen, const uint8_t* data, size_t dataLen) {
    constexpr size_t kBlockSize = 64;
    uint8_t k0[kBlockSize] = {0};

    if (keyLen > kBlockSize) {
        auto hashed = Sha256::Hash(key, keyLen);
        std::memcpy(k0, hashed.data(), hashed.size());
    } else {
        std::memcpy(k0, key, keyLen);
    }

    uint8_t ipad[kBlockSize], opad[kBlockSize];
    for (size_t i = 0; i < kBlockSize; ++i) {
        ipad[i] = k0[i] ^ 0x36;
        opad[i] = k0[i] ^ 0x5c;
    }

    Sha256 inner;
    inner.Update(ipad, kBlockSize);
    inner.Update(data, dataLen);
    auto innerHash = inner.Finalize();

    Sha256 outer;
    outer.Update(opad, kBlockSize);
    outer.Update(innerHash.data(), innerHash.size());
    return outer.Finalize();
}