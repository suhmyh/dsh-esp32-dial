#pragma once
#include "lvgl.h"

#define XIAOLAN_IDLE_FRAMES 7
#define XIAOLAN_ACTIVE_FRAMES 5
#define XIAOLAN_WORKING_FRAMES 6
#define XIAOLAN_SUCCESS_FRAMES 4
#define XIAOLAN_FAILED_FRAMES 8
#define XIAOLAN_REST_FRAMES 6

#ifdef __cplusplus
extern "C" {
#endif
extern const lv_image_dsc_t xiaolan_idle[XIAOLAN_IDLE_FRAMES];
extern const lv_image_dsc_t xiaolan_active[XIAOLAN_ACTIVE_FRAMES];
extern const lv_image_dsc_t xiaolan_working[XIAOLAN_WORKING_FRAMES];
extern const lv_image_dsc_t xiaolan_success[XIAOLAN_SUCCESS_FRAMES];
extern const lv_image_dsc_t xiaolan_failed[XIAOLAN_FAILED_FRAMES];
extern const lv_image_dsc_t xiaolan_rest[XIAOLAN_REST_FRAMES];
#ifdef __cplusplus
}
#endif

