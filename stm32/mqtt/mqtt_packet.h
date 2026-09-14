#ifndef MQTT_PACKET_H
#define MQTT_PACKET_H

#include <stdint.h>

/* ============================================================
 * MQTT 3.1.1 数据包编解码
 * 不依赖任何外部库, 纯 C 实现
 * ============================================================ */

/* MQTT 报文类型 */
#define MQTT_TYPE_CONNECT     1
#define MQTT_TYPE_CONNACK     2
#define MQTT_TYPE_PUBLISH     3
#define MQTT_TYPE_PUBACK      4
#define MQTT_TYPE_SUBSCRIBE   8
#define MQTT_TYPE_SUBACK      9
#define MQTT_TYPE_PINGREQ    12
#define MQTT_TYPE_PINGRESP   13

/* CONNACK 返回码 */
#define MQTT_CONNACK_ACCEPTED       0
#define MQTT_CONNACK_BAD_PROTOCOL   1
#define MQTT_CONNACK_BAD_CLIENT_ID  2
#define MQTT_CONNACK_SERVER_UNAVAIL 3
#define MQTT_CONNACK_BAD_USER       4
#define MQTT_CONNACK_BAD_PASSWORD   5

/* 解析后的 MQTT 消息 */
typedef enum {
    MQTT_MSG_UNKNOWN = 0,
    MQTT_MSG_CONNACK,
    MQTT_MSG_PUBLISH,
    MQTT_MSG_SUBACK,
    MQTT_MSG_PINGRESP,
    MQTT_MSG_PUBACK,
} mqtt_msg_type_t;

typedef struct {
    mqtt_msg_type_t type;
    /* CONNACK */
    int return_code;
    /* PUBLISH */
    char topic[64];
    int  topic_len;
    uint8_t payload[256];
    int  payload_len;
    /* SUBACK / PUBACK */
    uint16_t packet_id;
} mqtt_msg_t;

/* ---- 编码函数 ----
 * 返回编码后的总字节数, 0=失败
 */

/* 编码 CONNECT 报文
 * client_id:  客户端 ID
 * will_topic: 遗嘱主题 (NULL=无遗嘱)
 * will_msg:   遗嘱消息 (NULL=无遗嘱)
 * keepalive:  心跳间隔(秒)
 */
int mqtt_encode_connect(uint8_t *buf, int buf_size,
                        const char *client_id,
                        const char *will_topic,
                        const char *will_msg,
                        uint16_t keepalive);

/* 编码 PUBLISH 报文 (QoS 0) */
int mqtt_encode_publish(uint8_t *buf, int buf_size,
                        const char *topic,
                        const char *payload, int payload_len);

/* 编码 SUBSCRIBE 报文 (QoS 1) */
int mqtt_encode_subscribe(uint8_t *buf, int buf_size,
                          uint16_t packet_id,
                          const char *topic, int qos);

/* 编码 PINGREQ 报文 */
int mqtt_encode_pingreq(uint8_t *buf, int buf_size);

/* ---- 解码函数 ----
 * 返回消息类型, 失败返回 MQTT_MSG_UNKNOWN
 */

int mqtt_decode_packet(uint8_t *buf, int len, mqtt_msg_t *msg);

#endif /* MQTT_PACKET_H */
