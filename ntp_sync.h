/**
 * @file    ntp_sync.h
 * @brief   轻量级 NTP 时间同步客户端
 *
 * 使用标准 UDP socket + NTP 协议 (RFC 5905) 从互联网 NTP 服务器获取 UTC 时间，
 * 自动校准 Linux 系统时钟 (settimeofday)，确保 LVGL 屏幕显示正确的北京时间。
 *
 * 特性:
 *   - 启动时立即同步一次，之后每 3600 秒 (1 小时) 校准
 *   - 支持多个 NTP 服务器自动切换 (阿里云 → pool.ntp.org → time.google.com)
 *   - 设置环境变量 TZ=CST-8 使 localtime_r() 返回北京时间
 *   - 线程安全: 所有公开 API 均可跨线程调用
 */

#ifndef NTP_SYNC_H
#define NTP_SYNC_H

#include <stdbool.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 启动 NTP 时间同步后台线程
 *
 * 启动后立即尝试同步一次，之后每 3600 秒校准一次。
 * 多个 NTP 服务器自动切换，全部失败则等待下一个周期重试。
 *
 * @return 0=成功, -1=线程创建失败
 */
int ntp_sync_init(void);

/**
 * @brief 停止 NTP 时间同步
 */
void ntp_sync_stop(void);

/**
 * @brief 获取 NTP 同步状态
 * @param synced  输出: 是否已成功同步过 (至少一次)
 * @param last_sync 输出: 上次成功同步的时间戳 (未同步则为0)
 */
void ntp_sync_get_status(bool *synced, time_t *last_sync);

/**
 * @brief 手动触发一次立即同步
 * @return 0=同步成功, -1=失败
 */
int ntp_sync_once(void);

#ifdef __cplusplus
}
#endif

#endif /* NTP_SYNC_H */
