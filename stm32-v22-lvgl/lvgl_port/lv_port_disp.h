#ifndef LV_PORT_DISP_H
#define LV_PORT_DISP_H

#include <stdint.h>

/* ============================================================
 * LVGL 显示驱动移植 (v2.2, 无 FreeRTOS)
 * 提供 lv_disp_flush 回调, 把 LVGL 渲染数据写入 ILI9341
 * LVGL 时间源由 lv_conf.h 的 LV_TICK_CUSTOM 直读 sys_tick(),
 * 不再需要 tick hook / lv_tick_inc_port()
 * ============================================================ */

/* 初始化 LVGL 显示系统 (创建 display + 注册 flush_cb) */
void lv_port_disp_init(void);

#endif /* LV_PORT_DISP_H */
