#ifndef STM32_V2_CONFIG_H
#define STM32_V2_CONFIG_H

#include "stm32f10x.h"
#include "bsp.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ============================================================
 * Smart Factory v2.0 — STM32 配置 (FreeRTOS + LVGL + OTA)
 * 适配正点原子 STM32F103ZET6 精英版 + 2.8" ILI9341 LCD
 * 标准库 (SPL) 版本, 不需要 CubeMX
 * ============================================================ */

/* ---- 设备标识 ---- */
#define DEVICE_ID         "dev01"
#define DEVICE_LOCATION    "workshop-A"
#define FW_VERSION         "2.0.0"

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
#define LCD_CMD_ADDR      (*(volatile uint16_t *)(LCD_BASE + 0x00000))
#define LCD_DATA_ADDR     (*(volatile uint16_t *)(LCD_BASE + 0x10000))
#define LCD_WIDTH          320
#define LCD_HEIGHT         240  /* 横屏模式 (MADCTL=0x28) */

/* ---- 触摸屏 (XPT2046 SPI) ----
 * 正点原子精英版触摸芯片: XPT2046, SPI1
 * CS=PA4, SCK=PA5, MISO=PA6, MOSI=PA7
 */
#define TCK_CS_PORT        GPIOA
#define TCK_CS_PIN         GPIO_Pin_4
#define TCK_SCK_PORT       GPIOA
#define TCK_SCK_PIN        GPIO_Pin_5
#define TCK_MISO_PORT      GPIOA
#define TCK_MISO_PIN       GPIO_Pin_6
#define TCK_MOSI_PORT      GPIOA
#define TCK_MOSI_PIN       GPIO_Pin_7

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

/* ---- LVGL 配置 ---- */
#define LVGL_DISP_BUF_LINES  15   /* 部分刷新, 每次 15 行 (320*15*2*2=19.2KB, 适配 64KB SRAM) */
#define LVGL_TICK_PERIOD_MS  1    /* LVGL tick 周期 */

/* ---- FreeRTOS 任务优先级 ---- */
#define PRIO_SENSOR         2
#define PRIO_MQTT            3
#define PRIO_LVGL            1    /* 最低, UI 不抢占通信 */
#define PRIO_CONTROL         4
#define PRIO_OTA             5    /* OTA 最高, 独占 */

/* ---- FreeRTOS 任务栈大小 (word = 4 bytes) ----
 * 注意: Keil 标准库 snprintf 内部消耗 500-1500B 栈 (远超预期)。
 * 已将 DBG 缓冲区移到全局 BSS (省 128B/帧), 但仍需充足栈空间。
 * 建议: Keil 中启用 MicroLIB 可将 snprintf 栈消耗降至 ~150B */
#define STACK_SENSOR         768   /* 3072 bytes (snprintf+ds18b20+ow调用链) */
#define STACK_MQTT           512   /* 2048 bytes (esp8266 缓冲区+snprintf+DBG) */
#define STACK_LVGL           768   /* 3072 bytes (lv_timer_handler+渲染+snprintf) */
#define STACK_CONTROL        256   /* 1024 bytes (DBG+队列操作) */
#define STACK_OTA            512   /* 2048 bytes (HTTP+Flash+snprintf) */

/* ---- 调试开关 ----
 * 注意: DBG 宏使用 static 全局缓冲区 (非栈), 避免每次调用消耗 128B 栈空间。
 * Keil 标准库 snprintf 内部已需 500-1500B 栈, 叠加 128B 局部缓冲极易栈溢出。
 * 代价: 非可重入 (多个任务同时 DBG 可能输出交错), 但调试输出可接受。 */
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

#endif /* STM32_V2_CONFIG_H */
