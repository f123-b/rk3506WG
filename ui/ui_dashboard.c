/**
 * @file    ui_dashboard.c
 * @brief   仪表盘 UI 实现 (从 main.c 提取)
 *
 * 布局 (480×800 竖屏):
 *   ┌──────────────────────┐ y=0
 *   │ IP  │  MQTT  │ TIME  │ STATUS_H (55px)
 *   ├──────────┬───────────┤
 *   │  温度卡   │  湿度卡    │ CARD_H (130px)
 *   │  25.5 C  │  65 %     │
 *   ├──────────────────────┤
 *   │                      │
 *   │   📈 实时曲线图        │ CHART_H
 *   │   (温湿度折线图)       │
 *   │                      │
 *   ├──────────────────────┤
 *   │ [1] [2]  [3]  [4]    │ BTN_BAR_H (100px)
 *   │ 开关 OTA  设置 返回   │
 *   └──────────────────────┘ y=800
 */

#include "ui_dashboard.h"
#include "../app_config.h"
#include "../infra/logger.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>
#include <ifaddrs.h>
#include <arpa/inet.h>

/* ==================== 内部状态 ==================== */
static lv_obj_t *scr;
static lv_obj_t *label_ip;
static lv_obj_t *label_mqtt;
static lv_obj_t *label_clock;
static lv_obj_t *label_temp;
static lv_obj_t *label_humi;
static lv_obj_t *label_timestamp;
static lv_obj_t *chart;
static lv_chart_series_t *ser_temp;
static lv_chart_series_t *ser_humi;
static lv_obj_t *btn[4];
static lv_obj_t *mqtt_dot;
static lv_obj_t *can_led;
static lv_obj_t *rs485_led;
static lv_obj_t *label_ota_status;

/* CAN 面板元素 */
static lv_obj_t *label_can_info;
static lv_obj_t *label_can_tx;
static lv_obj_t *can_bar_fill;

/* RS485 面板元素 */
static lv_obj_t *label_rs485_info;
static lv_obj_t *label_rs485_tx;
static lv_obj_t *rs485_bar_fill;

static lv_style_t style_bg;
static lv_style_t style_card;
static lv_style_t style_card_temp;
static lv_style_t style_card_humi;
static lv_style_t style_btn[4];
static lv_style_t style_status_bar;
static lv_style_t style_chart;
static lv_style_t style_title;
static lv_style_t style_value;
static lv_style_t style_label;
static lv_style_t style_bus_panel;
static lv_style_t style_led_on;
static lv_style_t style_led_off;
static lv_style_t style_bar_bg;

/* 图表数据缓冲区 */
static float temp_buf[MAX_CHART_PTS] = {0};
static float humi_buf[MAX_CHART_PTS] = {0};

