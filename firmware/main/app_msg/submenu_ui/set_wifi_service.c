#include "set_wifi_service.h"
#include <stdio.h>  
#include <string.h>
#include <stdlib.h>

#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"

#include "set_lv_style.h"

static lv_obj_t *return_screen = NULL;
static lv_obj_t *wifi_screen = NULL;
static lv_obj_t *list1 = NULL;
static lv_obj_t *back_btn = NULL;     
static lv_obj_t *scan_btn  = NULL; 
static lv_obj_t *spinner  = NULL; 
static lv_obj_t *status_bar = NULL;

static const char *TAG = "wifi_scan";
static EventGroupHandle_t s_wifi_event_group;
static void wifi_scan_ui_task(void *arg);

/* 系统状态栏WiFi图标更新回调 */
static wifi_status_bar_cb_t s_status_bar_cb = NULL;

void wifi_register_status_bar_callback(wifi_status_bar_cb_t cb)
{
    s_status_bar_cb = cb;
}

/**
 * @brief 根据RSSI值计算WiFi信号强度等级
 */
static int rssi_to_signal_level(int rssi)
{
    if (rssi >= -60) {
        return WIFI_STATE_SIGNAL_3;  /* 信号强 */
    } else if (rssi >= -70) {
        return WIFI_STATE_SIGNAL_2;  /* 信号中 */
    } else if (rssi >= -80) {
        return WIFI_STATE_SIGNAL_1;  /* 信号弱 */
    }
    return WIFI_STATE_DISCONNECTED;
}

/* ---------- 密码弹窗 ---------- */
static lv_obj_t *pw_dialog = NULL;
static lv_obj_t *pw_kb = NULL;
static lv_obj_t *pw_textarea = NULL;
static lv_obj_t *pw_status_label = NULL;
static char g_target_ssid[33] = {0};
static bool g_is_connected = false;

#define WIFI_START_SCAN      BIT0
#define WIFI_CONNECTED_BIT   BIT1
#define WIFI_DISCONNECT_BIT  BIT2


static void back_btn_click_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        if (return_screen) {
            lv_screen_load(return_screen);
        }
    }
}

/* 清理 AP 按钮上绑定的堆数据 */
static void cleanup_ap_list_data(void)
{
    if (list1 == NULL) return;
    uint32_t cnt = lv_obj_get_child_cnt(list1);
    for (uint32_t i = 0; i < cnt; i++) {
        lv_obj_t *child = lv_obj_get_child(list1, i);
        void *data = lv_obj_get_user_data(child);
        if (data) {
            free(data);
            lv_obj_set_user_data(child, NULL);
        }
    }
}

static void scan_wifi_btn_click_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        cleanup_ap_list_data();
        lv_obj_clean(list1);
        xEventGroupSetBits(s_wifi_event_group, WIFI_START_SCAN);
        lv_obj_remove_flag(spinner, LV_OBJ_FLAG_HIDDEN);
    }
}

/* ===================== 密码弹窗 ===================== */

static void hide_password_dialog(void)
{
    if (pw_kb) { lv_obj_delete(pw_kb); pw_kb = NULL; }
    if (pw_dialog) { lv_obj_delete(pw_dialog); pw_dialog = NULL; }
    pw_textarea = NULL;
    pw_status_label = NULL;
}

static void password_cancel_btn_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) hide_password_dialog();
}

static void password_connect_btn_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    const char *password = lv_textarea_get_text(pw_textarea);
    if (strlen(password) == 0) {
        lv_label_set_text(pw_status_label, "Enter password!");
        return;
    }
    lv_label_set_text(pw_status_label, "Connecting...");

    wifi_config_t wifi_config = {0};
    strncpy((char *)wifi_config.sta.ssid, g_target_ssid, sizeof(wifi_config.sta.ssid));
    wifi_config.sta.ssid[sizeof(wifi_config.sta.ssid) - 1] = '\0';
    strncpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password));
    wifi_config.sta.password[sizeof(wifi_config.sta.password) - 1] = '\0';

    ESP_LOGI(TAG, "Connecting to SSID: %s", g_target_ssid);
    ESP_ERROR_CHECK(esp_wifi_disconnect());
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    esp_err_t ret = esp_wifi_connect();

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_connect failed: %s", esp_err_to_name(ret));
        lv_label_set_text(pw_status_label, "Connect failed!");
    }
}

