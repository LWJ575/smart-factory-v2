#ifndef STM32_V21_CONFIG_H
#define STM32_V21_CONFIG_H

#include "stm32f10x.h"
#include "bsp.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ============================================================
 * Smart Factory v2.1 — STM32 配置 (FreeRTOS, 无 LVGL)
 * 屏幕显示使用正点原子风格 LCD 直接绘制
 * 适配正点原子 STM32F103ZET6 精英版 + 2.8" ILI9341 LCD
 * 标准库 (SPL) 版本
 *
 * RAM 预算 (64KB SRAM, 无 LVGL):
 *   FreeRTOS heap (heap_4) .... 20 KB
 *   ESP8266 ring buffer ........ 2 KB
 *   其他全局变量 ............... ~2 KB
 *   任务栈 (7 个任务) .......... ~10 KB
 *   合计 ≈ 34 KB, 剩余 ~30 KB 供栈和 BSS
 * ============================================================ */

/* ---- 设备标识 ---- */
#define DEVICE_ID         "dev01"
#define DEVICE_LOCATION   "workshop-A"
#define FW_VERSION        "2.1.0"

/* ---- WiFi 配置 ---- */
#define WIFI_SSID         "YourWiFi"
#define WIFI_PASSWORD     "YourPassword"

/* ---- MQTT Broker 配置 ---- */
#define BROKER_IP         "192.168.1.178"
#define BROKER_PORT       1883
#define MQTT_KEEPALIVE    30

/* ---- 采集参数 ---- */
#define TELEMETRY_INTERVAL  3
#define TEMP_ALARM_HIGH     35

/* ---- OTA 配置 ---- */
#define OTA_HTTP_HOST      "192.168.1.178"
#define OTA_HTTP_PORT      8080
#define OTA_HTTP_PATH      "/firmware.bin"
#define OTA_FLASH_PAGE_SIZE  2048
#define OTA_CHUNK_SIZE       1024

/* ---- Flash 布局 ---- */
#define FLASH_BASE_ADDR      0x08000000
#define BOOTLOADER_SIZE      (16 * 1024)
#define APP_START_ADDR       (FLASH_BASE_ADDR + BOOTLOADER_SIZE)
#define APP_MAX_SIZE         (512 * 1024 - BOOTLOADER_SIZE)
#define OTA_FLAG_BKP_DR      BKP_DR1
#define OTA_FLAG_MAGIC       0xA5A5
#define OTA_FLAG_DONE        0x5A5A

/* ---- ESP8266 UART ---- */
#define ESP8266_UART          USART2
#define ESP8266_UART_IRQn     USART2_IRQn

/* ---- LCD (ILI9341 via FSMC) ---- */
#define LCD_BASE           0x6C000000
#define LCD_CMD_ADDR      (*(volatile uint16_t *)(LCD_BASE + 0x00000))
#define LCD_DATA_ADDR     (*(volatile uint16_t *)(LCD_BASE + 0x10000))
#define LCD_WIDTH          320
#define LCD_HEIGHT         240

/* ---- LED 引脚 ---- */
#define LED1_PORT         GPIOB
#define LED1_PIN          GPIO_Pin_5
#define LED2_PORT         GPIOE
#define LED2_PIN          GPIO_Pin_5
#define LED_ACTIVE_LOW    1

/* ---- 蜂鸣器引脚 ---- */
#define BUZZER_PORT        GPIOB
#define BUZZER_PIN         GPIO_Pin_8
#define BUZZER_ACTIVE_HIGH 1

/* ---- DS18B20 温度传感器 ---- */
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

/* ---- FreeRTOS 任务优先级 (无 LVGL 任务) ---- */
#define PRIO_SENSOR         2
#define PRIO_MQTT           3
#define PRIO_DISPLAY        1    /* 最低, 显示不抢占通信 */
#define PRIO_CONTROL        4
#define PRIO_OTA            5

/* ---- FreeRTOS 任务栈大小 (word = 4 bytes) ----
 * 无 LVGL, 栈需求大幅降低
 * 建议: Keil 中启用 MicroLIB 进一步减少 snprintf 栈消耗 */
#define STACK_SENSOR         384   /* 1536 bytes */
#define STACK_MQTT           384   /* 1536 bytes */
#define STACK_DISPLAY        192   /* 768 bytes (无LVGL, 仅LCD文字绘制) */
#define STACK_CONTROL        128   /* 512 bytes */
#define STACK_OTA            256   /* 1024 bytes */

/* ---- 调试开关 ---- */
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

#endif /* STM32_V21_CONFIG_H */
