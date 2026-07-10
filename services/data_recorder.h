/**
 * @file    data_recorder.h
 * @brief   数据记录器 — 批量写入 + 离线缓存
 *
 * 原理:
 *   传感器数据每分钟产生1条记录，如果每次都立即写入 SQLite:
 *     - 频繁 I/O 增加 CPU 占用
 *     - NAND Flash 写入寿命有限 (通常 10万次擦除)
 *     - 大量小事务增加数据库碎片
 *
 *   批量写入策略:
 *     - 内存中缓冲最多 60 条记录 (约 60 分钟数据)
 *     - 每 60 秒或缓冲区满时批量 flush 到 SQLite
 *     - 使用事务包裹批量写入 (大幅提速)
 *
 *   离线缓存策略:
 *     - MQTT 断连时，数据仍被记录 ("离线缓存")
 *     - 重连后自动补发到 Web 服务器共享数据区
 *     - 不丢数据，保证时序完整性
 *
 * 如何修改:
 *   - 修改写入间隔: 编辑 app_config.h 中的 DB_WRITE_INTERVAL
 *   - 修改缓冲大小: 修改 DATA_BUF_MAX (本文件)
 */

#ifndef SERVICES_DATA_RECORDER_H
#define SERVICES_DATA_RECORDER_H

#include <stdbool.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 单条数据记录 */
typedef struct {
    time_t timestamp;
    float  temperature;
    float  humidity;
    bool   valid;
} data_record_t;

/**
 * @brief 初始化数据记录器
 * @return 0=成功, -1=失败
 */
int data_recorder_init(void);

/**
 * @brief 记录一条传感器数据 (内部缓冲, 到期自动批量写入)
 *
 * @param temp   温度值
 * @param humi   湿度值
 * @param valid  数据有效性
 */
void data_recorder_record(float temp, float humi, bool valid);

/**
 * @brief 强制将缓冲数据写入数据库 (通常在程序退出前调用)
 */
void data_recorder_flush(void);

/**
 * @brief 获取缓冲区中的记录数 (调试用)
 */
int data_recorder_get_buffered_count(void);

/**
 * @brief 获取批量写入定时器应调用的 tick 函数
 *
 * 应由主循环每秒钟调用一次。
 * 内部判断是否到达写入间隔，自动触发批量写入。
 */
void data_recorder_tick(void);

/**
 * @brief 清理过期数据 (保留最近 N 天)
 * @param keep_days 保留天数
 */
void data_recorder_cleanup(int keep_days);

/**
 * @brief 关闭数据记录器 (flush 剩余数据 + 清理)
 */
void data_recorder_close(void);

#ifdef __cplusplus
}
#endif

#endif /* SERVICES_DATA_RECORDER_H */
