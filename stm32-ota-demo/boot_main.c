/* ============================================================
 * OTA Bootloader — STM32F103ZET6 正点原子精英版 (SPL)
 * OTA Demo 专用版 (E:\STM_ota_demo\bootloader)
 *
 * 功能:
 *   1. 检查 BKP_DR1 OTA 标志 (0xA5A5)
 *   2. 有标志: WiFi + HTTP 下载 firmware.bin → 写入 App 区 → 校验
 *   3. 无标志/下载失败: 直接跳转到 App (0x08004000)
 *
 * Flash 布局:
 *   0x08000000 - 0x08003FFF  Bootloader (16KB)
 *   0x08004000 - 0x0807FFFF  Application (496KB)
 *
 * Keil 配置: IROM1 = 0x08000000, Size = 0x4000; RAM = 0x20000000, 0x10000
 *
 * 相对 v2.1 boot/boot_main.c 的修复:
 *   - +IPD 双环形缓冲区 (HTTP 载荷与 AT 响应分离, 防数据流污染)
 *   - FLASH_ClearFlag 加固
 *   - 下载进度按百分比打印
 * ============================================================ */

#include "stm32f10x.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>

/* ---- OTA 配置 (与 v2.1 环境一致) ---- */
#define WIFI_SSID       "102"
#define WIFI_PASSWORD   "asdfghjk."
#define OTA_HOST        "192.168.1.3"     /* PC (Mosquitto/HTTP 服务器) */
#define OTA_PORT        8080
#define OTA_PATH        "/firmware.bin"

#define APP_START       0x08004000
#define FLASH_PAGE_SZ   2048
#define OTA_MAGIC       0xA5A5
#define BKP_DR_OTA      BKP_DR1

/* ==================== 简易延时 ==================== */
static volatile uint32_t s_tick = 0;

void SysTick_Handler(void)
{
    s_tick++;
}

static void delay_ms(uint32_t ms)
{
    uint32_t start = s_tick;
    while (s_tick - start < ms);
}

static uint32_t get_tick(void)
{
    return s_tick;
}

/* ==================== DWT (main 里启用计数器) ==================== */
#define DWT_CYCCNT (*(volatile uint32_t *)0xE0001004)
#define DEM_CR     (*(volatile uint32_t *)0xE000EDFC)

/* ==================== 调试串口 (USART1) ==================== */
static void debug_init(void)
{
    GPIO_InitTypeDef gpio;
    USART_InitTypeDef uart;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1 | RCC_APB2Periph_GPIOA, ENABLE);

    gpio.GPIO_Pin = GPIO_Pin_9;
    gpio.GPIO_Mode = GPIO_Mode_AF_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &gpio);

    gpio.GPIO_Pin = GPIO_Pin_10;
    gpio.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOA, &gpio);

    uart.USART_BaudRate = 115200;
    uart.USART_WordLength = USART_WordLength_8b;
    uart.USART_StopBits = USART_StopBits_1;
    uart.USART_Parity = USART_Parity_No;
    uart.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    uart.USART_Mode = USART_Mode_Tx;
    USART_Init(USART1, &uart);
    USART_Cmd(USART1, ENABLE);
}

/* 全局缓冲: 避免 vsnprintf 吃栈 (Keil 标准库 500-1500B) */
static char g_dbg_buf[128];

static void dbg(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(g_dbg_buf, sizeof(g_dbg_buf), fmt, ap);
    va_end(ap);
    if (n > 0) {
        for (int i = 0; i < n; i++) {
            while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET);
            USART_SendData(USART1, g_dbg_buf[i]);
        }
    }
}

/* ==================== ESP8266 驱动 (双环形缓冲区) ====================
 * s_ring  : AT 响应 (OK/ERROR/SEND OK/CONNECT), 供子串匹配
 * s_dring : +IPD 载荷 (纯 HTTP 数据), 固件下载用
 * ISR 单生产者 + main 单消费者, 无需加锁
 * ============================================================ */

#define ESP_UART     USART2
#define RING_SZ      2048
#define DRING_SZ     4096            /* ≥ TCP 单包 MSS(1460) 的 2 倍, 防溢出丢字节 */

