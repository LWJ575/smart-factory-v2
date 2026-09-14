# Keil 工程配置指南 — Smart Factory v2.0 (App)

## 1. 目录结构确认

```
smart-factory-v2/stm32/
├── app/main.c
├── bsp/bsp.c, lcd.c
├── core/json_helper.c, FreeRTOSConfig.h
├── drivers/sensors.c, actuators.c
├── lvgl_port/lv_port_disp.c
├── mqtt/esp8266.c, mqtt_client.c, mqtt_packet.c
├── ota/ota_update.c
├── ui/ui_main.c
├── config.h          ← 全局配置
├── lv_conf.h         ← LVGL 配置 (已生成)
├── third_party/
│   ├── lvgl/         ← LVGL v8.3.11 (已下载)
│   └── FreeRTOS/     ← FreeRTOS v10.4.3 (已下载)
```

## 2. Keil 工程 — 创建 Group 并添加文件

在 Keil 左侧 Project 面板, 右键 Target → Manage Project Items, 创建以下 Group:

### Group: Application
- `app\main.c`

### Group: BSP
- `bsp\bsp.c`
- `bsp\lcd.c`

### Group: Core
- `core\json_helper.c`

### Group: Drivers
- `drivers\sensors.c`
- `drivers\actuators.c`

### Group: MQTT
- `mqtt\esp8266.c`
- `mqtt\mqtt_client.c`
- `mqtt\mqtt_packet.c`

### Group: UI
- `ui\ui_main.c`

### Group: LVGL_Port
- `lvgl_port\lv_port_disp.c`

### Group: OTA
- `ota\ota_update.c`

### Group: FreeRTOS
- `third_party\FreeRTOS\tasks.c`
- `third_party\FreeRTOS\queue.c`
- `third_party\FreeRTOS\list.c`
- `third_party\FreeRTOS\timers.c`
- `third_party\FreeRTOS\event_groups.c`
- `third_party\FreeRTOS\stream_buffer.c`
- `third_party\FreeRTOS\portable\RVDS\ARM_CM3\port.c`  ← Keil/ARMCC 用 RVDS 版
- `third_party\FreeRTOS\portable\MemMang\heap_4.c`

### Group: LVGL
添加以下 **每个目录** 里的 **全部 .c 文件** (Ctrl+A 全选):

| 目录 | 说明 |
|------|------|
| `third_party\lvgl\src\core\` | 核心对象系统 |
| `third_party\lvgl\src\draw\` | 绘制引擎 (只加这一层, 不进子目录) |
| `third_party\lvgl\src\draw\sw\` | 软件渲染器 |
| `third_party\lvgl\src\font\` | 字体 (未启用的会被 #if 跳过) |
| `third_party\lvgl\src\hal\` | HAL (显示/输入/tick) |
| `third_party\lvgl\src\misc\` | 工具 (内存/动画/样式/定时器等) |
| `third_party\lvgl\src\widgets\` | 基础控件 |
| `third_party\lvgl\src\extra\` | extra 初始化 (只加这一层的 lv_extra.c) |
| `third_party\lvgl\src\extra\layouts\flex\` | Flex 布局 |
| `third_party\lvgl\src\extra\layouts\grid\` | Grid 布局 |
| `third_party\lvgl\src\extra\themes\basic\` | Basic 主题 |
| `third_party\lvgl\src\extra\themes\default\` | Default 主题 |
| `third_party\lvgl\src\extra\themes\mono\` | Mono 主题 |

**不要添加** 以下目录 (硬件专用, STM32F1 用不到):
- `src\draw\arm2d\` `src\draw\nxp\` `src\draw\renesas\` `src\draw\sdl\`
- `src\draw\stm32_dma2d\` `src\draw\swm341_dma2d\`
- `src\extra\libs\` (PNG/GIF/BMP 等, 都已在 lv_conf.h 关闭)
- `src\extra\others\` (fragment/monkey/snapshot 等)
- `src\extra\widgets\` (calendar/chart/menu 等, 都已关闭)

> 提示: 未启用的 .c 文件即便加了也不会出错, 因为 LVGL 内部有 `#if LV_USE_XXX` 保护,
> 编译器会跳过空文件, 只是稍慢. 如果不确定就多加, 比漏加好.

