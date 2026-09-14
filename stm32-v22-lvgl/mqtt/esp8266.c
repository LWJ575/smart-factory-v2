#include "esp8266.h"
#include "config.h"
#include "bsp.h"
#include <string.h>

/* ============================================================
 * ESP8266 AT 驱动实现 — v2.2 超级循环适配版 (无 FreeRTOS)
 * 核心变化 (相对 v2.0):
 * 1. 去掉互斥锁 —— 单线程 (超级循环) 架构中所有 AT 收发
 *    都在 main 上下文顺序执行, 唯一并发者是 USART2 ISR (只写
 *    ring buffer 的 head 指针), 不需要互斥保护。
 *    注意: 千万不要用 __disable_irq() 包住整个等待循环 ——
 *    SysTick 会被关掉, sys_tick() 停走, 所有超时逻辑死锁!
 * 2. 等待用 sys_delay_ms() (内部泵 LVGL), UI 不冻结
 * ============================================================ */

/* 环形缓冲区 */
#define RING_SIZE  (RX_BUF_SIZE)
static volatile uint8_t  s_ring[RING_SIZE];
static volatile uint16_t s_ring_head = 0;
static volatile uint16_t s_ring_tail = 0;

/* TCP 连接状态 */
static volatile int s_tcp_connected = 0;

/* +IPD 数据解析 */
static volatile int    s_ipd_active = 0;
static volatile int    s_ipd_remaining = 0;
static volatile int    s_ipd_total = 0;

/* ==================== TCP 数据环形缓冲区 ====================
 * 与上面的 AT 响应缓冲区分离:
 * - AT 缓冲区 (s_ring): 存 AT 响应/WiFi 状态/UART 噪音, 供 ring_find 匹配
 * - 数据缓冲区 (s_dring): 只存 +IPD 载荷 (纯 TCP 数据), 供 MQTT 层读取
 * 单生产者 (ISR) + 单消费者 (main 循环), 免锁无竞态 */
#define DRING_SIZE  1024   /* TCP 载荷缓冲区: 独立于 AT 缓冲区, 1KB 够 MQTT 包用 */
static volatile uint8_t  s_dring[DRING_SIZE];
static volatile uint16_t s_dring_head = 0;
static volatile uint16_t s_dring_tail = 0;

static void dring_push(uint8_t byte)
{
    uint16_t next = (s_dring_head + 1) % DRING_SIZE;
    if (next == s_dring_tail) return;  /* 满, 丢字节 (TCP 层会重传) */
    s_dring[s_dring_head] = byte;
    s_dring_head = next;
}

static int dring_pop(uint8_t *byte)
{
    if (s_dring_head == s_dring_tail) return -1;
    *byte = s_dring[s_dring_tail];
    s_dring_tail = (s_dring_tail + 1) % DRING_SIZE;
    return 0;
}

static void dring_clear(void)
{
    s_dring_tail = s_dring_head;
}

/* ==================== 串口底层 ==================== */

static void uart_send_bytes(USART_TypeDef *uart, const uint8_t *data, int len)
{
    while (len--) {
        while (USART_GetFlagStatus(uart, USART_FLAG_TXE) == RESET);
        USART_SendData(uart, *data++);
    }
}

/* ==================== 环形缓冲区 ==================== */

static int ring_push(uint8_t byte)
{
    uint16_t next = (s_ring_head + 1) % RING_SIZE;
    if (next == s_ring_tail) return -1;  /* 满 */
    s_ring[s_ring_head] = byte;
    s_ring_head = next;
    return 0;
}

static int ring_pop(uint8_t *byte)
{
    if (s_ring_head == s_ring_tail) return -1;  /* 空 */
    *byte = s_ring[s_ring_tail];
    s_ring_tail = (s_ring_tail + 1) % RING_SIZE;
    return 0;
}

static int ring_count(void)
{
    int diff = (int)s_ring_head - (int)s_ring_tail;
    if (diff < 0) diff += RING_SIZE;
    return diff;
}

static void ring_clear(void)
{
    s_ring_tail = s_ring_head;
}

/* 在环形缓冲区中搜索字符串 */
static int ring_find(const char *str, int len)
{
    if (ring_count() < len) return -1;
    for (int i = 0; i <= ring_count() - len; i++) {
        int idx = (s_ring_tail + i) % RING_SIZE;
        int match = 1;
        for (int j = 0; j < len; j++) {
            int idx2 = (idx + j) % RING_SIZE;
            if ((char)s_ring[idx2] != str[j]) {
                match = 0;
                break;
            }
        }
        if (match) return i;
    }
    return -1;
}

