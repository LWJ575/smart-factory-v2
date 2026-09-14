# Smart Factory v2 — 基于 STM32F103ZET6 的智能工厂项目

正点原子精英版 (STM32F103ZET6, SPL 标准库, Keil MDK ARMCC v5.06) 智能工厂项目，
涵盖传感器采集 / 执行器控制 / ESP8266 联网 / MQTT 通信 / LCD+LVGL 仪表盘 / OTA 远程升级。

## 目录结构

| 目录 | 说明 |
|------|------|
| `stm32/` | v2.0 历史版本 (FreeRTOS+LVGL 共存, 因 64KB SRAM 不足已归档) |
| `stm32-v21-freertos/` | **v2.1**: 纯 FreeRTOS + 正点原子 LCD 点阵显示 (已跑通) |
| `stm32-v22-lvgl/` | **v2.2**: 纯 LVGL v8.3 + 超级循环仪表盘 (已跑通) |
| `stm32-ota-demo/` | OTA 独立测试工程: bootloader v1.2 (Range 分块下载) + 演示 App |
| `tools/` | 辅助工具 |
| `docs/` | 文档 |

每个工程内含 `KEIL_SETUP.md` / `README.md`，包含完整的建工程步骤、必改配置项
(Stack_Size、IROM 地址等) 与排错记录。

## 硬件连接

- LCD: ILI9341 2.8" 320x240 横屏, FSMC NE4 + **A10 (PG0)** 作 RS
- ESP8266: USART2 (PA2/PA3), 115200
- DS18B20: PG11; 光敏 ADC: PA1; LED: PB5/PE5; 蜂鸣器: PB8; 电机 PWM: PA6
- 调试串口: USART1 (PA9/PA10), 115200

## OTA 升级 (v1.2 方案)

```
App (0x08004000) --MQTT {"cmd":"start"}--> BKP_DR1=0xA5A5 + 复位
    --> Bootloader (0x08000000): HTTP Range 分块下载 (16KB/块)
    --> 块间写 Flash + 逐页校验 --> 跳转新固件
```

- `stm32-ota-demo/tools/http_server.py`: 支持 Range 的固件 HTTP 服务器
- 触发方式: MQTT 主题 `factory/ota/dev01`, 载荷 `{"cmd":"start"}`
- 详见 `stm32-ota-demo/README.md`

## 关键经验 (排错记录)

- 精英板 LCD_RS = FSMC_A10 (PG0), 不是 A16/PD11 — 用 0xD3 读 ID (0x9341) 验证总线
- v2.2 全部代码跑 MSP 主栈, 启动文件 Stack_Size 必须 0x1000 (默认 0x400 会静默
  溢出破坏 LVGL 内存, 表现为"串口正常+屏幕白")
- ESP8266 +IPD 头字节会污染 TCP 数据流, AT 响应与载荷必须双环形缓冲区分离
- F103 单 bank Flash: 擦写期间 CPU 停摆会丢 UART 字节, OTA 必须
  "分块请求 → 块间安全窗口写入", 不能边收边写
