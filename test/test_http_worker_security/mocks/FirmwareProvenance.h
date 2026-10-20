#pragma once
#include <string>
namespace firmware { namespace provenance {
inline std::string currentToken() { return "public-provenance-fixture"; }
inline std::string currentTokenId() { return "public-token-id"; }
inline std::string currentImageSha256() { return "public-image-hash"; }
} }
