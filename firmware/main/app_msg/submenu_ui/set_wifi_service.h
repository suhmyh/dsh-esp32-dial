#pragma once

#include "bsp/esp-bsp.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief WiFi状态枚举，对应系统状态栏的WifiIconState
 */
typedef enum {
    WIFI_STATE_DISCONNECTED = 0,  /* 未连接 */
    WIFI_STATE_SIGNAL_1     = 1,  /* 信号弱 */
    WIFI_STATE_SIGNAL_2     = 2,  /* 信号中 */
    WIFI_STATE_SIGNAL_3     = 3,  /* 信号强 */
} wifi_icon_state_t;

/**
 * @brief 系统状态栏WiFi图标更新回调函数类型
 * @param state WiFi状态，见 wifi_icon_state_t
 */
typedef void (*wifi_status_bar_cb_t)(int state);

/**
 * @brief 注册系统状态栏WiFi图标更新回调
 * @param cb 回调函数指针，传入NULL可取消注册
 */
void wifi_register_status_bar_callback(wifi_status_bar_cb_t cb);

void wifi_init_sta(void);
void init_wifi_service_screen(lv_obj_t *parent, lv_obj_t *ret_scr);

#ifdef __cplusplus
}
#endif
