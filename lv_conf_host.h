/**
 * @file    lv_conf.h
 * @brief   LVGL v9.1 configuration for host SDL simulation
 *
 * Minimal config: enable only what we need for the simulation.
 * Copy from lv_conf_template.h, keep only essential settings.
 */

#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_CONF_SUPPRESS_DEFINE_CHECK 1

/*====================
   COLOR SETTINGS
 *====================*/
#define LV_COLOR_DEPTH 32

/*====================
   DISPLAY SETTINGS
 *====================*/
#define LV_USE_SDL          1
#define LV_USE_DRAW_SDL     0 /* No SDL_image available */

/*====================
   WIDGETS
 *====================*/
#define LV_USE_TABVIEW  0
#define LV_USE_BTN      1
#define LV_USE_LABEL    1
#define LV_USE_CHART    1
#define LV_USE_LED      1
#define LV_USE_OBJ      1
#define LV_USE_ARC      0
#define LV_USE_ANIMIMG  0
#define LV_USE_CALENDAR 0
#define LV_USE_CANVAS   0
#define LV_USE_CHECKBOX 0
#define LV_USE_DROPDOWN 0
#define LV_USE_IMAGE    0
#define LV_USE_IMAGEBUTTON 0
#define LV_USE_KEYBOARD 0
#define LV_USE_LINE     0
#define LV_USE_LIST     0
#define LV_USE_LOTTIE   0
#define LV_USE_MENU     0
#define LV_USE_MSGBOX   0
#define LV_USE_ROLLER   0
#define LV_USE_SCALE    0
#define LV_USE_SLIDER   0
#define LV_USE_SPAN     0
#define LV_USE_SPINBOX  0
#define LV_USE_SPINNER  0
#define LV_USE_SWITCH   0
#define LV_USE_TEXTAREA 0
#define LV_USE_TABLE    0
#define LV_USE_TILEVIEW 0
#define LV_USE_WIN      0

/* Flex layout (core feature, needed for object-based layouts) */
#define LV_USE_FLEX     1

/*====================
   FONT
 *====================*/
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_18 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_24 1
#define LV_FONT_MONTSERRAT_32 1

/* CJK 中文字体 (宋体 16px, 覆盖常用汉字) */
#define LV_FONT_SIMSUN_16_CJK 1

/*====================
   OTHER
 *====================*/
#define LV_USE_SYSMON  0
#define LV_USE_PERF_MONITOR 0
#define LV_USE_LOG     0
#define LV_USE_ASSERT_NULL 0
#define LV_USE_ASSERT_MALLOC 0
#define LV_USE_ASSERT_STYLE 0
#define LV_USE_ASSERT_MEM_INTEGRITY 0
#define LV_USE_ASSERT_OBJ 0
#define LV_USE_SNAPSHOT 0

#endif /* LV_CONF_H */
