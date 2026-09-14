#include "mqtt_packet.h"
#include <string.h>

/* ============================================================
 * MQTT 3.1.1 数据包编解码实现
 * 参考: http://docs.oasis-open.org/mqtt/mqtt/v3.1.1/os/mqtt-v3.1.1-os.html
 * ============================================================ */

/* 编码 Remaining Length (可变长度 1-4 字节) */
static int encode_remaining_length(uint8_t *buf, int value)
{
    int bytes = 0;
    do {
        uint8_t b = (uint8_t)(value % 128);
        value /= 128;
        if (value > 0) b |= 0x80;
        buf[bytes++] = b;
    } while (value > 0 && bytes < 4);
    return bytes;
}

/* 计算编码 Remaining Length 需要的字节数 */
static int remaining_length_bytes(int value)
{
    if (value <= 127)    return 1;
    if (value <= 16383)   return 2;
    if (value <= 2097151) return 3;
    return 4;
}

/* 写入 UTF-8 编码字符串: 2字节长度 + 字符串内容 */
static int write_string(uint8_t *buf, const char *str)
{
    int len = (int)strlen(str);
    buf[0] = (uint8_t)(len >> 8);
    buf[1] = (uint8_t)(len & 0xFF);
    memcpy(&buf[2], str, len);
    return 2 + len;
}

/* ==================== CONNECT 编码 ==================== */

int mqtt_encode_connect(uint8_t *buf, int buf_size,
                        const char *client_id,
                        const char *will_topic,
                        const char *will_msg,
                        uint16_t keepalive)
{
    /* 计算载荷长度 */
    int cid_len = (int)strlen(client_id);
    int payload_len = 2 + cid_len;

    int will_len = 0;
    if (will_topic && will_msg) {
        will_len = (int)strlen(will_msg);
        payload_len += 2 + (int)strlen(will_topic);
        payload_len += 2 + will_len;
    }

    /* 可变报头: 协议名(6) + 级别(1) + 标志(1) + 心跳(2) = 10 */
    int var_hdr_len = 10;
    int rem_len = var_hdr_len + payload_len;

    /* 检查缓冲区大小 */
    int rl_bytes = remaining_length_bytes(rem_len);
    int total = 1 + rl_bytes + rem_len;
    if (total > buf_size) return 0;

    int idx = 0;

    /* Fixed header */
    buf[idx++] = (MQTT_TYPE_CONNECT << 4);  /* 0x10 */
    idx += encode_remaining_length(&buf[idx], rem_len);

    /* Variable header */
    /* 协议名 "MQTT" */
    buf[idx++] = 0x00;
    buf[idx++] = 0x04;
    buf[idx++] = 'M';
    buf[idx++] = 'Q';
    buf[idx++] = 'T';
    buf[idx++] = 'T';
    /* 协议级别 3.1.1 = 4 */
    buf[idx++] = 0x04;
    /* 连接标志 */
    uint8_t flags = 0x02;  /* Clean Session */
    if (will_topic && will_msg) {
        flags |= (1 << 2);  /* Will Flag */
        flags |= (1 << 3);  /* Will QoS 1 */
        /* 不设置 Will Retain */
    }
    buf[idx++] = flags;
    /* 心跳间隔 */
    buf[idx++] = (uint8_t)(keepalive >> 8);
    buf[idx++] = (uint8_t)(keepalive & 0xFF);

    /* Payload: Client ID */
    buf[idx++] = (uint8_t)(cid_len >> 8);
    buf[idx++] = (uint8_t)(cid_len & 0xFF);
    memcpy(&buf[idx], client_id, cid_len);
    idx += cid_len;

    /* Payload: Will Topic + Will Message */
    if (will_topic && will_msg) {
        idx += write_string(&buf[idx], will_topic);

        buf[idx++] = (uint8_t)(will_len >> 8);
        buf[idx++] = (uint8_t)(will_len & 0xFF);
        memcpy(&buf[idx], will_msg, will_len);
        idx += will_len;
    }

    return idx;
}

/* ==================== PUBLISH 编码 (QoS 0) ==================== */

int mqtt_encode_publish(uint8_t *buf, int buf_size,
                        const char *topic,
                        const char *payload, int payload_len)
{
    int topic_len = (int)strlen(topic);
    int rem_len = 2 + topic_len + payload_len;
    int rl_bytes = remaining_length_bytes(rem_len);
    int total = 1 + rl_bytes + rem_len;
    if (total > buf_size) return 0;

    int idx = 0;

    /* Fixed header: PUBLISH, QoS 0, no retain */
    buf[idx++] = (MQTT_TYPE_PUBLISH << 4);  /* 0x30 */
    idx += encode_remaining_length(&buf[idx], rem_len);

    /* Topic */
    buf[idx++] = (uint8_t)(topic_len >> 8);
    buf[idx++] = (uint8_t)(topic_len & 0xFF);
    memcpy(&buf[idx], topic, topic_len);
    idx += topic_len;

    /* Payload (QoS 0 没有 Packet ID) */
    if (payload_len > 0 && payload) {
        memcpy(&buf[idx], payload, payload_len);
        idx += payload_len;
    }

    return idx;
}

