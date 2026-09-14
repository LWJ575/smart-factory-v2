#include "mqtt_client.h"
#include "mqtt_packet.h"
#include "esp8266.h"
#include "config.h"
#include <string.h>

/* ============================================================
 * MQTT 客户端实现
 * ============================================================ */

static int s_mqtt_connected = 0;
static uint16_t s_packet_id = 1;
static uint32_t s_last_ping = 0;
static int s_pong_received = 1;

static uint8_t s_tx_buf[MQTT_BUF_SIZE];
static uint8_t s_rx_buf[MQTT_BUF_SIZE];

/* ==================== 初始化 ==================== */

void mqtt_init(void)
{
    s_mqtt_connected = 0;
    s_packet_id = 1;
    s_last_ping = 0;
    s_pong_received = 1;
}

/* ==================== 连接 ==================== */

int mqtt_connect(const char *client_id,
                 const char *will_topic,
                 const char *will_msg)
{
    int retries = 3;

    while (retries-- > 0) {
        /* 确保 TCP 连接 */
        if (!esp8266_is_connected()) {
            if (!esp8266_connect_tcp(BROKER_IP, BROKER_PORT)) {
                DBG("[MQTT] TCP connect failed, retry...\r\n");
                sys_delay_ms(1000);
                continue;
            }
        }

        /* 编码 CONNECT */
        int len = mqtt_encode_connect(s_tx_buf, sizeof(s_tx_buf),
                                     client_id, will_topic, will_msg,
                                     MQTT_KEEPALIVE);
        if (len <= 0) {
            DBG("[MQTT] CONNECT encode failed\r\n");
            return 0;
        }

        /* 发送 CONNECT */
        if (!esp8266_send_tcp(s_tx_buf, len)) {
            DBG("[MQTT] CONNECT send failed\r\n");
            esp8266_disconnect_tcp();
            continue;
        }

        /* 等待 CONNACK (最多 5 秒)
         * 累积接收: 4 字节 CONNACK 可能分多次到达 */
        uint32_t start = sys_tick();
        int acc_len = 0;
        while (sys_tick() - start < 5000 && acc_len < (int)sizeof(s_rx_buf)) {
            int n = esp8266_recv_data(s_rx_buf + acc_len,
                                      sizeof(s_rx_buf) - acc_len, 200);
            if (n > 0) {
                acc_len += n;
            }

            if (acc_len >= 4) {
                mqtt_msg_t msg;
                if (mqtt_decode_packet(s_rx_buf, acc_len, &msg) == MQTT_MSG_CONNACK) {
                    if (msg.return_code == MQTT_CONNACK_ACCEPTED) {
                        s_mqtt_connected = 1;
                        s_pong_received = 1;
                        s_last_ping = sys_tick();
                        DBG("[MQTT] Connected to broker\r\n");
                        return 1;
                    } else {
                        DBG("[MQTT] CONNACK error: %d\r\n", msg.return_code);
                        return 0;
                    }
                }
            }
        }

        DBG("[MQTT] CONNACK timeout, retry...\r\n");
        esp8266_disconnect_tcp();
        sys_delay_ms(500);
    }

    return 0;
}

/* ==================== 发布 ==================== */

int mqtt_publish(const char *topic, const char *payload)
{
    if (!s_mqtt_connected) return 0;

    int payload_len = (int)strlen(payload);
    int len = mqtt_encode_publish(s_tx_buf, sizeof(s_tx_buf),
                                 topic, payload, payload_len);
    if (len <= 0) {
        DBG("[MQTT] PUBLISH encode failed\r\n");
        return 0;
    }

    if (!esp8266_send_tcp(s_tx_buf, len)) {
        DBG("[MQTT] PUBLISH send failed\r\n");
        s_mqtt_connected = 0;
        return 0;
    }

    return 1;
}

/* ==================== 订阅 ==================== */