/* Range 分块下载 (v1.2):
 * F103 单 bank Flash —— 擦/写期间 CPU 停摆, 任何"在途数据"都会丢。
 * 方案: 每次用 HTTP Range 请求一小块 (16KB), 响应结束后服务器关闭连接,
 * 块间无数据在途, 此窗口内写 Flash 绝对安全。任意固件大小都适用
 * (demo 4.5KB 1 块, v2.1 24.5KB 2 块, v2.2 191KB 12 块)。
 * 要求 HTTP 服务器支持 Range (http_server.py 已实现) */
#define CHUNK_BUF_SZ (16U * 1024U)

static volatile uint8_t  s_ring[RING_SZ];
static volatile uint16_t s_head = 0;
static volatile uint16_t s_tail = 0;

static volatile uint8_t  s_dring[DRING_SZ];
static volatile uint16_t s_dhead = 0;
static volatile uint16_t s_dtail = 0;

/* +IPD 状态机 (ISR 上下文)
 * 0=idle (字节进 AT ring), 1=匹配到 "+IPD," 读长度, 2=载荷进 dring */
static volatile int  s_ipd_state = 0;
static volatile uint32_t s_ipd_remaining = 0;
static char s_ipd_lenbuf[12];
static int  s_ipd_lenpos = 0;

static void esp_rx_byte(uint8_t byte)
{
    switch (s_ipd_state) {
    case 0: {
        /* 字节进 AT ring */
        uint16_t next = (s_head + 1) % RING_SZ;
        if (next != s_tail) {
            s_ring[s_head] = byte;
            s_head = next;
        }
        /* 同时参与 "+IPD," 匹配 (滑动窗口 5 字节) */
        s_ipd_lenbuf[s_ipd_lenpos++] = (char)byte;
        if (s_ipd_lenpos >= 5) {
            if (memcmp((const void *)&s_ipd_lenbuf[s_ipd_lenpos - 5],
                       "+IPD,", 5) == 0) {
                s_ipd_state = 1;
                s_ipd_lenpos = 0;
            } else if (s_ipd_lenpos >= 16) {
                /* 窗口太长, 保留最后 4 字节滑动 */
                memmove((void *)s_ipd_lenbuf,
                        (const void *)&s_ipd_lenbuf[s_ipd_lenpos - 4], 4);
                s_ipd_lenpos = 4;
            }
        }
        break;
    }
    case 1: {
        /* 读长度数字直到 ':' (这些字节不进任何 ring) */
        if (byte == ':') {
            s_ipd_lenbuf[s_ipd_lenpos] = '\0';
            s_ipd_remaining = (uint32_t)atoi(s_ipd_lenbuf);
            s_ipd_state = (s_ipd_remaining > 0) ? 2 : 0;
            s_ipd_lenpos = 0;
        } else if (s_ipd_lenpos < 10) {
            s_ipd_lenbuf[s_ipd_lenpos++] = (char)byte;
        } else {
            /* 长度异常, 回 idle */
            s_ipd_state = 0;
            s_ipd_lenpos = 0;
        }
        break;
    }
    case 2: {
        /* 载荷 → 数据环形缓冲区 */
        uint16_t dnext = (s_dhead + 1) % DRING_SZ;
        if (dnext != s_dtail) {
            s_dring[s_dhead] = byte;
            s_dhead = dnext;
        }
        /* dring 满时丢字节但计数照减, 避免状态机卡死 */
        if (--s_ipd_remaining == 0) {
            s_ipd_state = 0;
            s_ipd_lenpos = 0;
        }
        break;
    }
    }
}

void USART2_IRQHandler(void)
{
    /* ORE (溢出) 必须清除, 否则标志置位后接收可能异常 */
    if (USART_GetFlagStatus(ESP_UART, USART_FLAG_ORE) != RESET) {
        (void)USART_ReceiveData(ESP_UART);   /* 读 SR + 读 DR 清除 ORE */
        return;
    }
    if (USART_GetITStatus(ESP_UART, USART_IT_RXNE) != RESET) {
        uint8_t byte = USART_ReceiveData(ESP_UART);
        esp_rx_byte(byte);
    }
}

static void esp_uart_send(const uint8_t *data, int len)
{
    while (len--) {
        while (USART_GetFlagStatus(ESP_UART, USART_FLAG_TXE) == RESET);
        USART_SendData(ESP_UART, *data++);
    }
}

