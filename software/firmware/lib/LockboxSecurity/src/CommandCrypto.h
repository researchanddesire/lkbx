#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#if defined(ARDUINO_ARCH_ESP32)
#include <mbedtls/aes.h>
#include <mbedtls/md.h>
#endif

namespace lockbox::security {

constexpr std::size_t HMAC_SHA256_BYTES = 32;
constexpr std::size_t COMMAND_AES_BLOCK = 16;

#if defined(ARDUINO_ARCH_ESP32)

inline bool hmacSha256(const std::uint8_t* key, std::size_t keyLength,
                       const std::uint8_t* message, std::size_t messageLength,
                       std::uint8_t* output) {
    const mbedtls_md_info_t* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    return info && mbedtls_md_hmac(info, key, keyLength, message, messageLength,
                                   output) == 0;
}

// Input length must already be a non-zero multiple of the AES block size.
inline bool aes256CbcDecrypt(const std::uint8_t* key, const std::uint8_t* iv,
                             const std::uint8_t* input, std::size_t length,
                             std::uint8_t* output) {
    std::uint8_t chain[COMMAND_AES_BLOCK];
    std::memcpy(chain, iv, sizeof(chain));
    mbedtls_aes_context context;
    mbedtls_aes_init(&context);
    const bool decrypted =
        mbedtls_aes_setkey_dec(&context, key, 256) == 0 &&
        mbedtls_aes_crypt_cbc(&context, MBEDTLS_AES_DECRYPT, length, chain, input,
                              output) == 0;
    mbedtls_aes_free(&context);
    return decrypted;
}

#else

// Host-only primitives for native tests. Devices use mbedTLS above; the shared
// vector in test_integration exercises that path on hardware.
namespace host_crypto {

inline std::uint32_t rotateRight(std::uint32_t value, int bits) {
    return (value >> bits) | (value << (32 - bits));
}

class Sha256 {
 public:
    void update(const std::uint8_t* data, std::size_t length) {
        total_ += length;
        while (length > 0) {
            const std::size_t count =
                length < sizeof(block_) - used_ ? length : sizeof(block_) - used_;
            std::memcpy(block_ + used_, data, count);
            used_ += count;
            data += count;
            length -= count;
            if (used_ == sizeof(block_)) {
                transform();
                used_ = 0;
            }
        }
    }

    void finish(std::uint8_t* output) {
        const std::uint64_t bits = total_ * 8;
        const std::uint8_t marker = 0x80;
        update(&marker, 1);
        const std::uint8_t zero = 0;
        while (used_ != 56) update(&zero, 1);
        std::uint8_t length[8];
        for (int index = 0; index < 8; ++index) {
            length[index] = static_cast<std::uint8_t>(bits >> (56 - index * 8));
        }
        update(length, sizeof(length));
        for (int index = 0; index < 8; ++index) {
            output[index * 4] = static_cast<std::uint8_t>(state_[index] >> 24);
            output[index * 4 + 1] = static_cast<std::uint8_t>(state_[index] >> 16);
            output[index * 4 + 2] = static_cast<std::uint8_t>(state_[index] >> 8);
            output[index * 4 + 3] = static_cast<std::uint8_t>(state_[index]);
        }
    }

