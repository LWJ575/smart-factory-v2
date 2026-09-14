#include "stm32f10x.h"
#include "config.h"
#include "bsp.h"
#include "lcd.h"
#include "display.h"
#include "esp8266.h"
#include "mqtt_packet.h"
#include "mqtt_client.h"
#include "json_helper.h"
#include "sensors.h"
#include "actuators.h"
#include "ota_update.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include <string.h>
#include <stdio.h>

/* ============================================================
 * Smart Factory v2.1 — Main Application
 * FreeRTOS + 正点原子 LCD (无 LVGL) + MQTT + OTA
 * ============================================================ */

/* ==================== 全局状态 ==================== */

static volatile int s_mqtt_connected = 0;

typedef struct {
    uint8_t cmd_type;   /* 0=LED1, 1=LED2, 2=motor, 3=buzzer */
    uint8_t value;
} control_cmd_t;

static QueueHandle_t s_ctrl_queue = NULL;

static volatile int16_t  s_temp_x10 = 0;
static volatile uint16_t s_light_raw = 0;
static volatile uint8_t  s_motor_speed = 0;

/* ==================== 中断处理 ==================== */

void USART2_IRQHandler(void)
{
    uint8_t byte;
    if (USART_GetITStatus(ESP8266_UART, USART_IT_RXNE) != RESET) {
        byte = USART_ReceiveData(ESP8266_UART);
        esp8266_handle_rx_byte(byte);
        USART_ClearITPendingBit(ESP8266_UART, USART_IT_RXNE);
    }
}

/* ==================== FreeRTOS Tick Hook ==================== */

void vApplicationTickHook(void)
{
    bsp_tick_hook();
}

/* ==================== 栈溢出/堆失败钩子 ==================== */

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void)xTask;
    DBG("[FATAL] Stack overflow in task: %s\r\n", pcTaskName);
    for (volatile int i = 0; i < 720000; i++);
    NVIC_SystemReset();
}

void vApplicationMallocFailedHook(void)
{
    DBG("[FATAL] malloc failed\r\n");
    for (volatile int i = 0; i < 720000; i++);
    NVIC_SystemReset();
}

/* ==================== 外设初始化 ==================== */

static void hw_init_all(void)
{
    /* OTA: App 链接在 0x08004000 (bootloader 占前 16KB), 必须最先重定位向量表 */
    SCB->VTOR = 0x08004000;

    sys_clock_init();
    dwt_init();
    debug_init();
    bkp_init();

    DBG("[SYS] Smart Factory v2.1 booting...\r\n");
    DBG("[SYS] FW: %s, Device: %s\r\n", FW_VERSION, DEVICE_ID);

    actuators_init();
    sensors_init();

    esp8266_init();
    DBG("[SYS] ESP8266 UART initialized\r\n");

    DBG("[SYS] Initializing LCD...\r\n");
    disp_init();
    DBG("[SYS] LCD initialized, ID=0x%04X (%s)\r\n",
        lcd_read_id(), (lcd_read_id() == 0x9341) ? "ILI9341 OK" : "CHECK WIRING");

    DBG("[SYS] Hardware init complete\r\n");
}

/* ==================== MQTT 回调 ==================== */

static void mqtt_msg_cb(const char *topic, const char *payload, int len)
{
    if (strstr(topic, "control")) {
        static char cmd_buf[128];
        char cmd_type[16] = {0};
        char action[16] = {0};
        int value = 0;
        int copy_len;
        control_cmd_t ctrl = {0};

        copy_len = (len < (int)sizeof(cmd_buf) - 1) ? len : (int)sizeof(cmd_buf) - 1;
        memcpy(cmd_buf, payload, copy_len);
        cmd_buf[copy_len] = '\0';

        json_parse_control(cmd_buf, len, cmd_type, sizeof(cmd_type),
                          action, sizeof(action), &value);

        if (strcmp(cmd_type, "led1") == 0) {
            ctrl.cmd_type = 0;
            ctrl.value = (strcmp(action, "on") == 0) ? 1 : 0;
        } else if (strcmp(cmd_type, "led2") == 0) {
            ctrl.cmd_type = 1;
            ctrl.value = (strcmp(action, "on") == 0) ? 1 : 0;
        } else if (strcmp(cmd_type, "motor") == 0) {
            ctrl.cmd_type = 2;
            ctrl.value = (uint8_t)value;
        } else if (strcmp(cmd_type, "buzzer") == 0) {
            ctrl.cmd_type = 3;
            ctrl.value = (strcmp(action, "on") == 0) ? 1 : 0;
        }

        xQueueSend(s_ctrl_queue, &ctrl, 0);
        DBG("[MQTT] Control: %s %s val=%d\r\n", cmd_type, action, value);
    }

    if (strstr(topic, "ota")) {
        ota_handle_mqtt_msg(topic, payload, len);
    }
}

