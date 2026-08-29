/* DSH bridge app for the Waveshare Desktop phone shell. */
#include "dsh_app.hpp"

#include "cJSON.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"
#include "lvgl.h"
#include "esp_brookesia.hpp"

#include <cstdio>
#include <cstring>

namespace {
constexpr char TAG[] = "DshApp";
constexpr uint32_t kStaleMs = 8000;
constexpr uint32_t kDoneMs = 4500;

static lv_obj_t *makeLabel(lv_obj_t *parent, const char *text, lv_color_t color, lv_coord_t width = LV_SIZE_CONTENT)
{
    lv_obj_t *obj = lv_label_create(parent);
    lv_obj_set_style_text_color(obj, color, 0);
    lv_label_set_text(obj, text);
    if (width != LV_SIZE_CONTENT) {
        lv_obj_set_width(obj, width);
        lv_label_set_long_mode(obj, LV_LABEL_LONG_MODE_WRAP);
    }
    return obj;
}

static const char *jsonString(cJSON *root, const char *key)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(root, key);
    return (item && cJSON_IsString(item) && item->valuestring) ? item->valuestring : "";
}

static int jsonInt(cJSON *root, const char *key, int fallback = 0)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(root, key);
    return (item && cJSON_IsNumber(item)) ? item->valueint : fallback;
}

static bool jsonBool(cJSON *root, const char *key, bool fallback = false)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(root, key);
    return item ? cJSON_IsTrue(item) : fallback;
}

static uint32_t nowMs()
{
    return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
}

static lv_color_t phaseColor(const char *phase)
{
    if (strcmp(phase, "WORKING") == 0 || strcmp(phase, "STREAMING") == 0) return lv_color_hex(0x35A7FF);
    if (strcmp(phase, "THINKING") == 0 || strcmp(phase, "WAITING") == 0) return lv_color_hex(0xB184FF);
    if (strcmp(phase, "DONE") == 0) return lv_color_hex(0x39D98A);
    if (strcmp(phase, "ERROR") == 0) return lv_color_hex(0xFF667A);
    if (strcmp(phase, "IDLE") == 0) return lv_color_hex(0x9AA7BA);
    return lv_color_hex(0xFFB454);
}

static const char *phaseName(const char *phase)
{
    if (!phase || !*phase) return "IDLE";
    if (strcmp(phase, "working") == 0) return "WORKING";
    if (strcmp(phase, "thinking") == 0) return "THINKING";
    if (strcmp(phase, "streaming") == 0) return "STREAMING";
    if (strcmp(phase, "waiting") == 0) return "WAITING";
    if (strcmp(phase, "done") == 0) return "DONE";
    if (strcmp(phase, "error") == 0) return "ERROR";
    if (strcmp(phase, "offline") == 0) return "OFFLINE";
    return "IDLE";
}
}

