/**
 * @file    ui_page_can.h
 * @brief   CAN 总线页面 — 收发统计 + 信号仪表 + 帧显示 + 功能按钮
 */

#ifndef UI_PAGE_CAN_H
#define UI_PAGE_CAN_H

#include <lvgl.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

lv_obj_t * ui_page_can_create(lv_obj_t *parent);

/** 更新 CAN 收发统计 */
void ui_page_can_update(uint32_t can_id, int tx_cnt, int rx_cnt, bool tx_ok);

/** 更新信号仪表 (大号数值 + 进度条) */
void ui_page_can_update_signal(const char *name, float value, const char *unit,
                                float min_val, float max_val);

/** 更新 CAN 发送帧信息 */
void ui_page_can_update_tx_frame(uint32_t can_id, uint8_t dlc, const uint8_t *data);

/** 更新 CAN 接收帧信息 */
void ui_page_can_update_rx_frame(uint32_t can_id, uint8_t dlc, const uint8_t *data);

/** 设置 CAN 状态 LED */
void ui_page_can_set_led(bool active);

/** 切换监听状态 (页面内部状态) */
void ui_page_can_toggle_listen(void);
int  ui_page_can_get_listen_state(void);

/* 按钮获取函数 */
lv_obj_t * ui_page_can_get_btn_send(void);
lv_obj_t * ui_page_can_get_btn_listen(void);
lv_obj_t * ui_page_can_get_btn_clear(void);
lv_obj_t * ui_page_can_get_test_btn(void);  /* 兼容旧 API */

#ifdef __cplusplus
}
#endif

#endif
