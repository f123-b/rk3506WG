/**
 * @file    main.c
 * @brief   环境监测站入口 — 自定义标签栏三页切换架构
 *
 * 不使用 lv_tabview (兼容性问题), 改用:
 *   - 顶部自定义标签栏 (3个按钮, 点击切换)
 *   - 3个独立页面容器 (同一时刻只显示一个, 通过 LV_OBJ_FLAG_HIDDEN 控制)
 *   - 屏幕级滑动手势检测 (左右滑动切换)
 *
 * 页面:
 *   Tab 0: MQTT 传感器 (温湿度卡片 + 曲线图)
 *   Tab 1: Modbus/RS485 (设备状态 + 测试发送)
 *   Tab 2: CAN 总线 (收发面板 + 测试发送)
 */

#include <lvgl.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>  /* tzset */
#include <string.h>
#include <pthread.h>

#include "app_config.h"
#include "infra/logger.h"
#include "infra/watchdog.h"
#ifdef HOST_SIMULATION
#include "hal/display_sdl.h"
#else
#include "hal/display_drm.h"
#include "hal/touch_evdev.h"
#endif
#include "hal/can_socket.h"
#include "hal/rs485_uart.h"
#include "services/mqtt_client.h"
#include "services/data_recorder.h"
#include "services/data_bus.h"
#include "services/modbus_master.h"
#include "services/can_manager.h"
#include "ui/ui_page_mqtt.h"
#include "ui/ui_page_modbus.h"
#include "ui/ui_page_can.h"
#include "ui/ui_page_ota.h"
#include "ui/ui_ota.h"
#include "web_server.h"
#include "ntp_sync.h"
#include "ota_manager.h"

/* ==================== 标签栏常量 ==================== */
#define TAB_BAR_H   56        /* 标签栏高度 */
#define TAB_COUNT   4         /* 标签数量 */
#define TAB_BTN_W   (SCREEN_WIDTH / TAB_COUNT)  /* 每个标签宽度: 160px */

static const char *g_tab_labels[] = {
    "MQTT",
    "Modbus",
    "CAN",
    "OTA"
};

/* ==================== 全局对象 ==================== */
static lv_obj_t *g_tab_btns[TAB_COUNT];   /* 标签按钮 */
static lv_obj_t *g_tab_indicators[TAB_COUNT]; /* 底部指示条 */
static lv_obj_t *g_pages[TAB_COUNT];      /* 页面容器 */
static int       g_active_tab = 0;        /* 当前活动页 */

/* 标签栏样式 */
static lv_style_t style_tab_active;
static lv_style_t style_tab_inactive;
static lv_style_t style_indicator;

/* ==================== 全局数据 ==================== */
static float g_latest_temp = 0.0f;
static float g_latest_humi = 0.0f;
static bool  g_latest_valid = false;
static bool  g_new_data = false;
static pthread_mutex_t g_sensor_mutex = PTHREAD_MUTEX_INITIALIZER;

/* CAN 接收线程只写缓存，LVGL 主线程在 timer 中消费，避免跨线程调用 LVGL */
static pthread_mutex_t g_can_ui_mutex = PTHREAD_MUTEX_INITIALIZER;
static bool g_can_raw_pending = false;
static uint32_t g_can_raw_id = 0;
static uint8_t g_can_raw_dlc = 0;
static uint8_t g_can_raw_data[8] = {0};
static bool g_can_signal_pending = false;
static char g_can_signal_name[32] = "";
static char g_can_signal_unit[16] = "";
static float g_can_signal_value = 0.0f;

/* 测试/运行计数器 */
int can_tx_cnt = 0, can_rx_cnt = 0;  /* extern: web/api_status.c */
int rs485_tx_cnt = 0;                /* extern: web/api_status.c */
#if CAN_TEST_SEND_ENABLE
static bool can_tx_ok = false;
#endif
#if RS485_TEST_SEND_ENABLE
static bool rs485_tx_ok = false;
#endif
static int ip_update_tick = 0;  /* IP 更新节流 */

/* ==================== 背光控制 (防烧屏) ==================== */
#define BL_PATH       "/sys/class/backlight/backlight/brightness"
#define BL_NORMAL     200    /* 正常亮度 */
#define BL_DIM         20    /* 空闲30秒后微亮 */
#define BL_OFF          0    /* 空闲120秒后关闭 */
#define IDLE_DIM_SEC   30    /* 空闲多少秒后降低背光 */
#define IDLE_OFF_SEC  120    /* 空闲多少秒后关闭背光 */

