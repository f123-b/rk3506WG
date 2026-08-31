/**
 * @file    ui_page_can.c — minimalist CAN Bus page
 *
 * ① Status bar (36px): CAN info + LED
 * ② TX/RX stats card: frame counts + progress bars
 * ③ Signal gauge card: signal name + big value + bar
 * ④ Last frame card: latest RX frame data
 * ⑤ Optional test controls and listen switch
 */
#include "ui_page_can.h"
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
#define FB  (&lv_font_montserrat_32)

static lv_obj_t *g_tx, *g_rx, *g_sig, *g_sigv, *g_frame_tx, *g_frame_rx, *g_led;
static lv_obj_t *g_bar_tx, *g_bar_rx, *g_bar_sig;
static lv_obj_t *g_btn[4];
static int g_listen = 1;

static lv_style_t st_card, st_bbg, st_btx, st_brx, st_bsig;
static lv_style_t st_btn[4];
static const uint32_t cb[4] = {0x10b981, 0x3b82f6, 0x8b5cf6, 0xef4444};

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

    lv_style_init(&st_btx); lv_style_set_bg_color(&st_btx,lv_color_hex(0x3b82f6)); lv_style_set_radius(&st_btx,4); lv_style_set_border_width(&st_btx,0);
    lv_style_init(&st_brx); lv_style_set_bg_color(&st_brx,lv_color_hex(0x10b981)); lv_style_set_radius(&st_brx,4); lv_style_set_border_width(&st_brx,0);
    lv_style_init(&st_bsig);lv_style_set_bg_color(&st_bsig,lv_color_hex(0xf59e0b));lv_style_set_radius(&st_bsig,4);lv_style_set_border_width(&st_bsig,0);

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

