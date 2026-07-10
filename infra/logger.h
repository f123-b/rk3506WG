/**
 * @file    logger.h
 * @brief   分级日志系统 — 支持控制台 + 文件输出
 *
 * 4个日志级别 (从严重到详细):
 *   LOG_ERROR  — 严重错误, 程序可能无法继续
 *   LOG_WARN   — 警告, 非预期但可继续运行
 *   LOG_INFO   — 正常运行信息 (例如 "MQTT已连接")
 *   LOG_DEBUG  — 调试详细信息 (例如 "收到数据: T=25.3")
 *
 * 使用方法:
 *   #include "infra/logger.h"
 *   LOG_INFO("MQTT connected to %s:%d", broker, port);
 *   LOG_ERROR("Failed to open %s: %s", path, strerror(errno));
 *
 * 原理:
 *   底层使用 vfprintf() 同时输出到 stdout 和文件。
 *   每个日志行带时间戳和级别标签:
 *   [2026-07-03 14:30:25] [INFO] MQTT connected to 192.168.5.10:1883
 */

#ifndef LOGGER_H
#define LOGGER_H

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化日志系统
 * @param log_file  日志文件路径, NULL 表示不写文件只输出到控制台
 */
void logger_init(const char *log_file);

/**
 * @brief 关闭日志系统 (关闭文件句柄)
 */
void logger_close(void);

/**
 * @brief 获取当前日志文件路径
 */
const char *logger_get_path(void);

/* ==================== 日志宏 ==================== */

/** 严重错误: 程序可能无法继续运行 */
#define LOG_ERROR(fmt, ...) \
    logger_write('E', __FILE__, __LINE__, fmt, ##__VA_ARGS__)

/** 警告: 非预期情况但程序可继续 */
#define LOG_WARN(fmt, ...) \
    logger_write('W', __FILE__, __LINE__, fmt, ##__VA_ARGS__)

/** 信息: 正常运行事件 */
#define LOG_INFO(fmt, ...) \
    logger_write('I', __FILE__, __LINE__, fmt, ##__VA_ARGS__)

/** 调试: 详细的诊断信息 */
#define LOG_DEBUG(fmt, ...) \
    logger_write('D', __FILE__, __LINE__, fmt, ##__VA_ARGS__)

/**
 * @brief 底层日志写入函数 (通常不直接调用, 用宏)
 * @param level   'E'/'W'/'I'/'D'
 * @param file    源文件名
 * @param line    行号
 * @param fmt     printf 格式串
 */
void logger_write(char level, const char *file, int line,
                  const char *fmt, ...) __attribute__((format(printf, 4, 5)));

#ifdef __cplusplus
}
#endif

#endif /* LOGGER_H */
