#ifndef BSP_H
#define BSP_H

#include "stm32f10x.h"
#include <stdint.h>

/* ============================================================
 * BSP — Board Support Package (v2.1, FreeRTOS, 无 LVGL)
 * 系统时钟 / DWT 微秒延时 / 调试串口 / Flash 操作
 * ============================================================ */

/* ---- 系统时钟 (72MHz) ---- */
void     sys_clock_init(void);
uint32_t sys_tick(void);
void     sys_delay_ms(uint32_t ms);
void     sys_delay_us(uint32_t us);

/* ---- DWT ---- */
void     dwt_init(void);

/* ---- 调试串口 (USART1, PA9/PA10) ---- */
void     debug_init(void);
void     debug_send(const uint8_t *data, int len);

/* ---- BKP 备份寄存器 (OTA 标志) ---- */
void     bkp_init(void);
void     bkp_write(uint16_t dr, uint16_t val);
uint16_t bkp_read(uint16_t dr);

/* ---- Flash 操作 (OTA 用) ---- */
void     flash_unlock(void);
void     flash_lock(void);
int      flash_erase_page(uint32_t addr);
int      flash_write_halfword(uint32_t addr, uint16_t data);
int      flash_write_buf(uint32_t addr, const uint8_t *buf, uint32_t len);
int      flash_verify(uint32_t addr, const uint8_t *expected, uint32_t len);

/* ---- 跳转 ---- */
void     jump_to_app(uint32_t app_addr);

/* ---- 系统复位 ---- */
void     system_reset(void);

/* ---- Tick hook (在 vApplicationTickHook 中调用) ---- */
void     bsp_tick_hook(void);

#endif /* BSP_H */