static void pw_ta_kb_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *ta = lv_event_get_target_obj(e);
    if (code == LV_EVENT_FOCUSED) {
        lv_keyboard_set_textarea(pw_kb, ta);
        lv_obj_remove_flag(pw_kb, LV_OBJ_FLAG_HIDDEN);
        // Move dialog upward to make room for keyboard on small round display
        lv_obj_align(pw_dialog, LV_ALIGN_TOP_MID, 0, 5);
    } else if (code == LV_EVENT_DEFOCUSED) {
        lv_keyboard_set_textarea(pw_kb, NULL);
        lv_obj_add_flag(pw_kb, LV_OBJ_FLAG_HIDDEN);
        // Restore dialog to center
        lv_obj_center(pw_dialog);
    }
}

static void show_password_dialog(const char *ssid)
{
    hide_password_dialog();
    strncpy(g_target_ssid, ssid, sizeof(g_target_ssid) - 1);
    g_target_ssid[sizeof(g_target_ssid) - 1] = '\0';
    lv_obj_t *active_screen = lv_screen_active();

    pw_dialog = lv_obj_create(active_screen);
    lv_obj_set_size(pw_dialog, 260, 190);
    lv_obj_center(pw_dialog);
    lv_obj_set_style_bg_color(pw_dialog, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_radius(pw_dialog, 16, LV_PART_MAIN);
    lv_obj_set_style_border_width(pw_dialog, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(pw_dialog, lv_color_hex(0xE5E7EB), LV_PART_MAIN);
    lv_obj_set_style_shadow_width(pw_dialog, 8, LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(pw_dialog, LV_OPA_30, LV_PART_MAIN);
    lv_obj_set_style_shadow_offset_y(pw_dialog, 4, LV_PART_MAIN);

    lv_obj_t *ssid_label = lv_label_create(pw_dialog);
    lv_label_set_text(ssid_label, "Connect to:");
    lv_obj_set_style_text_font(ssid_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(ssid_label, lv_color_hex(0x6C757D), 0);
    lv_obj_align(ssid_label, LV_ALIGN_TOP_MID, 0, 10);

    lv_obj_t *ssid_name = lv_label_create(pw_dialog);
    char buf[64];
    snprintf(buf, sizeof(buf), "\"%s\"", ssid);
    lv_label_set_text(ssid_name, buf);
    lv_obj_set_style_text_font(ssid_name, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(ssid_name, lv_color_hex(0x0D6EFD), 0);
    lv_obj_align(ssid_name, LV_ALIGN_TOP_MID, 0, 28);

    pw_textarea = lv_textarea_create(pw_dialog);
    lv_textarea_set_one_line(pw_textarea, true);
    lv_textarea_set_password_mode(pw_textarea, true);
    lv_textarea_set_placeholder_text(pw_textarea, "  Enter password...");
    lv_textarea_set_max_length(pw_textarea, 63);
    lv_obj_set_size(pw_textarea, 220, 38);
    lv_obj_align(pw_textarea, LV_ALIGN_TOP_MID, 0, 65);
    lv_obj_set_style_radius(pw_textarea, 8, LV_PART_MAIN);
    lv_obj_set_style_border_width(pw_textarea, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(pw_textarea, lv_color_hex(0xD1D5DB), LV_PART_MAIN);
    lv_obj_set_style_bg_color(pw_textarea, lv_color_hex(0xF9FAFB), LV_PART_MAIN);
    lv_obj_set_style_text_font(pw_textarea, &lv_font_montserrat_18, LV_PART_MAIN);

    pw_status_label = lv_label_create(pw_dialog);
    lv_label_set_text(pw_status_label, "");
    lv_obj_set_style_text_font(pw_status_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(pw_status_label, lv_color_hex(0x6C757D), 0);
    lv_obj_align(pw_status_label, LV_ALIGN_BOTTOM_MID, 0, -38);

    lv_obj_t *btn_cont = lv_obj_create(pw_dialog);
    lv_obj_set_size(btn_cont, 240, 36);
    lv_obj_align(btn_cont, LV_ALIGN_BOTTOM_MID, 0, -6);
    lv_obj_set_style_bg_opa(btn_cont, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn_cont, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(btn_cont, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(btn_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_cont, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *cancel_btn = lv_button_create(btn_cont);
    lv_obj_set_size(cancel_btn, 105, 34);
    lv_obj_set_style_radius(cancel_btn, 17, LV_PART_MAIN);
    lv_obj_set_style_bg_color(cancel_btn, lv_color_hex(0xF3F4F6), LV_PART_MAIN);
    lv_obj_add_event_cb(cancel_btn, password_cancel_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *cancel_lbl = lv_label_create(cancel_btn);
    lv_label_set_text(cancel_lbl, "Cancel");
    lv_obj_set_style_text_font(cancel_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(cancel_lbl, lv_color_hex(0x374151), 0);
    lv_obj_center(cancel_lbl);

    lv_obj_t *connect_btn = lv_button_create(btn_cont);
    lv_obj_set_size(connect_btn, 105, 34);
    lv_obj_set_style_radius(connect_btn, 17, LV_PART_MAIN);
    lv_obj_set_style_bg_color(connect_btn, lv_color_hex(0x0D6EFD), LV_PART_MAIN);
    lv_obj_add_event_cb(connect_btn, password_connect_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *connect_lbl = lv_label_create(connect_btn);
    lv_label_set_text(connect_lbl, "Connect");
    lv_obj_set_style_text_font(connect_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(connect_lbl, lv_color_white(), 0);
    lv_obj_center(connect_lbl);

    pw_kb = lv_keyboard_create(active_screen);
    lv_obj_set_size(pw_kb, 280, 120);
    lv_obj_align(pw_kb, LV_ALIGN_BOTTOM_MID, 0, -2);
    lv_obj_add_flag(pw_kb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(pw_textarea, pw_ta_kb_event_cb, LV_EVENT_ALL, NULL);
    lv_obj_move_foreground(pw_dialog);
}

/* ===================== AP 点击 → 弹出密码框 ===================== */

typedef struct {
    wifi_ap_record_t ap;
} ap_btn_data_t;

static void ap_btn_click_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        lv_obj_t *btn = lv_event_get_target_obj(e);
        ap_btn_data_t *data = (ap_btn_data_t *)lv_obj_get_user_data(btn);
        if (data) {
            ESP_LOGI(TAG, "Tapped AP: %s (RSSI:%d CH:%d AUTH:%d)",
                     data->ap.ssid, data->ap.rssi,
                     data->ap.primary, data->ap.authmode);
            bsp_display_lock(-1);
            show_password_dialog((const char *)data->ap.ssid);
            bsp_display_unlock();
        }
    }
}

/* ===================== 状态栏刷新 ===================== */

static void update_status_bar(void)
{
    if (status_bar == NULL) return;
    if (g_is_connected) {
        wifi_ap_record_t ap_info;
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
            esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
            if (netif) {
                esp_netif_ip_info_t ip_info;
                if (esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
                    char buf[72];
                    snprintf(buf, sizeof(buf), LV_SYMBOL_WIFI "  %s  " IPSTR,
                             (char *)ap_info.ssid, IP2STR(&ip_info.ip));
                    lv_label_set_text(status_bar, buf);
                    lv_obj_set_style_text_color(status_bar, lv_color_hex(0x198754), 0);
                    return;
                }
            }
            lv_label_set_text_fmt(status_bar, LV_SYMBOL_WIFI "  %s", (char *)ap_info.ssid);
            lv_obj_set_style_text_color(status_bar, lv_color_hex(0x198754), 0);
        }
    } else {
        lv_label_set_text(status_bar, LV_SYMBOL_WIFI "  Not connected");
        lv_obj_set_style_text_color(status_bar, lv_color_hex(0x6C757D), 0);
    }
}

/* ===================== WiFi 事件回调（增强版） ===================== */

static void event_handler(void *arg, esp_event_base_t event_base,
                           int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "Wi-Fi started");
        g_is_connected = false;
        bsp_display_lock(-1);
        update_status_bar();
        bsp_display_unlock();
        if (s_status_bar_cb) {
            s_status_bar_cb(WIFI_STATE_DISCONNECTED);
        }

    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED) {
        wifi_event_sta_connected_t *evt = (wifi_event_sta_connected_t *)event_data;
        ESP_LOGI(TAG, "Connected! SSID:%s CH:%d", evt->ssid, evt->channel);
        bsp_display_lock(-1);
        if (pw_status_label) {
            lv_label_set_text(pw_status_label, "Connected! Getting IP...");
            lv_obj_set_style_text_color(pw_status_label, lv_color_hex(0x198754), 0);
        }
        bsp_display_unlock();
        /* 连接成功但尚未获取IP，暂不更新系统状态栏（等获取IP后再更新信号强度） */

    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *evt = (wifi_event_sta_disconnected_t *)event_data;
        ESP_LOGI(TAG, "Disconnected, reason: %d", evt->reason);
        g_is_connected = false;
        bsp_display_lock(-1);
        if (pw_status_label) {
            switch (evt->reason) {
                case WIFI_REASON_AUTH_FAIL:
                    lv_label_set_text(pw_status_label, "Wrong password!");
                    lv_obj_set_style_text_color(pw_status_label, lv_color_hex(0xDC3545), 0);
                    break;
                case WIFI_REASON_NO_AP_FOUND:
                    lv_label_set_text(pw_status_label, "AP not found!");
                    lv_obj_set_style_text_color(pw_status_label, lv_color_hex(0xDC3545), 0);
                    break;
                case WIFI_REASON_ASSOC_LEAVE:
                    lv_label_set_text(pw_status_label, "Disconnected");
                    lv_obj_set_style_text_color(pw_status_label, lv_color_hex(0x6C757D), 0);
                    break;
                default:
                    lv_label_set_text_fmt(pw_status_label, "Disconnected (%d)", evt->reason);
                    lv_obj_set_style_text_color(pw_status_label, lv_color_hex(0xDC3545), 0);
                    break;
            }
        }
        update_status_bar();
        bsp_display_unlock();
        if (s_status_bar_cb) {
            s_status_bar_cb(WIFI_STATE_DISCONNECTED);
        }

    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *evt = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&evt->ip_info.ip));
        g_is_connected = true;
        bsp_display_lock(-1);
        hide_password_dialog();
        update_status_bar();
        bsp_display_unlock();
        /* 获取信号强度并更新系统状态栏 */
        if (s_status_bar_cb) {
            wifi_ap_record_t ap_info;
            if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
                int level = rssi_to_signal_level(ap_info.rssi);
                s_status_bar_cb(level);
            } else {
                s_status_bar_cb(WIFI_STATE_SIGNAL_1);  /* 连接成功但无法获取RSSI，默认显示信号1 */
            }
        }
    }
}

/* ===================== WiFi 初始化 ===================== */

void wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t inst_any;
    esp_event_handler_instance_t inst_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, &inst_any));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, &inst_ip));

    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_FLASH));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_init_sta finished.");

    xTaskCreate(wifi_scan_ui_task, "wifi_scan_ui_task", 4096, NULL, 5, NULL);
}

