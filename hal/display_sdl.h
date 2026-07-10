/**
 * @file    display_sdl.h
 * @brief   SDL2 显示后端 — 仅用于 host 模拟
 */
#ifndef HAL_DISPLAY_SDL_H
#define HAL_DISPLAY_SDL_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int  hal_display_init(void);
void hal_display_deinit(void);
unsigned char * hal_display_get_fb(void);
int  hal_display_get_width(void);
int  hal_display_get_height(void);
size_t hal_display_get_fb_size(void);
int  hal_display_get_drm_fd(void);

#ifdef __cplusplus
}
#endif

#endif