static void esp_init(void)
{
    GPIO_InitTypeDef gpio;
    USART_InitTypeDef uart;
    NVIC_InitTypeDef nvic;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART2, ENABLE);

    gpio.GPIO_Pin = GPIO_Pin_2;
    gpio.GPIO_Mode = GPIO_Mode_AF_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &gpio);

    gpio.GPIO_Pin = GPIO_Pin_3;
    gpio.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOA, &gpio);

    uart.USART_BaudRate = 115200;
    uart.USART_WordLength = USART_WordLength_8b;
    uart.USART_StopBits = USART_StopBits_1;
    uart.USART_Parity = USART_Parity_No;
    uart.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    uart.USART_Mode = USART_Mode_Tx | USART_Mode_Rx;
    USART_Init(ESP_UART, &uart);

    USART_ITConfig(ESP_UART, USART_IT_RXNE, ENABLE);

    nvic.NVIC_IRQChannel = USART2_IRQn;
    nvic.NVIC_IRQChannelPreemptionPriority = 3;
    nvic.NVIC_IRQChannelSubPriority = 0;
    nvic.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&nvic);

    USART_Cmd(ESP_UART, ENABLE);
}

static void ring_clear(void) { s_tail = s_head; }
static void dring_clear(void) { s_dtail = s_dhead; s_ipd_state = 0; s_ipd_remaining = 0; }

static int ring_find(const char *str, int len)
{
    int count = (int)s_head - (int)s_tail;
    if (count < 0) count += RING_SZ;
    if (count < len) return -1;
    for (int i = 0; i <= count - len; i++) {
        int idx = (s_tail + i) % RING_SZ;
        int match = 1;
        for (int j = 0; j < len; j++) {
            if ((char)s_ring[(idx + j) % RING_SZ] != str[j]) {
                match = 0; break;
            }
        }
        if (match) return i;
    }
    return -1;
}

static int esp_at(const char *cmd, const char *expect, uint32_t timeout_ms)
{
    ring_clear();
    esp_uart_send((const uint8_t *)cmd, (int)strlen(cmd));

    uint32_t start = get_tick();
    int elen = expect ? (int)strlen(expect) : 0;

    while (get_tick() - start < timeout_ms) {
        if (elen > 0 && ring_find(expect, elen) >= 0) return 1;
        if (ring_find("ERROR", 5) >= 0) return 0;
        delay_ms(10);
    }
    return 0;
}

static int esp_wifi_connect(const char *ssid, const char *pass)
{
    if (!esp_at("AT\r\n", "OK", 2000)) return 0;
    esp_at("AT+CWMODE=1\r\n", "OK", 2000);
    delay_ms(500);
    esp_at("AT+CWQAP\r\n", "OK", 2000);
    delay_ms(500);

    snprintf(g_dbg_buf, sizeof(g_dbg_buf), "AT+CWJAP=\"%s\",\"%s\"\r\n", ssid, pass);
    ring_clear();
    esp_uart_send((const uint8_t *)g_dbg_buf, (int)strlen(g_dbg_buf));

    uint32_t start = get_tick();
    while (get_tick() - start < 20000) {
        if (ring_find("WIFI GOT IP", 11) >= 0) return 1;
        if (ring_find("FAIL", 4) >= 0) return 0;
        delay_ms(100);
    }
    return 0;
}

static int esp_tcp_connect(const char *ip, uint16_t port)
{
    char cmd[128];

    esp_at("AT+CIPCLOSE\r\n", "OK", 1000);
    esp_at("AT+CIPMUX=0\r\n", "OK", 2000);

    snprintf(cmd, sizeof(cmd), "AT+CIPSTART=\"TCP\",\"%s\",%d\r\n", ip, port);
    ring_clear();
    esp_uart_send((const uint8_t *)cmd, (int)strlen(cmd));

    uint32_t start = get_tick();
    while (get_tick() - start < 15000) {
        if (ring_find("CONNECT", 7) >= 0) return 1;
        if (ring_find("ERROR", 5) >= 0) return 0;
        delay_ms(50);
    }
    return 0;
}

static int esp_send_data(const uint8_t *data, int len)
{
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "AT+CIPSEND=%d\r\n", len);
    if (!esp_at(cmd, ">", 2000)) return 0;
    esp_uart_send(data, len);

    uint32_t start = get_tick();
    while (get_tick() - start < 3000) {
        if (ring_find("SEND OK", 7) >= 0) return 1;
        if (ring_find("SEND FAIL", 9) >= 0) return 0;
        delay_ms(10);
    }
    return 0;
}

