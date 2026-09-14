/* ============================================================
 * Bootloader — STM32F103ZET6 精英版 (SPL)
 * 功能:
 *   1. 检查 BKP_DR1 OTA 标志
 *   2. 如果 OTA_MAGIC: 执行固件下载 (WiFi + HTTP + Flash 写入)
 *   3. 否则: 直接跳转到 App
 *
 * Flash 布局:
 *   0x08000000 - 0x08003FFF  Bootloader (16KB)
 *   0x08004000 - 0x0807FFFF  Application (496KB)
 *
 * 编译: 独立 Keil 工程, ROM 起始 0x08000000, 大小 0x4000
 * ============================================================ */

#include "stm32f10x.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>

/* ---- 配置 (与 config.h 一致, 但独立定义避免依赖) ---- */
#define WIFI_SSID       "YourWiFi"
#define WIFI_PASSWORD   "YourPassword"
#define OTA_HOST        "192.168.1.178"
#define OTA_PORT        8080
#define OTA_PATH        "/firmware.bin"

#define APP_START       0x08004000
#define FLASH_PAGE_SZ   2048
#define OTA_MAGIC       0xA5A5
#define BKP_DR_OTA      BKP_DR1
#define OTA_CHUNK_SIZE  1024          /* 每次下载缓冲区大小 (与 config.h 一致) */

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

/* ==================== DWT 微秒延时 ==================== */
#define DWT_CYCCNT (*(volatile uint32_t *)0xE0001004)
#define DEM_CR     (*(volatile uint32_t *)0xE000EDFC)

static void delay_us(uint32_t us)
{
    uint32_t start = DWT_CYCCNT;
    uint32_t ticks = us * 72;  /* 72MHz */
    while ((DWT_CYCCNT - start) < ticks);
}

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

static void dbg(const char *fmt, ...)
{
    char buf[128];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n > 0) {
        for (int i = 0; i < n; i++) {
            while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET);
            USART_SendData(USART1, buf[i]);
        }
    }
}

/* ==================== ESP8266 简易驱动 ==================== */

#define ESP_UART     USART2
#define RING_SZ      2048

static volatile uint8_t  s_ring[RING_SZ];
static volatile uint16_t s_head = 0;
static volatile uint16_t s_tail = 0;

