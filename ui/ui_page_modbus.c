/**
 * @file    ui_page_modbus.c — minimalist Modbus/RS485 page
 *
 * ① Status bar (36px): RS485 info + LED
 * ② Device card (340px): slave info + register values + bar graphs + poll status
 * ③ Activity bars (80px): TX/RX counts + progress
 * ④ Optional test button (only when RS485_TEST_SEND_ENABLE is enabled)
 */
#include "ui_page_modbus.h"
#include "../app_config.h"
#include "../infra/logger.h"
#include <stdio.h>
#include <string.h>

#define PW      464
#define MG      8
#define GAP     12
#define STAT_H  36
#define BTN_H   42
#define BTN_W   ((PW - MG - GAP) / 2)
#define BTN_ROW (BTN_H * 2 + GAP)
#define PAGE_H  744
#define BTN_Y   (PAGE_H - MG - BTN_ROW)  /* 650 */

#define FS  (&lv_font_montserrat_12)
#define FN  (&lv_font_montserrat_16)
#define FB  (&lv_font_montserrat_24)

static lv_obj_t *g_slave, *g_reg0, *g_reg1, *g_poll;
static lv_obj_t *g_tx, *g_rx, *g_led;
static lv_obj_t *g_bar0, *g_bar1, *g_bar_tx, *g_bar_rx;
static lv_obj_t *g_btn[4];

static lv_style_t st_card, st_bbg, st_bt, st_bh, st_btx, st_brx;
static lv_style_t st_btn[4];
static const uint32_t cb[4] = {0x3b82f6, 0x8b5cf6, 0x10b981, 0xd97706};

