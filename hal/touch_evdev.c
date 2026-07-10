/**
 * @file    touch_evdev.c
 * @brief   触摸屏驱动实现 (从 main.c 提取)
 */

#include "touch_evdev.h"
#include "../infra/logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <linux/input.h>

static int touch_fd = -1;

int hal_touch_init(void)
{
    const char *devices[] = {
        "/dev/input/event0", "/dev/input/event1",
        "/dev/input/event2", "/dev/input/event3", "/dev/input/event4"
    };

    for (int i = 0; i < 5; i++) {
        touch_fd = open(devices[i], O_RDONLY | O_NONBLOCK);
        if (touch_fd >= 0) {
            LOG_INFO("Touch: %s", devices[i]);
            return 0;
        }
    }

    LOG_WARN("No touch device found (checked event0~4)");
    return -1;
}

void hal_touch_read(int16_t *x, int16_t *y, bool *pressed)
{
    static int16_t last_x = 0, last_y = 0;
    static bool touch_pressed = false;

    if (touch_fd < 0) {
        /* 尝试重新初始化 */
        hal_touch_init();
        if (touch_fd < 0) {
            *pressed = false;
            return;
        }
    }

    /* 读取所有待处理的输入事件 */
    struct input_event ev;
    while (read(touch_fd, &ev, sizeof(struct input_event)) > 0) {
        switch (ev.type) {
            case EV_ABS:
                if (ev.code == ABS_MT_POSITION_X || ev.code == ABS_X)
                    last_x = ev.value;
                else if (ev.code == ABS_MT_POSITION_Y || ev.code == ABS_Y)
                    last_y = ev.value;
                break;
            case EV_KEY:
                if (ev.code == BTN_TOUCH)
                    touch_pressed = ev.value;
                break;
        }
    }

    *x = last_x;
    *y = last_y;
    *pressed = touch_pressed;
}

void hal_touch_close(void)
{
    if (touch_fd >= 0) {
        close(touch_fd);
        touch_fd = -1;
    }
}
