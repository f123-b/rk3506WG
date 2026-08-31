/**
 * @file    logger.c
 * @brief   分级日志系统实现
 */

#include "logger.h"
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>

#define LOGGER_MAX_SIZE (1024 * 1024)

static FILE *log_fp = NULL;
static char  log_path[256] = {0};
static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

static void rotate_log_locked(void)
{
    if (!log_fp || !log_path[0]) return;
    long pos = ftell(log_fp);
    if (pos < 0 || pos < LOGGER_MAX_SIZE) return;

    fclose(log_fp);
    log_fp = NULL;

    char backup_path[sizeof(log_path) + 3];
    snprintf(backup_path, sizeof(backup_path), "%s.1", log_path);
    unlink(backup_path);
    if (rename(log_path, backup_path) != 0) {
        fprintf(stderr, "LOGGER: cannot rotate %s\n", log_path);
    }
    log_fp = fopen(log_path, "a");
}

void logger_init(const char *log_file)
{
    pthread_mutex_lock(&log_mutex);

    /* 关闭旧文件 */
    if (log_fp) {
        fclose(log_fp);
        log_fp = NULL;
    }

    if (log_file && log_file[0]) {
        strncpy(log_path, log_file, sizeof(log_path) - 1);
        log_path[sizeof(log_path) - 1] = '\0';
        log_fp = fopen(log_file, "a");  /* 追加模式 */
        if (!log_fp) {
            fprintf(stderr, "LOGGER: cannot open log file %s\n", log_file);
        }
    }

    pthread_mutex_unlock(&log_mutex);
}

void logger_close(void)
{
    pthread_mutex_lock(&log_mutex);
    if (log_fp) {
        fclose(log_fp);
        log_fp = NULL;
    }
    log_path[0] = '\0';
    pthread_mutex_unlock(&log_mutex);
}

const char *logger_get_path(void)
{
    static _Thread_local char path_copy[sizeof(log_path)];
    pthread_mutex_lock(&log_mutex);
    strncpy(path_copy, log_path, sizeof(path_copy) - 1);
    path_copy[sizeof(path_copy) - 1] = '\0';
    pthread_mutex_unlock(&log_mutex);
    return path_copy[0] ? path_copy : NULL;
}

void logger_write(char level, const char *file, int line,
                  const char *fmt, ...)
{
    /* 获取时间戳 */
    time_t now = time(NULL);
    struct tm tm_buf;
#ifdef _WIN32
    localtime_s(&tm_buf, &now);
#else
    localtime_r(&now, &tm_buf);
#endif
    char time_str[32];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", &tm_buf);

    /* 级别标签 */
    const char *level_str;
    switch (level) {
        case 'E': level_str = "ERROR"; break;
        case 'W': level_str = "WARN "; break;
        case 'I': level_str = "INFO "; break;
        case 'D': level_str = "DEBUG"; break;
        default:  level_str = "?????"; break;
    }

    /* 提取文件名 (去掉路径) */
    const char *fname = strrchr(file, '/');
    fname = fname ? fname + 1 : file;

    pthread_mutex_lock(&log_mutex);

    rotate_log_locked();

    /* 写入控制台 */
    fprintf(stderr, "[%s] [%s] %s:%d: ", time_str, level_str, fname, line);
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, "\n");

    /* 写入文件 */
    if (log_fp) {
        fprintf(log_fp, "[%s] [%s] %s:%d: ", time_str, level_str, fname, line);
        va_start(args, fmt);
        vfprintf(log_fp, fmt, args);
        va_end(args);
        fprintf(log_fp, "\n");
        fflush(log_fp);  /* 立即刷盘, 确保崩溃时不丢日志 */
    }

    pthread_mutex_unlock(&log_mutex);
}
