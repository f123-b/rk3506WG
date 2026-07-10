/**
 * @file    ui_page_mqtt.c — minimalist MQTT sensors page
 *
 * ① Status bar (36px): MQTT connection + clock
 * ② Sensor cards (140px): TEMP + HUMI with big values
 * ③ Chart (fills gap): 60-point line chart
 * ④ Buttons (94px, bottom-aligned at y=650): 2x2
 */
#include "ui_page_mqtt.h"
#include "../app_config.h"
#include "../infra/logger.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <stdlib.h>
#include <ifaddrs.h>
#include <arpa/inet.h>

#define PW      464
#define MG      8
#define GAP     12
#define STAT_H  36
#define CARD_H  140
#define BTN_H   42
#define BTN_W   ((PW - MG - GAP) / 2)
#define BTN_ROW (BTN_H * 2 + GAP)
#define PAGE_H  744
#define BTN_Y   (PAGE_H - MG - BTN_ROW)  /* 650 */

#define FS  (&lv_font_montserrat_12)
#define FN  (&lv_font_montserrat_16)
#define FB  (&lv_font_montserrat_32)

static lv_obj_t *g_tv, *g_hv, *g_ts, *g_hs, *g_clock, *g_dot, *g_mqtt, *g_ip;
static lv_obj_t *g_chart;
static lv_chart_series_t *g_st, *g_sh;
static lv_obj_t *g_btn[4];

static lv_style_t st_card, st_ct, st_ch;
static lv_style_t st_cbg, st_cln, st_cpt;
static lv_style_t st_btn[4];
static const uint32_t cb[4] = {0x3b82f6, 0x10b981, 0xd97706, 0xef4444};

static void init_styles(void)
{
    lv_style_init(&st_card);
    lv_style_set_bg_color(&st_card, lv_color_hex(0x1e293b));
    lv_style_set_radius(&st_card, 8);
    lv_style_set_border_width(&st_card, 1);
    lv_style_set_border_color(&st_card, lv_color_hex(0x334155));
    lv_style_set_pad_all(&st_card, 10);

    lv_style_init(&st_ct);
    lv_style_set_border_color(&st_ct, lv_color_hex(0xf59e0b));
    lv_style_set_border_width(&st_ct, 3);
    lv_style_set_border_side(&st_ct, LV_BORDER_SIDE_TOP);

    lv_style_init(&st_ch);
    lv_style_set_border_color(&st_ch, lv_color_hex(0x06b6d4));
    lv_style_set_border_width(&st_ch, 3);
    lv_style_set_border_side(&st_ch, LV_BORDER_SIDE_TOP);

    lv_style_init(&st_cbg);
    lv_style_set_bg_color(&st_cbg, lv_color_hex(0x0f172a));
    lv_style_set_border_color(&st_cbg, lv_color_hex(0x334155));
    lv_style_set_border_width(&st_cbg, 1);
    lv_style_set_radius(&st_cbg, 8);
    lv_style_set_pad_all(&st_cbg, 6);
    lv_style_set_line_color(&st_cbg, lv_color_hex(0x1e3a5f));
    lv_style_set_line_width(&st_cbg, 1);
    lv_style_set_line_opa(&st_cbg, LV_OPA_50);

    lv_style_init(&st_cln);
    lv_style_set_line_width(&st_cln, 2);
    lv_style_set_line_rounded(&st_cln, true);

    lv_style_init(&st_cpt);
    lv_style_set_width(&st_cpt, 0);
    lv_style_set_height(&st_cpt, 0);

    for (int i = 0; i < 4; i++) {
        lv_style_init(&st_btn[i]);
        lv_style_set_bg_color(&st_btn[i], lv_color_hex(cb[i]));
        lv_style_set_radius(&st_btn[i], 8);
        lv_style_set_border_width(&st_btn[i], 0);
        lv_style_set_pad_all(&st_btn[i], 4);
    }
}

static lv_obj_t *L(lv_obj_t *p, const char *t, const lv_font_t *f, uint32_t c)
{
    lv_obj_t *l = lv_label_create(p);
    lv_label_set_text(l, t);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(c), 0);
    return l;
}