/* ==================== 样式初始化 ==================== */
static void init_styles(void)
{
    /* 屏幕背景 */
    lv_style_init(&style_bg);
    lv_style_set_bg_color(&style_bg, lv_color_hex(0x0a0e17));
    lv_style_set_bg_opa(&style_bg, LV_OPA_COVER);

    /* 状态栏 */
    lv_style_init(&style_status_bar);
    lv_style_set_bg_color(&style_status_bar, lv_color_hex(0x111827));
    lv_style_set_bg_grad_color(&style_status_bar, lv_color_hex(0x0f172a));
    lv_style_set_bg_grad_dir(&style_status_bar, LV_GRAD_DIR_VER);
    lv_style_set_border_color(&style_status_bar, lv_color_hex(0x1e293b));
    lv_style_set_border_width(&style_status_bar, 1);
    lv_style_set_border_side(&style_status_bar, LV_BORDER_SIDE_BOTTOM);
    lv_style_set_pad_hor(&style_status_bar, 16);
    lv_style_set_pad_ver(&style_status_bar, 0);

    /* 卡片样式 */
    lv_style_init(&style_card);
    lv_style_set_bg_color(&style_card, lv_color_hex(0x1e293b));
    lv_style_set_bg_grad_color(&style_card, lv_color_hex(0x0f172a));
    lv_style_set_bg_grad_dir(&style_card, LV_GRAD_DIR_VER);
    lv_style_set_border_color(&style_card, lv_color_hex(0x334155));
    lv_style_set_border_width(&style_card, 1);
    lv_style_set_radius(&style_card, 16);
    lv_style_set_pad_all(&style_card, 12);

    /* 温度卡片顶部强调色 */
    lv_style_init(&style_card_temp);
    lv_style_set_border_color(&style_card_temp, lv_color_hex(0xf59e0b));
    lv_style_set_border_width(&style_card_temp, 2);
    lv_style_set_border_side(&style_card_temp, LV_BORDER_SIDE_TOP);

    /* 湿度卡片顶部强调色 */
    lv_style_init(&style_card_humi);
    lv_style_set_border_color(&style_card_humi, lv_color_hex(0x06b6d4));
    lv_style_set_border_width(&style_card_humi, 2);
    lv_style_set_border_side(&style_card_humi, LV_BORDER_SIDE_TOP);

    /* 图表区域 */
    lv_style_init(&style_chart);
    lv_style_set_bg_color(&style_chart, lv_color_hex(0x0b1120));
    lv_style_set_border_color(&style_chart, lv_color_hex(0x334155));
    lv_style_set_border_width(&style_chart, 1);
    lv_style_set_radius(&style_chart, 12);
    lv_style_set_pad_all(&style_chart, 10);

    /* 标题文字 */
    lv_style_init(&style_title);
    lv_style_set_text_color(&style_title, lv_color_hex(0xcbd5e1));
    lv_style_set_text_font(&style_title, &lv_font_montserrat_14);

    /* 大数值文字 */
    lv_style_init(&style_value);
    lv_style_set_text_color(&style_value, lv_color_hex(0xffffff));
    lv_style_set_text_font(&style_value, &lv_font_montserrat_32);

    /* 小标签文字 */
    lv_style_init(&style_label);
    lv_style_set_text_color(&style_label, lv_color_hex(0x64748b));
    lv_style_set_text_font(&style_label, &lv_font_montserrat_12);

    /* 按钮样式 */
    lv_color_t btn_colors[4] = {
        lv_color_hex(0x3b82f6),
        lv_color_hex(0x10b981),
        lv_color_hex(0xf59e0b),
        lv_color_hex(0xef4444),
    };
    for (int i = 0; i < 4; i++) {
        lv_style_init(&style_btn[i]);
        lv_style_set_bg_color(&style_btn[i], btn_colors[i]);
        lv_style_set_radius(&style_btn[i], 14);
        lv_style_set_border_width(&style_btn[i], 0);
        lv_style_set_shadow_color(&style_btn[i], btn_colors[i]);
        lv_style_set_shadow_width(&style_btn[i], 15);
        lv_style_set_shadow_opa(&style_btn[i], LV_OPA_30);
        lv_style_set_pad_all(&style_btn[i], 4);
    }

    /* 总线面板 */
    lv_style_init(&style_bus_panel);
    lv_style_set_bg_color(&style_bus_panel, lv_color_hex(0x111827));
    lv_style_set_border_color(&style_bus_panel, lv_color_hex(0x334155));
    lv_style_set_border_width(&style_bus_panel, 1);
    lv_style_set_radius(&style_bus_panel, 10);
    lv_style_set_pad_all(&style_bus_panel, 8);

    /* LED 灯 */
    lv_style_init(&style_led_on);
    lv_style_set_bg_color(&style_led_on, lv_color_hex(0x22c55e));
    lv_style_set_radius(&style_led_on, LV_RADIUS_CIRCLE);
    lv_style_set_border_width(&style_led_on, 0);

    lv_style_init(&style_led_off);
    lv_style_set_bg_color(&style_led_off, lv_color_hex(0xef4444));
    lv_style_set_radius(&style_led_off, LV_RADIUS_CIRCLE);
    lv_style_set_border_width(&style_led_off, 0);

    /* 进度条背景 */
    lv_style_init(&style_bar_bg);
    lv_style_set_bg_color(&style_bar_bg, lv_color_hex(0x1e293b));
    lv_style_set_radius(&style_bar_bg, 3);
    lv_style_set_border_width(&style_bar_bg, 0);
}

