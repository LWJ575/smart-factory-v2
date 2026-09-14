# Keil 工程配置指南 — Smart Factory v2.2 (App, 纯 LVGL 无 FreeRTOS)

> v2.2 = LVGL v8.3 + MQTT + OTA, 超级循环架构 (无 RTOS)
> 姊妹版本: v2.1 = FreeRTOS + 正点原子 LCD 库 (无 LVGL)

## 1. 目录结构确认

```
smart-factory-v2/stm32-v22-lvgl/
├── app/main.c              ← 超级循环 (替代 v2.0 的 5 个任务)
├── bsp/bsp.c, lcd.c        ← bsp.c 为 v2.2 改版 (SysTick 计时)
├── core/json_helper.c
├── drivers/sensors.c, actuators.c
├── lvgl_port/lv_port_disp.c
├── mqtt/esp8266.c, mqtt_client.c, mqtt_packet.c
├── ota/ota_update.c        ← ota_task → ota_poll
├── ui/ui_main.c
├── config.h                ← 全局配置 (v2.2 改版)
├── lv_conf.h               ← LVGL 配置 (v2.2 改版: 内置内存池)
└── (LVGL 源码复用 ../stm32/third_party/lvgl/, 无需复制)
```

**不需要** `third_party/FreeRTOS/`、`FreeRTOSConfig.h`。

> LVGL v8.3.11 源码已下载在 `smart-factory-v2/stm32/third_party/lvgl/`,
> v2.2 直接引用该路径即可 (相对本目录: `..\stm32\third_party\lvgl\...`)。

## 2. Keil 工程 — 创建 Group 并添加文件

右键 Target → Manage Project Items, 创建以下 Group:

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

**不要添加**: `src\draw\arm2d\` `src\draw\nxp\` `src\draw\renesas\` `src\draw\sdl\`
`src\draw\stm32_dma2d\` `src\draw\swm341_dma2d\` `src\extra\libs\` `src\extra\others\` `src\extra\widgets\`

## 3. Include Paths

Options for Target → C/C++ → Include Paths (LVGL 用相对路径指向 v2.0 的源码):

```
.;app;bsp;core;drivers;lvgl_port;mqtt;ota;ui;..\stm32\third_party\lvgl;..\stm32\third_party\lvgl\src
```

> 注意: **没有** FreeRTOS 的两条路径了。

## 4. C/C++ 设置

- **C99 Mode**: ✅ 勾选 (必须! LVGL 需要)
- **Optimization**: Level 1 (-O1) 或 Level 2 (-O2)
- **One ELF Section per Function**: ✅ 勾选
- **Define**:
  ```
  LV_CONF_INCLUDE_SIMPLE, STM32F10X_HD, USE_STDPERIPH_DRIVER
  ```
- **MicroLIB**: ✅ 建议勾选 (Target 选项卡) —— snprintf 栈消耗从 ~1500B 降到 ~150B

## 5. Linker 设置 (App 工程)

Options for Target → Target 选项卡:
- **IROM1**: Start=`0x08004000`, Size=`0x7C000` (496KB, 跳过 16KB bootloader)
- **IRAM1**: Start=`0x20000000`, Size=`0x10000` (64KB)

> Bootloader 工程与 v2.0 完全共用 (`boot\boot_main.c` 无需改动)。

## 6. 启动文件 — ⚠️ v2.2 关键改动

使用标准 SPL 启动文件 `startup_stm32f10x_hd.s` (MDK 版), 需要修改两处:

### 6.1 Stack_Size 必须加大

v2.0 中每个任务有自己的栈 (任务栈由 FreeRTOS 分配); v2.2 所有代码
(传感器/MQTT/LVGL 渲染/snprintf 调用链) 都跑在主栈 MSP 上,
启动文件默认 `Stack_Size EQU 0x00000400` (1KB) **远远不够**:

```asm
Stack_Size      EQU     0x00001000   ; 4KB (原 0x400, 必须改!)
```

### 6.2 SysTick_Handler 冲突处理

bsp.c 定义了自己的 `SysTick_Handler`。启动文件里 `SysTick_Handler`
是弱符号, 会被自动覆盖, **不需要改启动文件**; 但要确认:

- 工程中**不要**加入任何 FreeRTOS 源文件 (port.c 里也有 SysTick_Handler)
- `stm32f10x_it.c` 中如已有 `SysTick_Handler`, 删掉它 (与 bsp.c 重复定义会链接报错)

> v2.0 中 FreeRTOSConfig.h 用宏把 SysTick_Handler 映射到
> xPortSysTickHandler 的做法在 v2.2 中不存在, 无需任何映射。

## 7. SPL 标准库文件

与 v2.0 相同 (正点原子精英版模板通常已有):
- `system_stm32f10x.c`, `stm32f10x_it.c`, `misc.c`
- `stm32f10x_gpio.c`, `stm32f10x_rcc.c`, `stm32f10x_usart.c`
- `stm32f10x_fsmc.c` (LCD)
- `stm32f10x_adc.c` (光敏)
- `stm32f10x_tim.c` (电机 PWM)
- `stm32f10x_flash.c` (OTA)
- `stm32f10x_bkp.c`, `stm32f10x_pwr.c` (BKP)

## 8. RAM 预算 (64KB)

| 项 | 大小 |
|----|------|
| LVGL 显示双缓冲 (320×20×2×2) | 25.6 KB |
| LVGL 对象池 (lv_conf.h LV_MEM_SIZE) | 24 KB |
| ESP8266 ring buffer | 2 KB |
| 其他全局/static | ~3 KB |
| 主栈 MSP (启动文件 Stack_Size) | 4 KB |
| **合计** | **≈ 59 KB** |

编译后检查:
- **RAM 使用量** < 62KB (64KB 可用)
- **Flash 使用量** < 256KB (496KB 可用)

如果 RAM 超了, 按顺序减:
1. `config.h` 的 `LVGL_DISP_BUF_LINES` 20 → 15 (省 6.4KB)
2. `lv_conf.h` 的 `LV_MEM_SIZE` 24 → 20 (省 4KB)
3. 启动文件 `Stack_Size` 0x1000 → 0xC00 (省 1KB, 有风险, 最后动)

## 9. 与 v2.0 / v2.1 的行为差异 (调试时注意)

1. **阻塞等待不冻结 UI**: `sys_delay_ms()` 内部调用 `bsp_idle_hook()`
   → `lv_timer_handler()`。DS18B20 750ms 转换、WiFi 20s 连接期间屏幕照常刷新。
2. **传感器在 MQTT 连接期间暂停**: WiFi/TCP 连接是阻塞步骤 (内部泵 UI
   但不回主循环), 期间温度显示暂停更新几秒, 属预期行为。
3. **无栈溢出钩子**: v2.0 的 `vApplicationStackOverflowHook` 不存在了。
   如果跑飞, 优先怀疑主栈不够 → 加大启动文件 `Stack_Size`。
4. **MQTT 回调直接执行控制指令**: 无队列, 回调在 `mqtt_poll()` 的
   main 上下文中直接操作 LED/电机, 不存在 v2.0 的跨任务延迟。
5. **OTA 指令处理**: `ota_poll()` 在主循环检查标志, 收到 "start" 后
   显示 OTA 界面 → 1s 后写 BKP 标志并复位 (与 v2.0 逻辑一致)。