/* ==================== FreeRTOS 任务 ==================== */

/* ---- Sensor Task ---- */
static void sensor_task(void *params)
{
    static char json[JSON_BUF_SIZE];
    static char topic[48];
    uint32_t last_report = 0;
    int16_t temp_c;
    int json_len;

    (void)params;

    while (1) {
        s_temp_x10 = ds18b20_read_temp_x10();
        s_light_raw = light_read_raw();

        if (s_temp_x10 > -9000) {
            DBG("[DS18B20] temp = %d.%d C\r\n",
                s_temp_x10 / 10, s_temp_x10 % 10);
        }
        /* 未插传感器时不打印, 避免刷屏 */

        /* 更新显示数据 */
        g_disp_data.temp_x10 = s_temp_x10;
        g_disp_data.light_raw = s_light_raw;
        g_disp_data.motor_speed = s_motor_speed;

        /* 温度报警 */
        temp_c = s_temp_x10 / 10;
        if (temp_c >= TEMP_ALARM_HIGH) {
            g_disp_data.alarm_active = 1;
            buzzer_on_nonblock(200);
        } else {
            g_disp_data.alarm_active = 0;
        }

        buzzer_update();

        /* 定时上报遥测 */
        if (s_mqtt_connected && (sys_tick() - last_report >= TELEMETRY_INTERVAL * 1000)) {
            last_report = sys_tick();

            json_len = json_build_telemetry(json, sizeof(json),
                                            DEVICE_ID, s_temp_x10,
                                            s_light_raw, s_motor_speed,
                                            g_disp_data.led1_on,
                                            g_disp_data.led2_on);
            if (json_len > 0) {
                snprintf(topic, sizeof(topic), "factory/telemetry/%s", DEVICE_ID);
                mqtt_publish(topic, json);
                DBG("[MQTT] Telemetry published\r\n");
            }
        }

        vTaskDelay(200);
    }
}

/* ---- MQTT Task ---- */
static void mqtt_task(void *params)
{
    static char will_topic[48];
    static char ctrl_topic[48];
    static char ota_topic[48];
    static char status_topic[48];
    uint32_t last_ping = 0;
    const char *will_msg;
    const char *online_msg;

    (void)params;

    vTaskDelay(500);

    while (1) {
        g_disp_data.mqtt_online = 0;
        if (!esp8266_connect_wifi(WIFI_SSID, WIFI_PASSWORD)) {
            DBG("[MQTT] WiFi failed, retry in 5s\r\n");
            vTaskDelay(5000);
            continue;
        }

        if (!esp8266_connect_tcp(BROKER_IP, BROKER_PORT)) {
            DBG("[MQTT] TCP failed, retry in 5s\r\n");
            vTaskDelay(5000);
            continue;
        }

        snprintf(will_topic, sizeof(will_topic), "factory/status/%s", DEVICE_ID);
        will_msg = "{\"status\":\"offline\"}";

        if (!mqtt_connect(DEVICE_ID, will_topic, will_msg)) {
            DBG("[MQTT] Connect failed, retry in 5s\r\n");
            esp8266_disconnect_tcp();
            vTaskDelay(5000);
            continue;
        }

        snprintf(ctrl_topic, sizeof(ctrl_topic), "factory/control/%s", DEVICE_ID);
        mqtt_subscribe(ctrl_topic);

        snprintf(ota_topic, sizeof(ota_topic), "factory/ota/%s", DEVICE_ID);
        mqtt_subscribe(ota_topic);

        snprintf(status_topic, sizeof(status_topic), "factory/status/%s", DEVICE_ID);
        online_msg = "{\"status\":\"online\"}";
        mqtt_publish(status_topic, online_msg);

        s_mqtt_connected = 1;
        g_disp_data.mqtt_online = 1;
        DBG("[MQTT] Connected to broker %s:%d\r\n", BROKER_IP, BROKER_PORT);

        while (s_mqtt_connected) {
            if (!mqtt_loop(mqtt_msg_cb)) {
                DBG("[MQTT] Loop returned 0, reconnecting\r\n");
                s_mqtt_connected = 0;
                g_disp_data.mqtt_online = 0;
                break;
            }

            if (sys_tick() - last_ping >= MQTT_KEEPALIVE * 1000) {
                last_ping = sys_tick();
                if (!mqtt_ping()) {
                    DBG("[MQTT] Ping failed, reconnecting\r\n");
                    s_mqtt_connected = 0;
                    g_disp_data.mqtt_online = 0;
                    break;
                }
            }

            vTaskDelay(50);
        }

        esp8266_disconnect_tcp();
        vTaskDelay(3000);
    }
}