/* 读取环形缓冲区到线性缓冲区 */
static int ring_read_linear(uint8_t *buf, int max_len)
{
    int n = 0;
    while (n < max_len) {
        if (ring_pop(&buf[n]) < 0) break;
        n++;
    }
    return n;
}

/* ==================== +IPD 解析 ====================
 * ESP8266 接收 TCP 数据时输出: "\r\n+IPD,<len>:<payload>"
 * 状态机逐字节消费: 检测到 "+IPD," 后, 长度数字和冒号被状态机
 * 吃掉 (不进任何缓冲区), 只有 <payload> 进 s_dring。
 * 这样 MQTT 层读到的第一个字节就是 TCP 包头 (如 CONNACK 的 0x20)。
 * 此函数在 USART2 ISR 中被逐字节调用 */
static void parse_ipd_byte(uint8_t byte)
{
    static char ipd_buf[32];
    static int ipd_pos = 0;
    static int parse_state = 0;  /* 0=normal, 1=in +IPD len, 2=in payload */

    if (parse_state == 0) {
        /* 检测 +IPD 起始 */
        ipd_buf[ipd_pos++] = (char)byte;
        if (ipd_pos >= 32) ipd_pos = 0;

        if (ipd_pos >= 5 && memcmp(ipd_buf + ipd_pos - 5, "+IPD,", 5) == 0) {
            parse_state = 1;
            ipd_pos = 0;
            ipd_buf[0] = '\0';
        }
    } else if (parse_state == 1) {
        /* 读取长度直到 ':' —— 这些字节全部丢弃 */
        if (byte == ':') {
            ipd_buf[ipd_pos] = '\0';
            s_ipd_total = atoi(ipd_buf);
            s_ipd_remaining = s_ipd_total;
            s_ipd_active = 1;
            parse_state = 2;
            ipd_pos = 0;
        } else {
            if (ipd_pos < 15) {
                ipd_buf[ipd_pos++] = (char)byte;
            }
        }
    } else if (parse_state == 2) {
        /* 纯载荷进数据环形缓冲区 (注意: 不是 s_ring!) */
        dring_push(byte);
        s_ipd_remaining--;
        if (s_ipd_remaining <= 0) {
            s_ipd_active = 0;
            s_ipd_remaining = 0;
            parse_state = 0;
            ipd_pos = 0;
        }
    }
}

/* ==================== UART ISR 回调 ==================== */

void esp8266_handle_rx_byte(uint8_t byte)
{
    /* 正在接收 +IPD 载荷: 直接进数据缓冲区 */
    if (s_ipd_active || s_ipd_remaining > 0) {
        dring_push(byte);
        s_ipd_remaining--;
        if (s_ipd_remaining <= 0) {
            s_ipd_active = 0;
            s_ipd_remaining = 0;
        }
        return;
    }

    /* 解析 +IPD 头 (头字节被状态机消费, 不进 AT 缓冲区) */
    parse_ipd_byte(byte);

    /* 不在 +IPD 载荷中: 进 AT 响应缓冲区供 ring_find 匹配 */
    if (!s_ipd_active) {
        ring_push(byte);
    }
}

/* ==================== 初始化 ==================== */

void esp8266_init(void)
{
    GPIO_InitTypeDef gpio;
    USART_InitTypeDef uart;
    NVIC_InitTypeDef nvic;

    /* 开启时钟 */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART2, ENABLE);

    /* PA2 = TX, 复用推挽 */
    gpio.GPIO_Pin = GPIO_Pin_2;
    gpio.GPIO_Mode = GPIO_Mode_AF_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &gpio);

    /* PA3 = RX, 浮空输入 */
    gpio.GPIO_Pin = GPIO_Pin_3;
    gpio.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOA, &gpio);

    /* USART2: 115200 8N1 */
    uart.USART_BaudRate = 115200;
    uart.USART_WordLength = USART_WordLength_8b;
    uart.USART_StopBits = USART_StopBits_1;
    uart.USART_Parity = USART_Parity_No;
    uart.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    uart.USART_Mode = USART_Mode_Tx | USART_Mode_Rx;
    USART_Init(ESP8266_UART, &uart);

    /* 使能接收中断 */
    USART_ITConfig(ESP8266_UART, USART_IT_RXNE, ENABLE);

    /* NVIC 配置 */
    nvic.NVIC_IRQChannel = ESP8266_UART_IRQn;
    nvic.NVIC_IRQChannelPreemptionPriority = 3;
    nvic.NVIC_IRQChannelSubPriority = 0;
    nvic.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&nvic);

    USART_Cmd(ESP8266_UART, ENABLE);

    ring_clear();
    dring_clear();
    s_tcp_connected = 0;
    s_ipd_active = 0;
    s_ipd_remaining = 0;
}

