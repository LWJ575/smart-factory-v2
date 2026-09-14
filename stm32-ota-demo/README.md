# OTA Demo — STM32F103ZET6 正点原子精英版

独立的 OTA 功能测试工程，验证完整的远程升级链路：

```
App v1.0 (慢闪) --按KEY0--> BKP标志+复位 --> Bootloader --HTTP下载--> Flash写入+校验
                                                                      |
App v1.1 (快闪) <-------------- 跳转 0x08004000 <---------------------+
```

与正式项目 (v2.1/v2.2) 的关系：**机制完全相同**（BKP_DR1 = 0xA5A5 → bootloader
HTTP 下载 → 按页写 Flash → 校验向量表 → 跳转）。正式项目只是把"按 KEY0 触发"
换成"MQTT 指令触发"，demo 验证通过后正式项目的 OTA 无需再调硬件链路。

## 目录结构

```
STM_ota_demo/
├── bootloader/        Bootloader Keil 工程 (IROM 0x08000000, 16KB)
│   └── USER/boot_main.c   核心逻辑 (已修复 +IPD 数据流污染 bug)
├── app/               Demo App Keil 工程 (IROM 0x08004000, 496KB)
│   └── USER/main.c        v1.0.0 慢闪 / KEY0 触发 OTA
├── tools/
│   ├── http_server.py PC 端固件 HTTP 服务器
│   └── firmware.bin   升级固件 (fromelf 生成后放这里)
└── README.md          本文件
```

CORE / SYSTEM / STM32F10x_FWLib 直接复制自 E:\STM_smart_factory_v2.1 模板。

## 关键参数（boot_main.c 顶部，按需修改）

| 宏 | 值 | 说明 |
|----|----|------|
| `WIFI_SSID` | `"1401"` | 与 v2.1 相同 |
| `WIFI_PASSWORD` | `"liaojia00."` | |
| `OTA_HOST` | `"192.168.31.43"` | 你的 PC IP |
| `OTA_PORT` | `8080` | http_server.py 端口 |
| `OTA_PATH` | `"/firmware.bin"` | |

## 第 1 步：建 Bootloader 工程