static int  idle_seconds = 0;
static bool idle_dimmed  = false;

static void backlight_set(int val)
{
    FILE *f = fopen(BL_PATH, "w");
    if (f) { fprintf(f, "%d", val); fclose(f); }
}

/* ==================== 页面切换 ==================== */
static void switch_to_tab(int idx)
{
    if (idx < 0 || idx >= TAB_COUNT || idx == g_active_tab) return;

    /* 隐藏当前页, 显示目标页 */
    lv_obj_add_flag(g_pages[g_active_tab], LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(g_pages[idx], LV_OBJ_FLAG_HIDDEN);

    /* 更新标签按钮样式 */
    for (int i = 0; i < TAB_COUNT; i++) {
        if (i == idx) {
            lv_obj_set_style_bg_color(g_tab_btns[i], lv_color_hex(0x1e3a5f), 0);
            lv_obj_set_style_border_color(g_tab_btns[i], lv_color_hex(0x3b82f6), LV_PART_MAIN);
            lv_obj_set_style_text_color(g_tab_btns[i], lv_color_hex(0xffffff), 0);
            lv_obj_clear_flag(g_tab_indicators[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_set_style_bg_color(g_tab_btns[i], lv_color_hex(0x0f172a), 0);
            lv_obj_set_style_border_color(g_tab_btns[i], lv_color_hex(0x1e293b), LV_PART_MAIN);
            lv_obj_set_style_text_color(g_tab_btns[i], lv_color_hex(0x64748b), 0);
            lv_obj_add_flag(g_tab_indicators[i], LV_OBJ_FLAG_HIDDEN);
        }
    }

    g_active_tab = idx;
    LOG_INFO("Tab switched to %d", idx);
}

/* 标签按钮点击回调 */
static void tab_btn_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    switch_to_tab(idx);
}

/* 滑动手势回调 — 放在整个屏幕上 */
static void swipe_cb(lv_event_t *e)
{
    lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_active());
    if (dir == LV_DIR_LEFT && g_active_tab + 1 < TAB_COUNT) {
        switch_to_tab(g_active_tab + 1);
    } else if (dir == LV_DIR_RIGHT && g_active_tab > 0) {
        switch_to_tab(g_active_tab - 1);
    }
}

/* 创建自定义标签栏 */
static void create_tab_bar(lv_obj_t *parent)
{
    /* 标签栏背景容器 */
    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_set_size(bar, SCREEN_WIDTH, TAB_BAR_H);
    lv_obj_set_pos(bar, 0, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x0a0e17), 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    /* 底部边框线 */
    lv_obj_set_style_border_color(bar, lv_color_hex(0x1e293b), LV_PART_MAIN);
    lv_obj_set_style_border_width(bar, 1, LV_PART_MAIN);
    lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);

    /* 初始化样式 */
    lv_style_init(&style_tab_inactive);
    lv_style_set_bg_color(&style_tab_inactive, lv_color_hex(0x0f172a));
    lv_style_set_border_color(&style_tab_inactive, lv_color_hex(0x1e293b));
    lv_style_set_border_width(&style_tab_inactive, 1);
    lv_style_set_radius(&style_tab_inactive, 0);
    lv_style_set_pad_all(&style_tab_inactive, 0);

    lv_style_init(&style_tab_active);
    lv_style_set_bg_color(&style_tab_active, lv_color_hex(0x1e3a5f));
    lv_style_set_border_color(&style_tab_active, lv_color_hex(0x3b82f6));
    lv_style_set_border_width(&style_tab_active, 1);
    lv_style_set_radius(&style_tab_active, 0);
    lv_style_set_pad_all(&style_tab_active, 0);

    lv_style_init(&style_indicator);
    lv_style_set_bg_color(&style_indicator, lv_color_hex(0x3b82f6));
    lv_style_set_radius(&style_indicator, 0);
    lv_style_set_border_width(&style_indicator, 0);

    for (int i = 0; i < TAB_COUNT; i++) {
        lv_obj_t *btn = lv_btn_create(bar);
        lv_obj_set_size(btn, TAB_BTN_W, TAB_BAR_H);
        lv_obj_set_pos(btn, i * TAB_BTN_W, 0);
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);

        /* 初始全部 inactive */
        lv_obj_add_style(btn, &style_tab_inactive, 0);

        /* 标签文字 */
        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, g_tab_labels[i]);
