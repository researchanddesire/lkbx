#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace lockbox::security {

inline int base64UrlValue(char value) {
    if (value >= 'A' && value <= 'Z') return value - 'A';
    if (value >= 'a' && value <= 'z') return value - 'a' + 26;
    if (value >= '0' && value <= '9') return value - '0' + 52;
    if (value == '-') return 62;
    if (value == '_') return 63;
    return -1;
}

inline std::size_t base64UrlDecodedLength(std::size_t length) {
    const std::size_t remainder = length % 4;
    return length / 4 * 3 + (remainder == 0 ? 0 : remainder - 1);
}

// RFC 4648 section 5 without padding. One value has exactly one encoding, so
// other alphabets, impossible lengths and non-zero trailing bits are rejected.
inline bool base64UrlDecode(const char* text, std::size_t length,
                            std::uint8_t* output, std::size_t capacity,
                            std::size_t& written) {
    written = 0;
    if ((!text && length != 0) || length % 4 == 1 ||
        base64UrlDecodedLength(length) > capacity) return false;
    std::uint32_t accumulator = 0;
    int bits = 0;
    for (std::size_t index = 0; index < length; ++index) {
        const int value = base64UrlValue(text[index]);
        if (value < 0) return false;
        accumulator = (accumulator << 6) | static_cast<std::uint32_t>(value);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            output[written++] =
                static_cast<std::uint8_t>((accumulator >> bits) & 0xFF);
        }
    }
    return (accumulator & ((1u << bits) - 1u)) == 0;
}

inline bool isBase64UrlOfLength(const char* text, std::size_t length,
                                std::size_t expectedBytes) {
    if (!text || base64UrlDecodedLength(length) != expectedBytes) return false;
    std::uint32_t accumulator = 0;
    int bits = 0;
    for (std::size_t index = 0; index < length; ++index) {
        const int value = base64UrlValue(text[index]);
        if (value < 0) return false;
        accumulator = (accumulator << 6) | static_cast<std::uint32_t>(value);
        bits = (bits + 6) % 8;
    }
    return length % 4 != 1 && (accumulator & ((1u << bits) - 1u)) == 0;
}

inline std::string base64UrlEncode(const std::uint8_t* data,
                                   std::size_t length) {
    static const char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string text;
    text.reserve((length * 4 + 2) / 3);
    std::uint32_t accumulator = 0;
    int bits = 0;
    for (std::size_t index = 0; index < length; ++index) {
        accumulator = (accumulator << 8) | data[index];
        bits += 8;
        while (bits >= 6) {
            bits -= 6;
            text.push_back(alphabet[(accumulator >> bits) & 0x3F]);
        }
    }
    if (bits > 0) text.push_back(alphabet[(accumulator << (6 - bits)) & 0x3F]);
    return text;
}

}  // namespace lockbox::security