lv_obj_t *ui_page_can_create(lv_obj_t *parent)
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

    L(bar, "CAN  can0  500kbps", FS, 0x94a3b8);
    lv_obj_align(lv_obj_get_child(bar, -1), LV_ALIGN_LEFT_MID, 24, 0);

    y += STAT_H + GAP;

    /* Space budget: 650(BTN_Y) - 52(y after status) - 12(GAP before btn) = 586px
     * Split: stats(140) + signal(220) + frame(80) + gaps(2*12) = 464, remaining 122 spread */
    int total = BTN_Y - y - GAP;
    int stats_h = total * 20 / 100;   /* ~117 → 120 */
    int sig_h   = total * 50 / 100;   /* ~293 → 280 */
    int frame_h = total - stats_h - sig_h - GAP*2; /* remainder */

    /* = ② TX/RX Stats = */
    lv_obj_t *sc = lv_obj_create(parent);
    lv_obj_set_size(sc, PW, stats_h); lv_obj_set_pos(sc, MG, y);
    lv_obj_add_style(sc, &st_card, 0);
    lv_obj_clear_flag(sc, LV_OBJ_FLAG_SCROLLABLE);

    L(sc, "TX/RX Statistics", FN, 0x3b82f6);
    lv_obj_align(lv_obj_get_child(sc, -1), LV_ALIGN_TOP_LEFT, 2, 4);

    L(sc, "TX", FN, 0x3b82f6); lv_obj_set_pos(lv_obj_get_child(sc, -1), 6, 32);
    g_tx = L(sc, "0", FB, 0xfbbf24); lv_obj_set_pos(g_tx, 46, 24);
    mkbar(sc, 120, 36, PW - 140, 14, &g_bar_tx, &st_btx);

    int half = stats_h / 2;
    L(sc, "RX", FN, 0x10b981); lv_obj_set_pos(lv_obj_get_child(sc, -1), 6, half + 10);
    g_rx = L(sc, "0", FB, 0x22d3ee); lv_obj_set_pos(g_rx, 46, half + 2);
    mkbar(sc, 120, half + 14, PW - 140, 14, &g_bar_rx, &st_brx);

    y += stats_h + GAP;

    /* = ③ Signal gauge = */
    lv_obj_t *sigc = lv_obj_create(parent);
    lv_obj_set_size(sigc, PW, sig_h); lv_obj_set_pos(sigc, MG, y);
    lv_obj_add_style(sigc, &st_card, 0);
    lv_obj_clear_flag(sigc, LV_OBJ_FLAG_SCROLLABLE);

    g_sig = L(sigc, "Engine RPM", FN, 0xf59e0b);
    lv_obj_align(g_sig, LV_ALIGN_TOP_LEFT, 2, 4);

    g_sigv = L(sigc, "----", FB, 0xfde68a);
    lv_obj_align(g_sigv, LV_ALIGN_CENTER, 0, -16);
    L(sigc, "rpm", FN, 0xf59e0b);
    lv_obj_align(lv_obj_get_child(sigc, -1), LV_ALIGN_CENTER, 0, 18);

    /* Bar + range labels: move bar up to leave room for labels below */
    int bar_h = 20;
    int bar_y = sig_h - 54;       /* was sig_h-40, give more bottom space for labels */
    int bar_w = PW - 32;
    mkbar(sigc, 6, bar_y, bar_w, bar_h, &g_bar_sig, &st_bsig);

    /* range labels — now well clear of bar bottom */
    L(sigc, "0", FS, 0x64748b);
    lv_obj_set_pos(lv_obj_get_child(sigc, -1), 6, bar_y + bar_h + 2);
    L(sigc, "8000", FS, 0x64748b);
    lv_obj_set_pos(lv_obj_get_child(sigc, -1), bar_w - 22, bar_y + bar_h + 2);

    y += sig_h + GAP;

    /* = ④ Last Frame = */
    lv_obj_t *fc = lv_obj_create(parent);
    lv_obj_set_size(fc, PW, frame_h); lv_obj_set_pos(fc, MG, y);
    lv_obj_add_style(fc, &st_card, 0);
    lv_obj_clear_flag(fc, LV_OBJ_FLAG_SCROLLABLE);

    L(fc, "Last Frame", FN, 0x06b6d4);
    lv_obj_align(lv_obj_get_child(fc, -1), LV_ALIGN_TOP_LEFT, 2, 4);

    g_frame_tx = L(fc, "TX: ----", FS, 0xf59e0b);
    lv_obj_align(g_frame_tx, LV_ALIGN_TOP_LEFT, 2, 26);

    g_frame_rx = L(fc, "RX: ---- (waiting...)", FS, 0x22d3ee);
    lv_obj_align(g_frame_rx, LV_ALIGN_TOP_LEFT, 2, 44);

    /* = ⑤ Real controls; test controls are compiled only when enabled = */
    int bx = MG;
#if CAN_TEST_SEND_ENABLE
    g_btn[0] = lv_btn_create(parent); lv_obj_set_size(g_btn[0], BTN_W, BTN_H); lv_obj_set_pos(g_btn[0], bx, BTN_Y); lv_obj_add_style(g_btn[0], &st_btn[0], 0); lv_obj_clear_flag(g_btn[0], LV_OBJ_FLAG_SCROLLABLE); { lv_obj_t *l = L(g_btn[0], "Send Frame", FN, 0xffffff); lv_obj_center(l); }
#endif
    g_btn[1] = lv_btn_create(parent); lv_obj_set_size(g_btn[1], BTN_W, BTN_H); lv_obj_set_pos(g_btn[1], bx+BTN_W+GAP, BTN_Y); lv_obj_add_style(g_btn[1], &st_btn[1], 0); lv_obj_clear_flag(g_btn[1], LV_OBJ_FLAG_SCROLLABLE); { lv_obj_t *l = L(g_btn[1], "Listen:ON", FN, 0xffffff); lv_obj_center(l); }
#if CAN_TEST_SEND_ENABLE
    g_btn[3] = lv_btn_create(parent); lv_obj_set_size(g_btn[3], BTN_W, BTN_H); lv_obj_set_pos(g_btn[3], bx+BTN_W+GAP, BTN_Y+BTN_H+GAP); lv_obj_add_style(g_btn[3], &st_btn[3], 0); lv_obj_clear_flag(g_btn[3], LV_OBJ_FLAG_SCROLLABLE); { lv_obj_t *l = L(g_btn[3], "Clear Cnt", FN, 0xffffff); lv_obj_center(l); }
