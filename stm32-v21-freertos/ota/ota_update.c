#include "ota_update.h"
#include "config.h"
#include "bsp.h"
#include "display.h"
#include "FreeRTOS.h"
#include "task.h"
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
 * OTA App 端实现 (v2.1, 无 LVGL)
 * 收到 MQTT OTA 指令后, 用 display 模块显示提示, 设 BKP 标志, 重启
 * ============================================================ */

static volatile int s_ota_requested = 0;

void ota_handle_mqtt_msg(const char *topic, const char *payload, int len)
{
    if (strstr(topic, "ota") == NULL) return;

    if (memmem(payload, len, "start", 5) ||
        strstr(payload, "start")) {
        s_ota_requested = 1;
        DBG("[OTA] Update requested via MQTT\r\n");
    }
}

void ota_trigger_update(void)
{
    DBG("[OTA] Triggering OTA update, rebooting...\r\n");

    /* 用 display 模块显示 OTA 界面 */
    g_disp_data.ota_active = 1;
    g_disp_data.ota_percent = 0;
    disp_show_ota(0, "Rebooting for OTA...");

    /* 等待显示渲染 + 串口输出 */
    vTaskDelay(1000);

    /* 设置 BKP OTA 标志 */
    bkp_write(OTA_FLAG_BKP_DR, OTA_FLAG_MAGIC);

    /* 系统复位 */
    system_reset();

    while (1);
}

void ota_task(void *params)
{
    (void)params;

    while (1) {
        if (s_ota_requested) {
            ota_trigger_update();
        }
        vTaskDelay(500);
    }
}