/* ===================== WiFi 扫描 ===================== */

static void do_wifi_scan(void)
{
    ESP_LOGI(TAG, "Starting Wi-Fi scan...");

    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = true,
    };

    ESP_ERROR_CHECK(esp_wifi_scan_start(&scan_config, true));

    uint16_t ap_count = 0;
    esp_wifi_scan_get_ap_num(&ap_count);
    ESP_LOGI(TAG, "Found %d networks", ap_count);

    if (ap_count == 0) {
        bsp_display_lock(-1);
        lv_obj_add_flag(spinner, LV_OBJ_FLAG_HIDDEN);
        bsp_display_unlock();
        return;
    }

    wifi_ap_record_t *ap_list = malloc(sizeof(wifi_ap_record_t) * ap_count);
    if (ap_list == NULL) {
        ESP_LOGE(TAG, "malloc failed for AP list");
        return;
    }

    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&ap_count, ap_list));

    bsp_display_lock(-1);

    for (int i = 0; i < ap_count; i++) {
        const char *auth_str = "";
        switch (ap_list[i].authmode) {
            case WIFI_AUTH_OPEN:          auth_str = "[OPEN]";      break;
            case WIFI_AUTH_WEP:           auth_str = "[WEP]";       break;
            case WIFI_AUTH_WPA_PSK:       auth_str = "[WPA]";       break;
            case WIFI_AUTH_WPA2_PSK:      auth_str = "[WPA2]";      break;
            case WIFI_AUTH_WPA_WPA2_PSK:  auth_str = "[WPA/WPA2]";  break;
            case WIFI_AUTH_WPA3_PSK:      auth_str = "[WPA3]";      break;
            case WIFI_AUTH_WPA2_WPA3_PSK: auth_str = "[WPA2/WPA3]"; break;
            default: break;
        }

        char label[72];
        snprintf(label, sizeof(label), "%s %s  (%d dBm)",
                 auth_str, (char *)ap_list[i].ssid, ap_list[i].rssi);

        lv_obj_t *btn = lv_list_add_button(list1, LV_SYMBOL_WIFI, label);

        ap_btn_data_t *btn_data = malloc(sizeof(ap_btn_data_t));
        if (btn_data) {
            memcpy(&btn_data->ap, &ap_list[i], sizeof(wifi_ap_record_t));
            lv_obj_set_user_data(btn, btn_data);
            lv_obj_add_event_cb(btn, ap_btn_click_event_cb, LV_EVENT_CLICKED, NULL);
        }
    }

    update_status_bar();
    lv_obj_add_flag(spinner, LV_OBJ_FLAG_HIDDEN);
    bsp_display_unlock();

    free(ap_list);
}

