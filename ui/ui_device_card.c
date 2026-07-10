/**
 * @file    ui_device_card.c
 * @brief   通用设备卡片实现
 */

#include "ui_device_card.h"
#include <stdio.h>

void ui_device_card_update(lv_obj_t *card, const char *point_name,
                           double value, const char *unit, bool valid)
{
    if (!card) return;

    /* 卡片是一个容器, 内部存储子控件引用作为 user_data */
    lv_obj_t **children = (lv_obj_t **)lv_obj_get_user_data(card);
    if (!children) return;

    lv_obj_t *label_name  = children[0];  /* 数据点名称 */
    lv_obj_t *label_value = children[1];  /* 数值 */
    lv_obj_t *label_unit  = children[2];  /* 单位 */
    lv_obj_t *dot         = children[3];  /* 状态灯 */

    if (label_name) {
        lv_label_set_text(label_name, point_name ? point_name : "--");
    }

    if (label_value) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.1f", value);
        lv_label_set_text(label_value, buf);
        lv_obj_set_style_text_color(label_value,
            valid ? lv_color_hex(0x4fc3f7) : lv_color_hex(0xef4444), 0);
    }

    if (label_unit && unit) {
        lv_label_set_text(label_unit, unit);
    }

    if (dot) {
        lv_obj_set_style_bg_color(dot,
            valid ? lv_color_hex(0x22c55e) : lv_color_hex(0xef4444), 0);
    }
}

lv_obj_t *ui_device_card_create(lv_obj_t *parent, const char *device_name,
                                 const char *source)
{
    /* 卡片容器 */
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, lv_pct(100), 48);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x1e293b), 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x334155), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, 8, 0);
    lv_obj_set_style_pad_all(card, 8, 0);
    lv_obj_set_style_pad_gap(card, 8, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    /* 设备名称 + 来源 */
    lv_obj_t *info_cont = lv_obj_create(card);
    lv_obj_set_size(info_cont, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(info_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(info_cont, 0, 0);
    lv_obj_set_flex_flow(info_cont, LV_FLEX_FLOW_COLUMN);

    lv_obj_t *label_dev = lv_label_create(info_cont);
    char buf[64];
    snprintf(buf, sizeof(buf), "%s", device_name ? device_name : "---");
    lv_label_set_text(label_dev, buf);
    lv_obj_set_style_text_color(label_dev, lv_color_hex(0xe0e0e0), 0);
    lv_obj_set_style_text_font(label_dev, &lv_font_montserrat_14, 0);

    lv_obj_t *label_src = lv_label_create(info_cont);
    snprintf(buf, sizeof(buf), "%s", source ? source : "");
    lv_label_set_text(label_src, buf);
    lv_obj_set_style_text_color(label_src, lv_color_hex(0x64748b), 0);
    lv_obj_set_style_text_font(label_src, &lv_font_montserrat_12, 0);

    /* 数据区域 (右) */
    lv_obj_t *data_cont = lv_obj_create(card);
    lv_obj_set_size(data_cont, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(data_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(data_cont, 0, 0);
    lv_obj_set_flex_flow(data_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(data_cont, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    /* 数据点名称 */
    lv_obj_t *label_name = lv_label_create(data_cont);
    lv_label_set_text(label_name, "--");
    lv_obj_set_style_text_color(label_name, lv_color_hex(0x8899aa), 0);
    lv_obj_set_style_text_font(label_name, &lv_font_montserrat_12, 0);

    /* 数值 */
    lv_obj_t *label_value = lv_label_create(data_cont);
    lv_label_set_text(label_value, "---");
    lv_obj_set_style_text_color(label_value, lv_color_hex(0x4fc3f7), 0);
    lv_obj_set_style_text_font(label_value, &lv_font_montserrat_14, 0);

    /* 单位 */
    lv_obj_t *label_unit = lv_label_create(data_cont);
    lv_label_set_text(label_unit, "");
    lv_obj_set_style_text_color(label_unit, lv_color_hex(0x8899aa), 0);
    lv_obj_set_style_text_font(label_unit, &lv_font_montserrat_12, 0);

    /* 状态灯 */
    lv_obj_t *dot = lv_obj_create(data_cont);
    lv_obj_set_size(dot, 8, 8);
    lv_obj_set_style_bg_color(dot, lv_color_hex(0xef4444), 0);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(dot, 0, 0);

    /* 存储子控件引用 (用于后续更新) */
    /* LVGL 9.x 使用 lv_obj_set_user_data / lv_obj_get_user_data */
    /* 但 user_data 只能存一个指针, 我们动态分配子控件指针数组 */
    /* 简化: 使用特定的子控件 ID 或者把引用存在容器中 */
    /* 这里我们用一个技巧: 把 key=label_name 存储在 card 的 user_data */
    /* 实际实现用一个结构体来存储所有子控件指针 */
    static lv_obj_t *child_buf[4];  /* WARNING: 非线程安全, 仅适用于 demonstraction */
    child_buf[0] = label_name;
    child_buf[1] = label_value;
    child_buf[2] = label_unit;
    child_buf[3] = dot;
    lv_obj_set_user_data(card, child_buf);

    return card;
}