/* 从 dring 读纯 TCP 载荷 (+IPD 头已被 ISR 状态机消费) */
static int esp_read_data(uint8_t *buf, int max_len, uint32_t timeout_ms)
{
    int n = 0;
    uint32_t start = get_tick();
    while (get_tick() - start < timeout_ms && n < max_len) {
        if (s_dhead != s_dtail) {
            buf[n++] = s_dring[s_dtail];
            s_dtail = (s_dtail + 1) % DRING_SZ;
            start = get_tick();
        }
    }
    return n;
}

/* ==================== LED 进度指示 ==================== */

static void led_init(void)
{
    GPIO_InitTypeDef gpio;
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
    gpio.GPIO_Pin = GPIO_Pin_5;
    gpio.GPIO_Mode = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &gpio);
    GPIO_SetBits(GPIOB, GPIO_Pin_5);   /* 低电平点亮, 先灭 */
}

static void led_toggle(void)
{
    if (GPIO_ReadOutputDataBit(GPIOB, GPIO_Pin_5))
        GPIO_ResetBits(GPIOB, GPIO_Pin_5);
    else
        GPIO_SetBits(GPIOB, GPIO_Pin_5);
}

/* ==================== Flash 操作 ==================== */

static void flash_write_page(uint32_t page_addr, const uint8_t *data, int len)
{
    FLASH_ClearFlag(FLASH_FLAG_BSY | FLASH_FLAG_PGERR | FLASH_FLAG_WRPRTERR);
    FLASH_ErasePage(page_addr);

    for (int i = 0; i < len; i += 2) {
        uint16_t hw;
        if (i + 1 < len)
            hw = data[i] | ((uint16_t)data[i + 1] << 8);
        else
            hw = data[i] | 0xFF00;
        FLASH_ProgramHalfWord(page_addr + i, hw);
    }
}

static int flash_verify_page(uint32_t addr, const uint8_t *expected, int len)
{
    const uint8_t *flash = (const uint8_t *)addr;
    for (int i = 0; i < len; i++) {
        if (flash[i] != expected[i]) return -1;
    }
    return 0;
}

/* ==================== OTA 下载 (v1.2 Range 分块) ==================== */

/* 状态行检查: "HTTP/1.x 206 ..." (Range 请求成功的标志) */
static int http_status_is_206(const uint8_t *hdr, int hdr_len)
{
    if (hdr_len < 13 || memcmp(hdr, "HTTP/1.", 7) != 0) return 0;
    const uint8_t *p = hdr + 7;
    while (p < hdr + hdr_len && *p != ' ') p++;
    if (p + 3 > hdr + hdr_len || p[0] != ' ') return 0;
    return (p[1] == '2' && p[2] == '0' && p[3] == '6');
}

/* 从 HTTP 头中解析字段值 (返回写到 out 的长度, -1 表示没找到) */
static int http_get_header(const uint8_t *hdr, int hdr_len,
                           const char *field, char *out, int out_max)
{
    int flen = (int)strlen(field);
    for (int i = 0; i <= hdr_len - flen; i++) {
        if ((i == 0 || hdr[i-1] == '\n') &&
            strncmp((const char *)&hdr[i], field, flen) == 0) {
            int j = i + flen;
            while (j < hdr_len && (hdr[j] == ' ' || hdr[j] == ':')) j++;
            int k = 0;
            while (j < hdr_len && hdr[j] != '\r' && hdr[j] != '\n' && k < out_max - 1)
                out[k++] = (char)hdr[j++];
            out[k] = '\0';
            return k;
        }
    }
    return -1;
}