/* ---- Display Task ---- */
static void display_task(void *params)
{
    uint32_t last_update = 0;

    (void)params;

    while (1) {
        if (g_disp_data.ota_active) {
            /* OTA 模式下显示 OTA 进度 */
            disp_show_ota(g_disp_data.ota_percent, "Updating...");
        } else {
            /* 正常模式: 每 200ms 更新仪表盘 */
            if (sys_tick() - last_update >= 200) {
                last_update = sys_tick();
                disp_update_dashboard();
            }
        }

        vTaskDelay(50);
    }
}

/* ---- Control Task ---- */
static void control_task(void *params)
{
    control_cmd_t cmd;

    (void)params;

    while (1) {
        if (xQueueReceive(s_ctrl_queue, &cmd, portMAX_DELAY) == pdTRUE) {
            switch (cmd.cmd_type) {
                case 0:
                    if (cmd.value) { led1_on(); g_disp_data.led1_on = 1; }
                    else            { led1_off(); g_disp_data.led1_on = 0; }
                    DBG("[CTRL] LED1 %s\r\n", cmd.value ? "ON" : "OFF");
                    break;
                case 1:
                    if (cmd.value) { led2_on(); g_disp_data.led2_on = 1; }
                    else            { led2_off(); g_disp_data.led2_on = 0; }
                    DBG("[CTRL] LED2 %s\r\n", cmd.value ? "ON" : "OFF");
                    break;
                case 2:
                    s_motor_speed = cmd.value;
                    motor_set_speed(cmd.value);
                    DBG("[CTRL] Motor %d%%\r\n", cmd.value);
                    break;
                case 3:
                    if (cmd.value) { buzzer_on_nonblock(2000); g_disp_data.buzzer_on = 1; }
                    else            { buzzer_off(); g_disp_data.buzzer_on = 0; }
                    DBG("[CTRL] Buzzer %s\r\n", cmd.value ? "ON" : "OFF");
                    break;
            }
        }
    }
}

/* ==================== Main ==================== */

int main(void)
{
    hw_init_all();

    s_ctrl_queue = xQueueCreate(16, sizeof(control_cmd_t));

    xTaskCreate(sensor_task,   "sensor",  STACK_SENSOR,  NULL, PRIO_SENSOR,  NULL);
    xTaskCreate(mqtt_task,     "mqtt",    STACK_MQTT,     NULL, PRIO_MQTT,    NULL);
    xTaskCreate(display_task,  "disp",    STACK_DISPLAY,  NULL, PRIO_DISPLAY, NULL);
    xTaskCreate(control_task,  "ctrl",    STACK_CONTROL,  NULL, PRIO_CONTROL, NULL);
    xTaskCreate(ota_task,      "ota",     STACK_OTA,      NULL, PRIO_OTA,     NULL);

    DBG("[SYS] FreeRTOS tasks created, starting scheduler\r\n");

    vTaskStartScheduler();

    DBG("[FATAL] Scheduler failed to start\r\n");
    while (1);
}