#ifdef HOST_SIMULATION
        lv_obj_set_style_text_font(lbl, &lv_font_simsun_16_cjk, 0);
#else
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
#endif
        lv_obj_set_style_text_color(lbl, lv_color_hex(0x64748b), 0);
        lv_obj_center(lbl);

        /* 底部指示条 (默认隐藏, 活动时显示) */
        lv_obj_t *ind = lv_obj_create(bar);
        lv_obj_set_size(ind, TAB_BTN_W - 20, 3);
        lv_obj_set_pos(ind, i * TAB_BTN_W + 10, TAB_BAR_H - 3);
        lv_obj_add_style(ind, &style_indicator, 0);
        lv_obj_clear_flag(ind, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(ind, LV_OBJ_FLAG_HIDDEN); /* 默认隐藏 */

        /* 点击事件 */
        lv_obj_add_event_cb(btn, tab_btn_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);

        g_tab_btns[i] = btn;
        g_tab_indicators[i] = ind;
    }

    /* 激活第一个标签 */
    lv_obj_set_style_bg_color(g_tab_btns[0], lv_color_hex(0x1e3a5f), 0);
    lv_obj_set_style_border_color(g_tab_btns[0], lv_color_hex(0x3b82f6), LV_PART_MAIN);
    lv_obj_set_style_text_color(lv_obj_get_child(g_tab_btns[0], 0), lv_color_hex(0xffffff), 0);
    lv_obj_clear_flag(g_tab_indicators[0], LV_OBJ_FLAG_HIDDEN);
}

/* ==================== MQTT 回调 ==================== */
static void on_sensor_data(float temp, float humi, bool valid)
{
    pthread_mutex_lock(&g_sensor_mutex);
    g_latest_temp = temp;
    g_latest_humi = humi;
    g_latest_valid = valid;
    g_new_data = true;
    pthread_mutex_unlock(&g_sensor_mutex);

    /* MQTT 也作为标准 Producer 发布到 DataBus，统一三种协议的数据模型 */
    data_point_t point;
    memset(&point, 0, sizeof(point));
    point.source = DATA_SOURCE_MQTT;
    strncpy(point.device_name, "ESP32-C6", sizeof(point.device_name) - 1);
    point.timestamp = time(NULL);
    point.valid = valid;

    strncpy(point.point_name, "temperature", sizeof(point.point_name) - 1);
    strncpy(point.unit, "C", sizeof(point.unit) - 1);
    point.value = temp;
    data_bus_publish(&point);

    strncpy(point.point_name, "humidity", sizeof(point.point_name) - 1);
    strncpy(point.unit, "%", sizeof(point.unit) - 1);
    point.value = humi;
    data_bus_publish(&point);

    web_server_update_data(temp, humi, valid);
    data_recorder_record(temp, humi, valid);

    if (valid && (temp > ALARM_TEMP_HIGH || humi > ALARM_HUMI_HIGH)) {
        LOG_WARN("ALARM: temp=%.1f humi=%.0f", temp, humi);
    }
}

/* ==================== Modbus 按钮回调 ==================== */
#if RS485_TEST_SEND_ENABLE
static void btn_rs485_send_cb(lv_event_t *e)
{
    (void)e;
    char buf[80];
    int len = snprintf(buf, sizeof(buf),
                       "RS485 TEST [#%d] RK3506 UART3 -> RS485 Transceiver\r\n",
                       rs485_tx_cnt);
    if (rs485_write((const uint8_t *)buf, (size_t)len) >= 0) {
        rs485_tx_ok = true;
        rs485_tx_cnt++;
        LOG_INFO("RS485 manual send OK (cnt=%d)", rs485_tx_cnt);
    } else {
        rs485_tx_ok = false;
        LOG_WARN("RS485 manual send FAILED");
    }
}
#endif

