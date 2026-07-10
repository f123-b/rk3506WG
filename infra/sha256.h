/**
 * @file    sha256.h
 * @brief   SHA-256 哈希算法 (FIPS 180-4)
 *
 * 零依赖, 适用于嵌入式平台。支持流式计算和文件哈希。
 */

#ifndef SHA256_H
#define SHA256_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SHA256_BLOCK_SIZE  64
#define SHA256_DIGEST_SIZE 32
#define SHA256_HEX_SIZE    65   /**< 64 hex chars + null */

/** SHA-256 上下文 (流式计算) */
typedef struct {
    uint32_t state[8];
    uint64_t count;
    uint8_t  buf[SHA256_BLOCK_SIZE];
} sha256_ctx_t;

/** 文件哈希进度回调 */
typedef void (*sha256_progress_cb)(void *user_ctx, int64_t bytes_read, int64_t total);

/** 初始化 SHA-256 上下文 */
void sha256_init(sha256_ctx_t *ctx);

/** 增量更新哈希 (可多次调用) */
void sha256_update(sha256_ctx_t *ctx, const uint8_t *data, size_t len);

/** 完成哈希计算, 输出 32 字节摘要 */
void sha256_final(sha256_ctx_t *ctx, uint8_t digest[SHA256_DIGEST_SIZE]);

/**
 * @brief 计算文件的 SHA-256 哈希 (无进度回调)
 * @param path     文件路径
 * @param hex_out  输出: 64 字符小写 hex + null (65 字节缓冲区)
 * @return 0=成功, -1=失败
 */
int sha256_file(const char *path, char hex_out[SHA256_HEX_SIZE]);

/**
 * @brief 计算文件的 SHA-256 哈希 (带进度回调, 适合大文件)
 * @param path     文件路径
 * @param hex_out  输出: 64 字符小写 hex + null (65 字节缓冲区)
 * @param cb       进度回调 (可为 NULL)
 * @param user_ctx 透传给回调 (可为 NULL)
 * @return 0=成功, -1=失败
 */
int sha256_file_ex(const char *path, char hex_out[SHA256_HEX_SIZE],
                   sha256_progress_cb cb, void *user_ctx);

#ifdef __cplusplus
}
#endif

#endif /* SHA256_H */
