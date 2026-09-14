#ifndef DISPLAY_H
#define DISPLAY_H

#include <stdint.h>

/* ============================================================
 * 显示模块 — 正点原子风格 LCD 文字/图形绘制 (v2.1)
 * 直接使用 FSMC LCD 驱动, 无 LVGL 依赖
 * 320x240 横屏, RGB565
 * ============================================================ */

/* 显示数据结构 (任务间共享) */
typedef struct {
    int16_t  temp_x10;
    uint16_t light_raw;
    uint8_t  motor_speed;
    uint8_t  mqtt_online;
    uint8_t  led1_on;
    uint8_t  led2_on;
    uint8_t  buzzer_on;
    uint8_t  alarm_active;
    uint8_t  ota_active;
    uint8_t  ota_percent;
} disp_data_t;

/* 全局显示数据 */
extern volatile disp_data_t g_disp_data;

/* 初始化显示 (LCD + 静态布局) */
void disp_init(void);

/* 更新仪表盘动态数据 (在 display_task 中周期调用) */
void disp_update_dashboard(void);

/* 显示 OTA 进度 */
void disp_show_ota(uint8_t percent, const char *status);

/* 返回仪表盘 */
void disp_show_dashboard(void);

/* 底层绘制函数 */
void lcd_show_char(uint16_t x, uint16_t y, char ch, uint16_t fg, uint16_t bg);
void lcd_show_string(uint16_t x, uint16_t y, const char *str, uint16_t fg, uint16_t bg);
void lcd_show_num(uint16_t x, uint16_t y, int32_t num, uint8_t len, uint16_t fg, uint16_t bg);
void lcd_fill_rect(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color);
void lcd_draw_rect(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color);
void lcd_draw_hline(uint16_t x1, uint16_t y, uint16_t x2, uint16_t color);

#endif /* DISPLAY_H */