/* ==================== CAN 按钮回调 ==================== */
#if CAN_TEST_SEND_ENABLE
static void btn_can_send_cb(lv_event_t *e)
{
    (void)e;
    can_frame_t frame;
    memset(&frame, 0, sizeof(frame));
    frame.can_id = CAN_TEST_ID;
    frame.can_dlc = 8;
    frame.data[0] = (can_tx_cnt >> 0) & 0xFF;
    frame.data[1] = (can_tx_cnt >> 8) & 0xFF;
    frame.data[2] = (can_tx_cnt >> 16) & 0xFF;
    frame.data[3] = (can_tx_cnt >> 24) & 0xFF;
    frame.data[4] = 0xAA; frame.data[5] = 0xBB;
    frame.data[6] = 0xCC; frame.data[7] = 0xDD;

    if (can_write_frame(&frame) == 0) {
        can_tx_ok = true;
        can_tx_cnt++;
        ui_page_can_update_tx_frame(frame.can_id, frame.can_dlc, frame.data);
        LOG_INFO("CAN manual send OK (cnt=%d)", can_tx_cnt);
    } else {
        can_tx_ok = false;
        LOG_WARN("CAN manual send FAILED");
    }
}
#endif

/* ==================== MQTT 按钮回调 ==================== */
static void btn_mqtt_sub_cb(lv_event_t *e)
{
    (void)e;
    mqtt_client_reconnect();
    LOG_INFO("MQTT re-subscribe triggered");
}

static void btn_mqtt_pub_cb(lv_event_t *e)
{
    (void)e;
    float temp, humi;
    pthread_mutex_lock(&g_sensor_mutex);
    temp = g_latest_temp;
    humi = g_latest_humi;
    pthread_mutex_unlock(&g_sensor_mutex);

    char payload[64];
    snprintf(payload, sizeof(payload),
             "{\"temp\":%.1f,\"humi\":%.0f}", temp, humi);
    mqtt_client_publish(MQTT_TOPIC, payload, 1, false);
    LOG_INFO("MQTT publish: %s", payload);
}

static void btn_mqtt_refresh_cb(lv_event_t *e)
{
    (void)e;
    /* 请求主线程用最近一次数据刷新图表 */
    pthread_mutex_lock(&g_sensor_mutex);
    g_new_data = true;
    pthread_mutex_unlock(&g_sensor_mutex);
    LOG_INFO("Chart refresh requested");
}

static void btn_mqtt_clear_cb(lv_event_t *e)
{
    (void)e;
    /* 用 LV_CHART_POINT_NONE 清空图表 */
    for (int i = 0; i < MAX_CHART_PTS; i++) {
        ui_page_mqtt_add_chart_point(0, 0, false);
    }
    LOG_INFO("Chart data cleared");
}

/* ==================== Modbus 新按钮回调 ==================== */
#if RS485_TEST_SEND_ENABLE
static void btn_modbus_scan_cb(lv_event_t *e)
{
    (void)e;
    LOG_INFO("Modbus scan slaves triggered (1-247)");
    /* TODO: 实际扫描逻辑 */
}

static void btn_modbus_read_cb(lv_event_t *e)
{
    (void)e;
    LOG_INFO("Manual register read triggered");
    /* 模拟读取寄存器 */
    uint16_t sim_regs[2];
    sim_regs[0] = (uint16_t)(g_latest_temp * 10.0f);
    sim_regs[1] = (uint16_t)(g_latest_humi * 10.0f);
    ui_page_modbus_update_slave(1, "Temp/Humi Sensor", 2, sim_regs, g_latest_valid);
}

#endif

static void btn_modbus_auto_cb(lv_event_t *e)
{
    (void)e;
    ui_page_modbus_toggle_auto();
    bool enabled = ui_page_modbus_get_auto_state() != 0;
    modbus_master_set_polling(enabled);
    LOG_INFO("Auto polling toggled: %d", enabled);
}

/* ==================== CAN 新按钮回调 ==================== */
static void btn_can_listen_cb(lv_event_t *e)
{
    (void)e;
    ui_page_can_toggle_listen();
    LOG_INFO("CAN listen toggled: %d", ui_page_can_get_listen_state());
}

static void btn_can_filter_cb(lv_event_t *e)
{
    (void)e;
    if (can_set_filter(CAN_TEST_ID, CAN_SFF_MASK) == 0) {
        LOG_INFO("CAN filter applied: ID=0x%X mask=0x%X", CAN_TEST_ID, CAN_SFF_MASK);
    } else {
        LOG_WARN("CAN filter apply failed");
    }
}

static void btn_can_clear_cb(lv_event_t *e)
{
    (void)e;
    pthread_mutex_lock(&g_can_ui_mutex);
    can_tx_cnt = 0;
    can_rx_cnt = 0;
    g_can_raw_pending = false;
    g_can_signal_pending = false;
    pthread_mutex_unlock(&g_can_ui_mutex);
#if CAN_TEST_SEND_ENABLE
    can_tx_ok = false;
#endif
    LOG_INFO("CAN counters cleared");
}

