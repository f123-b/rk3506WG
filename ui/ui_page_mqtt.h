/**
 * @file    ui_page_mqtt.h
 * @brief   MQTT 传感器数据页面 — 温湿度卡片 + 实时曲线图 + 功能按钮
 */

#ifndef UI_PAGE_MQTT_H
#define UI_PAGE_MQTT_H

#include <lvgl.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

lv_obj_t * ui_page_mqtt_create(lv_obj_t *parent);

void ui_page_mqtt_update_temp(float temp);
void ui_page_mqtt_update_humi(float humi);
void ui_page_mqtt_add_chart_point(float temp, float humi, bool valid);
void ui_page_mqtt_set_status(bool connected, int retry_count);
void ui_page_mqtt_update_clock(void);
void ui_page_mqtt_update_ip(void);
bool ui_page_mqtt_check_alarm(float temp, float humi);

/* 按钮获取函数 (外部注册事件) */
lv_obj_t * ui_page_mqtt_get_btn_sub(void);
lv_obj_t * ui_page_mqtt_get_btn_pub(void);
lv_obj_t * ui_page_mqtt_get_btn_refresh(void);
lv_obj_t * ui_page_mqtt_get_btn_clear(void);

#ifdef __cplusplus
}
#endif

#endif
