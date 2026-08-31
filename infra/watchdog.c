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
#include <stdatomic.h>

static int wd_fd = -1;
static atomic_bool wd_running = false;
static pthread_t wd_thread;
static bool wd_thread_valid = false;
static pthread_mutex_t wd_mutex = PTHREAD_MUTEX_INITIALIZER;

/** 后台喂狗线程 */
static void *watchdog_thread_func(void *arg)
{
    (void)arg;
    int timeout = (int)(intptr_t)arg;

    LOG_INFO("Watchdog started (timeout=%ds, feed every %ds)",
             timeout, timeout / 2);

    while (atomic_load(&wd_running)) {
        /* 每 timeout/2 秒喂狗一次 */
        for (int i = 0; i < timeout / 2 && atomic_load(&wd_running); i++) {
            sleep(1);
        }
        if (!atomic_load(&wd_running)) break;

        pthread_mutex_lock(&wd_mutex);
        int fd = wd_fd;
        ssize_t written = fd >= 0 ? write(fd, "x", 1) : -1;
        pthread_mutex_unlock(&wd_mutex);
        if (written < 1) {
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

    atomic_store(&wd_running, true);
    if (pthread_create(&wd_thread, NULL, watchdog_thread_func,
                       (void *)(intptr_t)timeout_sec) != 0) {
        LOG_ERROR("Watchdog thread creation failed");
        atomic_store(&wd_running, false);
        close(wd_fd);
        wd_fd = -1;
        return -1;
    }
    pthread_mutex_lock(&wd_mutex);
    wd_thread_valid = true;
    pthread_mutex_unlock(&wd_mutex);

    return 0;
}

void watchdog_feed(void)
{
    pthread_mutex_lock(&wd_mutex);
    if (wd_fd >= 0) {
        if (write(wd_fd, "x", 1) < 1) {
            LOG_ERROR("Watchdog manual feed failed");
        }
    }
    pthread_mutex_unlock(&wd_mutex);
}

void watchdog_stop(void)
{
    atomic_store(&wd_running, false);
    pthread_mutex_lock(&wd_mutex);
    bool thread_valid = wd_thread_valid;
    pthread_t thread = wd_thread;
    pthread_mutex_unlock(&wd_mutex);

    if (thread_valid) pthread_join(thread, NULL);

    pthread_mutex_lock(&wd_mutex);
    if (wd_fd >= 0) {
        /* 写入魔术字符 'V' 告知内核正常关闭看门狗 */
        write(wd_fd, "V", 1);
        close(wd_fd);
        wd_fd = -1;
    }
    wd_thread_valid = false;
    pthread_mutex_unlock(&wd_mutex);
    LOG_INFO("Watchdog stopped");
}