lv_obj_t *ui_page_mqtt_create(lv_obj_t *parent)
{
    init_styles();
    int y = MG;
    int cw = (PW - MG * 2 - GAP) / 2;

    /* = ① Status bar = */
    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_set_size(bar, PW, STAT_H); lv_obj_set_pos(bar, MG, y);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x111827), 0);
    lv_obj_set_style_border_color(bar, lv_color_hex(0x334155), 0);
    lv_obj_set_style_border_width(bar, 1, 0);
    lv_obj_set_style_radius(bar, 8, 0);
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    g_dot = lv_obj_create(bar);
    lv_obj_set_size(g_dot, 8, 8); lv_obj_set_pos(g_dot, 10, 14);
    lv_obj_set_style_bg_color(g_dot, lv_color_hex(0xef4444), 0);
    lv_obj_set_style_radius(g_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(g_dot, 0, 0);

    g_mqtt = L(bar, "MQTT Offline", FS, 0x94a3b8);
    lv_obj_align(g_mqtt, LV_ALIGN_LEFT_MID, 24, 0);

    g_ip = L(bar, "IP:---.---.---.---", FS, 0x64748b);
    lv_obj_align(g_ip, LV_ALIGN_CENTER, 0, 0);

    g_clock = L(bar, "----.--.-- --:--:--", FS, 0x64748b);
    lv_obj_align(g_clock, LV_ALIGN_RIGHT_MID, -10, 0);

    y += STAT_H + GAP;

    /* = ② Sensor cards = */
    lv_obj_t *ct = lv_obj_create(parent);
    lv_obj_set_size(ct, cw, CARD_H); lv_obj_set_pos(ct, MG, y);
    lv_obj_add_style(ct, &st_card, 0); lv_obj_add_style(ct, &st_ct, 0);
    lv_obj_clear_flag(ct, LV_OBJ_FLAG_SCROLLABLE);

    L(ct, "TEMP", FN, 0xf59e0b);
    lv_obj_align(lv_obj_get_child(ct, -1), LV_ALIGN_TOP_LEFT, 2, 6);
    g_tv = L(ct, "--.-", FB, 0xfbbf24);
    lv_obj_align(g_tv, LV_ALIGN_CENTER, -14, 0);
    L(ct, "C", FN, 0xf59e0b);
    lv_obj_align(lv_obj_get_child(ct, -1), LV_ALIGN_CENTER, 32, 2);
    g_ts = L(ct, "Wait...", FS, 0x22c55e);
    lv_obj_align(g_ts, LV_ALIGN_BOTTOM_MID, 0, -6);

    lv_obj_t *ch = lv_obj_create(parent);
    lv_obj_set_size(ch, cw, CARD_H); lv_obj_set_pos(ch, MG + cw + GAP, y);
    lv_obj_add_style(ch, &st_card, 0); lv_obj_add_style(ch, &st_ch, 0);
    lv_obj_clear_flag(ch, LV_OBJ_FLAG_SCROLLABLE);

    L(ch, "HUMI", FN, 0x06b6d4);
    lv_obj_align(lv_obj_get_child(ch, -1), LV_ALIGN_TOP_LEFT, 2, 6);
    g_hv = L(ch, "--", FB, 0x22d3ee);
    lv_obj_align(g_hv, LV_ALIGN_CENTER, -10, 0);
    L(ch, "%", FN, 0x06b6d4);
    lv_obj_align(lv_obj_get_child(ch, -1), LV_ALIGN_CENTER, 24, 2);
    g_hs = L(ch, "Wait...", FS, 0x22c55e);
    lv_obj_align(g_hs, LV_ALIGN_BOTTOM_MID, 0, -6);

    y += CARD_H + GAP;

    /* = ④ Buttons at bottom = */
    int bx = MG;
    g_btn[0] = lv_btn_create(parent); lv_obj_set_size(g_btn[0], BTN_W, BTN_H); lv_obj_set_pos(g_btn[0], bx, BTN_Y); lv_obj_add_style(g_btn[0], &st_btn[0], 0); lv_obj_clear_flag(g_btn[0], LV_OBJ_FLAG_SCROLLABLE); { lv_obj_t *l = L(g_btn[0], "Subscribe", FN, 0xffffff); lv_obj_center(l); }
    g_btn[1] = lv_btn_create(parent); lv_obj_set_size(g_btn[1], BTN_W, BTN_H); lv_obj_set_pos(g_btn[1], bx+BTN_W+GAP, BTN_Y); lv_obj_add_style(g_btn[1], &st_btn[1], 0); lv_obj_clear_flag(g_btn[1], LV_OBJ_FLAG_SCROLLABLE); { lv_obj_t *l = L(g_btn[1], "Publish", FN, 0xffffff); lv_obj_center(l); }
    g_btn[2] = lv_btn_create(parent); lv_obj_set_size(g_btn[2], BTN_W, BTN_H); lv_obj_set_pos(g_btn[2], bx, BTN_Y+BTN_H+GAP); lv_obj_add_style(g_btn[2], &st_btn[2], 0); lv_obj_clear_flag(g_btn[2], LV_OBJ_FLAG_SCROLLABLE); { lv_obj_t *l = L(g_btn[2], "Refresh", FN, 0xffffff); lv_obj_center(l); }
    g_btn[3] = lv_btn_create(parent); lv_obj_set_size(g_btn[3], BTN_W, BTN_H); lv_obj_set_pos(g_btn[3], bx+BTN_W+GAP, BTN_Y+BTN_H+GAP); lv_obj_add_style(g_btn[3], &st_btn[3], 0); lv_obj_clear_flag(g_btn[3], LV_OBJ_FLAG_SCROLLABLE); { lv_obj_t *l = L(g_btn[3], "Clear", FN, 0xffffff); lv_obj_center(l); }

    /* = ③ Chart (fills y to BTN_Y) = */
    int chart_h = BTN_Y - y - GAP;
    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_set_size(cont, PW, chart_h); lv_obj_set_pos(cont, MG, y);
    lv_obj_add_style(cont, &st_cbg, 0);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);

    L(cont, "Temperature & Humidity", FN, 0x94a3b8);
    lv_obj_align(lv_obj_get_child(cont, -1), LV_ALIGN_TOP_LEFT, 4, 4);
    L(cont, "Y:Temp  C:Humi", FS, 0x64748b);
    lv_obj_align(lv_obj_get_child(cont, -1), LV_ALIGN_TOP_RIGHT, -4, 4);

    g_chart = lv_chart_create(cont);
    lv_obj_set_size(g_chart, PW - 16, chart_h - 40);
    lv_obj_set_pos(g_chart, 4, 24);
    lv_obj_add_style(g_chart, &st_cbg, LV_PART_MAIN);
    lv_obj_add_style(g_chart, &st_cln, LV_PART_ITEMS);
    lv_obj_add_style(g_chart, &st_cpt, LV_PART_INDICATOR);
    lv_obj_set_style_border_width(g_chart, 0, LV_PART_MAIN);

    lv_chart_set_type(g_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(g_chart, MAX_CHART_PTS);
    lv_chart_set_div_line_count(g_chart, 4, 6);
    lv_chart_set_range(g_chart, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
    lv_chart_set_range(g_chart, LV_CHART_AXIS_SECONDARY_Y, 15, 40);

    g_sh = lv_chart_add_series(g_chart, lv_color_hex(0x06b6d4), LV_CHART_AXIS_PRIMARY_Y);
    g_st = lv_chart_add_series(g_chart, lv_color_hex(0xf59e0b), LV_CHART_AXIS_SECONDARY_Y);

    srand((unsigned)time(NULL));
    for (int i = 0; i < MAX_CHART_PTS; i++) {
        float ph = (float)i / (float)MAX_CHART_PTS * 6.28318f;
        float t = 25.5f + sinf(ph) * 3.5f + ((rand()%100)-50)*0.03f;
        float h = 65.0f + cosf(ph*0.8f) * 9.0f + ((rand()%100)-50)*0.1f;
        if (t < 15) t = 15; if (t > 40) t = 40;
        if (h < 0) h = 0;  if (h > 100) h = 100;
        lv_chart_set_next_value(g_chart, g_st, (int32_t)t);
        lv_chart_set_next_value(g_chart, g_sh, (int32_t)h);
    }

    LOG_INFO("MQTT page created");
    return parent;
}

void ui_page_mqtt_update_temp(float t)
{
    if (!g_tv) return;
    if (isnan(t) || t < -50 || t > 100) { lv_label_set_text(g_tv, "--.-"); return; }
    char b[16]; snprintf(b, sizeof(b), "%.1f", t); lv_label_set_text(g_tv, b);
    if (g_ts) {
        if (t > ALARM_TEMP_HIGH) { lv_label_set_text(g_ts, "HIGH"); lv_obj_set_style_text_color(g_ts, lv_color_hex(0xef4444), 0); }
        else if (t < 10) { lv_label_set_text(g_ts, "LOW"); lv_obj_set_style_text_color(g_ts, lv_color_hex(0x3b82f6), 0); }
        else { lv_label_set_text(g_ts, "OK"); lv_obj_set_style_text_color(g_ts, lv_color_hex(0x22c55e), 0); }
    }
}

void ui_page_mqtt_update_humi(float h)
{
    if (!g_hv) return;
    if (isnan(h) || h < 0 || h > 100) { lv_label_set_text(g_hv, "--"); return; }
    char b[16]; snprintf(b, sizeof(b), "%.0f", h); lv_label_set_text(g_hv, b);
    if (g_hs) {
        if (h > ALARM_HUMI_HIGH) { lv_label_set_text(g_hs, "HIGH"); lv_obj_set_style_text_color(g_hs, lv_color_hex(0xef4444), 0); }
        else if (h < 30) { lv_label_set_text(g_hs, "LOW"); lv_obj_set_style_text_color(g_hs, lv_color_hex(0xf59e0b), 0); }
        else { lv_label_set_text(g_hs, "OK"); lv_obj_set_style_text_color(g_hs, lv_color_hex(0x22c55e), 0); }
    }
}

void ui_page_mqtt_add_chart_point(float t, float h, bool v)
{
    if (!g_chart) return;
    if (v) {
        if (t < 15) t = 15; if (t > 40) t = 40;
        if (h < 0) h = 0;  if (h > 100) h = 100;
        lv_chart_set_next_value(g_chart, g_st, (int32_t)t);
        lv_chart_set_next_value(g_chart, g_sh, (int32_t)h);
    } else {
        lv_chart_set_next_value(g_chart, g_st, LV_CHART_POINT_NONE);
        lv_chart_set_next_value(g_chart, g_sh, LV_CHART_POINT_NONE);
    }
}

void ui_page_mqtt_set_status(bool ok, int retry)
{
    if (!g_mqtt || !g_dot) return;
    if (ok) {
        lv_label_set_text(g_mqtt, "MQTT Online");
        lv_obj_set_style_text_color(g_mqtt, lv_color_hex(0x22c55e), 0);
        lv_obj_set_style_bg_color(g_dot, lv_color_hex(0x22c55e), 0);
    } else if (retry > 0) {
        char b[32]; snprintf(b, sizeof(b), "MQTT Retry(%d)", retry);
        lv_label_set_text(g_mqtt, b);
        lv_obj_set_style_text_color(g_mqtt, lv_color_hex(0xf59e0b), 0);
        lv_obj_set_style_bg_color(g_dot, lv_color_hex(0xf59e0b), 0);
    } else {
        lv_label_set_text(g_mqtt, "MQTT Offline");
        lv_obj_set_style_text_color(g_mqtt, lv_color_hex(0xef4444), 0);
        lv_obj_set_style_bg_color(g_dot, lv_color_hex(0xef4444), 0);
    }
}

void ui_page_mqtt_update_clock(void)
{
    if (!g_clock) return;
    time_t n = time(NULL); struct tm tm; localtime_r(&n, &tm);
    char b[32]; strftime(b, sizeof(b), "%Y-%m-%d %H:%M:%S", &tm);
    lv_label_set_text(g_clock, b);
}

bool ui_page_mqtt_check_alarm(float t, float h) { return t > ALARM_TEMP_HIGH || h > ALARM_HUMI_HIGH; }

void ui_page_mqtt_update_ip(void)
{
    if (!g_ip) return;
    struct ifaddrs *ifa, *p;
    if (getifaddrs(&ifa) != 0) return;
    for (p = ifa; p; p = p->ifa_next) {
        if (!p->ifa_addr || p->ifa_addr->sa_family != AF_INET) continue;
        if (strcmp(p->ifa_name, "lo") == 0) continue;
        char ip[32];
        snprintf(ip, sizeof(ip), "IP:%s",
                 inet_ntoa(((struct sockaddr_in *)p->ifa_addr)->sin_addr));
        lv_label_set_text(g_ip, ip);
        break;
    }
    freeifaddrs(ifa);
}
lv_obj_t *ui_page_mqtt_get_btn_sub(void)     { return g_btn[0]; }
lv_obj_t *ui_page_mqtt_get_btn_pub(void)     { return g_btn[1]; }
lv_obj_t *ui_page_mqtt_get_btn_refresh(void) { return g_btn[2]; }
lv_obj_t *ui_page_mqtt_get_btn_clear(void)   { return g_btn[3]; }
