#ifndef ESP8266_H
#define ESP8266_H

#include <stdint.h>
#include <stdlib.h>

/* ============================================================
 * ESP8266 AT 驱动 — FreeRTOS 适配版 (v2.0)
 * 用互斥锁保护串口收发, ISR 中用 xHigherPriorityTaskWoken
 * ============================================================ */

/* 初始化 ESP8266 串口 (USART2 + 环形缓冲区 + 互斥锁) */
void esp8266_init(void);

/* UART 接收中断回调 (在 USART2_IRQHandler 中调用) */
void esp8266_handle_rx_byte(uint8_t byte);

/* AT 指令收发 */
int  esp8266_send_at(const char *at_cmd, const char *expect, uint32_t timeout_ms);
void esp8266_clear_buf(void);
int  esp8266_buf_find(const char *str, int len);

/* WiFi */
int  esp8266_connect_wifi(const char *ssid, const char *password);

/* TCP */
int  esp8266_connect_tcp(const char *ip, uint16_t port);
void esp8266_disconnect_tcp(void);
int  esp8266_is_tcp_connected(void);

/* 数据收发 (MQTT 层用) */
int  esp8266_send_data(const uint8_t *data, int len);
int  esp8266_send_data_fast(const uint8_t *data, int len);  /* 不加 AT 前缀 */
int  esp8266_recv_data(uint8_t *buf, int max_len, uint32_t timeout_ms);
int  esp8266_data_available(void);

/* HTTP GET (OTA 用) */
int  esp8266_http_get(const char *host, uint16_t port, const char *path,
                      uint8_t *buf, int buf_size, uint32_t *received);

/* ---- v1.0 兼容别名 (供 mqtt_client.c 使用) ---- */
#define esp8266_is_connected()          esp8266_is_tcp_connected()
#define esp8266_send_tcp(data, len)     esp8266_send_data(data, len)
#define esp8266_recv_tcp(buf, len)      esp8266_recv_data(buf, len, 100)

#endif /* ESP8266_H */