 private:
    void transform() {
        static const std::uint32_t constants[64] = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
            0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
            0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
            0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
            0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
            0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
            0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
            0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
            0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
            0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
            0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
        std::uint32_t words[64];
        for (int index = 0; index < 16; ++index) {
            words[index] = (static_cast<std::uint32_t>(block_[index * 4]) << 24) |
                           (static_cast<std::uint32_t>(block_[index * 4 + 1]) << 16) |
                           (static_cast<std::uint32_t>(block_[index * 4 + 2]) << 8) |
                           block_[index * 4 + 3];
        }
        for (int index = 16; index < 64; ++index) {
            const std::uint32_t low = rotateRight(words[index - 15], 7) ^
                                      rotateRight(words[index - 15], 18) ^
                                      (words[index - 15] >> 3);
            const std::uint32_t high = rotateRight(words[index - 2], 17) ^
                                       rotateRight(words[index - 2], 19) ^
                                       (words[index - 2] >> 10);
            words[index] = words[index - 16] + low + words[index - 7] + high;
        }
        std::uint32_t round[8];
        std::memcpy(round, state_, sizeof(round));
        for (int index = 0; index < 64; ++index) {
            const std::uint32_t sumHigh = rotateRight(round[4], 6) ^
                                          rotateRight(round[4], 11) ^
                                          rotateRight(round[4], 25);
            const std::uint32_t choose =
                (round[4] & round[5]) ^ (~round[4] & round[6]);
            const std::uint32_t first =
                round[7] + sumHigh + choose + constants[index] + words[index];
            const std::uint32_t sumLow = rotateRight(round[0], 2) ^
                                         rotateRight(round[0], 13) ^
                                         rotateRight(round[0], 22);
            const std::uint32_t majority = (round[0] & round[1]) ^
                                           (round[0] & round[2]) ^
                                           (round[1] & round[2]);
            const std::uint32_t second = sumLow + majority;
            round[7] = round[6];
            round[6] = round[5];
            round[5] = round[4];
            round[4] = round[3] + first;
            round[3] = round[2];
            round[2] = round[1];
            round[1] = round[0];
            round[0] = first + second;
        }
        for (int index = 0; index < 8; ++index) state_[index] += round[index];
    }