1. Keil 新建工程到 `STM_ota_demo\bootloader\USER\`，芯片选 STM32F103ZE
2. **Options → Target → IROM1: start `0x08000000`, size `0x4000`；RAM: `0x20000000`, size `0x10000`**
3. 勾选 **Use MicroLIB**
4. **Options → C/C++ → Define 填 `STM32F10X_HD,USE_STDPERIPH_DRIVER`**
   （必须！否则 stm32f10x.h 不包含 stm32f10x_conf.h，所有外设类型/宏 undefined）
4. 添加源文件（Group 任意）：
   - `CORE/startup_stm32f10x_hd.s`、`CORE/core_cm3.c`
   - `USER/system_stm32f10x.c`（SystemInit/SystemCoreClock，**必须加**）
   - `SYSTEM/sys/sys.c`
   - `STM32F10x_FWLib/src/stm32f10x_gpio.c`、`stm32f10x_rcc.c`、`stm32f10x_usart.c`、
     `stm32f10x_flash.c`、`stm32f10x_bkp.c`、`stm32f10x_pwr.c`、`stm32f10x_misc.c`
   - `USER/boot_main.c`（**不要加模板 main.c**；usart.c/delay.c/json_helper.c 可不加）
5. Include Paths 加上 `..\CORE;..\SYSTEM\sys;..\STM32F10x_FWLib\inc;..\USER`
6. 编译通过后，烧录 `bootloader.axf`（此时 App 区是空的，串口会提示
   `No valid app! Stuck in bootloader`，LED 慢闪——正常）

## 第 2 步：建 App 工程（v1.0.0）

1. Keil 新建工程到 `STM_ota_demo\app\USER\`，芯片 STM32F103ZE
2. **Options → Target → IROM1: start `0x08004000`, size `0x7C000`；RAM: `0x20000000`, size `0x10000`**
   （IROM 起始地址是和普通工程唯一的区别！）
3. 勾选 **Use MicroLIB**
4. **Options → C/C++ → Define 填 `STM32F10X_HD,USE_STDPERIPH_DRIVER`**（同 bootloader）
4. 添加源文件：
   - `CORE/startup_stm32f10x_hd.s`、`CORE/core_cm3.c`
   - `USER/system_stm32f10x.c`（**必须加**）
   - `SYSTEM/sys/sys.c`、`SYSTEM/delay/delay.c`、`SYSTEM/usart/usart.c`
   - `STM32F10x_FWLib/src/stm32f10x_gpio.c`、`stm32f10x_rcc.c`、`stm32f10x_usart.c`、
     `stm32f10x_bkp.c`、`stm32f10x_pwr.c`、`stm32f10x_misc.c`
   - `USER/main.c`、`USER/stm32f10x_it.c`
5. Include Paths 同上，另加 `..\SYSTEM\delay;..\SYSTEM\usart`
6. 编译烧录

> ⚠️ **烧 App 时 Keil 的 Flash Download 选项必须选 "Erase Sectors"，不能选
> "Erase Full Chip"**——否则 bootloader 会被擦掉。两个工程都要检查这一项。

## 第 3 步：制作 v1.1.0 升级固件

1. 把 `app/USER/main.c` 里两个宏改掉：
   ```c
   #define FW_VER   "1.1.0"
   #define BLINK_MS 100
   ```
2. Rebuild，然后用 fromelf 生成 bin（在 Keil 命令行或工程 Options → User 里）：
   ```
   fromelf --bin -o E:\STM_ota_demo\tools\firmware.bin .\OBJ\<工程名>.axf
   ```
   fromelf 在 Keil 安装目录 `ARM\ARMCC\bin\` 下。bin 的起始地址就是 0x08004000，
   不含 bootloader 区。

## 第 4 步：执行 OTA

1. PC 上启动固件服务器：
   ```
   cd E:\STM_ota_demo\tools
   python http_server.py
   ```
   确认打印 `firmware.bin 就绪: xxxx bytes`（Windows 防火墙弹窗要允许）
2. 板子先断电，接好 ESP8266（PA2/PA3，与 v2.1 相同接线）
3. 上电 → 串口看到 `[BOOT] Normal boot` → `[APP] OTA Demo App FW 1.0.0`，
   LED0 慢闪（500ms）
4. **按一下 KEY0** → 串口打印 `OTA requested` → 板子复位 → bootloader 检测到标志：
   ```
   [BOOT] OTA requested (magic=0xA5A5), starting update...
   [BOOT] WiFi connected
   [BOOT] TCP connected
   [BOOT] Content-Length: xxxx bytes
   [BOOT] Progress: 10% (...)
   ...
   [BOOT] Firmware written: xxxx bytes
   [BOOT] App SP=0x2000xxxx PC=0x0800xxxx
   [BOOT] Verification OK
   [BOOT] OTA successful, jumping to new app
   ```
5. 跳转后看到 `[APP] OTA Demo App FW 1.1.0`，**LED0 快闪（100ms）** → OTA 全链路验证通过 ✅

## 排错速查

| 现象 | 原因 |
|------|------|
| `AT no response` 类失败 | ESP8266 接线/供电（同 v2.1 排查方法） |
| `TCP connect failed` | http_server.py 没启动 / 防火墙拦截 / PC IP 变了 |
| `Bad Content-Length: 0` | tools/ 下没有 firmware.bin（404） |
| `No HTTP header end found` | 服务器响应异常，看 http_server 控制台有无请求日志 |
| `Flash verify failed` | Flash 时钟/供电不稳，极少见 |
| `Verification FAILED` / `Incomplete` | 下载中断（WiFi 不稳），按 KEY0 重试即可 |
| 跳转后死机/无输出 | App 工程忘了把 IROM 起始改 0x08004000，或没勾 MicroLIB |
| 重按 KEY0 无反应 | 正常——OTA 成功后版本已是 1.1.0，再升级需要换新 bin |

## 与正式项目对接（验证通过后）

- 正式项目触发 OTA 的 MQTT payload `{"cmd":"start"}` → `ota_trigger_update()`
  → `bkp_write(OTA_FLAG_BKP_DR, OTA_FLAG_MAGIC)` → 复位，**与 demo 的
  `trigger_ota()` 一字不差的机制**
- 唯一要做的：把 v2.1/v2.2 工程的 Keil IROM 起始改为 0x08004000 并加
  `SCB->VTOR` 重定位，再把本 demo 的 boot_main.c 作为正式 bootloader
  （改回正式的 OTA_PATH，如 `/fw/dev01.bin`）

## 附：ST-Link 烧录配置（推荐，替代串口 ISP）

接线（精英板 4 针 SWD 排针 GND/CLK/DIO/3V3）：
`SWDIO→DIO`、`SWCLK→CLK`、`GND→GND`、`3.3V→3V3`（可选）、`RST→RST`（建议接）

Keil 配置（bootloader 和 app 两个工程都要）：
1. Options → Debug → 选 "ST-Link Debugger" → Settings → Port: SW
2. Settings → Flash Download → **Erase Sectors**（不能 Full Chip！）
   + 勾 Program/Verify/Reset and Run
3. 连不上正在跑程序的板子: Connect 改 "under Reset"，Reset 改 "Hardware reset"
4. 之后 Rebuild + F8 一键烧录，无需 BOOT0 跳线

首次使用需装 ST-Link 驱动 (STSW-LINK007)，设备管理器出现 "ST-Link Debug" 即可。

## v2.1 / v2.2 正式项目移植（已完成配置）

**架构**: bootloader v1.2 (Range 分块下载) + 各 App 工程重定位到 0x08004000。
v2.2 固件 191KB 超出 RAM 缓冲, 因此 bootloader 用 Range 分块: 每次请求 16KB,
响应结束服务器断开、无数据在途, 此窗口写 Flash 绝对安全 (F103 单 bank 停摆问题根治)。

已完成:
1. v2.1/v2.2 的 Template.uvprojx: IROM → 0x08004000/0x7C000
   (<Cpu>/<IROM>/<OCR_RVCT4>/<TextAddressRange> 四处, 其中 TextAddressRange
   是 umfTarg=0 时链接器 R/O Base 的真正来源)
2. v2.1/v2.2 的 main.c: hw_init_all() 第一行 SCB->VTOR = 0x08004000
3. config.h 的 OTA 宏 (BKP_DR1 + 0xA5A5) 与 bootloader 一致, MQTT 触发链路原有
4. http_server.py 支持 Range (206); 三个工程 UV4 -r 命令行编译 0 错误

使用流程:
1. ST-Link 烧 bootloader (E:/STM_ota_demo/bootloader, 一次性)
2. ST-Link 烧 v2.1 或 v2.2 的 App (Erase Sectors!)
3. tools/ 放入对应固件 (fromelf --bin 生成, 如 v2_1_test.bin → firmware.bin)
4. python http_server.py → MQTT 发 {"cmd":"start"} 到 factory/ota/dev01
5. App 提示重启 → bootloader 分块下载 → 跳转新固件

注意: 若 Keil GUI 打开着这两个工程, 需关闭重开才能加载新的 IROM 设置,
否则 GUI 保存时会覆盖回旧地址。