namespace esp_brookesia::apps {

DshApp *DshApp::_instance = nullptr;

DshApp *DshApp::requestInstance(bool use_status_bar, bool use_navigation_bar)
{
    if (_instance == nullptr) _instance = new DshApp(use_status_bar, use_navigation_bar);
    return _instance;
}

DshApp::DshApp(bool use_status_bar, bool use_navigation_bar):
    App("DSH", nullptr, false, use_status_bar, use_navigation_bar)
{
}

void DshApp::loadBridgeConfig()
{
    // Keep compatibility with the original DSH dial's NVS namespace. The
    // Waveshare Settings app owns WiFi; this app only reads bridge settings.
    strncpy(_host, "149.88.88.167", sizeof(_host) - 1);
    strncpy(_token, "fdec2108ebe9f617eca8cde2ca64f999", sizeof(_token) - 1);
    _port = 7002;

    nvs_handle_t nvs = 0;
    if (nvs_open("dsh-dial", NVS_READONLY, &nvs) == ESP_OK) {
        size_t len = sizeof(_host);
        if (nvs_get_str(nvs, "host", _host, &len) != ESP_OK || !_host[0]) {
            strncpy(_host, "149.88.88.167", sizeof(_host) - 1);
        }
        uint16_t port = 0;
        if (nvs_get_u16(nvs, "port", &port) == ESP_OK && port > 0) _port = port;
        len = sizeof(_token);
        if (nvs_get_str(nvs, "token", _token, &len) != ESP_OK) {
            strncpy(_token, "fdec2108ebe9f617eca8cde2ca64f999", sizeof(_token) - 1);
        }
        nvs_close(nvs);
    }
    snprintf(_uri, sizeof(_uri), "ws://%s:%u/dev?token=%s", _host, _port, _token);
    ESP_LOGI(TAG, "Bridge endpoint: ws://%s:%u/dev", _host, _port);
}

void DshApp::setConnection(const char *text, uint32_t color)
{
    if (_connection) {
        lv_label_set_text(_connection, text);
        lv_obj_set_style_text_color(_connection, lv_color_hex(color), 0);
    }
}

void DshApp::startTransport()
{
    if (_transport_started || !_uri[0]) return;
    esp_websocket_client_config_t config = {};
    config.uri = _uri;
    config.buffer_size = 4096;
    config.task_stack = 6144;
    config.task_prio = 5;
    config.reconnect_timeout_ms = 3000;
    config.network_timeout_ms = 5000;
    config.ping_interval_sec = 10;
    _ws = esp_websocket_client_init(&config);
    if (!_ws) {
        setConnection("SOCKET INIT FAILED", 0xFF667A);
        return;
    }
    esp_websocket_register_events(_ws, WEBSOCKET_EVENT_ANY, websocketEvent, this);
    if (esp_websocket_client_start(_ws) != ESP_OK) {
        esp_websocket_client_destroy(_ws);
        _ws = nullptr;
        setConnection("CONNECT FAILED", 0xFF667A);
        return;
    }
    _transport_started = true;
    setConnection("CONNECTING...", 0xFFB454);
}

void DshApp::stopTransport()
{
    if (!_ws) return;
    esp_websocket_client_stop(_ws);
    esp_websocket_client_destroy(_ws);
    _ws = nullptr;
    _transport_started = false;
    _connected = false;
}

void DshApp::websocketEvent(void *handler_args, esp_event_base_t, int32_t event_id, void *event_data)
{
    auto *self = static_cast<DshApp *>(handler_args);
    if (!self) return;
    if (event_id == WEBSOCKET_EVENT_CONNECTED) {
        self->_connected = true;
        self->_last_frame_ms = nowMs();
        {
            esp_brookesia::gui::LvLockGuard guard;
            self->setConnection("CONNECTED", 0x39D98A);
        }
        constexpr char hello[] = "{\"t\":\"hello\",\"fw\":\"dsh-brookesia/1.0\",\"board\":\"ESP32-S3-Touch-LCD-1.85B\"}";
        esp_websocket_client_send_text(self->_ws, hello, sizeof(hello) - 1, pdMS_TO_TICKS(1000));
    } else if (event_id == WEBSOCKET_EVENT_DISCONNECTED || event_id == WEBSOCKET_EVENT_ERROR || event_id == WEBSOCKET_EVENT_CLOSED) {
        self->_connected = false;
        {
            esp_brookesia::gui::LvLockGuard guard;
            self->setConnection("OFFLINE / RETRYING", 0xFFB454);
            strncpy(self->_phase_name, "OFFLINE", sizeof(self->_phase_name) - 1);
            self->refreshUi();
        }
    } else if (event_id == WEBSOCKET_EVENT_DATA && event_data) {
        auto *data = static_cast<esp_websocket_event_data_t *>(event_data);
        if (data->op_code == 0x1 && data->data_ptr && data->payload_offset == 0 && data->fin) {
            self->handleMessage(data->data_ptr, data->data_len);
        }
    }
}

void DshApp::handleMessage(const char *text, size_t length)
{
    if (!text || !length) return;
    char *copy = static_cast<char *>(malloc(length + 1));
    if (!copy) return;
    memcpy(copy, text, length);
    copy[length] = '\0';
    cJSON *root = cJSON_Parse(copy);
    free(copy);
    if (!root) return;
    _last_frame_ms = nowMs();
    const char *type = jsonString(root, "t");
    {
        esp_brookesia::gui::LvLockGuard guard;
        if (strcmp(type, "state") == 0) applyState(root);
        else if (strcmp(type, "ask") == 0) showAsk(root);
        else if (strcmp(type, "pong") == 0) setConnection("CONNECTED", 0x39D98A);
    }
    cJSON_Delete(root);
}

void DshApp::applyState(void *json)
{
    auto *root = static_cast<cJSON *>(json);
    strncpy(_phase_name, phaseName(jsonString(root, "phase")), sizeof(_phase_name) - 1);
    const char *title = jsonString(root, "title");
    const char *detail = jsonString(root, "detail");
    if (title[0]) strncpy(_state_title, title, sizeof(_state_title) - 1);
    if (detail[0]) strncpy(_state_detail, detail, sizeof(_state_detail) - 1);
    _context_percent = jsonInt(root, "ctx", 0);
    if (_context_percent < 0) _context_percent = 0;
    if (_context_percent > 100) _context_percent = 100;
    if (strcmp(_phase_name, "DONE") == 0) _last_done_ms = nowMs();

    for (int i = 0; i < 3; ++i) {
        _state_activities[i][0] = '\0';
        _activity_running[i] = false;
    }
    cJSON *acts = cJSON_GetObjectItemCaseSensitive(root, "acts");
    if (acts && cJSON_IsArray(acts)) {
        int i = 0;
        cJSON *act = nullptr;
        cJSON_ArrayForEach(act, acts) {
            if (i >= 3 || !cJSON_IsObject(act)) break;
            strncpy(_state_activities[i], jsonString(act, "t"), sizeof(_state_activities[i]) - 1);
            _activity_running[i] = jsonBool(act, "r");
            ++i;
        }
    }
    refreshUi();
}

void DshApp::showAsk(void *json)
{
    auto *root = static_cast<cJSON *>(json);
    strncpy(_ask_id, jsonString(root, "id"), sizeof(_ask_id) - 1);
    _ask_count = 0;
    if (_ask_title) lv_label_set_text(_ask_title, jsonString(root, "title"));
    if (_ask_body) lv_label_set_text(_ask_body, jsonString(root, "body"));
    cJSON *options = cJSON_GetObjectItemCaseSensitive(root, "options");
    if (options && cJSON_IsArray(options)) {
        cJSON *option = nullptr;
        cJSON_ArrayForEach(option, options) {
            if (_ask_count >= 3 || !cJSON_IsObject(option)) break;
            strncpy(_ask_option_ids[_ask_count], jsonString(option, "id"), sizeof(_ask_option_ids[0]) - 1);
            if (_ask_buttons[_ask_count]) {
                lv_obj_remove_flag(_ask_buttons[_ask_count], LV_OBJ_FLAG_HIDDEN);
                lv_label_set_text(lv_obj_get_child(_ask_buttons[_ask_count], 0), jsonString(option, "label"));
            }
            ++_ask_count;
        }
    }
    for (int i = _ask_count; i < 3; ++i) if (_ask_buttons[i]) lv_obj_add_flag(_ask_buttons[i], LV_OBJ_FLAG_HIDDEN);
    if (_ask_panel) lv_obj_remove_flag(_ask_panel, LV_OBJ_FLAG_HIDDEN);
}

void DshApp::hideAsk()
{
    if (_ask_panel) lv_obj_add_flag(_ask_panel, LV_OBJ_FLAG_HIDDEN);
    _ask_id[0] = '\0';
    _ask_count = 0;
}

void DshApp::sendAnswer(int index)
{
    if (!_ws || !_connected || index < 0 || index >= _ask_count || !_ask_id[0]) return;
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "t", "answer");
    cJSON_AddStringToObject(root, "id", _ask_id);
    cJSON_AddStringToObject(root, "choice", _ask_option_ids[index]);
    char *text = cJSON_PrintUnformatted(root);
    if (text) {
        esp_websocket_client_send_text(_ws, text, strlen(text), pdMS_TO_TICKS(1000));
        free(text);
    }
    cJSON_Delete(root);
    hideAsk();
}