/* ==================== SUBSCRIBE 编码 (QoS 1) ==================== */

int mqtt_encode_subscribe(uint8_t *buf, int buf_size,
                          uint16_t packet_id,
                          const char *topic, int qos)
{
    int topic_len = (int)strlen(topic);
    int rem_len = 2 + 2 + topic_len + 1;  /* PacketID + Topic + QoS */
    int rl_bytes = remaining_length_bytes(rem_len);
    int total = 1 + rl_bytes + rem_len;
    if (total > buf_size) return 0;

    int idx = 0;

    /* Fixed header: SUBSCRIBE, QoS 1 */
    buf[idx++] = (MQTT_TYPE_SUBSCRIBE << 4) | 0x02;  /* 0x82 */
    idx += encode_remaining_length(&buf[idx], rem_len);

    /* Packet ID */
    buf[idx++] = (uint8_t)(packet_id >> 8);
    buf[idx++] = (uint8_t)(packet_id & 0xFF);

    /* Topic Filter */
    buf[idx++] = (uint8_t)(topic_len >> 8);
    buf[idx++] = (uint8_t)(topic_len & 0xFF);
    memcpy(&buf[idx], topic, topic_len);
    idx += topic_len;

    /* QoS */
    buf[idx++] = (uint8_t)qos;

    return idx;
}

/* ==================== PINGREQ 编码 ==================== */

int mqtt_encode_pingreq(uint8_t *buf, int buf_size)
{
    if (buf_size < 2) return 0;
    buf[0] = (MQTT_TYPE_PINGREQ << 4);  /* 0xC0 */
    buf[1] = 0x00;
    return 2;
}

/* ==================== 解码: 解析接收到的报文 ==================== */

/* 解析 Remaining Length, 返回值和消耗字节数 */
static int decode_remaining_length(const uint8_t *buf, int *bytes_consumed)
{
    int value = 0;
    int multiplier = 1;
    int i = 0;
    do {
        value += (buf[i] & 0x7F) * multiplier;
        multiplier *= 128;
        i++;
    } while (buf[i - 1] & 0x80);
    *bytes_consumed = i;
    return value;
}

int mqtt_decode_packet(uint8_t *buf, int len, mqtt_msg_t *msg)
{
    if (len < 2 || !buf || !msg) return MQTT_MSG_UNKNOWN;

    memset(msg, 0, sizeof(mqtt_msg_t));

    uint8_t type = (buf[0] >> 4) & 0x0F;
    int rl_bytes;
    int rem_len = decode_remaining_length(&buf[1], &rl_bytes);
    int hdr_size = 1 + rl_bytes;

    if (hdr_size + rem_len > len) return MQTT_MSG_UNKNOWN;  /* 不完整 */

    switch (type) {
    case MQTT_TYPE_CONNACK: {
        /* CONNACK: 2 bytes variable header */
        if (rem_len < 2) return MQTT_MSG_UNKNOWN;
        msg->type = MQTT_MSG_CONNACK;
        msg->return_code = buf[hdr_size + 1];
        return MQTT_MSG_CONNACK;
    }

    case MQTT_TYPE_PUBLISH: {
        /* PUBLISH: topic (2 len + string) + payload */
        msg->type = MQTT_MSG_PUBLISH;
        int pos = hdr_size;

        /* QoS flags */
        int qos = (buf[0] >> 1) & 0x03;

        if (pos + 2 > len) return MQTT_MSG_UNKNOWN;
        int topic_len = (buf[pos] << 8) | buf[pos + 1];
        pos += 2;

        if (pos + topic_len > len) return MQTT_MSG_UNKNOWN;
        if (topic_len > 0 && topic_len < (int)sizeof(msg->topic)) {
            memcpy(msg->topic, &buf[pos], topic_len);
            msg->topic[topic_len] = '\0';
            msg->topic_len = topic_len;
        }
        pos += topic_len;

        /* QoS 1/2 有 Packet ID */
        if (qos > 0) {
            if (pos + 2 > len) return MQTT_MSG_UNKNOWN;
            msg->packet_id = (buf[pos] << 8) | buf[pos + 1];
            pos += 2;
        }

        /* Payload: 剩余部分 */
        int payload_len = rem_len - (pos - hdr_size);
        if (payload_len > 0) {
            if (payload_len > (int)sizeof(msg->payload))
                payload_len = (int)sizeof(msg->payload);
            memcpy(msg->payload, &buf[pos], payload_len);
            msg->payload_len = payload_len;
        }
        return MQTT_MSG_PUBLISH;
    }

    case MQTT_TYPE_SUBACK: {
        if (rem_len < 3) return MQTT_MSG_UNKNOWN;
        msg->type = MQTT_MSG_SUBACK;
        msg->packet_id = (buf[hdr_size] << 8) | buf[hdr_size + 1];
        return MQTT_MSG_SUBACK;
    }

    case MQTT_TYPE_PINGRESP:
        msg->type = MQTT_MSG_PINGRESP;
        return MQTT_MSG_PINGRESP;

    case MQTT_TYPE_PUBACK:
        msg->type = MQTT_MSG_PUBACK;
        return MQTT_MSG_PUBACK;

    default:
        return MQTT_MSG_UNKNOWN;
    }
}