static int ota_download_and_flash(void)
{
    char http_req[256];
    char val[64];
    /* 16KB 分块缓冲: 块间服务器已关连接, 无数据在途, 写 Flash 安全 */
    static uint8_t chunk_buf[CHUNK_BUF_SZ];
    static uint8_t header_buf[512];

    uint32_t total = 0;              /* 固件总大小 (首个 206 响应的 Content-Range) */
    uint32_t offset = 0;
    int ret = -1;

    dbg("[BOOT] Starting firmware download (range-chunked)\r\n");

    /* 1. 连接 WiFi */
    if (!esp_wifi_connect(WIFI_SSID, WIFI_PASSWORD)) {
        dbg("[BOOT] WiFi connect failed\r\n");
        return -1;
    }
    dbg("[BOOT] WiFi connected\r\n");
    led_toggle();

    /* 2. 分块下载循环 */
    while (offset < total || total == 0) {
        uint32_t want = CHUNK_BUF_SZ;
        if (total > 0 && offset + want > total)
            want = total - offset;
        dring_clear();

        /* 2a. 建立连接 */
        if (!esp_tcp_connect(OTA_HOST, OTA_PORT)) {
            dbg("[BOOT] TCP connect failed (offset=%u)\r\n", offset);
            return -1;
        }

        /* 2b. 发送带 Range 的 GET */
        int req_len = snprintf(http_req, sizeof(http_req),
            "GET %s HTTP/1.1\r\n"
            "Host: %s:%d\r\n"
            "Range: bytes=%u-%u\r\n"
            "Connection: close\r\n"
            "\r\n", OTA_PATH, OTA_HOST, OTA_PORT,
            offset, offset + want - 1);

        if (!esp_send_data((const uint8_t *)http_req, req_len)) {
            dbg("[BOOT] HTTP request send failed\r\n");
            return -1;
        }
        dring_clear();   /* 丢弃响应头之前的 AT 噪音 */

        /* 2c. 接收响应头, 找 \r\n\r\n */
        int header_len = 0, header_end = 0;
        uint32_t start = get_tick();

        while (get_tick() - start < 10000) {
            int n = esp_read_data(header_buf + header_len,
                                  sizeof(header_buf) - header_len, 200);
            if (n > 0) {
                if (header_len + n > (int)sizeof(header_buf))
                    n = sizeof(header_buf) - header_len;
                header_len += n;
                start = get_tick();
                for (int i = 0; i < header_len - 3; i++) {
                    if (header_buf[i] == '\r' && header_buf[i+1] == '\n' &&
                        header_buf[i+2] == '\r' && header_buf[i+3] == '\n') {
                        header_end = i + 4;
                        break;
                    }
                }
                if (header_end > 0) break;
            }
            delay_ms(10);
        }

        if (header_end == 0) {
            dbg("[BOOT] No HTTP header end (offset=%u)\r\n", offset);
            return -1;
        }

        /* 2d. 校验 206 + 解析 Content-Length / Content-Range */
        if (!http_status_is_206(header_buf, header_end)) {
            dbg("[BOOT] Server did not return 206 (Range unsupported?)\r\n");
            return -1;
        }

        int expected = -1;
        if (http_get_header(header_buf, header_end, "Content-Length:",
                            val, sizeof(val)) > 0)
            expected = atoi(val);

        if (total == 0) {
            /* 首块: Content-Range: bytes 0-16383/TOTAL → 取 TOTAL */
            if (http_get_header(header_buf, header_end, "Content-Range:",
                                val, sizeof(val)) > 0) {
                char *slash = strchr(val, '/');
                if (slash) total = (uint32_t)atoi(slash + 1);
            }
            if (total == 0) {
                dbg("[BOOT] No Content-Range total\r\n");
                return -1;
            }
            dbg("[BOOT] Firmware total: %u bytes (%u chunks)\r\n",
                total, (total + CHUNK_BUF_SZ - 1) / CHUNK_BUF_SZ);
            if (offset + want > total)
                want = total - offset;
            if (expected < 0 || (uint32_t)expected > want)
                expected = (int)want;
        }

        if (expected <= 0) {
            dbg("[BOOT] Bad Content-Length\r\n");
            return -1;
        }

        /* 2e. 收满本块 (Connection: close, 服务器发完即断, 无数据在途) */
        uint32_t got = 0;
        start = get_tick();
        int idle_since = 0;
        while (got < (uint32_t)expected) {
            int n = esp_read_data(chunk_buf + got,
                                  expected - (int)got, 500);
            if (n <= 0) {
                if (++idle_since > 20) break;   /* 10s 超时 */
                continue;
            }
            idle_since = 0;
            start = get_tick();
            got += n;
        }

        if (got != (uint32_t)expected) {
            dbg("[BOOT] Chunk incomplete: %u/%d at offset %u\r\n",
                got, expected, offset);
            return -1;
        }

        esp_at("AT+CIPCLOSE\r\n", "OK", 1000);   /* 确保断开, 清理状态 */

        /* 2f. 块间安全窗口: 写入 Flash + 逐页校验
         * 块大小 16KB = 8 页, 页边界天然对齐, 尾块才有不满页 */
        uint32_t page_addr = APP_START + offset;
        int pages = (int)((got + FLASH_PAGE_SZ - 1) / FLASH_PAGE_SZ);

        FLASH_UnlockBank1();
        FLASH_ClearFlag(FLASH_FLAG_BSY | FLASH_FLAG_PGERR | FLASH_FLAG_WRPRTERR);

        for (int p = 0; p < pages; p++) {
            uint32_t po = p * FLASH_PAGE_SZ;
            int len = (int)((got - po < FLASH_PAGE_SZ) ? (got - po) : FLASH_PAGE_SZ);

            flash_write_page(page_addr + po, chunk_buf + po, len);
            if (flash_verify_page(page_addr + po, chunk_buf + po, len) != 0) {
                dbg("[BOOT] Flash verify failed at 0x%08X\r\n", page_addr + po);
                FLASH_LockBank1();
                return -1;
            }
            led_toggle();
        }
        FLASH_LockBank1();

        offset += got;
        dbg("[BOOT] Progress: %u%% (%u/%u)\r\n",
            (unsigned)((offset * 100) / total), offset, total);
    }

    dbg("[BOOT] Firmware written: %u bytes\r\n", total);

    /* 3. 验证 App 向量表 */
    {
        uint32_t app_sp = *(volatile uint32_t *)APP_START;
        uint32_t app_pc = *(volatile uint32_t *)(APP_START + 4);
        dbg("[BOOT] App SP=0x%08X PC=0x%08X\r\n", app_sp, app_pc);

        if (app_sp >= 0x20000000 && app_sp <= 0x20010000) {
            dbg("[BOOT] Verification OK\r\n");
            ret = 0;
        } else {
            dbg("[BOOT] Verification FAILED\r\n");
        }
    }
    return ret;
}

