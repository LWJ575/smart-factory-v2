#include "ui_main.h"
#include "config.h"
#include <stdio.h>

/* ============================================================
 * LVGL UI 实现 — 仪表盘 + OTA 屏幕
 * 屏幕尺寸: 320×240 (横屏)
 * ============================================================ */

/* 全局数据 */
volatile ui_data_t g_ui_data = {0};

/* 屏幕和控件引用 */
static lv_obj_t *s_scr_dashboard;
static lv_obj_t *s_scr_ota;

/* 仪表盘控件 */
static lv_obj_t *s_lbl_temp;
static lv_obj_t *s_lbl_light;
static lv_obj_t *s_lbl_motor;
static lv_obj_t *s_bar_light;
static lv_obj_t *s_bar_motor;
static lv_obj_t *s_lbl_mqtt_status;
static lv_obj_t *s_lbl_wifi;
static lv_obj_t *s_btn_led1;
static lv_obj_t *s_btn_led2;
static lv_obj_t *s_btn_motor_inc;
static lv_obj_t *s_btn_motor_dec;
static lv_obj_t *s_btn_buzzer;
static lv_obj_t *s_lbl_fw_ver;

/* OTA 控件 */
static lv_obj_t *s_bar_ota;
static lv_obj_t *s_lbl_ota_status;
static lv_obj_t *s_lbl_ota_percent;

/* 当前屏幕 */
static int s_current_screen = 0;

/* 前向声明 */
static void create_dashboard(void);
static void create_ota_screen(void);

/* ==================== 按钮回调 ==================== */

/* 这些回调通过全局变量通知 Control Task */
static volatile uint8_t s_btn_led1_req = 0;
static volatile uint8_t s_btn_led2_req = 0;
static volatile uint8_t s_btn_motor_inc_req = 0;
static volatile uint8_t s_btn_motor_dec_req = 0;
static volatile uint8_t s_btn_buzzer_req = 0;

uint8_t ui_get_led1_req(void) { uint8_t v = s_btn_led1_req; s_btn_led1_req = 0; return v; }
uint8_t ui_get_led2_req(void) { uint8_t v = s_btn_led2_req; s_btn_led2_req = 0; return v; }
uint8_t ui_get_motor_inc_req(void) { uint8_t v = s_btn_motor_inc_req; s_btn_motor_inc_req = 0; return v; }
uint8_t ui_get_motor_dec_req(void) { uint8_t v = s_btn_motor_dec_req; s_btn_motor_dec_req = 0; return v; }
uint8_t ui_get_buzzer_req(void) { uint8_t v = s_btn_buzzer_req; s_btn_buzzer_req = 0; return v; }

static void btn_led1_cb(lv_event_t *e)
{
    (void)e;
    s_btn_led1_req = 1;
}

static void btn_led2_cb(lv_event_t *e)
{
    (void)e;
    s_btn_led2_req = 1;
}

static void btn_motor_inc_cb(lv_event_t *e)
{
    (void)e;
    s_btn_motor_inc_req = 1;
}

static void btn_motor_dec_cb(lv_event_t *e)
{
    (void)e;
    s_btn_motor_dec_req = 1;
}

static void btn_buzzer_cb(lv_event_t *e)
{
    (void)e;
    s_btn_buzzer_req = 1;
}

/* ==================== 仪表盘屏幕 ==================== */