## 3. Include Paths

Options for Target → C/C++ → Include Paths, 添加以下路径 (相对于 stm32/ 目录):

```
.;app;bsp;core;drivers;lvgl_port;mqtt;ota;ui;third_party\lvgl;third_party\lvgl\src;third_party\FreeRTOS\include;third_party\FreeRTOS\portable\RVDS\ARM_CM3
```

或分行写:
```
.                                       (config.h, lv_conf.h, FreeRTOSConfig.h)
app
bsp
core
drivers
lvgl_port
mqtt
ota
ui
third_party\lvgl                        (lvgl.h)
third_party\lvgl\src                    (LVGL 内部头文件)
third_party\FreeRTOS\include            (FreeRTOS.h, task.h 等)
third_party\FreeRTOS\portable\RVDS\ARM_CM3  (portmacro.h)
```

## 4. C/C++ 设置

Options for Target → C/C++ 选项卡:

- **C99 Mode**: ✅ 勾选 (必须! LVGL 和 FreeRTOS 都需要)
- **Optimization**: Level 1 (-O1) 或 Level 2 (-O2) (Level 0 会导致 Flash 不够)
- **One ELF Section per Function**: ✅ 勾选 (配合 -O1 可以省 Flash)
- **Define** (预定义宏):
  ```
  LV_CONF_INCLUDE_SIMPLE, STM32F10X_HD, USE_STDPERIPH_DRIVER
  ```
  - `LV_CONF_INCLUDE_SIMPLE` — 让 LVGL 通过 include path 找 lv_conf.h
  - `STM32F10X_HD` — SPL 需要, 表示大容量芯片
  - `USE_STDPERIPH_DRIVER` — 使用标准外设库

## 5. Linker 设置 (App 工程)

Options for Target → Target 选项卡:
- **IROM1**: Start=`0x08004000`, Size=`0x7C000` (496KB, 跳过 16KB bootloader)
- **IRAM1**: Start=`0x20000000`, Size=`0x10000` (64KB)

> Bootloader 工程单独建, IROM1: Start=`0x08000000`, Size=`0x4000` (16KB)

## 6. 启动文件

使用标准 SPL 启动文件: `startup_stm32f10x_hd.s` (MDK 版)

在启动文件中确认中断向量名与 FreeRTOSConfig.h 一致:
- `PendSV_Handler` → FreeRTOS 的 `xPortPendSVHandler`
- `SVC_Handler` → FreeRTOS 的 `vPortSVCHandler`
- `SysTick_Handler` → FreeRTOS 的 `xPortSysTickHandler`

(FreeRTOSConfig.h 中已用 #define 映射, 不需要改启动文件)

## 7. SPL 标准库文件

确保工程中包含 STM32F10x SPL 的以下文件 (正点原子精英版模板通常已有):
- `system_stm32f10x.c` (系统时钟)
- `stm32f10x_it.c` (中断处理, 需删除其中的 SysTick_Handler/PendSV_Handler/SVC_Handler 避免与 FreeRTOS 冲突)
- `misc.c` (NVIC)
- `stm32f10x_gpio.c`, `stm32f10x_rcc.c`, `stm32f10x_usart.c`
- `stm32f10x_fsmc.c` (LCD)
- `stm32f10x_adc.c` (光敏)
- `stm32f10x_tim.c` (电机 PWM)
- `stm32f10x_flash.c` (OTA)
- `stm32f10x_bkp.c`, `stm32f10x_pwr.c` (BKP)

## 8. 编译验证

编译后检查:
- **Flash 使用量** < 256KB (496KB 可用, 但留余量)
- **RAM 使用量** < 60KB (64KB 可用, 留 4KB 给栈)

如果 RAM 不够:
- 减小 `config.h` 中的 `LVGL_DISP_BUF_LINES` (20→10)
- 减小 `FreeRTOSConfig.h` 中的 `configTOTAL_HEAP_SIZE` (30→24)

如果 Flash 不够:
- 提高 Optimization 到 -O2
- 勾选 One ELF Section per Function
- 在 lv_conf.h 关闭更多不用的字体 (只留 montserrat_14)