void DshApp::refreshUi()
{
    if (!_screen) return;
    lv_color_t color = phaseColor(_phase_name);
    lv_label_set_text(_phase, _phase_name);
    lv_obj_set_style_text_color(_phase, color, 0);
    lv_label_set_text(_title, _state_title);
    lv_label_set_text(_detail, _state_detail);
    char context[32];
    snprintf(context, sizeof(context), "CONTEXT %d%%", _context_percent);
    lv_label_set_text(_context, context);
    lv_arc_set_value(_arc, _context_percent);
    for (int i = 0; i < 3; ++i) {
        if (_state_activities[i][0]) {
            char line[60];
            snprintf(line, sizeof(line), "%s %s", _activity_running[i] ? ">" : "-", _state_activities[i]);
            lv_label_set_text(_activities[i], line);
            lv_obj_remove_flag(_activities[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(_activities[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

bool DshApp::run(void)
{
    _screen = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(_screen, lv_color_hex(0x10131B), 0);
    lv_obj_set_style_bg_opa(_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(_screen, 18, 0);

    _phase = makeLabel(_screen, "OFFLINE", lv_color_hex(0xFFB454), 320);
    lv_obj_set_style_text_font(_phase, &lv_font_montserrat_26, 0);
    lv_obj_set_style_text_align(_phase, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(_phase, LV_ALIGN_TOP_MID, 0, 12);
    _connection = makeLabel(_screen, "CONNECTING...", lv_color_hex(0xFFB454), 320);
    lv_obj_set_style_text_align(_connection, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(_connection, LV_ALIGN_TOP_MID, 0, 48);

    _arc = lv_arc_create(_screen);
    lv_obj_set_size(_arc, 145, 145);
    lv_obj_align(_arc, LV_ALIGN_TOP_MID, 0, 72);
    lv_arc_set_range(_arc, 0, 100);
    lv_arc_set_value(_arc, 0);
    lv_obj_remove_style(_arc, nullptr, LV_PART_KNOB);
    lv_obj_set_style_arc_width(_arc, 10, LV_PART_MAIN);
    lv_obj_set_style_arc_width(_arc, 10, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(_arc, lv_color_hex(0x273044), LV_PART_MAIN);
    lv_obj_set_style_arc_color(_arc, lv_color_hex(0x35A7FF), LV_PART_INDICATOR);

    _title = makeLabel(_screen, _state_title, lv_color_hex(0xFFFFFF), 320);
    lv_obj_set_style_text_font(_title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_align(_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(_title, LV_ALIGN_TOP_MID, 0, 226);
    _detail = makeLabel(_screen, _state_detail, lv_color_hex(0xA6ADBB), 320);
    lv_obj_set_style_text_align(_detail, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(_detail, LV_ALIGN_TOP_MID, 0, 258);
    _context = makeLabel(_screen, "CONTEXT 0%", lv_color_hex(0x7E8CA5), 320);
    lv_obj_set_style_text_align(_context, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(_context, LV_ALIGN_TOP_MID, 0, 290);

    for (int i = 0; i < 3; ++i) {
        _activities[i] = makeLabel(_screen, "", lv_color_hex(0xD7DEEA), 320);
        lv_obj_align(_activities[i], LV_ALIGN_TOP_LEFT, 4, 322 + i * 20);
        lv_obj_add_flag(_activities[i], LV_OBJ_FLAG_HIDDEN);
    }

    _ask_panel = lv_obj_create(_screen);
    lv_obj_set_size(_ask_panel, 320, 220);
    lv_obj_align(_ask_panel, LV_ALIGN_CENTER, 0, 10);
    lv_obj_set_style_bg_color(_ask_panel, lv_color_hex(0x202838), 0);
    lv_obj_set_style_border_width(_ask_panel, 2, 0);
    lv_obj_set_style_border_color(_ask_panel, lv_color_hex(0x35A7FF), 0);
    _ask_title = makeLabel(_ask_panel, "DSH asks", lv_color_hex(0xFFFFFF), 280);
    lv_obj_align(_ask_title, LV_ALIGN_TOP_MID, 0, 12);
    _ask_body = makeLabel(_ask_panel, "", lv_color_hex(0xD7DEEA), 280);
    lv_obj_align(_ask_body, LV_ALIGN_TOP_MID, 0, 42);
    for (int i = 0; i < 3; ++i) {
        _ask_buttons[i] = lv_button_create(_ask_panel);
        lv_obj_set_size(_ask_buttons[i], 270, 36);
        lv_obj_align(_ask_buttons[i], LV_ALIGN_TOP_MID, 0, 82 + i * 42);
        lv_obj_t *button_label = lv_label_create(_ask_buttons[i]);
        lv_obj_center(button_label);
        lv_obj_add_event_cb(_ask_buttons[i], [](lv_event_t *event) {
            auto *self = static_cast<DshApp *>(lv_event_get_user_data(event));
            if (!self) return;
            lv_obj_t *target = static_cast<lv_obj_t *>(lv_event_get_target(event));
            for (int button = 0; button < 3; ++button) {
                if (self->_ask_buttons[button] == target) {
                    self->sendAnswer(button);
                    break;
                }
            }
        }, LV_EVENT_CLICKED, this);
        lv_obj_add_flag(_ask_buttons[i], LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_add_flag(_ask_panel, LV_OBJ_FLAG_HIDDEN);

    loadBridgeConfig();
    refreshUi();
    lv_screen_load(_screen);
    _timer = lv_timer_create([](lv_timer_t *timer) {
        auto *self = static_cast<DshApp *>(timer->user_data);
        if (!self) return;
        self->_animation++;
        if (self->_arc && (strcmp(self->_phase_name, "WORKING") == 0 || strcmp(self->_phase_name, "THINKING") == 0 || strcmp(self->_phase_name, "STREAMING") == 0)) {
            lv_obj_set_style_arc_color(self->_arc, phaseColor(self->_phase_name), LV_PART_INDICATOR);
            lv_obj_set_style_opa(self->_arc, static_cast<lv_opa_t>(190 + (self->_animation % 3) * 20), LV_PART_INDICATOR);
        }
        if (self->_connected && nowMs() - self->_last_frame_ms > kStaleMs) self->setConnection("STALE DATA", 0xFFB454);
        if (self->_connected && self->_ws && nowMs() - self->_last_ping_ms >= 10000) {
            constexpr char ping[] = "{\"t\":\"ping\",\"battery\":0,\"charging\":false}";
            esp_websocket_client_send_text(self->_ws, ping, sizeof(ping) - 1, pdMS_TO_TICKS(1000));
            self->_last_ping_ms = nowMs();
        }
        if (self->_ask_panel && !lv_obj_has_flag(self->_ask_panel, LV_OBJ_FLAG_HIDDEN) && self->_last_frame_ms && nowMs() - self->_last_frame_ms > 120000) self->hideAsk();
        if (strcmp(self->_phase_name, "DONE") == 0 && self->_last_done_ms && nowMs() - self->_last_done_ms > kDoneMs) {
            strncpy(self->_phase_name, "IDLE", sizeof(self->_phase_name) - 1);
            self->refreshUi();
        }
    }, 250, this);
    startTransport();
    return true;
}

bool DshApp::back(void)
{
    if (_timer) {
        lv_timer_del(_timer);
        _timer = nullptr;
    }
    stopTransport();
    _screen = nullptr;
    return notifyCoreClosed();
}

} // namespace esp_brookesia::apps
