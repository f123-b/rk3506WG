/**
 * @file    signature_verify.h
 * @brief   OTA 数字签名验证 — RSA-PSS + SHA-256 (OpenSSL EVP)
 *
 * 设备端只保存 PEM 公钥。发布服务器使用对应私钥对规范化 OTA manifest
 * 进行签名，设备端在信任 version.json 中的版本/哈希/升级类型之前先验签。
 */
#ifndef INFRA_SIGNATURE_VERIFY_H
#define INFRA_SIGNATURE_VERIFY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 使用 PEM RSA 公钥验证 RSA-PSS/SHA-256 签名
 * @param message          被签名的原始消息
 * @param message_len      消息长度
 * @param signature_hex    二进制签名的十六进制字符串
 * @param public_key_path  PEM 公钥路径
 * @param err              可选错误信息缓冲区
 * @param err_size         错误缓冲区大小
 * @return true=签名有效, false=签名无效或配置错误
 */
bool signature_verify_rsa_pss_sha256(const uint8_t *message,
                                     size_t message_len,
                                     const char *signature_hex,
                                     const char *public_key_path,
                                     char *err,
                                     size_t err_size);

#ifdef __cplusplus
}
#endif

#endif /* INFRA_SIGNATURE_VERIFY_H */
