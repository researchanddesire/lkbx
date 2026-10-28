#pragma once
#include <cassert>
#include <openssl/evp.h>
struct mbedtls_sha256_context { EVP_MD_CTX* context; };
inline void mbedtls_sha256_init(mbedtls_sha256_context* c) { c->context = EVP_MD_CTX_new(); assert(c->context); }
inline int mbedtls_sha256_starts_ret(mbedtls_sha256_context* c, int) { return EVP_DigestInit_ex(c->context, EVP_sha256(), nullptr) == 1 ? 0 : -1; }
inline int mbedtls_sha256_update_ret(mbedtls_sha256_context* c, const unsigned char* data, size_t size) { return EVP_DigestUpdate(c->context, data, size) == 1 ? 0 : -1; }
inline int mbedtls_sha256_finish_ret(mbedtls_sha256_context* c, unsigned char* digest) { return EVP_DigestFinal_ex(c->context, digest, nullptr) == 1 ? 0 : -1; }
inline void mbedtls_sha256_free(mbedtls_sha256_context* c) { EVP_MD_CTX_free(c->context); }
