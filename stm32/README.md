# Smart Factory v2.0 — FreeRTOS + LVGL + OTA

## 架构概览

```
STM32F103ZET6 (FreeRTOS)                    PC (Mosquitto)       i.MX6ULL (Qt)
┌─────────────────────────┐                  ┌──────────┐        ┌──────────┐
│ Bootloader (16KB)       │                  │ MQTT     │        │ Dashboard│
│  BKP flag → jump/OTA    │←── WiFi/MQTT ──→│ Broker   │←──────→│ OTA Mgr  │
├─────────────────────────┤                  │ :1883    │        │          │
│ App (496KB)             │                  └──────────┘        └──────────┘
│  ┌───────────────────┐ │                         │                    │
│  │ FreeRTOS Tasks    │ │                         │                    │
│  │ Sensor MQTT LVGL  │ │←──── HTTP GET (OTA) ────┘──── firmware.bin ───┘
│  │ Control OTA Watch │ │
│  └───────────────────┘ │
│  LVGL UI → 2.8" LCD    │
│  ESP8266 WiFi + MQTT   │
│  DS18B20 / ADC / PWM   │
└─────────────────────────┘
```

## Flash 布局

| 地址范围                    | 大小    | 内容                                     |
| ----------------------- | ----- | -------------------------------------- |
| 0x08000000 - 0x08003FFF | 16KB  | Bootloader                             |
| 0x08004000 - 0x0807FFFF | 496KB | Application (含 FreeRTOS + LVGL + MQTT) |

## 文件结构

```
smart-factory-v2/stm32/
├── boot/
│   └── boot_main.c          # Bootloader: WiFi+HTTP下载+Flash写入+跳转
├── app/
│   └── main.c               # 主应用: 外设初始化 + FreeRTOS任务创建
├── bsp/
│   ├── bsp.h / bsp.c        # 系统时钟/DWT延时/Flash操作/BKP/跳转
│   ├── lcd.h / lcd.c        # ILI9341 via FSMC 驱动
├── lvgl_port/
│   └── lv_port_disp.h/.c    # LVGL显示驱动移植(flush回调+tick)
├── ui/
│   └── ui_main.h / ui_main.c  # LVGL界面: 仪表盘+OTA进度
├── mqtt/
│   ├── esp8266.h / esp8266.c   # ESP8266 AT驱动(FreeRTOS互斥锁版)
│   ├── mqtt_packet.h / .c     # MQTT 3.1.1 报文编解码(纯C,复用v1)
│   └── mqtt_client.h / .c     # MQTT客户端(复用v1, sys_tick替换)
├── drivers/
│   ├── sensors.h / .c         # DS18B20温度+光敏ADC(复用v1)
│   └── actuators.h / .c       # LED+蜂鸣器+PWM电机(复用v1)
├── core/
│   ├── FreeRTOSConfig.h       # FreeRTOS配置
│   ├── json_helper.h / .c    # JSON打包/解析(纯C,复用v1)
├── ota/
│   └── ota_update.h / .c      # OTA触发(App端:设BKP标志+重启)
├── config.h                   # 全局配置(WiFi/Broker/引脚/OTA)
└── README.md                  # 本文件

tools/
├── ota_file_server.py        # HTTP文件服务器(提供firmware.bin下载)
└── ota_trigger.py            # MQTT OTA指令发送脚本
```

## Keil 工程配置

### 1. Bootloader 工程 (独立工程)

- 新建 Keil 工程, 芯片选 STM32F103ZET6
- 添加 `boot/boot_main.c` 到工程
- **Options → Target → IROM1**: Start=`0x08000000`, Size=`0x4000` (16KB)
- **Options → Target → IRAM1**: Start=`0x20000000`, Size=`0x10000` (64KB)
- Include Paths: 添加 SPL 库路径
- 编译生成 `.axf`, 用 `fromelf --bin` 转为 `.bin`

### 2. Application 工程

