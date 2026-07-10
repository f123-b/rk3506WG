/**
 * @file    database.h
 * @brief   环境数据持久化 — SQLite3
 *
 * 功能:
 *   - 按分钟粒度存储温湿度记录
 *   - 查询历史数据（按小时范围）
 *   - 数据清理（保留最近30天）
 *   - 统计查询（日均值、最高/最低）
 */

#ifndef DATABASE_H
#define DATABASE_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 单条传感器记录
 */
typedef struct {
    time_t timestamp;   /**< Unix时间戳 */
    float  temperature; /**< 温度 (℃) */
    float  humidity;    /**< 湿度 (%) */
    bool   valid;       /**< 数据有效性: true=真实数据, false=传感器异常 */
} sensor_record_t;

/**
 * @brief 统计信息
 */
typedef struct {
    float temp_avg, temp_min, temp_max;
    float humi_avg, humi_min, humi_max;
    int   record_count;
} sensor_stats_t;

/**
 * @brief 初始化数据库（创建表）
 * @return 0成功，非0失败
 */
int database_init(void);

/**
 * @brief 插入一条记录
 * @param temp  温度值
 * @param humi  湿度值
 * @param valid 数据有效性
 * @return 0成功，非0失败
 */
int database_insert(float temp, float humi, bool valid);

/**
 * @brief 查询过去 N 小时的历史记录
 * @param hours     查询范围（小时）
 * @param records   输出数组
 * @param max_count 最大记录数
 * @return 实际返回的记录数
 */
int database_query_history(int hours, sensor_record_t *records, int max_count);

/**
 * @brief 查询统计信息
 * @param hours 统计范围（小时）
 * @param stats 输出统计结果
 * @return 0成功，非0失败
 */
int database_get_stats(int hours, sensor_stats_t *stats);

/**
 * @brief 清理过期数据（保留最近 N 天）
 * @param keep_days 保留天数
 * @return 0成功，非0失败
 */
int database_cleanup(int keep_days);

/**
 * @brief 插入一条通用设备数据记录
 * @param source    数据来源 (如 "modbus", "can")
 * @param device    设备名称
 * @param point_name 数据点名称
 * @param value     数值
 * @param unit      单位
 * @param valid     有效性
 * @return 0成功，非0失败
 */
int database_insert_device_data(const char *source, const char *device,
                                 const char *point_name, double value,
                                 const char *unit, bool valid);

/**
 * @brief 关闭数据库
 */
void database_close(void);

#ifdef __cplusplus
}
#endif

#endif /* DATABASE_H */