/* ==================== AT 指令 ==================== */

int esp8266_send_at(const char *at_cmd, const char *expect, uint32_t timeout_ms)
{
    ring_clear();
    uart_send_bytes(ESP8266_UART, (const uint8_t *)at_cmd, (int)strlen(at_cmd));

    uint32_t start = sys_tick();
    int result = 0;
    int expect_len = expect ? (int)strlen(expect) : 0;

    while (sys_tick() - start < timeout_ms) {
        if (expect_len > 0 && ring_find(expect, expect_len) >= 0) {
            result = 1;
            break;
        }
        if (ring_find("ERROR", 5) >= 0) {
            result = 0;
            break;
        }
        sys_delay_ms(10);  /* 等待期间泵 LVGL */
    }

    return result;
}

void esp8266_clear_buf(void)
{
    /* 只清 AT 响应缓冲区; 数据缓冲区 (TCP 载荷) 由 MQTT 层管理 */
    ring_clear();
}

int esp8266_buf_find(const char *str, int len)
{
    return ring_find(str, len);
}

/* ==================== WiFi ==================== */

int esp8266_connect_wifi(const char *ssid, const char *password)
{
    static char cmd[128];  /* static: 不占栈 */

    /* 测试 AT */
    if (!esp8266_send_at("AT\r\n", "OK", 2000)) {
        DBG("[ESP] AT no response\r\n");
        return 0;
    }

    /* Station 模式 */
    esp8266_send_at("AT+CWMODE=1\r\n", "OK", 2000);
    sys_delay_ms(500);

    /* 先断开旧连接 */
    esp8266_send_at("AT+CWQAP\r\n", "OK", 2000);
    sys_delay_ms(500);

    /* 连接 WiFi */
    snprintf(cmd, sizeof(cmd), "AT+CWJAP=\"%s\",\"%s\"\r\n", ssid, password);
    DBG("[ESP] Connecting WiFi: %s\r\n", ssid);

    esp8266_clear_buf();
    uart_send_bytes(ESP8266_UART, (const uint8_t *)cmd, (int)strlen(cmd));

    uint32_t start = sys_tick();
    while (sys_tick() - start < 20000) {
        if (ring_find("WIFI GOT IP", 11) >= 0) {
            DBG("[ESP] WiFi connected\r\n");
            return 1;
        }
        if (ring_find("FAIL", 4) >= 0) {
            DBG("[ESP] WiFi FAIL\r\n");
            return 0;
        }
        sys_delay_ms(100);  /* 等待期间泵 LVGL, 20s 内 UI 不冻结 */
    }

    DBG("[ESP] WiFi timeout\r\n");
    return 0;
}

/* ==================== TCP ==================== */

int esp8266_connect_tcp(const char *ip, uint16_t port)
{
    static char cmd[128];  /* static: 不占栈 */

    DBG("[ESP] TCP connect %s:%d\r\n", ip, port);

    /* 先关闭残留连接 */
    esp8266_send_at("AT+CIPCLOSE\r\n", "OK", 1000);

    /* 单连接模式 */
    if (!esp8266_send_at("AT+CIPMUX=0\r\n", "OK", 2000)) {
        DBG("[ESP] CIPMUX failed\r\n");
        return 0;
    }

    /* 检查是否已获得 IP */
    if (!esp8266_send_at("AT+CIFSR\r\n", "STAIP", 2000)) {
        DBG("[ESP] No IP address\r\n");
        return 0;
    }

    /* TCP 连接 */
    snprintf(cmd, sizeof(cmd),
             "AT+CIPSTART=\"TCP\",\"%s\",%d\r\n", ip, port);

    ring_clear();
    uart_send_bytes(ESP8266_UART, (const uint8_t *)cmd, (int)strlen(cmd));

    uint32_t start = sys_tick();
    int ok = 0;
    while (sys_tick() - start < 15000) {
        if (ring_find("CONNECT", 7) >= 0 ||
            ring_find("ALREADY CONNECTED", 17) >= 0) {
            s_tcp_connected = 1;
            ok = 1;
            break;
        }
        if (ring_find("ERROR", 5) >= 0 ||
            ring_find("fail", 4) >= 0) {
            DBG("[ESP] TCP connect ERROR\r\n");
            break;
        }
        sys_delay_ms(50);
    }

    if (ok) {
        DBG("[ESP] TCP connected\r\n");
    }
    return ok;
}

