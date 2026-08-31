cmake_minimum_required(VERSION 3.10)
project(my_test C)
set(CMAKE_C_STANDARD 11)

# ==========================================
# HOST SIMULATION
# ==========================================
if(HOST_SIMULATION)
    message(STATUS "=== Building for HOST (SDL2 simulation) ===")
    set(CMAKE_C_COMPILER gcc)
    find_path(SDL2_INCLUDE_DIR SDL2/SDL.h)
    find_library(SDL2_LIBRARY NAMES SDL2)
    if(NOT SDL2_INCLUDE_DIR OR NOT SDL2_LIBRARY)
        message(FATAL_ERROR "HOST_SIMULATION requires SDL2 headers and library")
    endif()
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -O0 -g -DHOST_SIMULATION=1 -D_REENTRANT")

    include_directories(
        ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/hal
        ${CMAKE_CURRENT_SOURCE_DIR}/services ${CMAKE_CURRENT_SOURCE_DIR}/ui
        ${CMAKE_CURRENT_SOURCE_DIR}/web ${CMAKE_CURRENT_SOURCE_DIR}/storage
        ${CMAKE_CURRENT_SOURCE_DIR}/infra
        ${SDL2_INCLUDE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/lvgl_src ${CMAKE_CURRENT_SOURCE_DIR}/lvgl_src/src
    )

    # LVGL library from source
    file(GLOB_RECURSE LVGL_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/lvgl_src/src/*.c)
    list(FILTER LVGL_SOURCES EXCLUDE REGEX ".*/test/")
    list(FILTER LVGL_SOURCES EXCLUDE REGEX ".*/wayland/")
    list(FILTER LVGL_SOURCES EXCLUDE REGEX ".*/x11/")
    list(FILTER LVGL_SOURCES EXCLUDE REGEX ".*/windows/")
    list(FILTER LVGL_SOURCES EXCLUDE REGEX ".*/nuttx/")

    add_library(lvgl_host ${LVGL_SOURCES})
    target_include_directories(lvgl_host PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/lvgl_src
        ${CMAKE_CURRENT_SOURCE_DIR}/lvgl_src/src)
    target_compile_definitions(lvgl_host PRIVATE LV_CONF_INCLUDE_SIMPLE=1)

    # Copy host lv_conf
    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/lv_conf_host.h
                   ${CMAKE_CURRENT_SOURCE_DIR}/lvgl_src/lv_conf.h COPYONLY)

    # Generate stubs for hardware/network dependencies
    set(SDIR ${CMAKE_BINARY_DIR})
    file(WRITE ${SDIR}/ntp_stub.c  "#include \"ntp_sync.h\"\nint ntp_sync_init(void){return 0;}\nvoid ntp_sync_stop(void){}\n")
    file(WRITE ${SDIR}/web_stub.c  "#include \"web_server.h\"\nint web_server_start(int p){(void)p;return 0;}\nvoid web_server_stop(void){}\nvoid web_server_update_data(float t,float h,bool v){(void)t;(void)h;(void)v;}\n")
    file(WRITE ${SDIR}/wd_stub.c   "#include \"watchdog.h\"\nint watchdog_init(int t){(void)t;return 0;}\nvoid watchdog_stop(void){}\n")
    file(WRITE ${SDIR}/db_stub.c   "#include \"database.h\"\n#include <string.h>\nint database_init(void){return 0;}\nvoid database_close(void){}\nbool database_is_ready(void){return true;}\nint database_insert(float t,float h,bool v){(void)t;(void)h;(void)v;return 0;}\nint database_insert_records(const sensor_record_t *r,int n){(void)r;(void)n;return 0;}\nint database_cleanup(int d){(void)d;return 0;}\nint database_query_history(int h,sensor_record_t *r,int m){if(r)memset(r,0,m*sizeof(*r));return 0;}\nint database_get_stats(int h,sensor_stats_t *s){(void)h;if(s)memset(s,0,sizeof(*s));return 0;}\nint database_insert_device_data(const char *a,const char *b,const char *c,double d,const char *e,bool f){(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;return 0;}\n")
    file(WRITE ${SDIR}/dr_stub.c   "#include \"data_recorder.h\"\nint data_recorder_init(void){return 0;}\nvoid data_recorder_close(void){}\nvoid data_recorder_record(float t,float h,bool v){(void)t;(void)h;(void)v;}\nvoid data_recorder_flush(void){}\nint data_recorder_get_buffered_count(void){return 0;}\nvoid data_recorder_tick(void){}\nvoid data_recorder_cleanup(int d){(void)d;}\n")
    file(WRITE ${SDIR}/ota_stub.c  "#include \"ota_manager.h\"\n#include \"app_config.h\"\n#include <stdbool.h>\n#include <stdio.h>\nvoid ota_init(const char*s){(void)s;}\nvoid ota_set_type(ota_type_t t){(void)t;}\nvoid ota_set_app_install_path(const char*p){(void)p;}\nvoid ota_cancel(void){}\nota_status_t ota_get_status(void){return OTA_IDLE;}\nint ota_get_progress(void){return 0;}\nconst char *ota_get_last_error_msg(void){return \"\";}\nbool ota_is_running(void){return false;}\nvoid ota_reboot(void){}\nbool ota_check_update(ota_version_info_t *i){(void)i;return false;}\nbool ota_download_and_apply(void){return false;}\nvoid ota_get_local_version(char *b,int l){if(b&&l>0)snprintf(b,l,\"%s\",APP_VERSION);}\nbool ota_try_lock(void){return true;}\nvoid ota_unlock(void){}\nvoid ota_write_health_marker(void){}\nvoid ota_set_signature_verify_callback(ota_signature_verify_cb cb){(void)cb;}\nvoid ota_set_progress_callback(ota_progress_cb cb){(void)cb;}\n")
    file(WRITE ${SDIR}/cfg_stub.c  "#include \"config_file.h\"\n#include <stdlib.h>\nconfig_file_t *config_load(const char *p){(void)p;return NULL;}\nvoid config_free(config_file_t *c){(void)c;}\nconst char *config_get_str(config_file_t *c,const char *k,const char *d){(void)c;(void)k;return d;}\nint config_get_int(config_file_t *c,const char *k,int d){(void)c;(void)k;return d;}\n")
    file(WRITE ${SDIR}/api_stub.c  "")
    file(WRITE ${SDIR}/ui_ota_stub.c "#include \"ui_ota.h\"\n#include <lvgl.h>\nvoid ui_ota_poll(lv_timer_t *t){(void)t;}\nbool ui_ota_is_running(void){return false;}\nbool ui_ota_start(void){return false;}\n")

    add_executable(my_test_sim
        main.c sqlite3.c
        hal/display_sdl.c hal/rs485_uart_stub.c hal/can_socket_stub.c
        services/mqtt_client_stub.c services/data_bus.c
        services/modbus_master.c services/can_manager.c
        ui/ui_dashboard.c ui/ui_page_mqtt.c ui/ui_page_modbus.c ui/ui_page_can.c ui/ui_page_ota.c
        infra/logger.c infra/sha256.c
        ${SDIR}/ntp_stub.c ${SDIR}/web_stub.c ${SDIR}/wd_stub.c
        ${SDIR}/db_stub.c ${SDIR}/dr_stub.c ${SDIR}/ota_stub.c
        ${SDIR}/cfg_stub.c ${SDIR}/api_stub.c ${SDIR}/ui_ota_stub.c
    )
    target_link_libraries(my_test_sim lvgl_host ${SDL2_LIBRARY} pthread m)

# ==========================================
# ARM TARGET: cross-compile for RK3506
# ==========================================
else()
    message(STATUS "=== Building for ARM (RK3506) ===")
    if(NOT DEFINED SDK_ROOT)
        set(SDK_ROOT "/home/liziteng/rk3506_sdk/rk3506_linux6.1_sdk_v1.2.0_iot_evm")
    endif()
    if(NOT DEFINED SYSROOT)
        set(SYSROOT "${SDK_ROOT}/buildroot/output/rockchip_hd_rk3506g_evm_nand/host/arm-buildroot-linux-gnueabihf/sysroot")
    endif()
    set(CMAKE_SYSROOT ${SYSROOT})
    if(NOT DEFINED CMAKE_C_COMPILER)
        set(CMAKE_C_COMPILER ${SDK_ROOT}/buildroot/output/rockchip_hd_rk3506g_evm_nand/host/bin/arm-buildroot-linux-gnueabihf-gcc)
    endif()
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -O2 -march=armv7-a -mfloat-abi=hard -mfpu=neon --sysroot=${SYSROOT}")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} --sysroot=${SYSROOT} -lpthread -lm")

    include_directories(
        ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/hal
        ${CMAKE_CURRENT_SOURCE_DIR}/services ${CMAKE_CURRENT_SOURCE_DIR}/ui
        ${CMAKE_CURRENT_SOURCE_DIR}/web ${CMAKE_CURRENT_SOURCE_DIR}/storage
        ${CMAKE_CURRENT_SOURCE_DIR}/infra
        ${SYSROOT}/usr/include ${SYSROOT}/usr/include/lvgl
        ${SYSROOT}/usr/include/drm ${SYSROOT}/usr/include/cjson
        ${SYSROOT}/usr/include/mosquitto
    )
    link_directories(${SYSROOT}/usr/lib)

    add_executable(my_test
        main.c sqlite3.c
        hal/display_drm.c hal/touch_evdev.c hal/rs485_uart.c hal/can_socket.c
        services/mqtt_client.c services/data_recorder.c services/data_bus.c
        services/modbus_master.c services/can_manager.c
        ui/ui_dashboard.c ui/ui_ota.c
        ui/ui_page_mqtt.c ui/ui_page_modbus.c ui/ui_page_can.c ui/ui_page_ota.c
        infra/logger.c infra/watchdog.c infra/sha256.c
        ntp_sync.c ota_manager.c web_server.c
        web/api_system.c web/api_ota_web.c web/api_device.c web/api_status.c
        database.c storage/config_file.c
    )
    target_link_libraries(my_test
        -Wl,--start-group lvgl mosquitto pthread m drm rkadk cjson rga png freetype z jpeg rockit -Wl,--end-group)
endif()
