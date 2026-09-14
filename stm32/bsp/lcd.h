#ifndef LCD_DRV_H
#define LCD_DRV_H

#include "stm32f10x.h"
#include <stdint.h>

/* ============================================================
 * LCD 驱动 — ILI9341 via FSMC (正点原子精英版 2.8" TFT)
 * 16-bit 并行总线, FSMC Bank1 NE4
 * ============================================================ */

/* FSMC 地址定义 */
#define LCD_BASE_ADDR    0x6C000000   /* NE4, A16 作为 RS 信号 */
#define LCD_REG          (*(volatile uint16_t *)(LCD_BASE_ADDR + 0x00000))  /* RS=0: 命令/寄存器 */
#define LCD_RAM          (*(volatile uint16_t *)(LCD_BASE_ADDR + 0x10000))  /* RS=1: 数据 */

/* 屏幕尺寸 (横屏模式, MADCTL=0x28 MV=1) */
#define LCD_W             320
#define LCD_H             240

/* 颜色 (RGB565) */
#define LCD_WHITE         0xFFFF
#define LCD_BLACK         0x0000
#define LCD_RED           0xF800
#define LCD_GREEN         0x07E0
#define LCD_BLUE          0x001F
#define LCD_CYAN          0x07FF
#define LCD_YELLOW        0xFFE0
#define LCD_ORANGE        0xFD20
#define LCD_GRAY          0x8410
#define LCD_DARKGRAY      0x4208

/* ILI9341 常用命令 */
#define ILI9341_NOP        0x00
#define ILI9341_SOFTRESET  0x01
#define ILI9341_SLEEP_OFF  0x11
#define ILI9341_DISPLAY_ON 0x29
#define ILI9341_COLADDR    0x2A
#define ILI9341_PAGEADDR   0x2B
#define ILI9341_MEMORY     0x2C
#define ILI9341_MADCTL     0x36
#define ILI9341_PIXFMT     0x3A

/* 初始化 */
void lcd_init(void);

/* 底层接口 */
void lcd_write_reg(uint16_t reg, uint16_t val);
uint16_t lcd_read_reg(uint16_t reg);
void lcd_write_cmd(uint16_t cmd);
void lcd_write_data(uint16_t data);

/* 全屏填充 */
void lcd_fill(uint16_t color);

/* 区域绘制 (供 LVGL flush 回调用) */
void lcd_set_window(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2);
void lcd_flush_area(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2,
                    const uint16_t *color_data);

/* 背光控制 */
void lcd_backlight_on(void);
void lcd_backlight_off(void);

#endif /* LCD_DRV_H */
