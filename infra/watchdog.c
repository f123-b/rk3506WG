/**
 * @file    watchdog.c
 * @brief   看门狗实现 — /dev/watchdog + 后台喂狗线程
 */

#include "watchdog.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>

static int wd_fd = -1;
static bool wd_running = false;
static pthread_t wd_thread;

/** 后台喂狗线程 */
static void *watchdog_thread_func(void *arg)
{
    (void)arg;
    int timeout = (int)(intptr_t)arg;

    LOG_INFO("Watchdog started (timeout=%ds, feed every %ds)",
             timeout, timeout / 2);

    while (wd_running) {
        /* 每 timeout/2 秒喂狗一次 */
        for (int i = 0; i < timeout / 2 && wd_running; i++) {
            sleep(1);
        }
        if (!wd_running) break;

        if (write(wd_fd, "x", 1) < 1) {
            LOG_ERROR("Watchdog feed failed");
        }
    }

    return NULL;
}

int watchdog_init(int timeout_sec)
{
    /* 打开 watchdog 设备 */
    wd_fd = open("/dev/watchdog", O_RDWR);
    if (wd_fd < 0) {
        /* 设备可能不存在 (嵌入式系统常有), 不强制要求 */
        LOG_WARN("Watchdog device /dev/watchdog not available: %s",
                 strerror(errno));
        return -1;
    }

    /* 设置超时时间 — 写入超时值到设备 */
    /* 注意: 有些 watchdog 驱动不支持动态设置超时, 忽略错误 */
    int t = timeout_sec;
    if (write(wd_fd, &t, sizeof(t)) < 0) {
        LOG_WARN("Cannot set watchdog timeout, using default");
    }

    wd_running = true;
    if (pthread_create(&wd_thread, NULL, watchdog_thread_func,
                       (void *)(intptr_t)timeout_sec) != 0) {
        LOG_ERROR("Watchdog thread creation failed");
        wd_running = false;
        close(wd_fd);
        wd_fd = -1;
        return -1;
    }

    return 0;
}

void watchdog_feed(void)
{
    if (wd_fd >= 0) {
        if (write(wd_fd, "x", 1) < 1) {
            LOG_ERROR("Watchdog manual feed failed");
        }
    }
}

void watchdog_stop(void)
{
    wd_running = false;

    if (wd_fd >= 0) {
        /* 写入魔术字符 'V' 告知内核正常关闭看门狗 */
        write(wd_fd, "V", 1);
        close(wd_fd);
        wd_fd = -1;
    }

    pthread_join(wd_thread, NULL);
    LOG_INFO("Watchdog stopped");
}
