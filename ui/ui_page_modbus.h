/**
 * @file    ui_page_modbus.h
 * @brief   Modbus/RS485 页面 — 从站设备 + 寄存器柱状图 + 活动指示 + 功能按钮
 */

#ifndef UI_PAGE_MODBUS_H
#define UI_PAGE_MODBUS_H

#include <lvgl.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

lv_obj_t * ui_page_modbus_create(lv_obj_t *parent);

/** 更新从站信息 */
void ui_page_modbus_update_slave(int slave_id, const char *name,
                                  int nb_regs, const uint16_t *regs, bool valid);

/** 更新 TX 发送计数和状态 */
void ui_page_modbus_update_tx(int tx_cnt, bool tx_ok);

/** 更新 RX 接收计数和状态 */
void ui_page_modbus_update_rx(int rx_cnt, bool rx_ok);

/** 设置 RS485 状态 LED */
void ui_page_modbus_set_led(bool active);

/** 设置轮询状态显示 */
void ui_page_modbus_set_poll_status(bool polling, int interval_ms);

/* 可选测试按钮获取函数 */
lv_obj_t * ui_page_modbus_get_btn_send(void);

#ifdef __cplusplus
}
#endif

#endif
