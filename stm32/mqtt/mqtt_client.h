#ifndef MQTT_CLIENT_H
#define MQTT_CLIENT_H

#include <stdint.h>

/* ============================================================
 * MQTT 客户端 — 基于 ESP8266 TCP 通道
 * 实现: CONNECT / PUBLISH / SUBSCRIBE / PINGREQ / 自动重连
 * ============================================================ */

/* 收到 PUBLISH 消息时的回调函数类型 */
typedef void (*mqtt_msg_callback_t)(const char *topic,
                                    const char *payload, int payload_len);

/* 初始化 MQTT 客户端 */
void mqtt_init(void);

/* 连接到 MQTT Broker
 * client_id:  客户端标识
 * will_topic: 遗嘱主题 (NULL=无)
 * will_msg:   遗嘱消息 (NULL=无)
 * 返回: 1=成功, 0=失败
 */
int mqtt_connect(const char *client_id,
                 const char *will_topic,
                 const char *will_msg);

/* 发布消息 (QoS 0)
 * topic:   主题 (如 "factory/telemetry/dev01")
 * payload: 消息内容
 * 返回: 1=成功, 0=失败
 */
int mqtt_publish(const char *topic, const char *payload);

/* 订阅主题 (QoS 1)
 * topic: 主题 (如 "factory/control/dev01")
 * 返回: 1=成功, 0=失败
 */
int mqtt_subscribe(const char *topic);

/* 发送心跳 (建议每 30 秒调用一次) */
int mqtt_ping(void);

/* 主循环处理 — 检查接收数据并回调
 * callback: 收到 PUBLISH 时调用的回调
 * 返回: 1=连接正常, 0=需要重连
 */
int mqtt_loop(mqtt_msg_callback_t callback);

/* 检查是否已连接 */
int mqtt_is_connected(void);

/* 断开连接 */
void mqtt_disconnect(void);

#endif /* MQTT_CLIENT_H */