static void init_styles(void)
{
    lv_style_init(&st_card);
    lv_style_set_bg_color(&st_card, lv_color_hex(0x1e293b));
    lv_style_set_radius(&st_card, 8);
    lv_style_set_border_width(&st_card, 1);
    lv_style_set_border_color(&st_card, lv_color_hex(0x334155));
    lv_style_set_pad_all(&st_card, 10);

    lv_style_init(&st_bbg);
    lv_style_set_bg_color(&st_bbg, lv_color_hex(0x0f172a));
    lv_style_set_radius(&st_bbg, 4);
    lv_style_set_border_width(&st_bbg, 0);

    lv_style_init(&st_bt); lv_style_set_bg_color(&st_bt, lv_color_hex(0xf59e0b)); lv_style_set_radius(&st_bt, 4); lv_style_set_border_width(&st_bt, 0);
    lv_style_init(&st_bh); lv_style_set_bg_color(&st_bh, lv_color_hex(0x06b6d4)); lv_style_set_radius(&st_bh, 4); lv_style_set_border_width(&st_bh, 0);
    lv_style_init(&st_btx);lv_style_set_bg_color(&st_btx,lv_color_hex(0x3b82f6));lv_style_set_radius(&st_btx,4);lv_style_set_border_width(&st_btx,0);
    lv_style_init(&st_brx);lv_style_set_bg_color(&st_brx,lv_color_hex(0x10b981));lv_style_set_radius(&st_brx,4);lv_style_set_border_width(&st_brx,0);

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

static void mkbar(lv_obj_t *p, int x, int y, int w, int h, lv_obj_t **f, lv_style_t *s)
{
    lv_obj_t *bg = lv_obj_create(p);
    lv_obj_set_size(bg, w, h); lv_obj_set_pos(bg, x, y);
    lv_obj_add_style(bg, &st_bbg, 0);
    lv_obj_set_style_pad_all(bg, 0, 0);  /* kill default LVGL padding */
    lv_obj_clear_flag(bg, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *fl = lv_obj_create(bg);
    lv_obj_set_size(fl, 0, h); lv_obj_set_pos(fl, 0, 0);
    lv_obj_add_style(fl, s, 0);
    lv_obj_set_style_pad_all(fl, 0, 0);
    lv_obj_clear_flag(fl, LV_OBJ_FLAG_SCROLLABLE);
    *f = fl;
}

lv_obj_t *ui_page_modbus_create(lv_obj_t *parent)
{
    init_styles();
    int y = MG;

    /* = ① Status bar = */
    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_set_size(bar, PW, STAT_H); lv_obj_set_pos(bar, MG, y);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x111827), 0);
    lv_obj_set_style_border_color(bar, lv_color_hex(0x334155), 0);
    lv_obj_set_style_border_width(bar, 1, 0);
    lv_obj_set_style_radius(bar, 8, 0);
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    g_led = lv_obj_create(bar);
    lv_obj_set_size(g_led, 8, 8); lv_obj_set_pos(g_led, 10, 14);
    lv_obj_set_style_bg_color(g_led, lv_color_hex(0xef4444), 0);
    lv_obj_set_style_radius(g_led, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(g_led, 0, 0);

    L(bar, "RS485  9600-8N1  ttyS3", FS, 0x94a3b8);
    lv_obj_align(lv_obj_get_child(bar, -1), LV_ALIGN_LEFT_MID, 24, 0);

    y += STAT_H + GAP;

    /* = ② Device card (expanded to 340px) = */
    int ch = BTN_Y - y - GAP - (80 + GAP); /* 650-52-12-92=494, but let's split between card and activity */
    /* Actually: total available = BTN_Y - y - GAP = 650-52-12=586px. Give 340 to card, 80 to activity, rest as gap */
    ch = 340;
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, PW, ch); lv_obj_set_pos(card, MG, y);
    lv_obj_add_style(card, &st_card, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    /* Title */
    L(card, "Slave Device #1", FN, 0x10b981);
    lv_obj_align(lv_obj_get_child(card, -1), LV_ALIGN_TOP_LEFT, 2, 4);

    /* Info line 1 */
    /* 3-line info (no separate labels — avoids ghosting on update) */
    g_slave = L(card, "ID: 1  |  Temp/Humi Sensor\n"
                      "Func: 03  |  Start: 0  |  Regs: 2\n"
                      "Status: Waiting...", FS, 0xcbd5e1);
    lv_obj_align(g_slave, LV_ALIGN_TOP_LEFT, 2, 28);

    /* Register value cards */
    int vy = 90, vw = (PW - 16 - GAP) / 2, vh = 64;
    lv_obj_t *v0 = lv_obj_create(card);
    lv_obj_set_size(v0, vw, vh); lv_obj_set_pos(v0, 2, vy);
    lv_obj_set_style_bg_color(v0, lv_color_hex(0x0f172a), 0);
    lv_obj_set_style_border_color(v0, lv_color_hex(0x78350f), 0);
    lv_obj_set_style_border_width(v0, 1, 0);
    lv_obj_set_style_radius(v0, 8, 0);
    lv_obj_set_style_pad_all(v0, 0, 0);
    lv_obj_clear_flag(v0, LV_OBJ_FLAG_SCROLLABLE);

    L(v0, "Temp", FS, 0xf59e0b);
    lv_obj_align(lv_obj_get_child(v0, -1), LV_ALIGN_TOP_MID, 0, 2);
    g_reg0 = L(v0, "--.-", FB, 0xfbbf24);
    lv_obj_align(g_reg0, LV_ALIGN_CENTER, -12, -6);
    L(v0, "C", FN, 0xf59e0b);
    lv_obj_align(lv_obj_get_child(v0, -1), LV_ALIGN_CENTER, 26, -4);

    lv_obj_t *v1 = lv_obj_create(card);
    lv_obj_set_size(v1, vw, vh); lv_obj_set_pos(v1, 2 + vw + GAP, vy);
    lv_obj_set_style_bg_color(v1, lv_color_hex(0x0f172a), 0);
    lv_obj_set_style_border_color(v1, lv_color_hex(0x164e63), 0);
    lv_obj_set_style_border_width(v1, 1, 0);
    lv_obj_set_style_radius(v1, 8, 0);
    lv_obj_set_style_pad_all(v1, 0, 0);
    lv_obj_clear_flag(v1, LV_OBJ_FLAG_SCROLLABLE);

    L(v1, "Humi", FS, 0x06b6d4);
    lv_obj_align(lv_obj_get_child(v1, -1), LV_ALIGN_TOP_MID, 0, 2);
    g_reg1 = L(v1, "--", FB, 0x22d3ee);
    lv_obj_align(g_reg1, LV_ALIGN_CENTER, -8, -6);
    L(v1, "%", FN, 0x06b6d4);
    lv_obj_align(lv_obj_get_child(v1, -1), LV_ALIGN_CENTER, 18, -4);

    /* Bar graphs with labels */
    int by = vy + vh + 18, bw = PW - 24 - 50;
    L(card, "Temp", FS, 0x64748b);
    lv_obj_set_pos(lv_obj_get_child(card, -1), 6, by + 10);
    mkbar(card, 46, by + 11, bw, 16, &g_bar0, &st_bt);

    int by2 = by + 44;
    L(card, "Humi", FS, 0x64748b);
    lv_obj_set_pos(lv_obj_get_child(card, -1), 6, by2 + 10);
    mkbar(card, 46, by2 + 11, bw, 16, &g_bar1, &st_bh);

    /* Polling status */
    g_poll = L(card, "Polling: Active", FS, 0x22c55e);
    lv_obj_align(g_poll, LV_ALIGN_BOTTOM_LEFT, 4, -4);

    y += ch + GAP;

    /* = ③ Activity bars = */
    int ah = BTN_Y - y - GAP;  /* fills remaining space to buttons */
    lv_obj_t *act = lv_obj_create(parent);
    lv_obj_set_size(act, PW, ah); lv_obj_set_pos(act, MG, y);
    lv_obj_add_style(act, &st_card, 0);
    lv_obj_clear_flag(act, LV_OBJ_FLAG_SCROLLABLE);

    L(act, "TX", FN, 0x3b82f6); lv_obj_set_pos(lv_obj_get_child(act, -1), 8, 10);
    g_tx = L(act, "0", FB, 0xfbbf24); lv_obj_set_pos(g_tx, 50, 6);
    mkbar(act, 110, 14, PW - 130, 14, &g_bar_tx, &st_btx);

    int rx_y = ah/2 + 8;
    L(act, "RX", FN, 0x10b981); lv_obj_set_pos(lv_obj_get_child(act, -1), 8, rx_y);
    g_rx = L(act, "0", FB, 0x22d3ee); lv_obj_set_pos(g_rx, 50, rx_y - 4);
    mkbar(act, 110, rx_y + 4, PW - 130, 14, &g_bar_rx, &st_brx);

    /* = ④ Optional test control = */
    int bx = MG;
#if RS485_TEST_SEND_ENABLE
    g_btn[0] = lv_btn_create(parent); lv_obj_set_size(g_btn[0], BTN_W, BTN_H); lv_obj_set_pos(g_btn[0], bx, BTN_Y); lv_obj_add_style(g_btn[0], &st_btn[0], 0); lv_obj_clear_flag(g_btn[0], LV_OBJ_FLAG_SCROLLABLE); { lv_obj_t *l = L(g_btn[0], "Send Test", FN, 0xffffff); lv_obj_center(l); }
#endif

    LOG_INFO("Modbus page created");
    return parent;
}

void ui_page_modbus_update_slave(int sid, const char *nm, int nr, const uint16_t *regs, bool valid)
{
    if (g_slave) {
        char b[128];
        if (valid) snprintf(b, sizeof(b), "ID: %d  |  %s\nFunc: 03  |  Start: 0  |  Regs: %d\nStatus: OK", sid, nm, nr);
        else snprintf(b, sizeof(b), "ID: %d  |  %s\nFunc: 03  |  Start: 0  |  Regs: %d\nStatus: Waiting...", sid, nm, nr);
        lv_label_set_text(g_slave, b);
    }
    if (regs && nr >= 1 && g_reg0) {
        float v = (float)((int16_t)regs[0]) / 10.0f;
        char b[16]; snprintf(b, sizeof(b), "%.1f", v); lv_label_set_text(g_reg0, b);
        if (g_bar0) { int p = (int)((v+10)/60*100); if(p<0)p=0; if(p>100)p=100; lv_obj_set_size(g_bar0, p*(PW-24-50)/100, 16); }
    }
    if (regs && nr >= 2 && g_reg1) {
        float v = (float)((int16_t)regs[1]) / 10.0f;
        char b[16]; snprintf(b, sizeof(b), "%.1f", v); lv_label_set_text(g_reg1, b);
        if (g_bar1) { int p = (int)v; if(p<0)p=0; if(p>100)p=100; lv_obj_set_size(g_bar1, p*(PW-24-50)/100, 16); }
    }
}

void ui_page_modbus_update_tx(int cnt, bool ok)
{
    if (g_tx) { char b[16]; snprintf(b, sizeof(b), "%d", cnt); lv_label_set_text(g_tx, b); }
    if (g_bar_tx) { int bw = PW-130; int w = cnt>100 ? bw : cnt*bw/100; lv_obj_set_size(g_bar_tx, w, 14); lv_obj_set_style_bg_color(g_bar_tx, ok?lv_color_hex(0x3b82f6):lv_color_hex(0xef4444),0); }
}

void ui_page_modbus_update_rx(int cnt, bool ok)
{
    if (g_rx) { char b[16]; snprintf(b, sizeof(b), "%d", cnt); lv_label_set_text(g_rx, b); }
    if (g_bar_rx) { int bw = PW-130; int w = cnt>100 ? bw : cnt*bw/100; lv_obj_set_size(g_bar_rx, w, 14); lv_obj_set_style_bg_color(g_bar_rx, ok?lv_color_hex(0x10b981):lv_color_hex(0xef4444),0); }
}

void ui_page_modbus_set_led(bool on) {
    if (g_led) lv_obj_set_style_bg_color(g_led, on?lv_color_hex(0x22c55e):lv_color_hex(0xef4444), 0);
}

void ui_page_modbus_set_poll_status(bool p, int iv)
{
    if (!g_poll) return;
    if (p) { char b[32]; snprintf(b, sizeof(b), "Polling: %dms", iv); lv_label_set_text(g_poll, b); lv_obj_set_style_text_color(g_poll, lv_color_hex(0x22c55e), 0); }
    else { lv_label_set_text(g_poll, "Polling: OFF"); lv_obj_set_style_text_color(g_poll, lv_color_hex(0xef4444), 0); }
}

lv_obj_t *ui_page_modbus_get_btn_send(void) { return g_btn[0]; }
