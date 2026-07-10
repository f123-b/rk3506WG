/**
 * @file    config_file.c
 * @brief   JSON 配置文件读写实现
 */

#include "config_file.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define CONFIG_MAX_SIZE  8192   /**< 配置文件最大大小 */
#define CONFIG_MAX_VALUE 256    /**< 单个值最大长度 */

struct config_file_t {
    char *raw;  /**< 原始 JSON 文本 */
};

config_file_t *config_load(const char *path)
{
    FILE *fp = fopen(path, "r");
    if (!fp) return NULL;

    config_file_t *cfg = calloc(1, sizeof(config_file_t));
    if (!cfg) {
        fclose(fp);
        return NULL;
    }

    cfg->raw = calloc(1, CONFIG_MAX_SIZE);
    if (!cfg->raw) {
        free(cfg);
        fclose(fp);
        return NULL;
    }

    size_t n = fread(cfg->raw, 1, CONFIG_MAX_SIZE - 1, fp);
    cfg->raw[n] = '\0';
    fclose(fp);
    return cfg;
}

void config_free(config_file_t *cfg)
{
    if (cfg) {
        free(cfg->raw);
        free(cfg);
    }
}

/**
 * @brief 在 JSON 文本中查找 key 对应的字符串值
 * @return 指向 value 字符串的静态缓冲区, 或 NULL
 */
static const char *find_json_value(config_file_t *cfg, const char *key,
                                   char *buf, size_t buf_size)
{
    if (!cfg || !cfg->raw || !key) return NULL;

    /* 构造搜索模式: "key" */
    char pattern[128];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);

    char *pos = strstr(cfg->raw, pattern);
    if (!pos) return NULL;

    /* 跳过 key 和冒号 */
    pos = strchr(pos + strlen(pattern), ':');
    if (!pos) return NULL;
    pos++;

    /* 跳过空白 */
    while (*pos && isspace((unsigned char)*pos)) pos++;

    /* 判断类型: 字符串 "...", 数字, true/false */
    if (*pos == '"') {
        /* 字符串值 */
        pos++;
        const char *end = strchr(pos, '"');
        if (!end) return NULL;
        size_t len = (size_t)(end - pos);
        if (len >= buf_size) len = buf_size - 1;
        memcpy(buf, pos, len);
        buf[len] = '\0';
        return buf;
    } else if (*pos == 't' || *pos == 'f') {
        /* 布尔值 */
        size_t len = (*pos == 't') ? 4 : 5;
        if (len >= buf_size) len = buf_size - 1;
        memcpy(buf, pos, len);
        buf[len] = '\0';
        return buf;
    } else {
        /* 数字值 */
        const char *end = pos;
        while (*end && (isdigit((unsigned char)*end) || *end == '.' ||
                        *end == '-' || *end == '+')) end++;
        size_t len = (size_t)(end - pos);
        if (len >= buf_size) len = buf_size - 1;
        memcpy(buf, pos, len);
        buf[len] = '\0';
        return buf;
    }
}

const char *config_get_str(config_file_t *cfg, const char *key,
                           const char *default_val)
{
    static char buf[CONFIG_MAX_VALUE];  /* 线程不安全, 嵌入式够用 */
    const char *val = find_json_value(cfg, key, buf, sizeof(buf));
    return val ? buf : default_val;
}

int config_get_int(config_file_t *cfg, const char *key, int default_val)
{
    static char buf[CONFIG_MAX_VALUE];
    const char *val = find_json_value(cfg, key, buf, sizeof(buf));
    return val ? atoi(val) : default_val;
}

float config_get_float(config_file_t *cfg, const char *key, float default_val)
{
    static char buf[CONFIG_MAX_VALUE];
    const char *val = find_json_value(cfg, key, buf, sizeof(buf));
    return val ? atof(val) : default_val;
}

bool config_get_bool(config_file_t *cfg, const char *key, bool default_val)
{
    static char buf[CONFIG_MAX_VALUE];
    const char *val = find_json_value(cfg, key, buf, sizeof(buf));
    if (!val) return default_val;
    return (strcmp(val, "true") == 0);
}
