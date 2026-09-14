#ifndef LV_PORT_DISP_H
#define LV_PORT_DISP_H

#include <stdint.h>

/* ============================================================
 * LVGL 显示驱动移植
 * 提供 lv_disp_flush 回调, 把 LVGL 渲染数据写入 ILI9341
 * ============================================================ */

/* 初始化 LVGL 显示系统 (创建 display + 注册 flush_cb) */
void lv_port_disp_init(void);

/* LVGL tick (在 FreeRTOS tick hook 中调用) */
void lv_tick_inc_port(uint32_t ms);

#endif /* LV_PORT_DISP_H */