/* ===================== 扫描后台任务 ===================== */

static void wifi_scan_ui_task(void *pvParameters)
{
    while (1) {
        EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                               WIFI_START_SCAN,
                                               pdTRUE, pdFALSE,
                                               portMAX_DELAY);
        if (bits & WIFI_START_SCAN) {
            do_wifi_scan();
        }
    }
}

/* ===================== WiFi 界面初始化 ===================== */

void init_wifi_service_screen(lv_obj_t *parent, lv_obj_t *ret_scr)
{
    return_screen = ret_scr;
    set_screen_styles_t *styles = get_set_screen_styles();

    wifi_screen = lv_obj_create(parent);
    lv_obj_set_size(wifi_screen, 360, 280);
    lv_obj_set_style_bg_color(wifi_screen, lv_color_hex(0xFAFAFA), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(wifi_screen, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(wifi_screen, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(wifi_screen, 16, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(wifi_screen, 4, LV_PART_MAIN);
    lv_obj_set_style_shadow_color(wifi_screen, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(wifi_screen, LV_OPA_10, LV_PART_MAIN);
    lv_obj_set_style_shadow_offset_y(wifi_screen, 4, LV_PART_MAIN);
    lv_obj_center(wifi_screen);

    back_btn = lv_button_create(wifi_screen);
    lv_obj_add_style(back_btn, &styles->back_btn, LV_PART_MAIN);
    lv_obj_add_style(back_btn, &styles->back_btn_pressed, LV_STATE_PRESSED);
    lv_obj_align(back_btn, LV_ALIGN_TOP_LEFT, 45, 0);  
    lv_obj_add_event_cb(back_btn, back_btn_click_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *back_icon = lv_label_create(back_btn);
    lv_label_set_text(back_icon, LV_SYMBOL_LEFT);
    lv_obj_center(back_icon);

    lv_obj_t *title_label = lv_label_create(wifi_screen);
    lv_label_set_text(title_label, "WIFI");  
    lv_obj_set_style_text_color(title_label, lv_color_black(), 0);
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_22, 0);
    lv_obj_align(title_label, LV_ALIGN_TOP_MID, 0, 10);

    scan_btn = lv_button_create(wifi_screen);
    lv_obj_add_style(scan_btn, &styles->back_btn, LV_PART_MAIN);
    lv_obj_add_style(scan_btn, &styles->back_btn_pressed, LV_STATE_PRESSED);
    lv_obj_align(scan_btn, LV_ALIGN_TOP_RIGHT, -45, 0);  
    lv_obj_add_event_cb(scan_btn, scan_wifi_btn_click_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *scan_icon = lv_label_create(scan_btn);
    lv_label_set_text(scan_icon, LV_SYMBOL_REFRESH);
    lv_obj_center(scan_icon);

    status_bar = lv_label_create(wifi_screen);
    lv_label_set_long_mode(status_bar, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_width(status_bar, 300);
    lv_obj_set_style_text_font(status_bar, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(status_bar, lv_color_hex(0x6C757D), 0);
    lv_obj_align(status_bar, LV_ALIGN_TOP_MID, 0, 38);
    update_status_bar();

    spinner = lv_spinner_create(parent);
    lv_obj_set_size(spinner, 100, 100);
    lv_obj_center(spinner);
    lv_spinner_set_anim_params(spinner, 1000, 200);
    lv_obj_add_flag(spinner, LV_OBJ_FLAG_HIDDEN);

    list1 = lv_list_create(wifi_screen);
    lv_obj_add_style(list1, &styles->list, LV_PART_MAIN);
    lv_obj_set_size(list1, 340, 205);
    lv_obj_align(list1, LV_ALIGN_CENTER, 0, 35);
    lv_obj_set_style_pad_top(list1, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(list1, 0, LV_PART_MAIN);
}