int mqtt_subscribe(const char *topic)
{
    if (!s_mqtt_connected) return 0;

    int len = mqtt_encode_subscribe(s_tx_buf, sizeof(s_tx_buf),
                                    s_packet_id++, topic, 1);
    if (len <= 0) {
        DBG("[MQTT] SUBSCRIBE encode failed\r\n");
        return 0;
    }

    if (!esp8266_send_tcp(s_tx_buf, len)) {
        DBG("[MQTT] SUBSCRIBE send failed\r\n");
        s_mqtt_connected = 0;
        return 0;
    }

    /* 等待 SUBACK (累积接收, 可能与 PUBLISH 交叉到达) */
    uint32_t start = sys_tick();
    int acc_len = 0;
    while (sys_tick() - start < 3000 && acc_len < (int)sizeof(s_rx_buf)) {
        int n = esp8266_recv_data(s_rx_buf + acc_len,
                                  sizeof(s_rx_buf) - acc_len, 100);
        if (n > 0) {
            acc_len += n;
        }

        if (acc_len >= 5) {
            mqtt_msg_t msg;
            if (mqtt_decode_packet(s_rx_buf, acc_len, &msg) == MQTT_MSG_SUBACK) {
                DBG("[MQTT] Subscribed to %s\r\n", topic);
                return 1;
            }
            /* 收到 PUBLISH 等其他报文: 继续等 SUBACK */
        }
    }

    DBG("[MQTT] SUBACK timeout\r\n");
    return 1;  /* 即使没收到 SUBACK 也认为成功 */
}

/* ==================== 心跳 ==================== */

int mqtt_ping(void)
{
    if (!s_mqtt_connected) return 0;

    int len = mqtt_encode_pingreq(s_tx_buf, sizeof(s_tx_buf));
    if (len <= 0) return 0;

    if (!esp8266_send_tcp(s_tx_buf, len)) {
        s_mqtt_connected = 0;
        return 0;
    }

    s_last_ping = sys_tick();
    s_pong_received = 0;
    return 1;
}

/* ==================== 主循环 ==================== */

int mqtt_loop(mqtt_msg_callback_t callback)
{
    /* 检查 TCP 连接 */
    int n = esp8266_recv_tcp(s_rx_buf, sizeof(s_rx_buf));

    if (n < 0) {
        /* TCP 断开 */
        s_mqtt_connected = 0;
        DBG("[MQTT] TCP disconnected\r\n");
        return 0;
    }

    if (n > 0) {
        /* 解析 MQTT 报文 */
        mqtt_msg_t msg;
        int msg_type = mqtt_decode_packet(s_rx_buf, n, &msg);

        switch (msg_type) {
        case MQTT_MSG_PUBLISH:
            /* 收到控制指令, 调用回调 */
            if (callback) {
                callback(msg.topic, (char *)msg.payload, msg.payload_len);
            }
            break;

        case MQTT_MSG_PINGRESP:
            s_pong_received = 1;
            break;

        case MQTT_MSG_CONNACK:
        case MQTT_MSG_SUBACK:
        case MQTT_MSG_PUBACK:
            /* 其他响应, 忽略 */
            break;

        case MQTT_MSG_UNKNOWN:
            /* 可能是分包数据, 暂时忽略 */
            break;
        }
    }

    /* 检查心跳超时 */
    if (s_mqtt_connected) {
        uint32_t now = sys_tick();
        uint32_t elapsed = (now - s_last_ping) / 1000;

        /* 每 MQTT_KEEPALIVE 秒发送一次心跳 */
        if (elapsed >= (uint32_t)MQTT_KEEPALIVE) {
            if (s_pong_received) {
                mqtt_ping();
            } else {
                /* 上次心跳没有收到 PINGRESP, 判定断线 */
                DBG("[MQTT] Ping timeout, need reconnect\r\n");
                s_mqtt_connected = 0;
                return 0;
            }
        }
    }

    return s_mqtt_connected ? 1 : 0;
}

/* ==================== 其他 ==================== */

int mqtt_is_connected(void)
{
    return s_mqtt_connected;
}

void mqtt_disconnect(void)
{
    s_mqtt_connected = 0;
    esp8266_disconnect_tcp();
}
