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
#include <stdint.h>
#include <time.h>
#include <sys/ioctl.h>
#include <linux/watchdog.h>
#include <stdatomic.h>

static int wd_fd = -1;
static atomic_bool atomic_store(&wd_running, false);
static bool wd_thread_created = false;
static pthread_t wd_thread;
static pthread_mutex_t heartbeat_mutex = PTHREAD_MUTEX_INITIALIZER;
static time_t last_heartbeat = 0;

void watchdog_heartbeat(void)
{
    pthread_mutex_lock(&heartbeat_mutex);
    last_heartbeat = time(NULL);
    pthread_mutex_unlock(&heartbeat_mutex);
}

/** 后台线程只在主循环心跳正常时喂狗 */
static void *watchdog_thread_func(void *arg)
{
    int timeout = (int)(intptr_t)arg;
    int feed_interval = timeout / 3;
    if (feed_interval < 1) feed_interval = 1;
    int feed_tick = 0;
    bool stale_reported = false;

    LOG_INFO("Watchdog started (timeout=%ds, feed every ~%ds while main heartbeat is alive)",
             timeout, feed_interval);

    while (wd_running) {
        sleep(1);
        if (!wd_running) break;

        time_t heartbeat;
        pthread_mutex_lock(&heartbeat_mutex);
        heartbeat = last_heartbeat;
        pthread_mutex_unlock(&heartbeat_mutex);

        time_t now = time(NULL);
        bool healthy = heartbeat > 0 && (now - heartbeat) <= timeout / 2;

        if (!healthy) {
            if (!stale_reported) {
                LOG_ERROR("Watchdog: main-loop heartbeat stale; stop feeding watchdog");
                stale_reported = true;
            }
            continue;
        }

        stale_reported = false;
        if (++feed_tick >= feed_interval) {
            feed_tick = 0;
            if (write(wd_fd, "x", 1) < 1) {
                LOG_ERROR("Watchdog feed failed");
            }
        }
    }

    return NULL;
}

int watchdog_init(int timeout_sec)
{
    wd_fd = open("/dev/watchdog", O_RDWR);
    if (wd_fd < 0) {
        LOG_WARN("Watchdog device /dev/watchdog not available: %s",
                 strerror(errno));
        return -1;
    }

    int actual_timeout = timeout_sec;
    if (ioctl(wd_fd, WDIOC_SETTIMEOUT, &actual_timeout) < 0) {
        LOG_WARN("Cannot set watchdog timeout via WDIOC_SETTIMEOUT, using driver default");
        actual_timeout = timeout_sec;
    } else {
        LOG_INFO("Watchdog timeout configured to %ds", actual_timeout);
    }

    watchdog_heartbeat();
    atomic_store(&wd_running, true);
    if (pthread_create(&wd_thread, NULL, watchdog_thread_func,
                       (void *)(intptr_t)actual_timeout) != 0) {
        LOG_ERROR("Watchdog thread creation failed");
        atomic_store(&wd_running, false);
        close(wd_fd);
        wd_fd = -1;
        return -1;
    }
    wd_thread_created = true;
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
    atomic_store(&wd_running, false);

    if (wd_thread_created) {
        pthread_join(wd_thread, NULL);
        wd_thread_created = false;
    }

    if (wd_fd >= 0) {
        /* 写入魔术字符 'V' 告知内核正常关闭看门狗 */
        write(wd_fd, "V", 1);
        close(wd_fd);
        wd_fd = -1;
    }

    LOG_INFO("Watchdog stopped");
}
