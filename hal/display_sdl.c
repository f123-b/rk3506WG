/**
 * @file    display_sdl.c
 * @brief   SDL2 显示后端 — 创建窗口 + LVGL 渲染
 *
 * 使用 LVGL v9 内置的 lv_sdl_window API。
 * 在 host 编译时替代 display_drm.c。
 */

#include "display_sdl.h"
#include <lvgl.h>
#include <src/drivers/sdl/lv_sdl_window.h>
#include <stdio.h>

static int g_width = 480;
static int g_height = 800;

int hal_display_init(void)
{
    /* lv_sdl_window_create 内部会:
     *   - 创建 SDL 窗口
     *   - 创建 LVGL display 并注册 flush 回调
     *   - 注册 SDL mouse/mousewheel/keyboard indev
     */
    lv_sdl_window_create(g_width, g_height);
    printf("SDL2 display: %dx%d window created\n", g_width, g_height);
    return 0;
}

void hal_display_deinit(void)
{
    /* SDL cleanup handled by LVGL */
}

unsigned char * hal_display_get_fb(void)
{
    /* lv_sdl_window 使用内部纹理, 不需要外部 fb */
    return NULL;
}

int hal_display_get_width(void)  { return g_width; }
int hal_display_get_height(void) { return g_height; }
size_t hal_display_get_fb_size(void) { return (size_t)g_width * g_height * 4; }
int hal_display_get_drm_fd(void) { return -1; }