    std::uint32_t state_[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                               0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    std::uint8_t block_[64] = {};
    std::size_t used_ = 0;
    std::uint64_t total_ = 0;
};

inline std::uint8_t multiply(std::uint8_t left, std::uint8_t right) {
    std::uint8_t result = 0;
    while (right) {
        if (right & 1) result ^= left;
        left = static_cast<std::uint8_t>((left << 1) ^ ((left & 0x80) ? 0x1b : 0));
        right >>= 1;
    }
    return result;
}

// Derives the AES S-box so the host helper carries no second copy of the table.
struct SBox {
    std::uint8_t forward[256] = {};
    std::uint8_t inverse[256] = {};
    SBox() {
        std::uint8_t power = 1, log = 1;
        do {
            power = static_cast<std::uint8_t>(power ^ (power << 1) ^
                                              ((power & 0x80) ? 0x1b : 0));
            log = static_cast<std::uint8_t>(log ^ (log << 1));
            log = static_cast<std::uint8_t>(log ^ (log << 2));
            log = static_cast<std::uint8_t>(log ^ (log << 4));
            if (log & 0x80) log ^= 0x09;
            const auto rotate = [](std::uint8_t value, int bits) {
                return static_cast<std::uint8_t>((value << bits) |
                                                 (value >> (8 - bits)));
            };
            const std::uint8_t value = static_cast<std::uint8_t>(
                log ^ rotate(log, 1) ^ rotate(log, 2) ^ rotate(log, 3) ^
                rotate(log, 4) ^ 0x63);
            forward[power] = value;
        } while (power != 1);
        forward[0] = 0x63;
        for (int index = 0; index < 256; ++index) {
            inverse[forward[index]] = static_cast<std::uint8_t>(index);
        }
    }
};

inline const SBox& sbox() {
    static const SBox table;
    return table;
}

inline void aes256DecryptBlock(const std::uint8_t* roundKeys,
                               const std::uint8_t* input, std::uint8_t* output) {
    const SBox& box = sbox();
    std::uint8_t state[16];
    std::memcpy(state, input, sizeof(state));
    const auto addRoundKey = [&](int round) {
        for (int index = 0; index < 16; ++index) {
            state[index] ^= roundKeys[round * 16 + index];
        }
    };
    const auto inverseShiftAndSub = [&]() {
        std::uint8_t shifted[16];
        for (int column = 0; column < 4; ++column) {
            for (int row = 0; row < 4; ++row) {
                shifted[(column + row) % 4 * 4 + row] =
                    box.inverse[state[column * 4 + row]];
            }
        }
        std::memcpy(state, shifted, sizeof(state));
    };
    addRoundKey(14);
    for (int round = 13; round >= 1; --round) {
        inverseShiftAndSub();
        addRoundKey(round);
        for (int column = 0; column < 4; ++column) {
            const std::uint8_t* word = state + column * 4;
            const std::uint8_t mixed[4] = {
                static_cast<std::uint8_t>(multiply(word[0], 14) ^ multiply(word[1], 11) ^
                                          multiply(word[2], 13) ^ multiply(word[3], 9)),
                static_cast<std::uint8_t>(multiply(word[0], 9) ^ multiply(word[1], 14) ^
                                          multiply(word[2], 11) ^ multiply(word[3], 13)),
                static_cast<std::uint8_t>(multiply(word[0], 13) ^ multiply(word[1], 9) ^
                                          multiply(word[2], 14) ^ multiply(word[3], 11)),
                static_cast<std::uint8_t>(multiply(word[0], 11) ^ multiply(word[1], 13) ^
                                          multiply(word[2], 9) ^ multiply(word[3], 14))};
            std::memcpy(state + column * 4, mixed, sizeof(mixed));
        }
    }
    inverseShiftAndSub();
    addRoundKey(0);
    std::memcpy(output, state, sizeof(state));
}

inline void aes256ExpandKey(const std::uint8_t* key, std::uint8_t* roundKeys) {
    const SBox& box = sbox();
    std::memcpy(roundKeys, key, 32);
    std::uint8_t rcon = 1;
    for (int index = 32; index < 240; index += 4) {
        std::uint8_t word[4];
        std::memcpy(word, roundKeys + index - 4, sizeof(word));
        if (index % 32 == 0) {
            const std::uint8_t first = word[0];
            word[0] = static_cast<std::uint8_t>(box.forward[word[1]] ^ rcon);
            word[1] = box.forward[word[2]];
            word[2] = box.forward[word[3]];
            word[3] = box.forward[first];
            rcon = multiply(rcon, 2);
        } else if (index % 32 == 16) {
            for (auto& byte : word) byte = box.forward[byte];
        }
        for (int part = 0; part < 4; ++part) {
            roundKeys[index + part] =
                static_cast<std::uint8_t>(roundKeys[index - 32 + part] ^ word[part]);
        }
    }
}

}  // namespace host_crypto

inline bool hmacSha256(const std::uint8_t* key, std::size_t keyLength,
                       const std::uint8_t* message, std::size_t messageLength,
                       std::uint8_t* output) {
    std::uint8_t block[64] = {};
    if (keyLength > sizeof(block)) {
        host_crypto::Sha256 keyHash;
        keyHash.update(key, keyLength);
        keyHash.finish(block);
    } else if (keyLength > 0) {
        std::memcpy(block, key, keyLength);
    }
    std::uint8_t inner[64], outer[64];
    for (std::size_t index = 0; index < sizeof(block); ++index) {
        inner[index] = block[index] ^ 0x36;
        outer[index] = block[index] ^ 0x5c;
    }
    std::uint8_t digest[HMAC_SHA256_BYTES];
    host_crypto::Sha256 innerHash;
    innerHash.update(inner, sizeof(inner));
    innerHash.update(message, messageLength);
    innerHash.finish(digest);
    host_crypto::Sha256 outerHash;
    outerHash.update(outer, sizeof(outer));
    outerHash.update(digest, sizeof(digest));
    outerHash.finish(output);
    return true;
}

// Input length must already be a non-zero multiple of the AES block size.
inline bool aes256CbcDecrypt(const std::uint8_t* key, const std::uint8_t* iv,
                             const std::uint8_t* input, std::size_t length,
                             std::uint8_t* output) {
    std::uint8_t roundKeys[240];
    host_crypto::aes256ExpandKey(key, roundKeys);
    std::uint8_t chain[COMMAND_AES_BLOCK];
    std::memcpy(chain, iv, sizeof(chain));
    for (std::size_t offset = 0; offset < length; offset += COMMAND_AES_BLOCK) {
        std::uint8_t block[COMMAND_AES_BLOCK];
        host_crypto::aes256DecryptBlock(roundKeys, input + offset, block);
        for (std::size_t index = 0; index < sizeof(block); ++index) {
            block[index] ^= chain[index];
        }
        std::memcpy(chain, input + offset, sizeof(chain));
        std::memcpy(output + offset, block, sizeof(block));
    }
    return true;
}

#endif

}  // namespace lockbox::security
