/**
 * @file    ui_device_card.h
 * @brief   通用设备卡片 UI 组件
 *
 * 用于显示 Modbus/CAN 设备的状态信息。
 * 每个卡片显示: 设备名称 + 数据点值 + 单位 + 有效性指示灯
 *
 * 使用 flex 布局自适应排列, 支持动态创建/更新。
 */

#ifndef UI_DEVICE_CARD_H
#define UI_DEVICE_CARD_H

#include <lvgl.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 创建一个设备状态卡片
 * @param parent      父容器
 * @param device_name 设备名称
 * @param source      来源文字 ("MQTT"/"Modbus"/"CAN")
 * @return 卡片对象
 */
lv_obj_t *ui_device_card_create(lv_obj_t *parent, const char *device_name,
                                 const char *source);

/**
 * @brief 更新设备卡片上的数据
 * @param card       卡片对象
 * @param point_name 数据点名称
 * @param value      数值
 * @param unit       单位
 * @param valid      有效性
 */
void ui_device_card_update(lv_obj_t *card, const char *point_name,
                           double value, const char *unit, bool valid);

#ifdef __cplusplus
}
#endif

#endif /* UI_DEVICE_CARD_H */