/* CAN 数据接收回调 (从 can_manager 接收线程调用 — 解析后的信号值) */
static void can_recv_cb(uint32_t can_id, const char *name, double value, const char *unit)
{
    pthread_mutex_lock(&g_can_ui_mutex);
    can_rx_cnt++;
    g_can_signal_pending = true;
    g_can_signal_value = (float)value;
    strncpy(g_can_signal_name, name ? name : "CAN Signal", sizeof(g_can_signal_name) - 1);
    g_can_signal_name[sizeof(g_can_signal_name) - 1] = '\0';
    strncpy(g_can_signal_unit, unit ? unit : "", sizeof(g_can_signal_unit) - 1);
    g_can_signal_unit[sizeof(g_can_signal_unit) - 1] = '\0';
    int rx_count = can_rx_cnt;
    pthread_mutex_unlock(&g_can_ui_mutex);

    LOG_DEBUG("CAN RX: ID=0x%X %s=%.2f %s (total rx=%d)",
              can_id, name, value, unit, rx_count);
}

/* CAN 接收线程只保存最新原始帧，不直接调用 LVGL */
static void can_recv_raw_cb(uint32_t can_id, uint8_t dlc, const uint8_t *data)
{
    pthread_mutex_lock(&g_can_ui_mutex);
    g_can_raw_id = can_id;
    g_can_raw_dlc = dlc > 8 ? 8 : dlc;
    memcpy(g_can_raw_data, data, g_can_raw_dlc);
    g_can_raw_pending = true;
    pthread_mutex_unlock(&g_can_ui_mutex);
}

/* OTA 按钮回调 */
static void btn_ota_check_cb(lv_event_t *e)
{
    (void)e;
    LOG_INFO("OTA check-only triggered");
    ui_ota_check();
}

static void btn_ota_start_cb(lv_event_t *e)
{
    (void)e;
    LOG_INFO("OTA install triggered");
    ui_ota_start();
}

/* ==================== LVGL 回调 ==================== */
#ifndef HOST_SIMULATION
static void drm_flush_cb(lv_display_t *disp, const lv_area_t *area,
                         unsigned char *color_p)
{
    (void)area; (void)color_p;
    lv_display_flush_ready(disp);
}

static void touchpad_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    static int16_t last_x = 0, last_y = 0;
    static bool pressed = false;
    hal_touch_read(&last_x, &last_y, &pressed);
    data->point.x = last_x;
    data->point.y = last_y;
    data->state = pressed ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;

    /* 检测触摸活动 — 恢复背光, 重置空闲计时 */
    if (pressed) {
        if (idle_dimmed) {
            backlight_set(BL_NORMAL);
            idle_dimmed = false;
        }
        idle_seconds = 0;
    }
}
#endif