void esp8266_disconnect_tcp(void)
{
    esp8266_send_at("AT+CIPCLOSE\r\n", "OK", 1000);
    s_tcp_connected = 0;
}

int esp8266_is_tcp_connected(void)
{
    return s_tcp_connected;
}

/* ==================== 数据收发 ==================== */

int esp8266_send_data(const uint8_t *data, int len)
{
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "AT+CIPSEND=%d\r\n", len);

    if (!esp8266_send_at(cmd, ">", 2000)) {
        DBG("[ESP] CIPSEND no > prompt\r\n");
        return -1;
    }

    uart_send_bytes(ESP8266_UART, data, len);

    /* 等待 SEND OK */
    ring_clear();
    uint32_t start = sys_tick();
    int ok = 0;
    while (sys_tick() - start < 3000) {
        if (ring_find("SEND OK", 7) >= 0) { ok = 1; break; }
        if (ring_find("SEND FAIL", 9) >= 0) break;
        sys_delay_ms(10);
    }

    return ok ? len : -1;
}

int esp8266_send_data_fast(const uint8_t *data, int len)
{
    /* 不等待 SEND OK, 用于连续发送 (减少延迟) */
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "AT+CIPSEND=%d\r\n", len);

    if (!esp8266_send_at(cmd, ">", 2000)) return -1;

    uart_send_bytes(ESP8266_UART, data, len);
    return len;
}

int esp8266_data_available(void)
{
    /* 数据缓冲区中待读的 TCP 载荷字节数 */
    int diff = (int)s_dring_head - (int)s_dring_tail;
    if (diff < 0) diff += DRING_SIZE;
    return diff;
}

int esp8266_recv_data(uint8_t *buf, int max_len, uint32_t timeout_ms)
{
    uint32_t start = sys_tick();
    int n = 0;

    while (sys_tick() - start < timeout_ms && n < max_len) {
        while (n < max_len) {
            uint8_t byte;
            if (dring_pop(&byte) < 0) break;
            buf[n++] = byte;
        }
        if (n > 0) break;
        sys_delay_ms(5);  /* 等待期间泵 LVGL, UI 不冻结 */
    }

    return n;
}

/* ==================== HTTP GET (OTA 用) ==================== */

int esp8266_http_get(const char *host, uint16_t port, const char *path,
                      uint8_t *buf, int buf_size, uint32_t *received)
{
    static char http_req[256];  /* static: 不占栈 */
    int req_len;

    /* 连接 HTTP 服务器 */
    if (!esp8266_connect_tcp(host, port)) {
        DBG("[OTA] HTTP TCP connect failed\r\n");
        return -1;
    }

    /* 构造 HTTP GET 请求 */
    req_len = snprintf(http_req, sizeof(http_req),
        "GET %s HTTP/1.1\r\n"
        "Host: %s:%d\r\n"
        "Connection: close\r\n"
        "\r\n", path, host, port);

    /* 发送请求 */
    if (esp8266_send_data((const uint8_t *)http_req, req_len) < 0) {
        DBG("[OTA] HTTP send failed\r\n");
        return -1;
    }

    /* 接收响应 */
    int total = 0;
    uint32_t start = sys_tick();
    int header_end = 0;

    while (sys_tick() - start < 30000) {
        int avail = esp8266_data_available();
        if (avail > 0 && total < buf_size) {
            int n = esp8266_recv_data(buf + total, buf_size - total, 100);
            if (n > 0) {
                total += n;
                start = sys_tick();  /* 重置超时 */

                /* 查找 \r\n\r\n (HTTP 头结束) */
                if (!header_end) {
                    for (int i = 0; i < total - 3; i++) {
                        if (buf[i] == '\r' && buf[i+1] == '\n' &&
                            buf[i+2] == '\r' && buf[i+3] == '\n') {
                            header_end = i + 4;
                            break;
                        }
                    }
                }
            }
        }
        sys_delay_ms(10);
    }

    /* 移除 HTTP 头, 只保留 body */
    if (header_end > 0 && header_end < total) {
        int body_len = total - header_end;
        memmove(buf, buf + header_end, body_len);
        *received = body_len;
        return 0;
    }

    *received = total;
    return (total > 0) ? 0 : -1;
}