- 新建 Keil 工程, 芯片选 STM32F103ZET6
- 添加所有 `.c` 文件 (除 `boot_main.c`)
- **Options → Target → IROM1**: Start=`0x08004000`, Size=`0x7C000` (496KB)
- Include Paths:
  ```
  .\core
  .\bsp
  .\lvgl_port
  .\ui
  .\mqtt
  .\drivers
  .\ota
  .\FreeRTOS\portable\GCC\ARM_CM3  (或 RVDS\ARM_CM3)
  .\FreeRTOS\src
  .\FreeRTOS\include
  .\lvgl
  .\lvgl\src
  ```
- **Define**: `USE_STDPERIPH_DRIVER,STM32F10X_HD`
- 勾选 **Use MicroLIB**
- 链接: 确保 `.sct` scatter file 中 App 起始为 `0x08004000`
- 生成 `.bin` 用于 OTA: `fromelf --bin -o firmware.bin app.axf`

### 3. 烧录顺序

1. **首次**: 用 J-Link/ST-Link 烧录 Bootloader 到 `0x08000000`
2. **首次**: 用 J-Link/ST-Link 烧录 App 到 `0x08004000`
3. **后续**: 通过 OTA 远程更新 App (不影响 Bootloader)

## LVGL 集成

### 依赖