/* ==================== 定时器 ==================== */
static void timer_1s_cb(lv_timer_t *timer)
{
    (void)timer;

    /* = 防烧屏: 空闲背光控制 = */
    idle_seconds++;
    if (idle_seconds >= IDLE_OFF_SEC) {
        backlight_set(BL_OFF);
        idle_dimmed = true;
    } else if (idle_seconds >= IDLE_DIM_SEC && !idle_dimmed) {
        backlight_set(BL_DIM);
        idle_dimmed = true;
    }

    /* 时钟 (MQTT 页面) */
    ui_page_mqtt_update_clock();

    /* IP 地址更新 (每10秒, 避免频繁 syscall) */
    ip_update_tick++;
    if (ip_update_tick >= 10) {
        ip_update_tick = 0;
        ui_page_mqtt_update_ip();
    }

    /* 传感器数据：后台 MQTT 线程只写缓存，这里在 LVGL 主线程消费 */
    float latest_temp = 0.0f, latest_humi = 0.0f;
    bool latest_valid = false, has_new_data = false;
    pthread_mutex_lock(&g_sensor_mutex);
    has_new_data = g_new_data;
    if (has_new_data) {
        latest_temp = g_latest_temp;
        latest_humi = g_latest_humi;
        latest_valid = g_latest_valid;
        g_new_data = false;
    }
    pthread_mutex_unlock(&g_sensor_mutex);

    if (has_new_data) {
        ui_page_mqtt_update_temp(latest_temp);
        ui_page_mqtt_update_humi(latest_humi);
        ui_page_mqtt_add_chart_point(latest_temp, latest_humi, latest_valid);
    }

    /* MQTT 状态 */
    ui_page_mqtt_set_status(mqtt_client_is_connected(),
                             mqtt_client_get_retry_count());

    /* Modbus 页面刷新 */
#if RS485_TEST_SEND_ENABLE
    ui_page_modbus_update_tx(rs485_tx_cnt, rs485_tx_ok);
    ui_page_modbus_update_rx(rs485_tx_cnt > 0 ? rs485_tx_cnt / 2 : 0, rs485_tx_ok);
    ui_page_modbus_set_led(rs485_tx_ok);
    /* 模拟从站数据刷新 (从 MQTT 传感器数据派生) */
    if (g_latest_valid) {
        uint16_t sim_regs[2];
        sim_regs[0] = (uint16_t)(g_latest_temp * 10.0f);
        sim_regs[1] = (uint16_t)(g_latest_humi * 10.0f);
        ui_page_modbus_update_slave(1, "Temp/Humi Sensor", 2, sim_regs, true);
    }
#endif

    /* CAN 页面刷新：所有 LVGL 调用都集中在主线程 */
    bool raw_pending, signal_pending;
    uint32_t raw_id;
    uint8_t raw_dlc, raw_data[8];
    char signal_name[32], signal_unit[16];
    float signal_value;
    int can_tx_snapshot, can_rx_snapshot;

    pthread_mutex_lock(&g_can_ui_mutex);
    raw_pending = g_can_raw_pending;
    signal_pending = g_can_signal_pending;
    raw_id = g_can_raw_id;
    raw_dlc = g_can_raw_dlc;
    memcpy(raw_data, g_can_raw_data, sizeof(raw_data));
    strncpy(signal_name, g_can_signal_name, sizeof(signal_name));
    signal_name[sizeof(signal_name) - 1] = '\0';
    strncpy(signal_unit, g_can_signal_unit, sizeof(signal_unit));
    signal_unit[sizeof(signal_unit) - 1] = '\0';
    signal_value = g_can_signal_value;
    can_tx_snapshot = can_tx_cnt;
    can_rx_snapshot = can_rx_cnt;
    g_can_raw_pending = false;
    g_can_signal_pending = false;
    pthread_mutex_unlock(&g_can_ui_mutex);

    ui_page_can_update(CAN_TEST_ID, can_tx_snapshot, can_rx_snapshot, true);
    ui_page_can_set_led(can_rx_snapshot > 0);
    if (raw_pending && ui_page_can_get_listen_state()) {
        ui_page_can_update_rx_frame(raw_id, raw_dlc, raw_data);
    }
    if (signal_pending) {
        ui_page_can_update_signal(signal_name, signal_value, signal_unit, 0, 8000);
    }

    watchdog_heartbeat();
    data_recorder_tick();
    mqtt_client_retry_tick();
}

/* ==================== 延迟服务初始化 (开机后执行, 不阻塞屏幕显示) ==================== */
static void services_init_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    LOG_INFO("=== Deferred service init start ===");

    data_recorder_init();
    web_server_start(HTTP_PORT);
    LOG_INFO("HTTP server: http://0.0.0.0:%d", HTTP_PORT);

    ntp_sync_init();
    ota_init(OTA_DEFAULT_SERVER);
    ota_set_type(OTA_TYPE_APP);
    ota_set_app_install_path(OTA_APP_INSTALL_PATH);

    /* DataBus 必须先于任何 Producer 启动，避免 MQTT 首包在 init 后被清空 */
    data_bus_init();

    /* 认证必须在首次 connect 前配置 */
    mqtt_client_set_auth(MQTT_DEVICE_ID, MQTT_DEVICE_SECRET);
    if (mqtt_client_init(MQTT_BROKER, MQTT_PORT, MQTT_TOPIC, on_sensor_data) == 0) {
        LOG_INFO("MQTT client initialized");
        mqtt_client_start();
    }

    if (modbus_master_init(MODBUS_DEVICE, MODBUS_BAUD, MODBUS_GPIO_PIN) == 0) {
        modbus_slave_config_t slv;
        memset(&slv, 0, sizeof(slv));
        slv.slave_id = 1;
        slv.func_code = 3;
        slv.start_addr = 0;
        slv.nb_regs = 2;
        slv.poll_interval_ms = MODBUS_POLL_MS;
        strncpy(slv.device_name, "Temp/Humi Sensor", sizeof(slv.device_name) - 1);
        modbus_master_add_slave(&slv);
        modbus_master_start();
        LOG_INFO("Modbus master started");
    }

    if (can_manager_init(CAN_INTERFACE, CAN_BITRATE) == 0) {
        can_signal_config_t sig;
        memset(&sig, 0, sizeof(sig));
        sig.can_id = CAN_TEST_ID;
        sig.start_bit = 0;
        sig.length = 16;
        sig.scale = 0.25f;
        sig.offset = 0;
        strncpy(sig.signal_name, "Engine RPM", sizeof(sig.signal_name) - 1);
        strncpy(sig.unit, "rpm", sizeof(sig.unit) - 1);
        can_manager_add_signal(&sig);
        /* 先注册回调再启动线程，避免首帧到达时 callback 尚未安装 */
        can_manager_set_callback(can_recv_cb);
        can_manager_set_raw_callback(can_recv_raw_cb);
        can_manager_start();
        LOG_INFO("CAN manager started");
    }

    LOG_INFO("=== Deferred service init done ===");
    lv_timer_del(timer);  /* 一次性执行 */
}

