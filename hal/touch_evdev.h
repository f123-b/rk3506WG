/**
 * @file    touch_evdev.h
 * @brief   触摸屏驱动接口 (硬件抽象层)
 *
 * 原理:
 *   Linux evdev 子系统将触摸屏抽象为 /dev/input/eventX 设备节点。
 *   触摸屏驱动程序将触控坐标以 struct input_event 格式上报:
 *     - EV_ABS + ABS_MT_POSITION_X/Y → 触摸坐标 (绝对坐标)
 *     - EV_KEY + BTN_TOUCH → 按下/抬起事件
 *
 *   本模块自动探测 /dev/input/event0~4，找到第一个可读的设备。
 *   读取模式设为 O_NONBLOCK (非阻塞)，配合 LVGL indev 机制工作。
 *
 * 如何修改:
 *   - 如果触摸不灵敏: 增大探测范围 (event0~9)
 *   - 如果需要坐标变换: 在 hal_touch_read() 中乘除缩放系数
 */

#ifndef HAL_TOUCH_EVDEV_H
#define HAL_TOUCH_EVDEV_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化触摸屏 (自动探测 /dev/input/event0~4)
 * @return 0=成功, -1=未找到触摸设备
 */
int hal_touch_init(void);

/**
 * @brief 读取触摸状态 (被 LVGL indev 回调调用)
 * @param x     输出: X 坐标
 * @param y     输出: Y 坐标
 * @param pressed 输出: 是否按下
 */
void hal_touch_read(int16_t *x, int16_t *y, bool *pressed);

/**
 * @brief 关闭触摸设备
 */
void hal_touch_close(void);

#ifdef __cplusplus
}
#endif

#endif /* HAL_TOUCH_EVDEV_H */