/* ==================== 跳转到 App ==================== */

static void jump_to_app(uint32_t addr)
{
    uint32_t app_sp = *(volatile uint32_t *)addr;
    uint32_t app_pc = *(volatile uint32_t *)(addr + 4);

    __disable_irq();

    USART_Cmd(USART1, DISABLE);
    USART_Cmd(USART2, DISABLE);
    SysTick->CTRL = 0;

    for (int i = 0; i < 8; i++) {
        NVIC->ICER[i] = 0xFFFFFFFF;
        NVIC->ICPR[i] = 0xFFFFFFFF;
    }

    __set_MSP(app_sp);
    ((void (*)(void))app_pc)();
}

/* ==================== BKP ==================== */

static void bkp_init(void)
{
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_PWR | RCC_APB1Periph_BKP, ENABLE);
    PWR_BackupAccessCmd(ENABLE);
}

/* ==================== 主函数 ==================== */

int main(void)
{
    SystemCoreClock = 72000000;

    /* DWT */
    DEM_CR |= (1 << 24);
    DWT_CYCCNT = 0;
    *(volatile uint32_t *)0xE0001000 |= 1;

    /* SysTick 1ms */
    SysTick_Config(SystemCoreClock / 1000);

    debug_init();
    led_init();
    bkp_init();

    dbg("\r\n[BOOT] OTA Demo Bootloader v1.2 (range-chunked download)\r\n");
    dbg("[BOOT] Checking OTA flag (BKP_DR1)...\r\n");

    if (BKP_ReadBackupRegister(BKP_DR_OTA) == OTA_MAGIC) {
        dbg("[BOOT] OTA requested (magic=0xA5A5), starting update...\r\n");

        /* 清除标志 */
        BKP_WriteBackupRegister(BKP_DR_OTA, 0);

        esp_init();
        delay_ms(500);

        if (ota_download_and_flash() == 0) {
            dbg("[BOOT] OTA successful, jumping to new app\r\n");
        } else {
            dbg("[BOOT] OTA failed, jumping to old app\r\n");
        }
        delay_ms(500);
    } else {
        dbg("[BOOT] Normal boot (flag=0x%04X), jumping to app\r\n",
            BKP_ReadBackupRegister(BKP_DR_OTA));
    }

    uint32_t app_sp = *(volatile uint32_t *)APP_START;
    if (app_sp >= 0x20000000 && app_sp <= 0x20010000) {
        dbg("[BOOT] Jumping to app at 0x08004000\r\n");
        delay_ms(100);
        jump_to_app(APP_START);
    } else {
        dbg("[BOOT] No valid app (SP=0x%08X)! Stuck in bootloader\r\n", app_sp);
        while (1) {
            led_toggle();
            delay_ms(500);
        }
    }
}
