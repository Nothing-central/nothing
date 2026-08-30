#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

using Key32 = std::array<uint8_t, 32>;

// Implemented via a vendored HMAC-SHA256 (e.g. from mbedtls/OpenSSL — link whichever
// packages/profile already depends on elsewhere in the monorepo).
Key32 HmacSha256(const uint8_t* key, size_t keyLen, const uint8_t* data, size_t dataLen);

inline Key32 HmacSha256(const Key32& key, const std::string& data) {
    return HmacSha256(key.data(), key.size(),
                       reinterpret_cast<const uint8_t*>(data.data()), data.size());
}

inline Key32 HmacSha256(const Key32& key, const uint8_t* data, size_t len) {
    return HmacSha256(key.data(), key.size(), data, len);
}

class XorShift128Plus {
public:
    XorShift128Plus(uint64_t s0, uint64_t s1) : s0_(s0 ? s0 : 1), s1_(s1 ? s1 : 2) {}

    static XorShift128Plus FromBytes(const uint8_t* bytes /* >=16 */) {
        uint64_t a, b;
        std::memcpy(&a, bytes, 8);
        std::memcpy(&b, bytes + 8, 8);
        return XorShift128Plus(a, b);
    }

    uint64_t Next() {
        uint64_t x = s0_;
        const uint64_t y = s1_;
        s0_ = y;
        x ^= x << 23;
        x ^= x >> 17;
        x ^= y ^ (y >> 26);
        s1_ = x;
        return x + y;
    }

private:
    uint64_t s0_, s1_;
};

// sessionUUID: generated once per browser session (or per-profile persisted N days).
// origin: scheme://host:port of the top-level navigation.
class SessionKeyStore {
public:
    explicit SessionKeyStore(Key32 sessionUuid) : sessionUuid_(sessionUuid) {}

    const Key32& PerSiteKey(const std::string& origin) {
        auto it = cache_.find(origin);
        if (it != cache_.end()) return it->second;
        Key32 k = HmacSha256(sessionUuid_, origin);
        return cache_.emplace(origin, k).first->second;
    }

    // Mirrors Firefox's RandomizeElements: HMAC for >=2500 bytes, cheaper path below.
    Key32 PerCallKey(const std::string& origin, const uint8_t* content, size_t len) {
        return HmacSha256(PerSiteKey(origin), content, len);
    }

private:
    Key32 sessionUuid_;
    std::unordered_map<std::string, Key32> cache_;
};