/* ==================== 状态栏 ==================== */
static void create_status_bar(void)
{
    lv_obj_t *bar = lv_obj_create(scr);
    lv_obj_set_size(bar, SCREEN_WIDTH, STATUS_H);
    lv_obj_set_pos(bar, 0, 0);
    lv_obj_add_style(bar, &style_status_bar, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    /* IP 地址 */
    lv_obj_t *ip_cont = lv_obj_create(bar);
    lv_obj_set_size(ip_cont, 180, STATUS_H);
    lv_obj_set_pos(ip_cont, 0, 0);
    lv_obj_clear_flag(ip_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(ip_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ip_cont, 0, 0);

    lv_obj_t *ip_title = lv_label_create(ip_cont);
    lv_label_set_text(ip_title, "IP");
    lv_obj_set_style_text_color(ip_title, lv_color_hex(0x64748b), 0);
    lv_obj_set_style_text_font(ip_title, &lv_font_montserrat_12, 0);
    lv_obj_align(ip_title, LV_ALIGN_LEFT_MID, 0, -6);

    label_ip = lv_label_create(ip_cont);
    lv_label_set_text(label_ip, "---.---.---.---");
    lv_obj_set_style_text_color(label_ip, lv_color_hex(0x94a3b8), 0);
    lv_obj_set_style_text_font(label_ip, &lv_font_montserrat_14, 0);
    lv_obj_align(label_ip, LV_ALIGN_LEFT_MID, 0, 8);

    /* MQTT + CAN + RS485 状态指示灯 */
    lv_obj_t *led_cont = lv_obj_create(bar);
    lv_obj_set_size(led_cont, 140, STATUS_H);
    lv_obj_set_pos(led_cont, 170, 0);
    lv_obj_clear_flag(led_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(led_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(led_cont, 0, 0);

    /* CAN LED + 标签 */
    can_led = lv_obj_create(led_cont);
    lv_obj_set_size(can_led, 7, 7);
    lv_obj_set_pos(can_led, 0, 14);
    lv_obj_add_style(can_led, &style_led_off, 0);

    lv_obj_t *can_lbl = lv_label_create(led_cont);
    lv_label_set_text(can_lbl, "CAN");
    lv_obj_set_style_text_color(can_lbl, lv_color_hex(0x64748b), 0);
    lv_obj_set_style_text_font(can_lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_pos(can_lbl, 12, 10);

    /* RS485 LED + 标签 */
    rs485_led = lv_obj_create(led_cont);
    lv_obj_set_size(rs485_led, 7, 7);
    lv_obj_set_pos(rs485_led, 52, 14);
    lv_obj_add_style(rs485_led, &style_led_off, 0);

    lv_obj_t *rs485_lbl = lv_label_create(led_cont);
    lv_label_set_text(rs485_lbl, "485");
    lv_obj_set_style_text_color(rs485_lbl, lv_color_hex(0x64748b), 0);
    lv_obj_set_style_text_font(rs485_lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_pos(rs485_lbl, 64, 10);

    /* MQTT LED + 标签 */
    mqtt_dot = lv_obj_create(led_cont);
    lv_obj_set_size(mqtt_dot, 7, 7);
    lv_obj_set_pos(mqtt_dot, 96, 14);
    lv_obj_add_style(mqtt_dot, &style_led_off, 0);

    label_mqtt = lv_label_create(led_cont);
    lv_label_set_text(label_mqtt, "MQTT");
    lv_obj_set_style_text_color(label_mqtt, lv_color_hex(0x64748b), 0);
    lv_obj_set_style_text_font(label_mqtt, &lv_font_montserrat_12, 0);
    lv_obj_set_pos(label_mqtt, 108, 10);

    /* 时钟 */
    lv_obj_t *clock_cont = lv_obj_create(bar);
    lv_obj_set_size(clock_cont, 120, STATUS_H);
    lv_obj_set_pos(clock_cont, 360, 0);
    lv_obj_clear_flag(clock_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(clock_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(clock_cont, 0, 0);

    lv_obj_t *clock_title = lv_label_create(clock_cont);
    lv_label_set_text(clock_title, "TIME");
    lv_obj_set_style_text_color(clock_title, lv_color_hex(0x64748b), 0);
    lv_obj_set_style_text_font(clock_title, &lv_font_montserrat_12, 0);
    lv_obj_align(clock_title, LV_ALIGN_RIGHT_MID, 0, -6);

    label_clock = lv_label_create(clock_cont);
    lv_label_set_text(label_clock, "00:00:00");
    lv_obj_set_style_text_color(label_clock, lv_color_hex(0x94a3b8), 0);
    lv_obj_set_style_text_font(label_clock, &lv_font_montserrat_14, 0);
    lv_obj_align(label_clock, LV_ALIGN_RIGHT_MID, 0, 8);
}

/* ==================== 传感器卡片 ==================== */
static void create_sensor_cards(void)
{
    /* 温度卡片 */
    lv_obj_t *card_temp = lv_obj_create(scr);
    lv_obj_set_size(card_temp, CARD_W, CARD_H);
    lv_obj_set_pos(card_temp, CARD_MARGIN, STATUS_H + CARD_MARGIN);
    lv_obj_add_style(card_temp, &style_card, 0);
    lv_obj_add_style(card_temp, &style_card_temp, 0);
    lv_obj_clear_flag(card_temp, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *icon_temp = lv_label_create(card_temp);
    lv_label_set_text(icon_temp, LV_SYMBOL_CHARGE " TEMP");
    lv_obj_set_style_text_color(icon_temp, lv_color_hex(0xf59e0b), 0);
    lv_obj_set_style_text_font(icon_temp, &lv_font_montserrat_14, 0);
    lv_obj_align(icon_temp, LV_ALIGN_TOP_MID, 0, 8);

    label_temp = lv_label_create(card_temp);
    lv_label_set_text(label_temp, "--.-");
    lv_obj_add_style(label_temp, &style_value, 0);
    lv_obj_set_style_text_color(label_temp, lv_color_hex(0xf59e0b), 0);
    lv_obj_align(label_temp, LV_ALIGN_CENTER, -10, 5);

    lv_obj_t *unit_temp = lv_label_create(card_temp);
    lv_label_set_text(unit_temp, "C");
    lv_obj_set_style_text_color(unit_temp, lv_color_hex(0xf59e0b), 0);
    lv_obj_set_style_text_font(unit_temp, &lv_font_montserrat_20, 0);
    lv_obj_align(unit_temp, LV_ALIGN_CENTER, 38, 5);

    lv_obj_t *sub_temp = lv_label_create(card_temp);
    lv_label_set_text(sub_temp, "TEMPERATURE");
    lv_obj_add_style(sub_temp, &style_label, 0);
    lv_obj_align(sub_temp, LV_ALIGN_BOTTOM_MID, 0, -4);

    /* 湿度卡片 */
    lv_obj_t *card_humi = lv_obj_create(scr);
    lv_obj_set_size(card_humi, CARD_W, CARD_H);
    lv_obj_set_pos(card_humi, CARD_MARGIN * 2 + CARD_W, STATUS_H + CARD_MARGIN);
    lv_obj_add_style(card_humi, &style_card, 0);
    lv_obj_add_style(card_humi, &style_card_humi, 0);
    lv_obj_clear_flag(card_humi, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *icon_humi = lv_label_create(card_humi);
    lv_label_set_text(icon_humi, LV_SYMBOL_OK " HUMI");
    lv_obj_set_style_text_color(icon_humi, lv_color_hex(0x06b6d4), 0);
    lv_obj_set_style_text_font(icon_humi, &lv_font_montserrat_14, 0);
    lv_obj_align(icon_humi, LV_ALIGN_TOP_MID, 0, 8);

    label_humi = lv_label_create(card_humi);
    lv_label_set_text(label_humi, "--");
    lv_obj_add_style(label_humi, &style_value, 0);
    lv_obj_set_style_text_color(label_humi, lv_color_hex(0x06b6d4), 0);
    lv_obj_align(label_humi, LV_ALIGN_CENTER, -10, 5);

    lv_obj_t *unit_humi = lv_label_create(card_humi);
    lv_label_set_text(unit_humi, "%");
    lv_obj_set_style_text_color(unit_humi, lv_color_hex(0x06b6d4), 0);
    lv_obj_set_style_text_font(unit_humi, &lv_font_montserrat_20, 0);
    lv_obj_align(unit_humi, LV_ALIGN_CENTER, 30, 5);

    lv_obj_t *sub_humi = lv_label_create(card_humi);
    lv_label_set_text(sub_humi, "HUMIDITY");
    lv_obj_add_style(sub_humi, &style_label, 0);
    lv_obj_align(sub_humi, LV_ALIGN_BOTTOM_MID, 0, -4);
}

/* ==================== CAN/RS485 数据面板 ==================== */
static void create_bus_panel(void)
{
    lv_obj_t *panel = lv_obj_create(scr);
    lv_obj_set_size(panel, SCREEN_WIDTH - CARD_MARGIN * 2, BUS_BAR_H);
    lv_obj_set_pos(panel, CARD_MARGIN, BUS_BAR_Y);
    lv_obj_add_style(panel, &style_bus_panel, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    /* --- CAN 行 --- */
    /* CAN 标签 */
    lv_obj_t *can_title = lv_label_create(panel);
    lv_label_set_text(can_title, "CAN");
    lv_obj_set_style_text_color(can_title, lv_color_hex(0xf59e0b), 0);
    lv_obj_set_style_text_font(can_title, &lv_font_montserrat_14, 0);
    lv_obj_align(can_title, LV_ALIGN_TOP_LEFT, 2, 2);

    /* CAN 信息 (ID + 计数) */
    label_can_info = lv_label_create(panel);
    lv_label_set_text(label_can_info, "ID:0x123  TX:0  RX:0");
    lv_obj_set_style_text_color(label_can_info, lv_color_hex(0x94a3b8), 0);
    lv_obj_set_style_text_font(label_can_info, &lv_font_montserrat_12, 0);
    lv_obj_align(label_can_info, LV_ALIGN_TOP_LEFT, 50, 3);

    /* CAN TX 计数 */
    label_can_tx = lv_label_create(panel);
    lv_label_set_text(label_can_tx, "TX:0");
    lv_obj_set_style_text_color(label_can_tx, lv_color_hex(0x64748b), 0);
    lv_obj_set_style_text_font(label_can_tx, &lv_font_montserrat_12, 0);
    lv_obj_align(label_can_tx, LV_ALIGN_TOP_RIGHT, -2, 2);

    /* CAN 进度条背景 */
    lv_obj_t *can_bar_bg = lv_obj_create(panel);
    lv_obj_set_size(can_bar_bg, SCREEN_WIDTH - CARD_MARGIN * 2 - 20, 4);
    lv_obj_align(can_bar_bg, LV_ALIGN_TOP_LEFT, 2, 22);
    lv_obj_add_style(can_bar_bg, &style_bar_bg, 0);
    lv_obj_clear_flag(can_bar_bg, LV_OBJ_FLAG_SCROLLABLE);

    /* CAN 进度条填充 (绿色=成功, 红色=失败) */
    can_bar_fill = lv_obj_create(can_bar_bg);
    lv_obj_set_size(can_bar_fill, 0, 4);
    lv_obj_set_pos(can_bar_fill, 0, 0);
    lv_obj_set_style_bg_color(can_bar_fill, lv_color_hex(0x22c55e), 0);
    lv_obj_set_style_radius(can_bar_fill, 2, 0);
    lv_obj_set_style_border_width(can_bar_fill, 0, 0);
    lv_obj_clear_flag(can_bar_fill, LV_OBJ_FLAG_SCROLLABLE);

    /* --- RS485 行 --- */
    lv_obj_t *rs485_title = lv_label_create(panel);
    lv_label_set_text(rs485_title, "RS485");
    lv_obj_set_style_text_color(rs485_title, lv_color_hex(0x10b981), 0);
    lv_obj_set_style_text_font(rs485_title, &lv_font_montserrat_14, 0);
    lv_obj_align(rs485_title, LV_ALIGN_TOP_LEFT, 2, 36);

    /* RS485 信息 */
    label_rs485_info = lv_label_create(panel);
    lv_label_set_text(label_rs485_info, "UART3  9600bps  TX:0");
    lv_obj_set_style_text_color(label_rs485_info, lv_color_hex(0x94a3b8), 0);
    lv_obj_set_style_text_font(label_rs485_info, &lv_font_montserrat_12, 0);
    lv_obj_align(label_rs485_info, LV_ALIGN_TOP_LEFT, 50, 37);

    /* RS485 TX 计数 */
    label_rs485_tx = lv_label_create(panel);
    lv_label_set_text(label_rs485_tx, "TX:0");
    lv_obj_set_style_text_color(label_rs485_tx, lv_color_hex(0x64748b), 0);
    lv_obj_set_style_text_font(label_rs485_tx, &lv_font_montserrat_12, 0);
    lv_obj_align(label_rs485_tx, LV_ALIGN_TOP_RIGHT, -2, 36);

    /* RS485 进度条背景 */
    lv_obj_t *rs485_bar_bg = lv_obj_create(panel);
    lv_obj_set_size(rs485_bar_bg, SCREEN_WIDTH - CARD_MARGIN * 2 - 20, 4);
    lv_obj_align(rs485_bar_bg, LV_ALIGN_TOP_LEFT, 2, 56);
    lv_obj_add_style(rs485_bar_bg, &style_bar_bg, 0);
    lv_obj_clear_flag(rs485_bar_bg, LV_OBJ_FLAG_SCROLLABLE);

    /* RS485 进度条填充 */
    rs485_bar_fill = lv_obj_create(rs485_bar_bg);
    lv_obj_set_size(rs485_bar_fill, 0, 4);
    lv_obj_set_pos(rs485_bar_fill, 0, 0);
    lv_obj_set_style_bg_color(rs485_bar_fill, lv_color_hex(0x22c55e), 0);
    lv_obj_set_style_radius(rs485_bar_fill, 2, 0);
    lv_obj_set_style_border_width(rs485_bar_fill, 0, 0);
    lv_obj_clear_flag(rs485_bar_fill, LV_OBJ_FLAG_SCROLLABLE);
}

/* ==================== 实时曲线图 ==================== */
static void create_chart(void)
{
    lv_obj_t *chart_cont = lv_obj_create(scr);
    lv_obj_set_size(chart_cont, SCREEN_WIDTH - CARD_MARGIN * 2, CHART_H);
    lv_obj_set_pos(chart_cont, CARD_MARGIN, CHART_Y);
    lv_obj_add_style(chart_cont, &style_chart, 0);
    lv_obj_clear_flag(chart_cont, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(chart_cont);
    lv_label_set_text(title, LV_SYMBOL_CHARGE " Real-time Data");
    lv_obj_add_style(title, &style_title, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 4, 4);

    /* 图例 */
    lv_obj_t *legend_temp = lv_obj_create(chart_cont);
    lv_obj_set_size(legend_temp, 8, 8);
    lv_obj_set_pos(legend_temp, 200, 8);
    lv_obj_set_style_bg_color(legend_temp, lv_color_hex(0xf59e0b), 0);
    lv_obj_set_style_radius(legend_temp, 2, 0);
    lv_obj_set_style_border_width(legend_temp, 0, 0);

    lv_obj_t *leg_t = lv_label_create(chart_cont);
    lv_label_set_text(leg_t, "Temp");
    lv_obj_set_style_text_color(leg_t, lv_color_hex(0x94a3b8), 0);
    lv_obj_set_style_text_font(leg_t, &lv_font_montserrat_12, 0);
    lv_obj_set_pos(leg_t, 212, 6);

    lv_obj_t *legend_humi = lv_obj_create(chart_cont);
    lv_obj_set_size(legend_humi, 8, 8);
    lv_obj_set_pos(legend_humi, 260, 8);
    lv_obj_set_style_bg_color(legend_humi, lv_color_hex(0x06b6d4), 0);
    lv_obj_set_style_radius(legend_humi, 2, 0);
    lv_obj_set_style_border_width(legend_humi, 0, 0);

    lv_obj_t *leg_h = lv_label_create(chart_cont);
    lv_label_set_text(leg_h, "Humi");
    lv_obj_set_style_text_color(leg_h, lv_color_hex(0x94a3b8), 0);
    lv_obj_set_style_text_font(leg_h, &lv_font_montserrat_12, 0);
    lv_obj_set_pos(leg_h, 272, 6);

    /* 图表 */
    chart = lv_chart_create(chart_cont);
    lv_obj_set_size(chart, SCREEN_WIDTH - CARD_MARGIN * 2 - 16, CHART_H - 50);
    lv_obj_align(chart, LV_ALIGN_BOTTOM_MID, 0, -4);
    lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
    lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
    lv_chart_set_range(chart, LV_CHART_AXIS_SECONDARY_Y, 0, 40);
    lv_chart_set_point_count(chart, MAX_CHART_PTS);
    lv_obj_set_style_bg_color(chart, lv_color_hex(0x0b1120), 0);
    lv_obj_set_style_border_color(chart, lv_color_hex(0x334155), 0);
    lv_obj_set_style_border_width(chart, 1, 0);
    lv_obj_set_style_line_color(chart, lv_color_hex(0x334155), 0);
    lv_obj_set_style_line_width(chart, 1, 0);
    lv_obj_set_style_pad_all(chart, 4, 0);
    lv_obj_set_style_line_color(chart, lv_color_hex(0x334155), LV_PART_ITEMS);
    lv_chart_set_div_line_count(chart, 5, 6);
    lv_obj_set_style_line_dash_width(chart, 1, 0);
    lv_obj_set_style_line_dash_gap(chart, 3, 0);

    /* 湿度曲线 (主轴: 0-100%) */
    ser_humi = lv_chart_add_series(chart, lv_color_hex(0x06b6d4),
                                    LV_CHART_AXIS_PRIMARY_Y);
    for (int i = 0; i < MAX_CHART_PTS; i++) {
        ser_humi->y_points[i] = LV_CHART_POINT_NONE;
    }

    /* 温度曲线 (副轴: 0-40℃) */
    ser_temp = lv_chart_add_series(chart, lv_color_hex(0xf59e0b),
                                    LV_CHART_AXIS_SECONDARY_Y);
    for (int i = 0; i < MAX_CHART_PTS; i++) {
        ser_temp->y_points[i] = LV_CHART_POINT_NONE;
    }

    /* 时间戳标签 */
    label_timestamp = lv_label_create(chart_cont);
    lv_label_set_text(label_timestamp, "----.--.-- --:--:--");
    lv_obj_set_style_text_color(label_timestamp, lv_color_hex(0x475569), 0);
    lv_obj_set_style_text_font(label_timestamp, &lv_font_montserrat_12, 0);
    lv_obj_align(label_timestamp, LV_ALIGN_BOTTOM_MID, 0, -2);
}

/* ==================== 按钮栏 ==================== */
static void create_buttons(void)
{
    const char *btn_labels[4] = {"CAN", "RS485", "OTA", "返回"};
    const char *btn_subs[4] = {"发送测试", "发送测试", "在线升级", "预留"};

    /* OTA 状态提示标签 (按钮栏上方) */
    label_ota_status = lv_label_create(scr);
    lv_label_set_text(label_ota_status, "");
    lv_obj_set_style_text_color(label_ota_status, lv_color_hex(0x94a3b8), 0);
    lv_obj_set_style_text_font(label_ota_status, &lv_font_montserrat_12, 0);
    lv_obj_align(label_ota_status, LV_ALIGN_BOTTOM_MID, 0, -(BTN_BAR_H + 4));

    int btn_w = (SCREEN_WIDTH - 10 * 5) / 4;
    int btn_h = 64;
    int start_y = SCREEN_HEIGHT - BTN_BAR_H + 18;

    for (int i = 0; i < 4; i++) {
        btn[i] = lv_btn_create(scr);
        lv_obj_set_size(btn[i], btn_w, btn_h);
        lv_obj_set_pos(btn[i], 10 + i * (btn_w + 10), start_y);
        lv_obj_add_style(btn[i], &style_btn[i], 0);
        lv_obj_clear_flag(btn[i], LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *lbl = lv_label_create(btn[i]);
        lv_label_set_text(lbl, btn_labels[i]);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xffffff), 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, 0);
        lv_obj_align(lbl, LV_ALIGN_CENTER, 0, -6);

        lv_obj_t *sub = lv_label_create(btn[i]);
        lv_label_set_text(sub, btn_subs[i]);
        lv_obj_set_style_text_color(sub, lv_color_hex(0xffffff), 0);
        lv_obj_set_style_text_font(sub, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_opa(sub, LV_OPA_70, 0);
        lv_obj_align(sub, LV_ALIGN_CENTER, 0, 12);
    }
}

/* ==================== 公开 API ==================== */

void ui_dashboard_create(void)
{
    scr = lv_scr_act();
    lv_obj_add_style(scr, &style_bg, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    init_styles();
    create_status_bar();
    create_sensor_cards();
    create_bus_panel();
    create_chart();
    create_buttons();

    /* 初始化图表数据 */
    for (int i = 0; i < MAX_CHART_PTS; i++) {
        temp_buf[i] = 0.0f;
        humi_buf[i] = 0.0f;
        ser_temp->y_points[i] = LV_CHART_POINT_NONE;
        ser_humi->y_points[i] = LV_CHART_POINT_NONE;
    }
    lv_chart_refresh(chart);

    LOG_INFO("Dashboard UI created");
}

void ui_dashboard_update_temp(float temp)
{
    if (!label_temp) return;

    char buf[16];
    if (isnan(temp) || temp < -50 || temp > 100) {
        lv_label_set_text(label_temp, "--.-");
    } else {
        snprintf(buf, sizeof(buf), "%.1f", temp);
        lv_label_set_text(label_temp, buf);
    }
}

void ui_dashboard_update_humi(float humi)
{
    if (!label_humi) return;

    char buf[16];
    if (isnan(humi) || humi < 0 || humi > 100) {
        lv_label_set_text(label_humi, "--");
    } else {
        snprintf(buf, sizeof(buf), "%.0f", humi);
        lv_label_set_text(label_humi, buf);
    }
}

void ui_dashboard_update_clock(void)
{
    if (!label_clock || !label_timestamp) return;

    time_t now = time(NULL);
    struct tm tm_buf;
    localtime_r(&now, &tm_buf);

    char time_str[16], date_str[32];
    strftime(time_str, sizeof(time_str), "%H:%M:%S", &tm_buf);
    strftime(date_str, sizeof(date_str), "%Y-%m-%d %H:%M:%S", &tm_buf);

    lv_label_set_text(label_clock, time_str);
    lv_label_set_text(label_timestamp, date_str);
}

void ui_dashboard_update_ip(void)
{
    if (!label_ip) return;

    struct ifaddrs *ifaddr, *ifa;
    char ip_str[32] = "No IP";

    if (getifaddrs(&ifaddr) == 0) {
        for (ifa = ifaddr; ifa; ifa = ifa->ifa_next) {
            if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET) continue;
            if (strcmp(ifa->ifa_name, "lo") == 0) continue;
            struct sockaddr_in *sin = (struct sockaddr_in *)ifa->ifa_addr;
            snprintf(ip_str, sizeof(ip_str), "%s", inet_ntoa(sin->sin_addr));
            break;
        }
        freeifaddrs(ifaddr);
    }
    lv_label_set_text(label_ip, ip_str);
}

void ui_dashboard_set_mqtt_status(bool connected, int retry_count)
{
    if (!label_mqtt || !mqtt_dot) return;

    if (connected) {
        lv_label_set_text(label_mqtt, "已连接");
        lv_obj_set_style_bg_color(mqtt_dot, lv_color_hex(0x22c55e), 0);
    } else if (retry_count > 0) {
        lv_label_set_text(label_mqtt, "重连中...");
        lv_obj_set_style_bg_color(mqtt_dot, lv_color_hex(0xef4444), 0);
    } else {
        lv_label_set_text(label_mqtt, "未连接");
        lv_obj_set_style_bg_color(mqtt_dot, lv_color_hex(0xef4444), 0);
    }
}

void ui_dashboard_set_can_led(bool active)
{
    if (!can_led) return;
    if (active) {
        lv_obj_add_style(can_led, &style_led_on, 0);
    } else {
        lv_obj_add_style(can_led, &style_led_off, 0);
    }
}

void ui_dashboard_set_rs485_led(bool active)
{
    if (!rs485_led) return;
    if (active) {
        lv_obj_add_style(rs485_led, &style_led_on, 0);
    } else {
        lv_obj_add_style(rs485_led, &style_led_off, 0);
    }
}

void ui_dashboard_update_can(uint32_t can_id, int tx_cnt, int rx_cnt, bool tx_ok)
{
    if (!label_can_info) return;

    char buf[64];
    snprintf(buf, sizeof(buf), "ID:0x%03X  TX:%d  RX:%d",
             can_id & 0x7FF, tx_cnt, rx_cnt);
    lv_label_set_text(label_can_info, buf);

    /* TX 计数标签 */
    if (label_can_tx) {
        snprintf(buf, sizeof(buf), "TX:%d", tx_cnt);
        lv_label_set_text(label_can_tx, buf);
    }

    /* 进度条: 成功绿色, 失败红色 */
    if (can_bar_fill) {
        int bar_w = (tx_cnt % 20) * ((SCREEN_WIDTH - CARD_MARGIN * 2 - 24) / 20);
        lv_obj_set_size(can_bar_fill, bar_w, 4);
        lv_obj_set_style_bg_color(can_bar_fill,
            tx_ok ? lv_color_hex(0x22c55e) : lv_color_hex(0xef4444), 0);
    }
}

void ui_dashboard_update_rs485(int tx_cnt, bool tx_ok)
{
    if (!label_rs485_info) return;

    char buf[64];
    snprintf(buf, sizeof(buf), "UART3  9600bps  TX:%d", tx_cnt);
    lv_label_set_text(label_rs485_info, buf);

    /* TX 计数标签 */
    if (label_rs485_tx) {
        snprintf(buf, sizeof(buf), "TX:%d", tx_cnt);
        lv_label_set_text(label_rs485_tx, buf);
    }

    /* 进度条 */
    if (rs485_bar_fill) {
        int bar_w = (tx_cnt % 20) * ((SCREEN_WIDTH - CARD_MARGIN * 2 - 24) / 20);
        lv_obj_set_size(rs485_bar_fill, bar_w, 4);
        lv_obj_set_style_bg_color(rs485_bar_fill,
            tx_ok ? lv_color_hex(0x22c55e) : lv_color_hex(0xef4444), 0);
    }
}

void ui_dashboard_add_chart_point(float temp, float humi, bool valid)
{
    if (!chart || !ser_temp || !ser_humi) return;

    /* 左移数据 */
    for (int i = 0; i < MAX_CHART_PTS - 1; i++) {
        temp_buf[i] = temp_buf[i + 1];
        humi_buf[i] = humi_buf[i + 1];
    }

    if (valid) {
        float t = temp, h = humi;
        if (t < 0) t = 0;
        if (t > 40) t = 40;
        if (h < 0) h = 0;
        if (h > 100) h = 100;
        temp_buf[MAX_CHART_PTS - 1] = t;
        humi_buf[MAX_CHART_PTS - 1] = h;
    } else {
        temp_buf[MAX_CHART_PTS - 1] = 0;
        humi_buf[MAX_CHART_PTS - 1] = 0;
    }

    /* 更新图表 */
    for (int i = 0; i < MAX_CHART_PTS; i++) {
        ser_temp->y_points[i] = (lv_coord_t)(temp_buf[i] + 0.5f);
        ser_humi->y_points[i] = (lv_coord_t)(humi_buf[i] + 0.5f);
    }
    lv_chart_refresh(chart);
}

lv_obj_t *ui_dashboard_get_button(int index)
{
    if (index < 0 || index >= 4) return NULL;
    return btn[index];
}

lv_obj_t *ui_dashboard_get_mqtt_dot(void)
{
    return mqtt_dot;
}

lv_obj_t *ui_dashboard_get_ota_label(void)
{
    return label_ota_status;
}

bool ui_dashboard_check_alarm(float temp, float humi)
{
    return (temp > ALARM_TEMP_HIGH) || (humi > ALARM_HUMI_HIGH);
}

void ui_dashboard_show_alarm(const char *msg)
{
    if (!msg) return;

    /* 简易告警: 使用 OTA 状态标签显示红色告警文字 */
    if (label_ota_status) {
        lv_label_set_text(label_ota_status, msg);
        lv_obj_set_style_text_color(label_ota_status, lv_color_hex(0xef4444), 0);
    }

    LOG_WARN("ALARM: %s", msg);
}
