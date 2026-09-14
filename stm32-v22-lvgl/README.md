# Smart Factory v2.2 — 纯 LVGL + MQTT + OTA (超级循环)

> **版本拆分说明**: v2.0 (FreeRTOS + LVGL) 在 STM32F103 的 64KB SRAM 上反复栈溢出,
> 拆分为两个可独立运行的版本:
>
> | 版本 | 目录 | 架构 | 显示方案 |
> |------|------|------|----------|
> | v2.1 | `../stm32-v21-freertos/` | FreeRTOS 多任务 | 正点原子 LCD 库 (8x16 字库直写 FSMC) |
> | **v2.2** | `../stm32-v22-lvgl/` | **超级循环 (无 RTOS)** | **LVGL v8.3 (双 partial buffer)** |
>
> 两个版本共用同一套 Bootloader (16KB, BKP 标志 + HTTP OTA) 和 MQTT 主题协议,
> 固件可互换烧录, 上位机 (i.MX6ULL Qt / PC) 无感知。

## 架构概览

```
STM32F103ZET6 (超级循环)                    PC (Mosquitto)       i.MX6ULL (Qt)
┌─────────────────────────┐                  ┌──────────┐        ┌──────────┐
│ Bootloader (16KB)       │                  │ MQTT     │        │ Dashboard│
│  BKP flag → jump/OTA    │←── WiFi/MQTT ──→│ Broker   │←──────→│ OTA Mgr  │
├─────────────────────────┤                  │ :1883    │        └──────────┘
│ App (496KB)             │                  └──────────┘             │
│  while(1) 超级循环:      │                         │                   │
│   sensor_poll()  200ms  │←──── HTTP GET (OTA) ────┴── firmware.bin ──┘
│   mqtt_poll()    状态机  │
│   control_poll() 按钮    │
│   ui_poll()     LVGL    │
│   ota_poll()            │
│  LVGL UI → 2.8" LCD     │
│  ESP8266 WiFi + MQTT    │
│  DS18B20 / ADC / PWM    │
└─────────────────────────┘
```

## 核心设计 (与 v2.0 的差异)

### 1. 任务 → 轮询函数
v2.0 的 5 个 FreeRTOS 任务变成 5 个 `*_poll()` 函数, 由 `while(1)` 主循环
约 5ms 一圈轮询。任务间通信 (队列/互斥锁) 全部取消 —— 所有代码跑在 main
上下文, 天然串行, 无需同步原语。

### 2. 阻塞等待不冻结 UI (关键机制)
v2.2 没有 RTOS 抢占, DS18B20 转换 (750ms) 和 WiFi 连接 (最长 20s) 都是阻塞等待。
解决: `sys_delay_ms()` 等待期间反复调用 `bsp_idle_hook()` → `lv_timer_handler()`,
即 **在延时里泵 LVGL**。屏幕在整个阻塞期间保持刷新, 只有传感器数值暂停更新。

```
sys_delay_ms(750)          ← DS18B20 转换等待
  └─ while (未到时间)
       └─ bsp_idle_hook()  ← main.c 实现 (防重入保护)
            └─ lv_timer_handler()  ← LVGL 渲染继续跑
```

### 3. SysTick 接管
FreeRTOS 不在了, SysTick (1ms) 由 `bsp.c` 接管做毫秒计时;
LVGL 时间源通过 `LV_TICK_CUSTOM=1` 直读 `sys_tick()`, 无需任何 tick hook。

### 4. LVGL 内存独立
`lv_conf.h` 改为 `LV_MEM_CUSTOM=0` (内置对象池 24KB),
不再借用 FreeRTOS 堆, 也不依赖启动文件的 C 库 Heap_Size。

## RAM 预算 (64KB)

| 项 | 大小 |
|----|------|
| LVGL 显示双缓冲 (320×20×2×2) | 25.6 KB |
| LVGL 对象池 (LV_MEM_SIZE) | 24 KB |
| ESP8266 ring buffer | 2 KB |
| 其他全局/static | ~3 KB |
| 主栈 MSP (**启动文件 Stack_Size 必须 0x1000**) | 4 KB |
| **合计** | **≈ 59 KB** |

> ⚠️ v2.2 所有代码 (含 snprintf/LVGL 渲染深调用链) 都跑在 MSP 上,
> 启动文件默认 1KB 栈必溢出, 务必按 `KEIL_SETUP.md` 第 6 节修改。

## 文件结构

```
smart-factory-v2/stm32-v22-lvgl/
├── boot/boot_main.c        # Bootloader (与 v2.0 完全共用)
├── app/main.c              # 超级循环主程序 (v2.2 新写)
├── bsp/bsp.c               # SysTick 计时 + idle hook + Flash/BKP (v2.2 改版)
├── bsp/lcd.c               # ILI9341 via FSMC (与 v2.0 相同)
├── lvgl_port/lv_port_disp.c# LVGL 显示移植 (去掉 tick hook)
├── ui/ui_main.c            # LVGL 仪表盘 + OTA 界面 (与 v2.0 相同)
├── mqtt/esp8266.c          # 去掉互斥锁 (单线程无需保护)
├── mqtt/mqtt_client.c      # MQTT 3.1.1 纯 C 实现 (与 v2.0 相同)
├── mqtt/mqtt_packet.c      # 报文编解码 (与 v2.0 相同)
├── drivers/                # DS18B20/ADC/PWM/LED/蜂鸣器 (与 v2.0 相同)
├── ota/ota_update.c        # ota_task → ota_poll (v2.2 改版)
├── config.h                # v2.2 配置 (无任务栈/优先级/FreeRTOS 堆)
├── lv_conf.h               # LVGL 配置 (内置内存池 24KB)
└── KEIL_SETUP.md           # Keil 工程搭建步骤 (含启动文件修改!)
```

## 快速开始

见 [KEIL_SETUP.md](KEIL_SETUP.md) —— 注意 **第 6 节启动文件修改是 v2.2 必做项**
(Stack_Size 0x400 → 0x1000, 检查 stm32f10x_it.c 无 SysTick_Handler 冲突)。
