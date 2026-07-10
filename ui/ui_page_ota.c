/**
 * @file    ui_page_ota.c — OTA upgrade page
 *
 * Layout: info card → status card → progress bar → buttons (bottom)
 */
#include "ui_page_ota.h"
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

static lv_obj_t *g_info, *g_status, *g_pbar;
static lv_obj_t *g_btn_check, *g_btn_start;

static lv_style_t st_card, st_bar_bg, st_bar_fill;
static lv_style_t st_btn[2];
static const uint32_t cb[2] = {0x3b82f6, 0x10b981};

static void init_styles(void)
{
    lv_style_init(&st_card);
    lv_style_set_bg_color(&st_card, lv_color_hex(0x1e293b));
    lv_style_set_radius(&st_card, 8);
    lv_style_set_border_width(&st_card, 1);
    lv_style_set_border_color(&st_card, lv_color_hex(0x334155));
    lv_style_set_pad_all(&st_card, 10);

    lv_style_init(&st_bar_bg);
    lv_style_set_bg_color(&st_bar_bg, lv_color_hex(0x0f172a));
    lv_style_set_radius(&st_bar_bg, 4);
    lv_style_set_border_width(&st_bar_bg, 0);

    lv_style_init(&st_bar_fill);
    lv_style_set_bg_color(&st_bar_fill, lv_color_hex(0x22c55e));
    lv_style_set_radius(&st_bar_fill, 4);
    lv_style_set_border_width(&st_bar_fill, 0);

    for (int i = 0; i < 2; i++) {
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

lv_obj_t *ui_page_ota_create(lv_obj_t *parent)
{
    init_styles();
    int y = MG;

    /* = Info card = */
    int ch = BTN_Y - y - GAP;  /* fill all remaining space */
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, PW, ch); lv_obj_set_pos(card, MG, y);
    lv_obj_add_style(card, &st_card, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    L(card, "OTA Upgrade", FN, 0x3b82f6);
    lv_obj_align(lv_obj_get_child(card, -1), LV_ALIGN_TOP_LEFT, 2, 4);

    g_info = L(card, "Version: --\nServer: --", FS, 0x94a3b8);
    lv_obj_align(g_info, LV_ALIGN_TOP_LEFT, 2, 28);

    L(card, "Status", FN, 0x64748b);
    lv_obj_align(lv_obj_get_child(card, -1), LV_ALIGN_TOP_LEFT, 2, 90);

    g_status = L(card, "Idle", FS, 0xcbd5e1);
    lv_obj_align(g_status, LV_ALIGN_TOP_LEFT, 2, 114);

    /* Progress bar */
    L(card, "Progress", FN, 0x64748b);
    lv_obj_align(lv_obj_get_child(card, -1), LV_ALIGN_TOP_LEFT, 2, 150);

    lv_obj_t *bbg = lv_obj_create(card);
    lv_obj_set_size(bbg, PW - 24, 22);
    lv_obj_align(bbg, LV_ALIGN_TOP_LEFT, 2, 174);
    lv_obj_add_style(bbg, &st_bar_bg, 0);
    lv_obj_clear_flag(bbg, LV_OBJ_FLAG_SCROLLABLE);

    g_pbar = lv_obj_create(bbg);
    lv_obj_set_size(g_pbar, 0, 22); lv_obj_set_pos(g_pbar, 0, 0);
    lv_obj_add_style(g_pbar, &st_bar_fill, 0);
    lv_obj_clear_flag(g_pbar, LV_OBJ_FLAG_SCROLLABLE);

    /* = Buttons (bottom) = */
    int bx = MG;
    g_btn_check = lv_btn_create(parent); lv_obj_set_size(g_btn_check, BTN_W, BTN_H); lv_obj_set_pos(g_btn_check, bx, BTN_Y); lv_obj_add_style(g_btn_check, &st_btn[0], 0); lv_obj_clear_flag(g_btn_check, LV_OBJ_FLAG_SCROLLABLE); { lv_obj_t *l = L(g_btn_check, "Check Update", FN, 0xffffff); lv_obj_center(l); }
    g_btn_start = lv_btn_create(parent); lv_obj_set_size(g_btn_start, BTN_W, BTN_H); lv_obj_set_pos(g_btn_start, bx+BTN_W+GAP, BTN_Y); lv_obj_add_style(g_btn_start, &st_btn[1], 0); lv_obj_clear_flag(g_btn_start, LV_OBJ_FLAG_SCROLLABLE); { lv_obj_t *l = L(g_btn_start, "Start Upgrade", FN, 0xffffff); lv_obj_center(l); }

    LOG_INFO("OTA page created");
    return parent;
}

void ui_page_ota_update(const char *status, int pct)
{
    if (g_status) lv_label_set_text(g_status, status ? status : "Idle");
    if (g_pbar) {
        if (pct < 0) pct = 0;
        if (pct > 100) pct = 100;
        lv_obj_set_size(g_pbar, pct * (PW - 24) / 100, 22);
    }
}

void ui_page_ota_set_info(const char *ver, const char *url)
{
    if (!g_info) return;
    char b[128];
    snprintf(b, sizeof(b), "Version: %s\nServer: %s", ver ? ver : "--", url ? url : "--");
    lv_label_set_text(g_info, b);
}

lv_obj_t *ui_page_ota_get_btn_check(void) { return g_btn_check; }
lv_obj_t *ui_page_ota_get_btn_start(void) { return g_btn_start; }