/* ==================== 主函数 ==================== */
int main(void)
{
    printf("\n============================================\n");
    printf("  RK3506 Environment Monitor v%s\n", APP_VERSION);
    printf("  4-Page Manual TabBar: MQTT / Modbus / CAN / OTA\n");
    printf("============================================\n\n");

    setenv("TZ", "CST-8", 1);
    tzset();

    logger_init(LOG_FILE_PATH);
    LOG_INFO("=== System starting v%s ===", APP_VERSION);

    watchdog_init(60);

    /* DRM 显示 */
#ifdef HOST_SIMULATION
    /* SDL 模式: lv_sdl_window_create 内部创建 LVGL display */
    lv_init();
    hal_display_init();  /* 创建 SDL 窗口 + LVGL display */
    lv_display_t *disp = lv_display_get_default();
#else
    if (hal_display_init() < 0) { LOG_ERROR("DRM init failed"); return -1; }

    /* LVGL */
    lv_init();
    lv_display_t *disp = lv_display_create(hal_display_get_width(),
                                           hal_display_get_height());
    lv_display_set_flush_cb(disp, drm_flush_cb);
    lv_display_set_buffers(disp, hal_display_get_fb(), NULL,
                           hal_display_get_fb_size(),
                           LV_DISPLAY_RENDER_MODE_DIRECT);
#endif
    lv_display_set_default(disp);

    /* 触摸 */
#ifdef HOST_SIMULATION
    /* SDL 模式: lv_sdl_window 已自动注册 mouse indev */
#else
    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touchpad_read_cb);
    lv_indev_set_display(indev, disp);
    hal_touch_init();
