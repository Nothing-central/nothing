#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

// Self-contained SHA-256 — public domain style, no external deps.
class Sha256 {
public:
    Sha256() { Reset(); }

    void Reset() {
        h_[0]=0x6a09e667; h_[1]=0xbb67ae85; h_[2]=0x3c6ef372; h_[3]=0xa54ff53a;
        h_[4]=0x510e527f; h_[5]=0x9b05688c; h_[6]=0x1f83d9ab; h_[7]=0x5be0cd19;
        bitlen_ = 0;
        buflen_ = 0;
    }

    void Update(const uint8_t* data, size_t len) {
        bitlen_ += static_cast<uint64_t>(len) * 8;
        while (len > 0) {
            size_t take = std::min(len, size_t(64) - buflen_);
            std::memcpy(buf_ + buflen_, data, take);
            buflen_ += take;
            data += take;
            len -= take;
            if (buflen_ == 64) {
                Transform(buf_);
                buflen_ = 0;
            }
        }
    }

    std::array<uint8_t, 32> Finalize() {
        uint8_t pad[64] = {0x80};
        size_t padLen = (buflen_ < 56) ? (56 - buflen_) : (120 - buflen_);
        uint64_t bitlenBE = 0;
        for (int i = 0; i < 8; ++i)
            reinterpret_cast<uint8_t*>(&bitlenBE)[i] = static_cast<uint8_t>(bitlen_ >> (56 - 8 * i));

        Update(pad, padLen);
        Update(reinterpret_cast<uint8_t*>(&bitlenBE), 8);

        std::array<uint8_t, 32> out;
        for (int i = 0; i < 8; ++i) {
            out[i*4+0] = static_cast<uint8_t>(h_[i] >> 24);
            out[i*4+1] = static_cast<uint8_t>(h_[i] >> 16);
            out[i*4+2] = static_cast<uint8_t>(h_[i] >> 8);
            out[i*4+3] = static_cast<uint8_t>(h_[i]);
        }
        return out;
    }

    static std::array<uint8_t, 32> Hash(const uint8_t* data, size_t len) {
        Sha256 s;
        s.Update(data, len);
        return s.Finalize();
    }

private:
    uint32_t h_[8];
    uint64_t bitlen_;
    uint8_t buf_[64];
    size_t buflen_;

    static uint32_t Rotr(uint32_t x, uint32_t n) { return (x >> n) | (x << (32 - n)); }

    void Transform(const uint8_t block[64]) {
        static const uint32_t k[64] = {
            0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
            0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
            0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
            0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
            0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
            0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
            0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
            0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
        };

        uint32_t w[64];
        for (int i = 0; i < 16; ++i) {
            w[i] = (uint32_t(block[i*4]) << 24) | (uint32_t(block[i*4+1]) << 16) |
                   (uint32_t(block[i*4+2]) << 8) | uint32_t(block[i*4+3]);
        }
        for (int i = 16; i < 64; ++i) {
            uint32_t s0 = Rotr(w[i-15], 7) ^ Rotr(w[i-15], 18) ^ (w[i-15] >> 3);
            uint32_t s1 = Rotr(w[i-2], 17) ^ Rotr(w[i-2], 19) ^ (w[i-2] >> 10);
            w[i] = w[i-16] + s0 + w[i-7] + s1;
        }

        uint32_t a=h_[0],b=h_[1],c=h_[2],d=h_[3],e=h_[4],f=h_[5],g=h_[6],hh=h_[7];
        for (int i = 0; i < 64; ++i) {
            uint32_t S1 = Rotr(e,6) ^ Rotr(e,11) ^ Rotr(e,25);
            uint32_t ch = (e & f) ^ (~e & g);
            uint32_t temp1 = hh + S1 + ch + k[i] + w[i];
            uint32_t S0 = Rotr(a,2) ^ Rotr(a,13) ^ Rotr(a,22);
            uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            uint32_t temp2 = S0 + maj;
            hh=g; g=f; f=e; e=d+temp1; d=c; c=b; b=a; a=temp1+temp2;
        }
        h_[0]+=a; h_[1]+=b; h_[2]+=c; h_[3]+=d; h_[4]+=e; h_[5]+=f; h_[6]+=g; h_[7]+=hh;
    }
};