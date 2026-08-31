/**
 * @file    config_file.h
 * @brief   JSON 配置文件读写
 *
 * 原理:
 *   使用 cJSON 解析完整 JSON 对象，支持字符串、整数、浮点数和布尔值。
 *   字符串读取缓冲区为线程局部存储，不使用共享静态缓冲区。
 *
 * 使用方法:
 *   config_file_t *cfg = config_load("config.json");
 *   const char *broker = config_get_str(cfg, "mqtt_broker", "192.168.5.10");
 *   int port = config_get_int(cfg, "mqtt_port", 1883);
 *   config_free(cfg);
 */

#ifndef STORAGE_CONFIG_FILE_H
#define STORAGE_CONFIG_FILE_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 配置文件句柄 (不透明指针) */
typedef struct config_file_t config_file_t;

/**
 * @brief 从文件加载 JSON 配置
 * @param path  配置文件路径 (如 "config.json")
 * @return 配置句柄, 失败返回 NULL
 */
config_file_t *config_load(const char *path);

/**
 * @brief 释放配置句柄
 */
void config_free(config_file_t *cfg);

/**
 * @brief 读取字符串配置项
 * @param cfg          配置句柄
 * @param key          键名 (JSON key, 不含引号)
 * @param default_val  默认值 (键不存在时返回)
 * @return 配置值 (指针指向内部内存, 不要 free)
 */
const char *config_get_str(config_file_t *cfg, const char *key,
                           const char *default_val);

/**
 * @brief 读取整数配置项
 */
int config_get_int(config_file_t *cfg, const char *key, int default_val);

/**
 * @brief 读取浮点数配置项
 */
float config_get_float(config_file_t *cfg, const char *key, float default_val);

/**
 * @brief 读取布尔配置项
 */
bool config_get_bool(config_file_t *cfg, const char *key, bool default_val);

#ifdef __cplusplus
}
#endif

#endif /* STORAGE_CONFIG_FILE_H */
