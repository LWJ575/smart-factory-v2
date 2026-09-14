#include "ota_update.h"
#include "config.h"
#include "bsp.h"
#include "ui_main.h"
#include <string.h>

/* ---- memmem: Keil ARMCLIB 无此函数, 手动实现 ---- */
static void *memmem(const void *haystack, size_t hlen, const void *needle, size_t nlen)
{
    const uint8_t *p = (const uint8_t *)haystack;
    const uint8_t *n = (const uint8_t *)needle;
    size_t i, j;

    if (nlen == 0) return (void *)haystack;
    if (nlen > hlen) return NULL;

    for (i = 0; i <= hlen - nlen; i++) {
        for (j = 0; j < nlen; j++) {
            if (p[i + j] != n[j]) break;
        }
        if (j == nlen) return (void *)(p + i);
    }
    return NULL;
}

/* ============================================================
 * OTA App 端实现 (v2.2, 无 FreeRTOS)
 * 简单设计: 收到 MQTT OTA 指令后, 显示提示, 设 BKP 标志, 重启
 * 实际下载和 Flash 写入由 Bootloader 完成
 * ============================================================ */

static volatile int s_ota_requested = 0;

void ota_handle_mqtt_msg(const char *topic, const char *payload, int len)
{
    /* 检查是否是 OTA 指令 topic */
    if (strstr(topic, "ota") == NULL) return;

    /* 简单 JSON 解析: 找 "start" 关键字 */
    if (memmem(payload, len, "start", 5) ||
        strstr(payload, "start")) {
        s_ota_requested = 1;
        DBG("[OTA] Update requested via MQTT\r\n");
    }
}

void ota_trigger_update(void)
{
    DBG("[OTA] Triggering OTA update, rebooting...\r\n");

    /* 显示 OTA 界面 */
    ui_show_ota();
    ui_update_ota_progress(0, "Rebooting for OTA...");

    /* 等 LVGL 渲染完成
     * (v2.2: sys_delay_ms 内部会泵 LVGL, 1s 足够渲染出 OTA 界面) */
    sys_delay_ms(1000);

    /* 设置 BKP OTA 标志 */
    bkp_write(OTA_FLAG_BKP_DR, OTA_FLAG_MAGIC);

    /* 系统复位 */
    system_reset();

    /* 不应该到达这里 */
    while (1);
}

/* OTA 轮询: 在主循环中调用 (替代 v2.0 的 ota_task) */
void ota_poll(void)
{
    if (s_ota_requested) {
        ota_trigger_update();
        /* 不会返回 (会复位) */
    }
}