static void create_dashboard(void)
{
    s_scr_dashboard = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr_dashboard, lv_color_hex(0x1a1a2e), 0);
    lv_obj_clear_flag(s_scr_dashboard, LV_OBJ_FLAG_SCROLLABLE);

    /* ---- 标题栏 ---- */
    lv_obj_t *title = lv_label_create(s_scr_dashboard);
    lv_label_set_text(title, "Smart Factory  v2.0");
    lv_obj_set_style_text_color(title, lv_color_hex(0xE0E0E0), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 8, 4);

    /* 固件版本 */
    s_lbl_fw_ver = lv_label_create(s_scr_dashboard);
    lv_label_set_text(s_lbl_fw_ver, "FW " FW_VERSION);
    lv_obj_set_style_text_color(s_lbl_fw_ver, lv_color_hex(0x666666), 0);
    lv_obj_align(s_lbl_fw_ver, LV_ALIGN_TOP_RIGHT, -8, 6);

    /* 分隔线 */
    lv_obj_t *line = lv_line_create(s_scr_dashboard);
    static lv_point_t line_pts[] = {{0, 0}, {304, 0}};
    lv_line_set_points(line, line_pts, 2);
    lv_obj_set_style_line_color(line, lv_color_hex(0x333344), 0);
    lv_obj_set_style_line_width(line, 1, 0);
    lv_obj_align(line, LV_ALIGN_TOP_LEFT, 8, 28);

    /* ---- 温度卡片 (左上) ---- */
    lv_obj_t *card_temp = lv_obj_create(s_scr_dashboard);
    lv_obj_set_size(card_temp, 130, 80);
    lv_obj_align(card_temp, LV_ALIGN_TOP_LEFT, 8, 36);
    lv_obj_set_style_bg_color(card_temp, lv_color_hex(0x16213e), 0);
    lv_obj_set_style_border_color(card_temp, lv_color_hex(0x0f3460), 0);
    lv_obj_clear_flag(card_temp, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl_temp_title = lv_label_create(card_temp);
    lv_label_set_text(lbl_temp_title, "Temperature");
    lv_obj_set_style_text_color(lbl_temp_title, lv_color_hex(0x888888), 0);
    lv_obj_align(lbl_temp_title, LV_ALIGN_TOP_LEFT, 0, 0);

    s_lbl_temp = lv_label_create(card_temp);
    lv_label_set_text(s_lbl_temp, "--.- C");
    lv_obj_set_style_text_color(s_lbl_temp, lv_color_hex(0xFF6B6B), 0);
    lv_obj_set_style_text_font(s_lbl_temp, &lv_font_montserrat_28, 0);
    lv_obj_align(s_lbl_temp, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    /* ---- 光照卡片 (左下) ---- */
    lv_obj_t *card_light = lv_obj_create(s_scr_dashboard);
    lv_obj_set_size(card_light, 130, 80);
    lv_obj_align(card_light, LV_ALIGN_BOTTOM_LEFT, 8, -8);
    lv_obj_set_style_bg_color(card_light, lv_color_hex(0x16213e), 0);
    lv_obj_set_style_border_color(card_light, lv_color_hex(0x0f3460), 0);
    lv_obj_clear_flag(card_light, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl_light_title = lv_label_create(card_light);
    lv_label_set_text(lbl_light_title, "Light");
    lv_obj_set_style_text_color(lbl_light_title, lv_color_hex(0x888888), 0);
    lv_obj_align(lbl_light_title, LV_ALIGN_TOP_LEFT, 0, 0);

    s_lbl_light = lv_label_create(card_light);
    lv_label_set_text(s_lbl_light, "0");
    lv_obj_set_style_text_color(s_lbl_light, lv_color_hex(0xF0F066), 0);
    lv_obj_set_style_text_font(s_lbl_light, &lv_font_montserrat_20, 0);
    lv_obj_align(s_lbl_light, LV_ALIGN_TOP_RIGHT, 0, 0);

    s_bar_light = lv_bar_create(card_light);
    lv_obj_set_size(s_bar_light, 120, 8);
    lv_obj_set_style_bg_color(s_bar_light, lv_color_hex(0x333344), 0);
    lv_obj_set_style_bg_color(s_bar_light, lv_color_hex(0xF0F066), LV_PART_INDICATOR);
    lv_obj_align(s_bar_light, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_bar_set_range(s_bar_light, 0, 4095);
    lv_bar_set_value(s_bar_light, 0, LV_ANIM_OFF);

    /* ---- 电机卡片 (右上) ---- */
    lv_obj_t *card_motor = lv_obj_create(s_scr_dashboard);
    lv_obj_set_size(card_motor, 130, 80);
    lv_obj_align(card_motor, LV_ALIGN_TOP_RIGHT, -8, 36);
    lv_obj_set_style_bg_color(card_motor, lv_color_hex(0x16213e), 0);
    lv_obj_set_style_border_color(card_motor, lv_color_hex(0x0f3460), 0);
    lv_obj_clear_flag(card_motor, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl_motor_title = lv_label_create(card_motor);
    lv_label_set_text(lbl_motor_title, "Motor");
    lv_obj_set_style_text_color(lbl_motor_title, lv_color_hex(0x888888), 0);
    lv_obj_align(lbl_motor_title, LV_ALIGN_TOP_LEFT, 0, 0);

    s_lbl_motor = lv_label_create(card_motor);
    lv_label_set_text(s_lbl_motor, "0%");
    lv_obj_set_style_text_color(s_lbl_motor, lv_color_hex(0x4ECDC4), 0);
    lv_obj_set_style_text_font(s_lbl_motor, &lv_font_montserrat_20, 0);
    lv_obj_align(s_lbl_motor, LV_ALIGN_TOP_RIGHT, 0, 0);

    s_bar_motor = lv_bar_create(card_motor);
    lv_obj_set_size(s_bar_motor, 120, 8);
    lv_obj_set_style_bg_color(s_bar_motor, lv_color_hex(0x333344), 0);
    lv_obj_set_style_bg_color(s_bar_motor, lv_color_hex(0x4ECDC4), LV_PART_INDICATOR);
    lv_obj_align(s_bar_motor, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_bar_set_range(s_bar_motor, 0, 100);
    lv_bar_set_value(s_bar_motor, 0, LV_ANIM_OFF);

    /* ---- 状态栏 (右中) ---- */
    lv_obj_t *card_status = lv_obj_create(s_scr_dashboard);
    lv_obj_set_size(card_status, 130, 50);
    lv_obj_align(card_status, LV_ALIGN_RIGHT_MID, -8, 10);
    lv_obj_set_style_bg_color(card_status, lv_color_hex(0x16213e), 0);
    lv_obj_set_style_border_color(card_status, lv_color_hex(0x0f3460), 0);
    lv_obj_clear_flag(card_status, LV_OBJ_FLAG_SCROLLABLE);

    s_lbl_mqtt_status = lv_label_create(card_status);
    lv_label_set_text(s_lbl_mqtt_status, LV_SYMBOL_WIFI " MQTT: --");
    lv_obj_set_style_text_color(s_lbl_mqtt_status, lv_color_hex(0xFF6B6B), 0);
    lv_obj_align(s_lbl_mqtt_status, LV_ALIGN_TOP_LEFT, 0, 0);

    s_lbl_wifi = lv_label_create(card_status);
    lv_label_set_text(s_lbl_wifi, LV_SYMBOL_WIFI " WiFi: --");
    lv_obj_set_style_text_color(s_lbl_wifi, lv_color_hex(0x666666), 0);
    lv_obj_align(s_lbl_wifi, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    /* ---- 控制按钮区 (右下) ---- */
    lv_obj_t *ctrl_panel = lv_obj_create(s_scr_dashboard);
    lv_obj_set_size(ctrl_panel, 145, 60);
    lv_obj_align(ctrl_panel, LV_ALIGN_BOTTOM_RIGHT, -8, -8);
    lv_obj_set_style_bg_color(ctrl_panel, lv_color_hex(0x16213e), 0);
    lv_obj_set_style_border_color(ctrl_panel, lv_color_hex(0x0f3460), 0);
    lv_obj_clear_flag(ctrl_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(ctrl_panel, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ctrl_panel, LV_FLEX_ALIGN_SPACE_EVENLY,
                           LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    s_btn_led1 = lv_btn_create(ctrl_panel);
    lv_obj_set_size(s_btn_led1, 40, 30);
    lv_obj_set_style_bg_color(s_btn_led1, lv_color_hex(0x444466), 0);
    lv_obj_t *lbl = lv_label_create(s_btn_led1);
    lv_label_set_text(lbl, LV_SYMBOL_POWER);
    lv_obj_center(lbl);
    lv_obj_add_event_cb(s_btn_led1, btn_led1_cb, LV_EVENT_CLICKED, NULL);

    s_btn_led2 = lv_btn_create(ctrl_panel);
    lv_obj_set_size(s_btn_led2, 40, 30);
    lv_obj_set_style_bg_color(s_btn_led2, lv_color_hex(0x444466), 0);
    lbl = lv_label_create(s_btn_led2);
    lv_label_set_text(lbl, "L2");
    lv_obj_center(lbl);
    lv_obj_add_event_cb(s_btn_led2, btn_led2_cb, LV_EVENT_CLICKED, NULL);

    s_btn_motor_inc = lv_btn_create(ctrl_panel);
    lv_obj_set_size(s_btn_motor_inc, 25, 30);
    lv_obj_set_style_bg_color(s_btn_motor_inc, lv_color_hex(0x4ECDC4), 0);
    lbl = lv_label_create(s_btn_motor_inc);
    lv_label_set_text(lbl, "+");
    lv_obj_center(lbl);
    lv_obj_add_event_cb(s_btn_motor_inc, btn_motor_inc_cb, LV_EVENT_CLICKED, NULL);

    s_btn_motor_dec = lv_btn_create(ctrl_panel);
    lv_obj_set_size(s_btn_motor_dec, 25, 30);
    lv_obj_set_style_bg_color(s_btn_motor_dec, lv_color_hex(0x4ECDC4), 0);
    lbl = lv_label_create(s_btn_motor_dec);
    lv_label_set_text(lbl, "-");
    lv_obj_center(lbl);
    lv_obj_add_event_cb(s_btn_motor_dec, btn_motor_dec_cb, LV_EVENT_CLICKED, NULL);

    s_btn_buzzer = lv_btn_create(ctrl_panel);
    lv_obj_set_size(s_btn_buzzer, 25, 30);
    lv_obj_set_style_bg_color(s_btn_buzzer, lv_color_hex(0xFF6B6B), 0);
    lbl = lv_label_create(s_btn_buzzer);
    lv_label_set_text(lbl, LV_SYMBOL_BELL);
    lv_obj_center(lbl);
    lv_obj_add_event_cb(s_btn_buzzer, btn_buzzer_cb, LV_EVENT_CLICKED, NULL);
}

/* ==================== OTA 屏幕 ==================== */

static void create_ota_screen(void)
{
    s_scr_ota = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr_ota, lv_color_hex(0x1a1a2e), 0);
    lv_obj_clear_flag(s_scr_ota, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(s_scr_ota);
    lv_label_set_text(title, "Firmware Update");
    lv_obj_set_style_text_color(title, lv_color_hex(0xE0E0E0), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

    s_lbl_ota_status = lv_label_create(s_scr_ota);
    lv_label_set_text(s_lbl_ota_status, "Connecting...");
    lv_obj_set_style_text_color(s_lbl_ota_status, lv_color_hex(0x4ECDC4), 0);
    lv_obj_align(s_lbl_ota_status, LV_ALIGN_TOP_MID, 0, 60);

    s_bar_ota = lv_bar_create(s_scr_ota);
    lv_obj_set_size(s_bar_ota, 240, 20);
    lv_obj_set_style_bg_color(s_bar_ota, lv_color_hex(0x333344), 0);
    lv_obj_set_style_bg_color(s_bar_ota, lv_color_hex(0x4ECDC4), LV_PART_INDICATOR);
    lv_obj_align(s_bar_ota, LV_ALIGN_CENTER, 0, 0);
    lv_bar_set_range(s_bar_ota, 0, 100);
    lv_bar_set_value(s_bar_ota, 0, LV_ANIM_OFF);

    s_lbl_ota_percent = lv_label_create(s_scr_ota);
    lv_label_set_text(s_lbl_ota_percent, "0%");
    lv_obj_set_style_text_color(s_lbl_ota_percent, lv_color_hex(0xE0E0E0), 0);
    lv_obj_set_style_text_font(s_lbl_ota_percent, &lv_font_montserrat_20, 0);
    lv_obj_align(s_lbl_ota_percent, LV_ALIGN_CENTER, 0, 30);
}

/* ==================== 公共接口 ==================== */

void ui_init(void)
{
    create_dashboard();
    create_ota_screen();
    lv_scr_load(s_scr_dashboard);
    s_current_screen = 0;
}

void ui_update_dashboard(void)
{
    char buf[32];

    /* 温度 */
    int16_t t = g_ui_data.temp_x10;
    int16_t int_part = t / 10;
    int16_t dec_part = (t < 0 ? -t : t) % 10;
    snprintf(buf, sizeof(buf), "%d.%d C", int_part, dec_part);
    lv_label_set_text(s_lbl_temp, buf);

    /* 温度报警变色 */
    if (int_part >= TEMP_ALARM_HIGH) {
        lv_obj_set_style_text_color(s_lbl_temp, lv_color_hex(0xFF0000), 0);
    } else {
        lv_obj_set_style_text_color(s_lbl_temp, lv_color_hex(0xFF6B6B), 0);
    }

    /* 光照 */
    snprintf(buf, sizeof(buf), "%d", g_ui_data.light_raw);
    lv_label_set_text(s_lbl_light, buf);
    lv_bar_set_value(s_bar_light, g_ui_data.light_raw, LV_ANIM_OFF);

    /* 电机 */
    snprintf(buf, sizeof(buf), "%d%%", g_ui_data.motor_speed);
    lv_label_set_text(s_lbl_motor, buf);
    lv_bar_set_value(s_bar_motor, g_ui_data.motor_speed, LV_ANIM_OFF);

    /* MQTT 状态 */
    if (g_ui_data.mqtt_online) {
        lv_label_set_text(s_lbl_mqtt_status, LV_SYMBOL_WIFI " MQTT: OK");
        lv_obj_set_style_text_color(s_lbl_mqtt_status, lv_color_hex(0x4ECDC4), 0);
    } else {
        lv_label_set_text(s_lbl_mqtt_status, LV_SYMBOL_WIFI " MQTT: OFF");
        lv_obj_set_style_text_color(s_lbl_mqtt_status, lv_color_hex(0xFF6B6B), 0);
    }

    /* 按钮状态颜色 */
    lv_obj_set_style_bg_color(s_btn_led1,
        g_ui_data.led1_on ? lv_color_hex(0x4ECDC4) : lv_color_hex(0x444466), 0);
    lv_obj_set_style_bg_color(s_btn_led2,
        g_ui_data.led2_on ? lv_color_hex(0x4ECDC4) : lv_color_hex(0x444466), 0);
    lv_obj_set_style_bg_color(s_btn_buzzer,
        g_ui_data.buzzer_on ? lv_color_hex(0xFF6B6B) : lv_color_hex(0x444466), 0);
}

void ui_show_ota(void)
{
    lv_scr_load(s_scr_ota);
    s_current_screen = 1;
}

void ui_show_dashboard(void)
{
    lv_scr_load(s_scr_dashboard);
    s_current_screen = 0;
}

void ui_update_ota_progress(uint8_t percent, const char *status_text)
{
    lv_bar_set_value(s_bar_ota, percent, LV_ANIM_ON);
    char buf[8];
    snprintf(buf, sizeof(buf), "%d%%", percent);
    lv_label_set_text(s_lbl_ota_percent, buf);
    if (status_text) {
        lv_label_set_text(s_lbl_ota_status, status_text);
    }
}

int ui_get_current_screen(void)
{
    return s_current_screen;
}
