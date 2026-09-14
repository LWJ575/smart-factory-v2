#ifndef OTA_UPDATE_H
#define OTA_UPDATE_H

#include <stdint.h>

/* ============================================================
 * OTA 远程固件升级 (v2.2, 无 FreeRTOS)
 * App 端: 收到 MQTT OTA 指令 → 设 BKP 标志 → 重启
 * Bootloader: 检测 BKP 标志 → HTTP 下载固件 → 写 Flash → 跳转
 * ============================================================ */

/* 从 App 端触发 OTA (显示升级界面, 设标志并重启) */
void ota_trigger_update(void);

/* OTA 轮询: 在超级循环主循环中调用, 检查是否收到 OTA 指令 */
void ota_poll(void);

/* 处理收到的 OTA MQTT 指令
 * topic: factory/ota/dev01
 * payload: {"cmd":"start"} */
void ota_handle_mqtt_msg(const char *topic, const char *payload, int len);

#endif /* OTA_UPDATE_H */
