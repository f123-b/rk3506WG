/**
 * @file    signature_verify.c
 * @brief   RSA-PSS + SHA-256 数字签名验证实现
 */

#include "signature_verify.h"

#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/err.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SIG_MAX_BYTES 512  /* 支持最高 RSA-4096 */

static void set_err(char *err, size_t err_size, const char *msg)
{
    if (!err || err_size == 0) return;
    snprintf(err, err_size, "%s", msg ? msg : "unknown error");
}

static int hex_nibble(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static bool hex_decode(const char *hex, uint8_t *out, size_t out_cap,
                       size_t *out_len)
{
    if (!hex || !out || !out_len) return false;

    size_t n = strlen(hex);
    if (n == 0 || (n & 1U) != 0 || n / 2 > out_cap) return false;

    for (size_t i = 0; i < n / 2; ++i) {
        int hi = hex_nibble(hex[i * 2]);
        int lo = hex_nibble(hex[i * 2 + 1]);
        if (hi < 0 || lo < 0) return false;
        out[i] = (uint8_t)((hi << 4) | lo);
    }

    *out_len = n / 2;
    return true;
}

static void set_openssl_err(char *err, size_t err_size, const char *prefix)
{
    unsigned long code = ERR_get_error();
    char detail[160] = {0};

    if (code != 0) {
        ERR_error_string_n(code, detail, sizeof(detail));
    } else {
        snprintf(detail, sizeof(detail), "OpenSSL verification error");
    }

    if (err && err_size > 0) {
        snprintf(err, err_size, "%s: %s", prefix ? prefix : "OpenSSL", detail);
    }
}

bool signature_verify_rsa_pss_sha256(const uint8_t *message,
                                     size_t message_len,
                                     const char *signature_hex,
                                     const char *public_key_path,
                                     char *err,
                                     size_t err_size)
{
    if (!message || message_len == 0) {
        set_err(err, err_size, "manifest is empty");
        return false;
    }
    if (!signature_hex || signature_hex[0] == '\0') {
        set_err(err, err_size, "signature is empty");
        return false;
    }
    if (!public_key_path || public_key_path[0] == '\0') {
        set_err(err, err_size, "public key path is empty");
        return false;
    }

    uint8_t signature[SIG_MAX_BYTES];
    size_t signature_len = 0;
    if (!hex_decode(signature_hex, signature, sizeof(signature), &signature_len)) {
        set_err(err, err_size, "signature must be even-length hex (max RSA-4096)");
        return false;
    }

    FILE *fp = fopen(public_key_path, "r");
    if (!fp) {
        if (err && err_size > 0)
            snprintf(err, err_size, "cannot open public key: %s", public_key_path);
        return false;
    }

    EVP_PKEY *pkey = PEM_read_PUBKEY(fp, NULL, NULL, NULL);
    fclose(fp);
    if (!pkey) {
        set_openssl_err(err, err_size, "PEM_read_PUBKEY");
        return false;
    }

    int key_type = EVP_PKEY_base_id(pkey);
    if (key_type != EVP_PKEY_RSA && key_type != EVP_PKEY_RSA_PSS) {
        EVP_PKEY_free(pkey);
        set_err(err, err_size, "public key is not RSA/RSA-PSS");
        return false;
    }

    EVP_MD_CTX *mdctx = EVP_MD_CTX_new();
    if (!mdctx) {
        EVP_PKEY_free(pkey);
        set_err(err, err_size, "EVP_MD_CTX_new failed");
        return false;
    }

    EVP_PKEY_CTX *pctx = NULL;
    bool ok = false;

    if (EVP_DigestVerifyInit(mdctx, &pctx, EVP_sha256(), NULL, pkey) <= 0) {
        set_openssl_err(err, err_size, "EVP_DigestVerifyInit");
        goto out;
    }

    if (EVP_PKEY_CTX_set_rsa_padding(pctx, RSA_PKCS1_PSS_PADDING) <= 0) {
        set_openssl_err(err, err_size, "set RSA-PSS padding");
        goto out;
    }

    if (EVP_PKEY_CTX_set_rsa_pss_saltlen(pctx, RSA_PSS_SALTLEN_DIGEST) <= 0) {
        set_openssl_err(err, err_size, "set RSA-PSS salt length");
        goto out;
    }

    if (EVP_DigestVerifyUpdate(mdctx, message, message_len) <= 0) {
        set_openssl_err(err, err_size, "EVP_DigestVerifyUpdate");
        goto out;
    }

    int rc = EVP_DigestVerifyFinal(mdctx, signature, signature_len);
    if (rc == 1) {
        ok = true;
        set_err(err, err_size, "");
    } else if (rc == 0) {
        set_err(err, err_size, "signature mismatch");
    } else {
        set_openssl_err(err, err_size, "EVP_DigestVerifyFinal");
    }

out:
    EVP_MD_CTX_free(mdctx);
    EVP_PKEY_free(pkey);
    return ok;
}
