/**
 * @file    display_drm.h
 * @brief   DRM 显示驱动接口 (硬件抽象层)
 *
 * 原理:
 *   DRM (Direct Rendering Manager) 是 Linux 内核的现代显示框架。
 *   应用程序通过 libdrm 用户态库打开 /dev/dri/cardX 设备，
 *   使用 "Dumb Buffer" API 分配连续物理内存作为帧缓冲 (framebuffer)。
 *
 *   LVGL DIRECT 渲染模式下，LVGL 直接绘制到这块 framebuffer 内存，
 *   无需中间缓冲，实现零拷贝 (zero-copy) 显示。
 *
 * 如何修改:
 *   - 修改显示分辨率: 编辑 SCREEN_WIDTH/SCREEN_HEIGHT (app_config.h)
 *   - 修改颜色格式: 编辑 create.drm_mode_create_dumb.bpp (32→16)
 */

#ifndef HAL_DISPLAY_DRM_H
#define HAL_DISPLAY_DRM_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化 DRM 显示
 *
 * 流程: 打开 /dev/dri/cardX → 查找480×800竖屏连接器 →
 *       创建 dumb buffer → mmap 映射到用户空间 → 设置 CRTC 显示
 *
 * @return 0=成功, -1=失败
 */
int hal_display_init(void);

/** @brief 释放 DRM framebuffer 和文件描述符 */
void hal_display_deinit(void);

/**
 * @brief 获取 framebuffer 内存指针 (LVGL DIRECT 渲染目标)
 */
void *hal_display_get_fb(void);

/**
 * @brief 获取 framebuffer 大小 (字节)
 */
size_t hal_display_get_fb_size(void);

/**
 * @brief 获取屏幕宽度 (像素)
 */
uint32_t hal_display_get_width(void);

/**
 * @brief 获取屏幕高度 (像素)
 */
uint32_t hal_display_get_height(void);

/**
 * @brief 获取 DRM 文件描述符 (LVGL flush 回调需要)
 */
int hal_display_get_drm_fd(void);

#ifdef __cplusplus
}
#endif

#endif /* HAL_DISPLAY_DRM_H */
