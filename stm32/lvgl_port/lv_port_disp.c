#include "lv_port_disp.h"
#include "config.h"
#include "lcd.h"
#include "lvgl.h"

/* ============================================================
 * LVGL 显示驱动移植实现
 * - 双 partial buffer (每次 40 行, 节省 RAM)
 * - flush 回调直接写 LCD GRAM
 * ============================================================ */

/* 显示缓冲区 (RGB565, 每行 240 像素 = 480 字节)
 * 2 个缓冲区交替使用, LVGL 可以一边渲染一边刷新 */
static lv_disp_draw_buf_t s_draw_buf;
static lv_color_t s_buf1[LCD_W * LVGL_DISP_BUF_LINES];
static lv_color_t s_buf2[LCD_W * LVGL_DISP_BUF_LINES];

/* LVGL tick 计数 */
static volatile uint32_t s_lvgl_tick = 0;

void lv_tick_inc_port(uint32_t ms)
{
    s_lvgl_tick += ms;
}

/* ==================== flush 回调 ==================== */

static void disp_flush_cb(lv_disp_drv_t *disp_drv, const lv_area_t *area,
                           lv_color_t *color_p)
{
    /* 把 LVGL 的像素数据写入 LCD */
    uint16_t x1 = area->x1;
    uint16_t y1 = area->y1;
    uint16_t x2 = area->x2;
    uint16_t y2 = area->y2;

    /* lcd_flush_area 接收 RGB565 数据 */
    lcd_flush_area(x1, y1, x2, y2, (const uint16_t *)color_p);

    /* 通知 LVGL 刷新完成 */
    lv_disp_flush_ready(disp_drv);
}

/* ==================== 初始化 ==================== */

void lv_port_disp_init(void)
{
    /* 1. 初始化 LCD 硬件 */
    lcd_init();

    /* 2. 初始化 LVGL 核心 */
    lv_init();

    /* 3. 创建显示缓冲 */
    lv_disp_draw_buf_init(&s_draw_buf,
                           s_buf1, s_buf2,
                           LCD_W * LVGL_DISP_BUF_LINES);

    /* 4. 创建显示驱动 */
    static lv_disp_drv_t s_disp_drv;
    lv_disp_drv_init(&s_disp_drv);
    s_disp_drv.hor_res = LCD_W;
    s_disp_drv.ver_res = LCD_H;
    s_disp_drv.draw_buf = &s_draw_buf;
    s_disp_drv.flush_cb = disp_flush_cb;
    /* 使用软件刷新 (不用 DMA/GPU) */
    s_disp_drv.antialiasing = 1;

    lv_disp_drv_register(&s_disp_drv);
}
