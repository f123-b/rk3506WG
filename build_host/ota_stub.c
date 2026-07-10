#include "ota_manager.h"
#include <stdbool.h>
#define APP_VERSION "3.0.0"
void ota_init(const char*s){(void)s;}
void ota_set_type(ota_type_t t){(void)t;}
void ota_set_app_install_path(const char*p){(void)p;}
void ota_cancel(void){}
ota_status_t ota_get_status(void){return 0;}
int ota_get_progress(void){return 0;}
const char *ota_get_last_error_msg(void){return "";}
bool ota_is_running(void){return false;}
void ota_reboot(void){}
bool ota_check_update(ota_version_info_t *i){(void)i;return false;}
bool ota_download_and_apply(void){return false;}
void ota_get_local_version(char *b,int l){if(b&&l>0)snprintf(b,l,"%s",APP_VERSION);}
