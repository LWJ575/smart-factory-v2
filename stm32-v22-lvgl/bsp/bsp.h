#ifndef BSP_H
#define BSP_H

#include "stm32f10x.h"
#include <stdint.h>

/* ============================================================
 * BSP — Board Support Package (v2.2, 无 FreeRTOS / 纯 LVGL 适配)
 * 系统时钟 / SysTick 毫秒计时 / DWT 微秒延时 / 调试串口 / Flash 操作
 *
 * 与 v2.0/v2.1 的区别:
 *   - SysTick 由本模块接管 (不再被 FreeRTOS 占用)
 *   - sys_delay_ms() 等待期间调用 bsp_idle_hook() 泵 LVGL,
 *     使 DS18B20 750ms 转换等待 / WiFi 20s 连接等待期间 UI 不冻结
 * ============================================================ */

/* ---- 系统时钟 (72MHz) ---- */
void     sys_clock_init(void);
void     bsp_systick_init(void);  /* SysTick 1ms 中断 (在 hw_init 早期调用) */
uint32_t sys_tick(void);          /* 毫秒计时 (SysTick 计数) */
void     sys_delay_ms(uint32_t ms);
void     sys_delay_us(uint32_t us);   /* DWT 微秒延时 */

/* ---- 空闲钩子 ----
 * sys_delay_ms() 在线程模式等待期间会反复调用。
 * main.c 提供强实现: 内部调 lv_timer_handler() 泵 LVGL。
 * 弱默认实现为空, 不依赖 LVGL。 */
void     bsp_idle_hook(void);

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

/* ---- 跳转到指定地址 (bootloader 用) ---- */
void     jump_to_app(uint32_t app_addr);

/* ---- 系统复位 ---- */
void     system_reset(void);

#endif /* BSP_H */
