#ifndef STM32_V22_CONFIG_H
#define STM32_V22_CONFIG_H

#include "stm32f10x.h"
#include "bsp.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ============================================================
 * Smart Factory v2.2 — STM32 配置 (纯 LVGL + OTA, 无 FreeRTOS)
 * 超级循环 (super loop) 架构, 屏幕显示使用 LVGL v8.3
 * 适配正点原子 STM32F103ZET6 精英版 + 2.8" ILI9341 LCD
 * 标准库 (SPL) 版本, 不需要 CubeMX
 *
 * v2.2 与 v2.1 的区别:
 *   v2.1 = FreeRTOS + 正点原子 LCD 库 (无 LVGL)
 *   v2.2 = LVGL + 超级循环 (无 FreeRTOS)
 * 拆分原因: F103 的 64KB SRAM 装不下 FreeRTOS(20KB堆+任务栈) + LVGL(显示缓冲+对象池)
 * ============================================================ */

/* ---- 设备标识 ---- */
#define DEVICE_ID         "dev01"
#define DEVICE_LOCATION    "workshop-A"
#define FW_VERSION         "2.2.0"

/* ---- WiFi 配置 ---- */
#define WIFI_SSID         "YourWiFi"
#define WIFI_PASSWORD     "YourPassword"

/* ---- MQTT Broker 配置 ---- */
#define BROKER_IP         "192.168.1.178"   /* PC 上 mosquitto 的 IP */
#define BROKER_PORT       1883
#define MQTT_KEEPALIVE    30                /* 秒 */

/* ---- 采集参数 ---- */
#define TELEMETRY_INTERVAL  3              /* 遥测上报间隔(秒) */
#define TEMP_ALARM_HIGH     35             /* 温度报警上限(C) */

/* ---- OTA 配置 ---- */
#define OTA_HTTP_HOST      "192.168.1.178"  /* HTTP 服务器 IP (i.MX6ULL 或 PC) */
#define OTA_HTTP_PORT      8080
#define OTA_HTTP_PATH      "/firmware.bin"  /* 固件文件路径 */
#define OTA_FLASH_PAGE_SIZE  2048           /* STM32F103 页大小 2KB */
#define OTA_CHUNK_SIZE       1024           /* 每次下载 1KB */

/* ---- Flash 布局 ---- */
#define FLASH_BASE_ADDR      0x08000000
#define BOOTLOADER_SIZE      (16 * 1024)    /* 16KB bootloader */
#define APP_START_ADDR       (FLASH_BASE_ADDR + BOOTLOADER_SIZE)  /* 0x08004000 */
#define APP_MAX_SIZE         (512 * 1024 - BOOTLOADER_SIZE)       /* 496KB */
#define OTA_FLAG_BKP_DR      BKP_DR1        /* BKP 数据寄存器, 存 OTA 标志 */
#define OTA_FLAG_MAGIC       0xA5A5         /* OTA 进行中的魔术字 */
#define OTA_FLAG_DONE        0x5A5A         /* OTA 完成, 等待跳转 */

/* ---- ESP8266 UART ----
 * 精英版 ESP8266 接在 USART2 (PA2/PA3)
 */
#define ESP8266_UART          USART2
#define ESP8266_UART_IRQn     USART2_IRQn

/* ---- LCD (ILI9341 via FSMC) ----
 * 正点原子精英版 2.8" TFT LCD
 * FSMC Bank1 NE4, 数据线 16-bit
 */
#define LCD_BASE           0x6C000000    /* FSMC NE4 base (RS=0: cmd, RS=1: data) */
#define LCD_CMD_ADDR      (*(volatile uint16_t *)(LCD_BASE + 0x007FE))  /* A10=0 (PG0) */
#define LCD_DATA_ADDR     (*(volatile uint16_t *)(LCD_BASE + 0x00800))  /* A10=1 (PG0) */
#define LCD_WIDTH          320
#define LCD_HEIGHT         240  /* 横屏模式 (MADCTL=0x28) */

/* ---- LED 引脚 ----
 * 精英版 LED0=PB5, LED1=PE5 (均低电平点亮)
 */
