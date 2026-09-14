#ifndef OTA_UPDATE_H
#define OTA_UPDATE_H

#include <stdint.h>

/* ============================================================
 * OTA 远程固件升级
 * App 端: 收到 MQTT OTA 指令 → 设 BKP 标志 → 重启
 * Bootloader: 检测 BKP 标志 → HTTP 下载固件 → 写 Flash → 跳转
 * ============================================================ */

/* 从 App 端触发 OTA (设置标志并重启) */
void ota_trigger_update(void);

/* OTA 任务: 在 FreeRTOS 中独立运行, 等待 MQTT OTA 指令 */
void ota_task(void *params);

/* 处理收到的 OTA MQTT 指令
 * topic: factory/ota/dev01
 * payload: {"cmd":"start"} */
void ota_handle_mqtt_msg(const char *topic, const char *payload, int len);

#endif /* OTA_UPDATE_H */