#endif

    LOG_INFO("CAN page created");
    return parent;
}

void ui_page_can_update(uint32_t can_id, int tx, int rx, bool ok)
{
    (void)can_id; (void)ok;
    if (g_tx) { char b[16]; snprintf(b, sizeof(b), "%d", tx); lv_label_set_text(g_tx, b); }
    if (g_rx) { char b[16]; snprintf(b, sizeof(b), "%d", rx); lv_label_set_text(g_rx, b); }
    /* 进度条: 百分比法, 不受容器宽度影响 */
    if (g_bar_tx) { int bw = PW-140; int w = tx>100 ? bw : tx*bw/100; lv_obj_set_size(g_bar_tx, w, 14); }
    if (g_bar_rx) { int bw = PW-140; int w = rx>100 ? bw : rx*bw/100; lv_obj_set_size(g_bar_rx, w, 14); }
}

void ui_page_can_update_signal(const char *nm, float v, const char *u, float vmin, float vmax)
{
    if (g_sig) { char b[64]; snprintf(b, sizeof(b), "%s", nm); lv_label_set_text(g_sig, b); }
    if (g_sigv) { char b[32]; snprintf(b, sizeof(b), v>=1000?"%.0f":"%.1f", v); lv_label_set_text(g_sigv, b); }
    if (g_bar_sig) {
        float r = vmax - vmin;
        int pct = r>0 ? (int)((v-vmin)/r*100) : 0;
        if (pct<0) pct=0; if (pct>100) pct=100;
        int bar_w = PW - 32;  /* 与创建时一致 */
        lv_obj_set_size(g_bar_sig, pct*bar_w/100, 20);
    }
}

void ui_page_can_update_tx_frame(uint32_t can_id, uint8_t dlc, const uint8_t *data)
{
    if (!g_frame_tx || !data) return;
    char b[96]; int off = snprintf(b, sizeof(b), "TX: ID=0x%03X DLC=%d [", can_id, dlc);
    for (int i = 0; i < dlc && i < 8; i++) off += snprintf(b+off, sizeof(b)-off, "%02X ", data[i]);
    snprintf(b+off, sizeof(b)-off, "]");
    lv_label_set_text(g_frame_tx, b);
}

void ui_page_can_update_rx_frame(uint32_t can_id, uint8_t dlc, const uint8_t *data)
{
    if (!g_frame_rx || !data) return;
    char b[96]; int off = snprintf(b, sizeof(b), "RX: ID=0x%03X DLC=%d [", can_id, dlc);
    for (int i = 0; i < dlc && i < 8; i++) off += snprintf(b+off, sizeof(b)-off, "%02X ", data[i]);
    snprintf(b+off, sizeof(b)-off, "]");
    lv_label_set_text(g_frame_rx, b);
}

void ui_page_can_set_led(bool on) {
    if (g_led) lv_obj_set_style_bg_color(g_led, on?lv_color_hex(0x22c55e):lv_color_hex(0xef4444), 0);
}

void ui_page_can_toggle_listen(void) {
    g_listen = !g_listen;
    if (g_btn[1]) { lv_obj_t *l = lv_obj_get_child(g_btn[1], 0); if (l) lv_label_set_text(l, g_listen?"Listen:ON":"Listen:OFF"); }
}

int  ui_page_can_get_listen_state(void)     { return g_listen; }
lv_obj_t *ui_page_can_get_btn_send(void)   { return g_btn[0]; }
lv_obj_t *ui_page_can_get_btn_listen(void) { return g_btn[1]; }
lv_obj_t *ui_page_can_get_btn_clear(void)  { return g_btn[3]; }
lv_obj_t *ui_page_can_get_test_btn(void)   { return g_btn[0]; }