#endif

    /* ========== 屏幕背景 ========== */
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0a0e17), 0);
    lv_obj_set_style_pad_all(scr, 0, 0);

    /* ========== 创建自定义标签栏 ========== */
    create_tab_bar(scr);

    /* ========== 创建3个页面容器 ========== */
    /* 页面区域: 标签栏下方, 480 x (800 - TAB_BAR_H) */
    lv_coord_t page_y = TAB_BAR_H;
    lv_coord_t page_h = SCREEN_HEIGHT - TAB_BAR_H;

    for (int i = 0; i < TAB_COUNT; i++) {
        g_pages[i] = lv_obj_create(scr);
        lv_obj_set_size(g_pages[i], SCREEN_WIDTH, page_h);
        lv_obj_set_pos(g_pages[i], 0, page_y);
        lv_obj_set_style_bg_color(g_pages[i], lv_color_hex(0x0a0e17), 0);
        lv_obj_set_style_border_width(g_pages[i], 0, 0);
        lv_obj_set_style_pad_all(g_pages[i], 0, 0);
        lv_obj_set_style_radius(g_pages[i], 0, 0);
        lv_obj_clear_flag(g_pages[i], LV_OBJ_FLAG_SCROLLABLE);

        /* 默认隐藏非活动页 */
        if (i != 0)
            lv_obj_add_flag(g_pages[i], LV_OBJ_FLAG_HIDDEN);
    }

    /* 在各页面容器中创建页面内容 */
    ui_page_mqtt_create(g_pages[0]);
    ui_page_modbus_create(g_pages[1]);
    ui_page_can_create(g_pages[2]);
    ui_page_ota_create(g_pages[3]);

    {
        char ver[32];
        ota_get_local_version(ver, sizeof(ver));
        ui_page_ota_set_info(ver, OTA_DEFAULT_SERVER);
    }

    /* ========== 屏幕级滑动手势 ========== */
    lv_obj_add_event_cb(scr, swipe_cb, LV_EVENT_GESTURE, NULL);

    /* ========== 注册按钮事件 ========== */
    /* MQTT 按钮 */
    {
        lv_obj_t *b;
        b = ui_page_mqtt_get_btn_sub();     if (b) lv_obj_add_event_cb(b, btn_mqtt_sub_cb, LV_EVENT_CLICKED, NULL);
        b = ui_page_mqtt_get_btn_pub();     if (b) lv_obj_add_event_cb(b, btn_mqtt_pub_cb, LV_EVENT_CLICKED, NULL);
        b = ui_page_mqtt_get_btn_refresh(); if (b) lv_obj_add_event_cb(b, btn_mqtt_refresh_cb, LV_EVENT_CLICKED, NULL);
        b = ui_page_mqtt_get_btn_clear();   if (b) lv_obj_add_event_cb(b, btn_mqtt_clear_cb, LV_EVENT_CLICKED, NULL);
    }
    /* Modbus 按钮：Auto 始终可用；测试发送/扫描/模拟读取仍受测试宏控制 */
    {
        lv_obj_t *b;
        b = ui_page_modbus_get_btn_auto(); if (b) lv_obj_add_event_cb(b, btn_modbus_auto_cb, LV_EVENT_CLICKED, NULL);
#if RS485_TEST_SEND_ENABLE
        b = ui_page_modbus_get_btn_send(); if (b) lv_obj_add_event_cb(b, btn_rs485_send_cb, LV_EVENT_CLICKED, NULL);
        b = ui_page_modbus_get_btn_scan(); if (b) lv_obj_add_event_cb(b, btn_modbus_scan_cb, LV_EVENT_CLICKED, NULL);
        b = ui_page_modbus_get_btn_read(); if (b) lv_obj_add_event_cb(b, btn_modbus_read_cb, LV_EVENT_CLICKED, NULL);
#endif
    }
    /* CAN 按钮：监听/过滤/清零始终可用，测试发送受宏控制 */
    {
        lv_obj_t *b;
        b = ui_page_can_get_btn_listen(); if (b) lv_obj_add_event_cb(b, btn_can_listen_cb, LV_EVENT_CLICKED, NULL);
        b = ui_page_can_get_btn_filter(); if (b) lv_obj_add_event_cb(b, btn_can_filter_cb, LV_EVENT_CLICKED, NULL);
        b = ui_page_can_get_btn_clear();  if (b) lv_obj_add_event_cb(b, btn_can_clear_cb, LV_EVENT_CLICKED, NULL);
#if CAN_TEST_SEND_ENABLE
        b = ui_page_can_get_btn_send();   if (b) lv_obj_add_event_cb(b, btn_can_send_cb, LV_EVENT_CLICKED, NULL);
#endif
    }

    /* OTA 按钮 */
    {
        lv_obj_t *b;
        b = ui_page_ota_get_btn_check(); if (b) lv_obj_add_event_cb(b, btn_ota_check_cb, LV_EVENT_CLICKED, NULL);
        b = ui_page_ota_get_btn_start(); if (b) lv_obj_add_event_cb(b, btn_ota_start_cb, LV_EVENT_CLICKED, NULL);
    }

    /* ========== 定时器 ========== */
    lv_timer_create(timer_1s_cb, 1000, NULL);
    lv_timer_create(ui_ota_poll, 500, NULL);

    /* 立即渲染第一帧 (屏幕立即可见, 不等定时器) */
    lv_refr_now(disp);

    /* ★ OTA 健康标志: 基本系统 (DRM+LVGL+UI) 已就绪, 立即写入.
     * 后台 OTA 脚本据此判断新版本是否成功启动.
     * 写在延迟服务初始化之前, 确保在回滚宽限期内完成. */
    ota_write_health_marker();

    /* 延迟服务初始化 (100ms后执行, 屏幕先显示) */
    lv_timer_create(services_init_timer_cb, 100, NULL);

    LOG_INFO("=== System ready, entering main loop ===");

    /* 主循环 */
    while (1) {
        lv_tick_inc(1);
        lv_timer_handler();
        usleep(1000);
    }

    return 0;
}
