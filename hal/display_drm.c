/**
 * @file    display_drm.c
 * @brief   DRM 显示驱动实现 (从 main.c 提取)
 *
 * 详细原理:
 *   1. 打开 DRM 设备: open("/dev/dri/card0") → 获取文件描述符 drm_fd
 *   2. 获取资源列表: drmModeGetResources(drm_fd) → 含 connector/crtc/encoder
 *   3. 遍历 connector: 找已连接且有显示模式 (mode) 的连接器
 *   4. 选择最佳模式: 按 480×800 竖屏匹配 (hdisplay < vdisplay)
 *   5. 创建 dumb buffer: DRM_IOCTL_MODE_CREATE_DUMB → 分配显存
 *   6. 添加 framebuffer: drmModeAddFB() → 绑定 dumb buffer 为 framebuffer
 *   7. 内存映射: drmIoctl(DRM_IOCTL_MODE_MAP_DUMB) + mmap() → 用户空间可写
 *   8. 设置显示: drmModeSetCrtc() → 激活显示输出
 *
 *   之后 LVGL 渲染到 mmap 映射的内存，显示控制器会自动扫描输出。
 */

#include "display_drm.h"
#include "../app_config.h"
#include "../infra/logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <drm/drm.h>
#include <drm/drm_mode.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <errno.h>

/* ==================== 模块内部状态 ==================== */
static int drm_fd = -1;
static uint32_t drm_crtc_id = 0;
static uint32_t drm_connector_id = 0;
static drmModeModeInfo drm_mode;
static uint32_t screen_width = 0;
static uint32_t screen_height = 0;
static uint32_t drm_fb_id = 0;
static void *drm_fb_map = NULL;
static size_t drm_fb_size = 0;

/* ==================== 公开 API ==================== */

int hal_display_init(void)
{
    const char *card_paths[] = {
        "/dev/dri/card0", "/dev/dri/card1",
        "/dev/dri/card2", "/dev/dri/card3"
    };
    int fd = -1;
    drmModeRes *res = NULL;
    drmModeConnector *conn = NULL;
    int i, j;

    /* 步骤1: 打开 DRM 设备 */
    for (i = 0; i < (int)(sizeof(card_paths)/sizeof(card_paths[0])); i++) {
        fd = open(card_paths[i], O_RDWR);
        if (fd >= 0) {
            res = drmModeGetResources(fd);
            if (res) break;
            close(fd);
            fd = -1;
        }
    }
    if (fd < 0 || !res) {
        LOG_ERROR("Cannot open any DRM device");
        return -1;
    }
    drm_fd = fd;

    /* 步骤2-4: 查找最佳显示模式和连接器 */
    for (i = 0; i < res->count_connectors; i++) {
        conn = drmModeGetConnector(drm_fd, res->connectors[i]);
        if (!conn || conn->connection != DRM_MODE_CONNECTED ||
            conn->count_modes == 0) {
            if (conn) drmModeFreeConnector(conn);
            conn = NULL;
            continue;
        }

        drmModeModeInfo best_mode = {0};
        int best_score = -1;
        for (j = 0; j < conn->count_modes; j++) {
            drmModeModeInfo *m = &conn->modes[j];
            int dx = abs((int)m->hdisplay - SCREEN_WIDTH);
            int dy = abs((int)m->vdisplay - SCREEN_HEIGHT);
            int score = dx + dy;
            /* 优先竖屏模式 */
            if (m->hdisplay < m->vdisplay) {
                if (best_score < 0 || score < best_score) {
                    best_score = score;
                    best_mode = *m;
                }
            }
        }
        if (best_score >= 0) {
            drm_connector_id = conn->connector_id;
            drm_mode = best_mode;
            screen_width = best_mode.hdisplay;
            screen_height = best_mode.vdisplay;
            drmModeFreeConnector(conn);
            break;
        }
        drmModeFreeConnector(conn);
        conn = NULL;
    }

    if (!drm_connector_id) {
        LOG_ERROR("No suitable display connector found");
        drmModeFreeResources(res);
        close(drm_fd);
        return -1;
    }

    if (res->count_crtcs > 0) {
        drm_crtc_id = res->crtcs[0];
    } else {
        LOG_ERROR("No CRTC available");
        drmModeFreeResources(res);
        close(drm_fd);
        return -1;
    }
    drmModeFreeResources(res);

    /* 步骤5: 创建 dumb buffer */
    struct drm_mode_create_dumb create = {0};
    create.width = screen_width;
    create.height = screen_height;
    create.bpp = 32;  /* 32位色深 (RGB8888) */
    create.flags = 0;
    if (drmIoctl(drm_fd, DRM_IOCTL_MODE_CREATE_DUMB, &create) < 0) {
        LOG_ERROR("DRM CREATE_DUMB failed: %s", strerror(errno));
        close(drm_fd);
        return -1;
    }
    drm_fb_size = create.size;

    /* 步骤6: 添加 framebuffer */
    if (drmModeAddFB(drm_fd, screen_width, screen_height, 32, 32,
                     create.pitch, create.handle, &drm_fb_id) < 0) {
        LOG_ERROR("DRM AddFB failed: %s", strerror(errno));
        close(drm_fd);
        return -1;
    }

    /* 步骤7: 内存映射 */
    struct drm_mode_map_dumb map = {0};
    map.handle = create.handle;
    if (drmIoctl(drm_fd, DRM_IOCTL_MODE_MAP_DUMB, &map) < 0) {
        LOG_ERROR("DRM MAP_DUMB failed: %s", strerror(errno));
        close(drm_fd);
        return -1;
    }
    drm_fb_map = mmap(NULL, drm_fb_size, PROT_READ | PROT_WRITE,
                      MAP_SHARED, drm_fd, map.offset);
    if (drm_fb_map == MAP_FAILED) {
        LOG_ERROR("mmap failed: %s", strerror(errno));
        close(drm_fd);
        return -1;
    }

    /* 清空 framebuffer (黑色背景) */
    memset(drm_fb_map, 0, drm_fb_size);

    /* 步骤8: 设置 CRTC 显示 */
    if (drmModeSetCrtc(drm_fd, drm_crtc_id, drm_fb_id, 0, 0,
                       &drm_connector_id, 1, &drm_mode) < 0) {
        LOG_ERROR("DRM SetCrtc failed: %s", strerror(errno));
        close(drm_fd);
        return -1;
    }

    /* 兜底: 如果探测失败, 使用默认分辨率 */
    if (screen_width == 0 || screen_height == 0) {
        screen_width = SCREEN_WIDTH;
        screen_height = SCREEN_HEIGHT;
    }

    LOG_INFO("DRM: %dx%d DIRECT mode initialized", screen_width, screen_height);
    return 0;
}

/* ==================== 访问器 ==================== */

void *hal_display_get_fb(void)
{
    return drm_fb_map;
}

size_t hal_display_get_fb_size(void)
{
    return drm_fb_size;
}

uint32_t hal_display_get_width(void)
{
    return screen_width;
}

uint32_t hal_display_get_height(void)
{
    return screen_height;
}

int hal_display_get_drm_fd(void)
{
    return drm_fd;
}
