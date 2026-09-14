# Keil 工程配置指南 — Smart Factory v2.1 (FreeRTOS + 正点原子 LCD)

## 问题诊断

链接器报 `Undefined symbol` = 对应 `.c` 源文件未加入 Keil 工程编译。

本次报错全部指向 **3 个漏加的文件**：

| 漏加文件 | 缺失符号 |
|----------|---------|
| `bsp/bsp.c` | bkp_init, bkp_write, bsp_tick_hook, debug_init, debug_send, dwt_init, g_dbg_buf, sys_clock_init, sys_tick, sys_delay_ms, sys_delay_us, system_reset |
| `bsp/lcd.c` | lcd_fill, lcd_init, lcd_set_window |
| `core/json_helper.c` | json_build_telemetry, json_parse_control |

## 1. 目录结构

```
smart-factory-v2/stm32-v21-freertos/
├── app/main.c
├── boot/boot_main.c              ← Bootloader 工程
├── bsp/bsp.c, lcd.c
├── core/json_helper.c, FreeRTOSConfig.h
├── display/display.c
├── drivers/sensors.c, actuators.c
├── mqtt/esp8266.c, mqtt_client.c, mqtt_packet.c
├── ota/ota_update.c
├── config.h
└── (third_party/ 复用 ../stm32/third_party/)
```

## 2. Keil 工程 — Group 与文件清单

在 Keil Project 面板右键 → Manage Project Items，确认以下 **全部 11 个** `.c` 文件都在工程里：

### Group: Application
- `app\main.c`

### Group: BSP  ← **本次漏加!**
- `bsp\bsp.c`      ← **加入**
- `bsp\lcd.c`      ← **加入**

### Group: Core  ← **本次漏加!**
- `core\json_helper.c`  ← **加入**

### Group: Display
- `display\display.c`

### Group: Drivers
- `drivers\sensors.c`
- `drivers\actuators.c`

### Group: MQTT
- `mqtt\esp8266.c`
- `mqtt\mqtt_client.c`
- `mqtt\mqtt_packet.c`

### Group: OTA
- `ota\ota_update.c`

### Group: FreeRTOS
> 路径相对于 `stm32-v21-freertos/`，实际指向 `../stm32/third_party/FreeRTOS/`
- `..\..\stm32\third_party\FreeRTOS\tasks.c`
- `..\..\stm32\third_party\FreeRTOS\queue.c`
- `..\..\stm32\third_party\FreeRTOS\list.c`
- `..\..\stm32\third_party\FreeRTOS\timers.c`
- `..\..\stm32\third_party\FreeRTOS\event_groups.c`
- `..\..\stm32\third_party\FreeRTOS\stream_buffer.c`
- `..\..\stm32\third_party\FreeRTOS\portable\RVDS\ARM_CM3\port.c`
- `..\..\stm32\third_party\FreeRTOS\portable\MemMang\heap_4.c`

### Group: SPL (正点原子模板通常已有)
- `system_stm32f10x.c`
- `stm32f10x_it.c` (需删除其中的 SysTick_Handler / PendSV_Handler / SVC_Handler)
- `misc.c`
- `stm32f10x_gpio.c`, `stm32f10x_rcc.c`, `stm32f10x_usart.c`
- `stm32f10x_fsmc.c` (LCD)
- `stm32f10x_adc.c` (光敏)
- `stm32f10x_tim.c` (电机 PWM)
- `stm32f10x_flash.c` (OTA)
- `stm32f10x_bkp.c`, `stm32f10x_pwr.c` (BKP)

### Group: Startup
- `startup_stm32f10x_hd.s`

**不需要**：LVGL 组、`lv_port_disp.c`、`ui_main.c`、`lv_conf.h`

## 3. Include Paths

Options for Target → C/C++ → Include Paths：

```
.;app;bsp;core;display;drivers;mqtt;ota;..\..\stm32\third_party\FreeRTOS\include;..\..\stm32\third_party\FreeRTOS\portable\RVDS\ARM_CM3
```

或分行写：
```
.                                        (config.h)
app
bsp
core                                     (FreeRTOSConfig.h)
display
drivers
mqtt
ota
..\..\stm32\third_party\FreeRTOS\include
..\..\stm32\third_party\FreeRTOS\portable\RVDS\ARM_CM3
```

## 4. C/C++ 设置

- **C99 Mode**: 勾选 (FreeRTOS 需要)
- **Optimization**: -O1 或 -O2
- **One ELF Section per Function**: 勾选 (省 Flash)
- **Define**:
  ```
  STM32F10X_HD, USE_STDPERIPH_DRIVER
  ```
  (v2.1 不需要 `LV_CONF_INCLUDE_SIMPLE`)

## 5. Linker 设置

Options for Target → Target 选项卡:
- **IROM1**: Start=`0x08004000`, Size=`0x7C000` (跳过 16KB bootloader)
- **IRAM1**: Start=`0x20000000`, Size=`0x10000` (64KB)

## 6. 启动文件注意

- 使用 `startup_stm32f10x_hd.s` (MDK 版)
- `stm32f10x_it.c` 中**删除** `SysTick_Handler`、`PendSV_Handler`、`SVC_Handler`（FreeRTOS 已接管）
- FreeRTOSConfig.h 中已用 `#define` 映射这三个向量，不需要改启动文件

## 7. RAM 预算 (64KB)

| 项目 | 大小 |
|------|------|
| FreeRTOS heap | 20KB |
| 7 个任务栈 | ~7KB |
| ESP8266 ring buffer | 2KB |
| 其他全局/BSS | ~3KB |
| **合计** | **~32KB** (余量充足) |

## 8. 快速修复步骤

1. Keil 左侧 Project 面板 → 右键 BSP group → Add Existing Files → 选 `bsp\bsp.c` 和 `bsp\lcd.c`
2. 右键 Core group → Add Existing Files → 选 `core\json_helper.c`
3. Project → Rebuild All (必须 Rebuild，不能只 Build，否则旧 .o 不会重新链接)
