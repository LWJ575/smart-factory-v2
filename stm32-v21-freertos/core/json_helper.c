#include "json_helper.h"
#include <string.h>
#include <stdio.h>

/* ============================================================
 * 轻量级 JSON 工具 v2 实现
 * ============================================================ */

/* ==================== 底层工具 ==================== */

/* 在 json[0..json_len) 中查找子串 */
static const char *find_substr(const char *json, int json_len, const char *key)
{
    int key_len = (int)strlen(key);
    for (int i = 0; i <= json_len - key_len; i++) {
        if (memcmp(json + i, key, key_len) == 0)
            return json + i;
    }
    return NULL;
}

int json_get_string(const char *json, int json_len, const char *key,
                    char *out, int out_size)
{
    /* 搜索 "key" */
    char pattern[32];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);

    const char *p = find_substr(json, json_len, pattern);
    if (!p) return 0;

    /* 跳过 "key" */
    p += strlen(pattern);

    /* 找冒号 */
    while (p < json + json_len && *p != ':') p++;
    if (p >= json + json_len) return 0;
    p++;  /* 跳过冒号 */

    /* 跳过空格 */
    while (p < json + json_len && (*p == ' ' || *p == '\t')) p++;

    /* 找引号开始 */
    if (p >= json + json_len || *p != '"') return 0;
    p++;  /* 跳过开始引号 */

    /* 提取到结束引号 */
    int n = 0;
    while (p < json + json_len && *p != '"' && n < out_size - 1) {
        out[n++] = *p++;
    }
    out[n] = '\0';
    return 1;
}

int json_get_int(const char *json, int json_len, const char *key, int *out)
{
    char pattern[32];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);

    const char *p = find_substr(json, json_len, pattern);
    if (!p) return 0;

    p += strlen(pattern);
    while (p < json + json_len && *p != ':') p++;
    if (p >= json + json_len) return 0;
    p++;

    while (p < json + json_len && (*p == ' ' || *p == '\t')) p++;

    int sign = 1;
    if (p < json + json_len && *p == '-') { sign = -1; p++; }

    int val = 0;
    while (p < json + json_len && *p >= '0' && *p <= '9') {
        val = val * 10 + (*p - '0');
        p++;
    }
    *out = val * sign;
    return 1;
}

/* ==================== 构建遥测 ==================== */

int json_build_telemetry(char *buf, int buf_size,
                         const char *device_id,
                         int16_t temp_x10,
                         uint16_t light_raw,
                         uint8_t motor_speed,
                         uint8_t led1_on,
                         uint8_t led2_on)
{
    /* 温度: 整数部分和小数部分分开, 避免 float printf */
    int temp_int = temp_x10 / 10;
    int temp_dec = (temp_x10 < 0 ? -temp_x10 : temp_x10) % 10;

    return snprintf(buf, buf_size,
        "{\"id\":\"%s\",\"t\":%d.%d,\"l\":%u,\"m\":%u,\"led1\":%d,\"led2\":%d}",
        device_id, temp_int, temp_dec,
        light_raw, motor_speed,
        led1_on ? 1 : 0, led2_on ? 1 : 0);
}

/* ==================== 解析控制指令 ==================== */

int json_parse_control(const char *json, int len,
                       char *cmd_out, int cmd_size,
                       char *action_out, int action_size,
                       int *value_out)
{
    /* 提取 "cmd" 字段 */
    if (!json_get_string(json, len, "cmd", cmd_out, cmd_size)) {
        return 0;
    }

    /* 提取 "action" 字段 (led1/led2/buzzer) */
    json_get_string(json, len, "action", action_out, action_size);

    /* 提取 "value" 字段 (motor speed) */
    if (!json_get_int(json, len, "value", value_out)) {
        *value_out = 0;
    }

    return 1;
}

/* ==================== 构建状态 ==================== */

int json_build_status(char *buf, int buf_size,
                      const char *device_id, const char *status)
{
    return snprintf(buf, buf_size,
        "{\"id\":\"%s\",\"status\":\"%s\"}", device_id, status);
}
