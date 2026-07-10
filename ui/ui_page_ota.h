/**
 * @file    ui_page_ota.h
 * @brief   OTA upgrade page — version info + progress + actions
 */
#ifndef UI_PAGE_OTA_H
#define UI_PAGE_OTA_H

#include <lvgl.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

lv_obj_t *ui_page_ota_create(lv_obj_t *parent);

/** Update OTA status text and progress bar */
void ui_page_ota_update(const char *status, int progress_pct);

/** Set version/server info */
void ui_page_ota_set_info(const char *local_ver, const char *server_url);

/** Get buttons for external event registration */
lv_obj_t *ui_page_ota_get_btn_check(void);
lv_obj_t *ui_page_ota_get_btn_start(void);

#ifdef __cplusplus
}
#endif
#endif
