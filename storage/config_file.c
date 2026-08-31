/**
 * @file    config_file.c
 * @brief   JSON 配置文件读写实现
 */

#include "config_file.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cjson/cJSON.h>

#define CONFIG_MAX_SIZE  8192
#define CONFIG_MAX_VALUE 256

struct config_file_t {
    cJSON *root;
};

config_file_t *config_load(const char *path)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) return NULL;

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }
    long size = ftell(fp);
    if (size < 0 || size >= CONFIG_MAX_SIZE) {
        fclose(fp);
        return NULL;
    }
    rewind(fp);

    char *raw = calloc(1, (size_t)size + 1);
    if (!raw) {
        fclose(fp);
        return NULL;
    }
    size_t read_size = fread(raw, 1, (size_t)size, fp);
    fclose(fp);
    if (read_size != (size_t)size) {
        free(raw);
        return NULL;
    }

    cJSON *root = cJSON_Parse(raw);
    free(raw);
    if (!root || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return NULL;
    }

    config_file_t *cfg = calloc(1, sizeof(*cfg));
    if (!cfg) {
        cJSON_Delete(root);
        return NULL;
    }
    cfg->root = root;
    return cfg;
}

void config_free(config_file_t *cfg)
{
    if (!cfg) return;
    cJSON_Delete(cfg->root);
    free(cfg);
}

static cJSON *find_json_value(config_file_t *cfg, const char *key)
{
    if (!cfg || !cfg->root || !key) return NULL;
    return cJSON_GetObjectItemCaseSensitive(cfg->root, key);
}

const char *config_get_str(config_file_t *cfg, const char *key,
                           const char *default_val)
{
    static _Thread_local char buf[CONFIG_MAX_VALUE];
    cJSON *item = find_json_value(cfg, key);
    if (!item || !cJSON_IsString(item) || !item->valuestring) {
        return default_val;
    }
    snprintf(buf, sizeof(buf), "%s", item->valuestring);
    return buf;
}

int config_get_int(config_file_t *cfg, const char *key, int default_val)
{
    cJSON *item = find_json_value(cfg, key);
    if (cJSON_IsNumber(item)) return item->valueint;
    if (cJSON_IsString(item) && item->valuestring) {
        char *end = NULL;
        long value = strtol(item->valuestring, &end, 10);
        if (end != item->valuestring && *end == '\0') return (int)value;
    }
    return default_val;
}

float config_get_float(config_file_t *cfg, const char *key, float default_val)
{
    cJSON *item = find_json_value(cfg, key);
    if (cJSON_IsNumber(item)) return (float)item->valuedouble;
    if (cJSON_IsString(item) && item->valuestring) {
        char *end = NULL;
        float value = strtof(item->valuestring, &end);
        if (end != item->valuestring && *end == '\0') return value;
    }
    return default_val;
}

bool config_get_bool(config_file_t *cfg, const char *key, bool default_val)
{
    cJSON *item = find_json_value(cfg, key);
    if (cJSON_IsBool(item)) return cJSON_IsTrue(item);
    if (cJSON_IsString(item) && item->valuestring) {
        if (strcmp(item->valuestring, "true") == 0) return true;
        if (strcmp(item->valuestring, "false") == 0) return false;
    }
    return default_val;
}