void USART2_IRQHandler(void)
{
    if (USART_GetITStatus(ESP_UART, USART_IT_RXNE) != RESET) {
        uint8_t byte = USART_ReceiveData(ESP_UART);
        uint16_t next = (s_head + 1) % RING_SZ;
        if (next != s_tail) {
            s_ring[s_head] = byte;
            s_head = next;
        }
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
    char cmd[128];

    if (!esp_at("AT\r\n", "OK", 2000)) return 0;
    esp_at("AT+CWMODE=1\r\n", "OK", 2000);
    delay_ms(500);
    esp_at("AT+CWQAP\r\n", "OK", 2000);
    delay_ms(500);

    snprintf(cmd, sizeof(cmd), "AT+CWJAP=\"%s\",\"%s\"\r\n", ssid, pass);
    ring_clear();
    esp_uart_send((const uint8_t *)cmd, (int)strlen(cmd));

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

/* +IPD 数据解析 */
static volatile int s_ipd_total = 0;
static volatile int s_ipd_remaining = 0;

static int esp_read_data(uint8_t *buf, int max_len, uint32_t timeout_ms)
{
    /* 这个简化版不做 +IPD 解析, 直接读环形缓冲区
     * 实际固件中需要 +IPD 解析, 这里简化处理 */
    int n = 0;
    uint32_t start = get_tick();
    while (get_tick() - start < timeout_ms && n < max_len) {
        if (s_head != s_tail) {
            buf[n++] = s_ring[s_tail];
            s_tail = (s_tail + 1) % RING_SZ;
            start = get_tick();
        }
    }
    return n;
}

/* ==================== LCD 简易显示 ==================== */
/* FSMC LCD 在 bootloader 中也可以用, 显示 OTA 进度 */
/* 这里省略 LCD 初始化 (与 app 的 lcd.c 类似)
 * 只用 LED 闪烁表示进度 */

static void led_init(void)
{
    GPIO_InitTypeDef gpio;
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
    gpio.GPIO_Pin = GPIO_Pin_5;
    gpio.GPIO_Mode = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &gpio);
}

static void led_toggle(void)
{
    /* PB5 读数据寄存器翻转 */
    if (GPIO_ReadOutputDataBit(GPIOB, GPIO_Pin_5))
        GPIO_ResetBits(GPIOB, GPIO_Pin_5);
    else
        GPIO_SetBits(GPIOB, GPIO_Pin_5);
}

/* ==================== Flash 操作 ==================== */

static void flash_write_page(uint32_t page_addr, const uint8_t *data, int len)
{
    /* 擦除页 */
    FLASH_ErasePage(page_addr);

    /* 写入 (半字) */
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

/* ==================== OTA 下载 ==================== */

static int ota_download_and_flash(void)
{
    char http_req[256];
    uint8_t buf[OTA_CHUNK_SIZE + 64];  /* HTTP 响应缓冲 */
    uint32_t total_received = 0;
    uint32_t flash_offset = 0;
    int ret = -1;

    dbg("[OTA] Starting firmware download\r\n");

    /* 1. 连接 WiFi */
    led_toggle();
    if (!esp_wifi_connect(WIFI_SSID, WIFI_PASSWORD)) {
        dbg("[OTA] WiFi connect failed\r\n");
        return -1;
    }
    dbg("[OTA] WiFi connected\r\n");
    led_toggle();

    /* 2. 连接 HTTP 服务器 */
    if (!esp_tcp_connect(OTA_HOST, OTA_PORT)) {
        dbg("[OTA] TCP connect failed\r\n");
        return -1;
    }
    dbg("[OTA] TCP connected\r\n");
    led_toggle();

    /* 3. 发送 HTTP GET 请求 */
    int req_len = snprintf(http_req, sizeof(http_req),
        "GET %s HTTP/1.1\r\n"
        "Host: %s:%d\r\n"
        "Connection: close\r\n"
        "\r\n", OTA_PATH, OTA_HOST, OTA_PORT);

    if (!esp_send_data((const uint8_t *)http_req, req_len)) {
        dbg("[OTA] HTTP request send failed\r\n");
        return -1;
    }
    dbg("[OTA] HTTP GET sent\r\n");
    led_toggle();

    /* 4. 解锁 Flash */
    FLASH_UnlockBank1();

    /* 5. 接收固件数据并写入 Flash */
    /* 先接收 HTTP 响应头, 找到 \r\n\r\n */
    uint8_t header_buf[512];
    int header_len = 0;
    int header_end = 0;
    uint32_t start = get_tick();

    /* 接收头 */
    while (get_tick() - start < 10000) {
        int n = esp_read_data(header_buf + header_len,
                              sizeof(header_buf) - header_len, 200);
        if (n > 0) {
            header_len += n;
            start = get_tick();
            /* 查找 \r\n\r\n */
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
        dbg("[OTA] No HTTP header end found\r\n");
        FLASH_LockBank1();
        return -1;
    }

    /* 提取 Content-Length */
    int content_length = 0;
    /* 简单搜索 "Content-Length:" 不使用 memmem (Keil 不一定有) */
    for (int i = 0; i < header_len - 15; i++) {
        if (memcmp(header_buf + i, "Content-Length:", 15) == 0) {
            content_length = atoi((const char *)(header_buf + i + 15));
            break;
        }
    }
    dbg("[OTA] Content-Length: %d bytes\r\n", content_length);

    /* 处理头之后剩余的数据 */
    int remaining = header_len - header_end;
    if (remaining > 0) {
        memcpy(buf, header_buf + header_end, remaining);
    }

    /* 6. 逐页接收固件并写入 Flash */
    uint32_t page_addr = APP_START;
    uint8_t page_buf[FLASH_PAGE_SZ];
    int page_pos = 0;

    while (total_received < content_length) {
        int n = esp_read_data(buf, sizeof(buf), 500);
        if (n <= 0) {
            if (get_tick() - start > 10000) break;
            delay_ms(10);
            continue;
        }
        start = get_tick();

        for (int i = 0; i < n; i++) {
            page_buf[page_pos++] = buf[i];
            total_received++;

            if (page_pos >= FLASH_PAGE_SZ) {
                /* 写入一页 */
                flash_write_page(page_addr, page_buf, FLASH_PAGE_SZ);
                if (flash_verify_page(page_addr, page_buf, FLASH_PAGE_SZ) != 0) {
                    dbg("[OTA] Flash verify failed at 0x%08X\r\n", page_addr);
                    FLASH_LockBank1();
                    return -1;
                }
                page_addr += FLASH_PAGE_SZ;
                page_pos = 0;

                /* LED 闪烁表示进度 */
                led_toggle();
            }
        }

        dbg("[OTA] Progress: %d/%d\r\n", total_received, content_length);
    }

    /* 写入最后一页 (不满一页) */
    if (page_pos > 0) {
        /* 补 0xFF 到整页 */
        for (int i = page_pos; i < FLASH_PAGE_SZ; i++)
            page_buf[i] = 0xFF;
        flash_write_page(page_addr, page_buf, FLASH_PAGE_SZ);
    }

    FLASH_LockBank1();
    dbg("[OTA] Firmware write complete: %d bytes\r\n", total_received);
    led_toggle();

    /* 7. 验证: 检查 app 向量表 */
    uint32_t app_sp = *(volatile uint32_t *)APP_START;
    uint32_t app_pc = *(volatile uint32_t *)(APP_START + 4);
    dbg("[OTA] App SP=0x%08X PC=0x%08X\r\n", app_sp, app_pc);

    /* 简单验证: SP 应该在 RAM 范围内 */
    if (app_sp >= 0x20000000 && app_sp <= 0x20010000) {
        dbg("[OTA] Verification OK\r\n");
        ret = 0;
    } else {
        dbg("[OTA] Verification FAILED\r\n");
    }

    return ret;
}

/* ==================== 跳转到 App ==================== */

static void jump_to_app(uint32_t addr)
{
    uint32_t app_sp = *(volatile uint32_t *)addr;
    uint32_t app_pc = *(volatile uint32_t *)(addr + 4);

    __disable_irq();

    /* 关闭外设 */
    USART_Cmd(USART1, DISABLE);
    USART_Cmd(USART2, DISABLE);
    SysTick->CTRL = 0;

    /* 清除所有中断挂起 */
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

static uint16_t bkp_read_dr1(void)
{
    return BKP_ReadBackupRegister(BKP_DR1);
}

static void bkp_write_dr1(uint16_t val)
{
    BKP_WriteBackupRegister(BKP_DR1, val);
}

/* ==================== 主函数 ==================== */

int main(void)
{
    /* 1. 系统时钟 (startup 已经调了 SystemInit, 72MHz) */
    SystemCoreClock = 72000000;

    /* 2. DWT 初始化 */
    DEM_CR |= (1 << 24);
    DWT_CYCCNT = 0;
    *(volatile uint32_t *)0xE0001000 |= 1;

    /* 3. SysTick 1ms */
    SysTick_Config(SystemCoreClock / 1000);

    /* 4. 初始化外设 */
    debug_init();
    led_init();
    bkp_init();

    dbg("\r\n[BOOT] Smart Factory Bootloader v2.0\r\n");
    dbg("[BOOT] Checking OTA flag...\r\n");

    /* 5. 检查 OTA 标志 */
    if (bkp_read_dr1() == OTA_MAGIC) {
        dbg("[BOOT] OTA requested, starting update...\r\n");

        /* 清除标志 (开始处理) */
        bkp_write_dr1(0);

        /* 初始化 ESP8266 */
        esp_init();
        delay_ms(500);

        /* 执行 OTA 下载 */
        if (ota_download_and_flash() == 0) {
            dbg("[BOOT] OTA successful, jumping to app\r\n");
            delay_ms(500);
        } else {
            dbg("[BOOT] OTA failed, jumping to old app\r\n");
            delay_ms(1000);
        }
    } else {
        dbg("[BOOT] Normal boot, jumping to app\r\n");
    }

    /* 6. 跳转到 App */
    /* 验证 App 是否存在 (SP 非法则说明没有 App) */
    uint32_t app_sp = *(volatile uint32_t *)APP_START;
    if (app_sp >= 0x20000000 && app_sp <= 0x20010000) {
        jump_to_app(APP_START);
    } else {
        dbg("[BOOT] No valid app found! Stuck in bootloader.\r\n");
        /* LED 慢闪表示无 App */
        while (1) {
            led_toggle();
            delay_ms(500);
        }
    }
}
