#ifndef SENSORS_H
#define SENSORS_H

#include <stdint.h>

/* ============================================================
 * 传感器驱动 — DS18B20 温度 + 光敏 ADC (标准库/SPL 版本)
 * 微秒延时移至 bsp.h: sys_delay_us()
 * ============================================================ */

/* 初始化传感器 (DS18B20 GPIO + ADC1) */
void sensors_init(void);

/* 读取 DS18B20 温度, 返回摄氏度 (如 25.5)
 * 失败返回 -999.0 */
float ds18b20_read_temp(void);

/* 读取 DS18B20 温度 ×10 (整数, 避免浮点), 如 255 = 25.5°C
 * 失败返回 -9990 */
int16_t ds18b20_read_temp_x10(void);

/* 读取光敏传感器 ADC 值 (0-4095) */
uint16_t light_read(void);

/* 读取光敏 ADC 原始值 (同 light_read, 便捷别名) */
uint16_t light_read_raw(void);

#endif /* SENSORS_H */