- LVGL v8.x (从 <https://github.com/lvgl/lvgl> 下载)
- 把 `lvgl/` 目录放入工程的 include path
- 把 `lv_conf.h` 配置文件放入 include path

### 关键配置 (lv_conf.h)

```c
#define LV_COLOR_DEPTH          16     /* RGB565 */
#define LV_HOR_RES_MAX          240
#define LV_VER_RES_MAX          320
#define LV_FONT_MONTSERRAT_14   1      /* 基础字体 */
#define LV_FONT_MONTSERRAT_16   1      /* 标题字体 */
#define LV_FONT_MONTSERRAT_20   1      /* 大字号 */
#define LV_FONT_MONTSERRAT_28   1      /* 温度显示 */
#define LV_USE_PERF_MONITOR     0      /* 关闭性能监控(省RAM) */
#define LV_MEM_SIZE            (32 * 1024U)  /* 32KB LVGL堆 */
#define LV_MEM_CUSTOM           0      /* 用LVGL自带内存管理 */
```

### RAM 预算 (64KB Total)

| 组件                      | 大小                      |
| ----------------------- | ----------------------- |
| FreeRTOS 堆 (任务栈+队列+信号量) | ~12KB                   |
| LVGL 堆                  | 32KB                    |
| LVGL 显示缓冲 (240×40×2 ×2) | ~38KB → **改用单缓冲** ~19KB |
| 环形缓冲区                   | 2KB                     |
| 其他全局变量                  | ~2KB                    |

> **注意**: 64KB RAM 比较紧张。如果 RAM 不够:
>
> 1. 把 LVGL 显示缓冲从双缓冲改为单缓冲 (`s_buf1` only, `s_buf2` = NULL)
> 2. 减少 LVGL 堆到 24KB
> 3. 减少任务栈大小

## OTA 升级流程

```
i.MX6ULL/PC                STM32 App              STM32 Bootloader
     │                        │                        │
     │ MQTT: ota/start         │                        │
     ├───────────────────────→│                        │
     │                        │ 显示 OTA 界面            │
     │                        │ 设置 BKP_DR1=0xA5A5     │
     │                        │ 系统复位                │
     │                        │                        │
     │                        │  ┌──────────────────┐  │
     │                        │  │ Bootloader 启动   │  │
     │                        │  │ 检测 BKP 标志     │  │
     │                        │  │ 连接 WiFi        │  │
     │                        │  │ HTTP GET 固件    │  │
     │                        │  │ 擦写 Flash       │  │
     │                        │  │ 校验             │  │
     │                        │  │ 跳转到新 App     │  │
     │                        │  └──────────────────┘  │
     │                        │                        │
     │ HTTP: firmware.bin      │                        │
     │←─────────────────────────────────────────────────┤
     │                        │                        │
     │                        │ 新固件运行              │
```

### 操作步骤

1. **启动 HTTP 文件服务器** (PC 上):
   ```bash
   cd tools
   python3 ota_file_server.py 8080 ../stm32/firmware.bin
   ```
2. **确保 mosquitto broker 在运行** (PC 上):
   ```bash
   # 管理员 PowerShell
   & "C:\Program Files (x86)\Mosquitto\mosquitto.exe" -c "C:\Users\14645\mosquitto_factory.conf" -v
   ```
3. **发送 OTA 触发指令**:
   ```bash
   python3 tools/ota_trigger.py dev01 192.168.1.178 1883
   ```
4. **观察 STM32**:
   - LCD 显示 "Firmware Update" 界面
   - 串口 (USART1, 115200) 输出 OTA 进度日志
   - LED (PB5) 闪烁表示正在写入 Flash
   - 完成后自动重启到新固件

## FreeRTOS 任务一览

| 任务      | 优先级 | 栈大小   | 功能                  |
| ------- | --- | ----- | ------------------- |
| Sensor  | 2   | 512B  | 采集温度/光照, 3秒上报遥测     |
| MQTT    | 3   | 1024B | WiFi连接/MQTT收发/心跳/重连 |
| LVGL    | 1   | 2048B | UI渲染(30fps)+数据刷新    |
| Control | 4   | 512B  | 执行器响应(MQTT指令+本地按钮)  |
| OTA     | 5   | 1024B | 等待OTA触发, 最高优先级      |

## v1 → v2 变更对照

| 项目    | v1.0       | v2.0                            |
| ----- | ---------- | ------------------------------- |
| 架构    | 裸机轮询       | FreeRTOS 多任务                    |
| 显示    | 无屏幕        | 2.8" LCD + LVGL GUI             |
| 通信    | MQTT AT 直发 | MQTT + FreeRTOS 互斥锁             |
| 升级    | 无          | HTTP OTA 远程固件升级                 |
| Flash | 单一应用       | Bootloader (16KB) + App (496KB) |
| 调试    | printf 到串口 | printf + LVGL 界面                |

## 硬件接线 (与 v1 相同)

| 外设      | 引脚                | 说明                |
| ------- | ----------------- | ----------------- |
| ESP8266 | USART2 (PA2/PA3)  | WiFi + MQTT       |
| 调试串口    | USART1 (PA9/PA10) | 115200 8N1        |
| LCD     | FSMC NE4 + 数据线    | ILI9341 16-bit 并行 |
| LCD 背光  | PB0               | 高电平点亮             |
| DS18B20 | PA0               | 1-Wire, 外接        |
| 光敏      | ADC1 CH1 (PA1)    | 板载                |
| LED0    | PB5               | 低电平亮              |
| LED1    | PE5               | 低电平亮              |
| 蜂鸣器     | PB8               | 高电平响              |
| 电机 PWM  | TIM3 CH1 (PA6)    | 外接 L298N          |

## 注意事项

1. **Bootloader 中的 WiFi 信息**: `boot_main.c` 中硬编码了 WiFi SSID/密码, 需要与 `config.h` 一致
2. **Keil scatter file**: App 工程的 ROM 起始必须设为 `0x08004000`, 否则向量表偏移错误
3. **NVIC 优先级**: FreeRTOS 要求中断优先级 >= `configMAX_SYSCALL_INTERRUPT_PRIORITY` 才能调 FreeRTOS API
4. **注释掉重复中断**: 在 `stm32f10x_it.c` 中注释掉 `SysTick_Handler` 和 `USART2_IRQHandler`
5. **LVGL 字体**: 只勾选需要的字体大小, 每种字体占用 Flash 空间
6. **OTA 风险**: 单 Bank Flash, 升级中途断电会变砖。Bootloader 支持串口恢复(可扩展)