#define LED1_PORT         GPIOB
#define LED1_PIN          GPIO_Pin_5
#define LED2_PORT         GPIOE
#define LED2_PIN          GPIO_Pin_5
#define LED_ACTIVE_LOW    1

/* ---- 蜂鸣器引脚 ---- */
#define BUZZER_PORT        GPIOB
#define BUZZER_PIN         GPIO_Pin_8
#define BUZZER_ACTIVE_HIGH 1

/* ---- DS18B20 温度传感器 (1-Wire) ----
 * 注意: PA0 在精英版上是 WKUP 按键引脚 (外部下拉电阻),
 *        开漏释放时被拉低 → 1-Wire 误检测存在 → 数据全 0
 *        改用 PG11 (扩展 IO, 无冲突) */
#define DS18B20_PORT       GPIOG
#define DS18B20_PIN        GPIO_Pin_11

/* ---- 光敏传感器 (ADC) ---- */
#define LIGHT_ADC           ADC1
#define LIGHT_ADC_CHANNEL   ADC_Channel_1

/* ---- 电机 PWM ---- */
#define MOTOR_TIM           TIM3
#define MOTOR_TIM_CHANNEL  1
#define MOTOR_TIM_PRESCALER 71
#define MOTOR_TIM_PERIOD    999

/* ---- 缓冲区大小 ---- */
#define RX_BUF_SIZE        2048
#define TX_BUF_SIZE        512
#define JSON_BUF_SIZE      256
#define MQTT_BUF_SIZE      256

/* ---- 超级循环节拍 ---- */
#define LOOP_PERIOD_MS       5     /* 主循环周期 (~5ms 一圈) */
#define SENSOR_PERIOD_MS     200   /* 传感器采集间隔 */
#define UI_UPDATE_PERIOD_MS  200   /* 仪表盘数值刷新间隔 */

/* ---- LVGL 配置 ----
 * v2.2 没有 FreeRTOS 抢 30KB 堆, 显示缓冲可以从 15 行加大到 20 行
 * 内存预算 (64KB SRAM):
 *   LVGL draw buf (双缓冲 static) . 320*20*2*2 = 25.6 KB
 *   LVGL 对象池 (lv_conf.h LV_MEM_SIZE) 24 KB
 *   ESP8266 ring buffer ............ 2 KB
 *   其他全局/static 变量 ........... ~3 KB
 *   主栈 MSP (启动文件 Stack_Size) . 4 KB
 *   合计 ≈ 59 KB, 剩余 ~5KB 余量
 * 如果链接报 RAM 溢出: 先减 LVGL_DISP_BUF_LINES (20→15, 省 6.4KB),
 *                     再减 lv_conf.h 的 LV_MEM_SIZE (24→20, 省 4KB) */
#define LVGL_DISP_BUF_LINES  20   /* 部分刷新, 每次 20 行 (320*20*2*2=25.6KB) */
#define LVGL_TICK_PERIOD_MS  1    /* LVGL tick 周期 (LV_TICK_CUSTOM 直读 sys_tick) */

/* ---- 调试开关 ----
 * 注意: DBG 宏使用全局缓冲区 (非栈), 避免每次调用消耗 128B 栈空间。
 * Keil 标准库 snprintf 内部已需 500-1500B 栈, 叠加局部缓冲极易栈溢出。
 * v2.2 无任务栈, 所有代码跑在 MSP 上 —— 启动文件 Stack_Size 建议设 0x1000 (4KB)!
 * 代价: 非可重入 (主循环与 ISR 同时 DBG 可能输出交错), 可接受 */
#define DEBUG_ENABLE    1

#if DEBUG_ENABLE
  extern char g_dbg_buf[128];
  #define DBG(fmt, ...)  do { \
      int n = snprintf(g_dbg_buf, sizeof(g_dbg_buf), fmt, ##__VA_ARGS__); \
      if (n > 0) debug_send((uint8_t*)g_dbg_buf, n); \
  } while(0)
#else
  #define DBG(fmt, ...)  ((void)0)
#endif

#endif /* STM32_V22_CONFIG_H */
