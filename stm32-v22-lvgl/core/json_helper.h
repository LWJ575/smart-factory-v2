#ifndef JSON_HELPER_H
#define JSON_HELPER_H

#include <stdint.h>

/* ============================================================
 * 轻量级 JSON 工具 v2 — 不依赖第三方库
 * 适配 v2.0 的 int16_t temp_x10 接口
 * ============================================================ */

/* 构建遥测 JSON 字符串
 * temp_x10: 温度 × 10 (如 256 = 25.6°C)
 * 返回字符串长度
 */
int json_build_telemetry(char *buf, int buf_size,
                         const char *device_id,
                         int16_t temp_x10,
                         uint16_t light_raw,
                         uint8_t motor_speed,
                         uint8_t led1_on,
                         uint8_t led2_on);

/* 解析控制 JSON 指令
 * 格式: {"cmd":"led1","action":"on"} 或 {"cmd":"motor","value":50}
 * json:   收到的 JSON 字符串
 * len:    字符串长度
 * cmd_out:     输出指令类型字符串 ("led1"/"led2"/"motor"/"buzzer")
 * cmd_size:    cmd_out 缓冲区大小
 * action_out:  输出动作字符串 ("on"/"off")
 * action_size: action_out 缓冲区大小
 * value_out:   输出数值 (电机速度等)
 * 返回: 1=成功, 0=失败
 */
int json_parse_control(const char *json, int len,
                       char *cmd_out, int cmd_size,
                       char *action_out, int action_size,
                       int *value_out);

/* 构建状态 JSON (online/offline) */
int json_build_status(char *buf, int buf_size,
                      const char *device_id, const char *status);

/* ---- 底层工具 ---- */

/* 在 JSON 中搜索 "key":"value", 提取 value 字符串 */
int json_get_string(const char *json, int json_len, const char *key,
                    char *out, int out_size);

/* 在 JSON 中搜索 "key":number, 提取整数 */
int json_get_int(const char *json, int json_len, const char *key, int *out);

#endif /* JSON_HELPER_H */
