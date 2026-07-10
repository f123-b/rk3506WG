/**
 * @file    watchdog.h
 * @brief   看门狗 (Watchdog) 模块 — 防止程序死机后无人值守
 *
 * 原理:
 *   Linux 内核提供 /dev/watchdog 设备节点。应用程序打开该设备后，
 *   必须在超时时间内写入数据（"喂狗"），否则内核判定系统卡死，
 *   自动重启整个系统。
 *
 *   本模块启动一个后台线程，每 30 秒喂狗一次。
 *   如果主循环卡死（LVGL 渲染线程阻塞），看门狗线程也会停止，
 *   系统将被硬件复位，实现自动恢复。
 *
 * 使用方法:
 *   #include "infra/watchdog.h"
 *   watchdog_init(60);  // 60秒超时, 每30秒喂狗
 */

#ifndef WATCHDOG_H
#define WATCHDOG_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化看门狗
 * @param timeout_sec  看门狗超时时间 (秒), 超时后系统自动重启
 *                     推荐值: 30~120 秒
 * @return 0=成功, -1=失败 (无 /dev/watchdog 或权限不足)
 */
int watchdog_init(int timeout_sec);

/**
 * @brief 停止看门狗 (正常关机时调用)
 *
 * 向 /dev/watchdog 写入魔术字符 'V' 告诉内核正常关闭，
 * 避免引发复位。然后关闭文件描述符。
 */
void watchdog_stop(void);

/**
 * @brief 手动喂狗一次 (通常不需要手动调用，后台线程自动执行)
 */
void watchdog_feed(void);

#ifdef __cplusplus
}
#endif

#endif /* WATCHDOG_H */
