#ifndef UI_MAIN_H
#define UI_MAIN_H

#include "lvgl.h"

/* ============================================================
 * LVGL UI — 智能工厂 v2.0 界面
 * 两个屏幕: 仪表盘 + OTA 升级
 * ============================================================ */

/* 数据结构 — 供其他任务通过 UI 更新显示 */
typedef struct {
    int16_t  temp_x10;     /* 温度 ×10 (避免浮点) */
    uint16_t light_raw;    /* 光照 ADC 原始值 0-4095 */
    uint8_t  motor_speed;  /* 电机转速 0-100% */
    uint8_t  mqtt_online;  /* 1=MQTT已连接, 0=断开 */
    uint8_t  wifi_rssi;    /* WiFi 信号强度 (0-4) */
    uint8_t  led1_on;      /* LED1 状态 */
    uint8_t  led2_on;      /* LED2 状态 */
    uint8_t  buzzer_on;    /* 蜂鸣器状态 */
    uint8_t  alarm_active; /* 报警激活 */
} ui_data_t;

/* 全局 UI 数据 (其他任务直接读写) */
extern volatile ui_data_t g_ui_data;

/* 初始化所有 UI */
void ui_init(void);

/* 更新仪表盘数值 (在 LVGL 任务中调用) */
void ui_update_dashboard(void);

/* 切换到 OTA 界面 */
void ui_show_ota(void);

/* 更新 OTA 进度 */
void ui_update_ota_progress(uint8_t percent, const char *status_text);

/* OTA 完成后返回仪表盘 */
void ui_show_dashboard(void);

/* 获取当前屏幕索引 (0=dashboard, 1=ota) */
int ui_get_current_screen(void);

/* UI 按钮请求 (读后自动清零, 在 Control Task 中调用) */
uint8_t ui_get_led1_req(void);
uint8_t ui_get_led2_req(void);
uint8_t ui_get_motor_inc_req(void);
uint8_t ui_get_motor_dec_req(void);
uint8_t ui_get_buzzer_req(void);

#endif /* UI_MAIN_H */
