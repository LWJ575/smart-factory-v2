#include "stm32f10x.h"
#include "config.h"
#include "bsp.h"
#include "lcd.h"
#include "lv_port_disp.h"
#include "ui_main.h"
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
#include "semphr.h"
#include <string.h>
#include <stdio.h>

/* ============================================================
 * Smart Factory v2.0 — Main Application
 * FreeRTOS + LVGL + MQTT + OTA
 * ============================================================ */

/* ==================== 全局状态 ==================== */

/* MQTT 连接状态 (v1 API 用全局函数, 无实例指针) */
static volatile int s_mqtt_connected = 0;

/* 控制指令队列 (Control Task 消费) */
typedef struct {
    uint8_t cmd_type;   /* 0=LED1, 1=LED2, 2=motor, 3=buzzer */
    uint8_t value;       /* 0=off, 1=on, 或 motor 速度 0-100 */
} control_cmd_t;

static QueueHandle_t s_ctrl_queue = NULL;

/* 传感器数据 (多个任务共享) */
static volatile int16_t  s_temp_x10 = 0;
static volatile uint16_t s_light_raw = 0;
static volatile uint8_t  s_motor_speed = 0;

/* ==================== 中断处理 ==================== */

/* USART2 接收中断 (ESP8266) */
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
    /* 更新 BSP 毫秒计数 */
    bsp_tick_hook();
    /* 更新 LVGL tick */
    lv_tick_inc_port(1);
}

/* ==================== FreeRTOS 栈溢出/堆失败钩子 ==================== */

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void)xTask;
    /* 栈已溢出, 不能调 vTaskDelay (可能二次崩溃), 用忙等延时 */
    DBG("[FATAL] Stack overflow in task: %s\r\n", pcTaskName);
    for (volatile int i = 0; i < 720000; i++);  /* ~100ms @72MHz */
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
    /* 时钟确认 */
    sys_clock_init();

    /* DWT 微秒延时 */
    dwt_init();

    /* 调试串口 */
    debug_init();

    /* BKP (OTA 标志) */
    bkp_init();

    DBG("[SYS] Smart Factory v2.0 booting...\r\n");
    DBG("[SYS] FW: %s, Device: %s\r\n", FW_VERSION, DEVICE_ID);

    /* LED */
    actuators_init();

    /* 蜂鸣器 (同在 actuators) */

    /* DS18B20 */
    sensors_init();

    /* ESP8266 */
    esp8266_init();
    DBG("[SYS] ESP8266 UART initialized\r\n");

    /* ADC (光敏) — 在 sensors_init 中已初始化 */

    /* 电机 PWM — 在 actuators_init 中已初始化 */

    /* LCD + LVGL */
    DBG("[SYS] Initializing LCD...\r\n");
    lv_port_disp_init();
    DBG("[SYS] LCD + LVGL initialized\r\n");

    /* UI */
    ui_init();
    DBG("[SYS] UI initialized\r\n");

    DBG("[SYS] Hardware init complete\r\n");
}

/* ==================== MQTT 回调 ==================== */

/* MQTT 消息回调 (在 MQTT Task 上下文中调用) */
static void mqtt_msg_cb(const char *topic, const char *payload, int len)
{
    /* 1. 检查是否是控制指令 */
    if (strstr(topic, "control")) {
        /* 所有变量声明放在块开头 (C90 兼容) */
        static char cmd_buf[128];  /* static: 避免占 128B 任务栈 */
        char cmd_type[16] = {0};
        char action[16] = {0};
        int value = 0;
        int copy_len;
        control_cmd_t ctrl = {0};

        /* 解析 JSON: {"cmd":"led1","action":"on"} */
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

    /* 2. 检查是否是 OTA 指令 */
    if (strstr(topic, "ota")) {
        ota_handle_mqtt_msg(topic, payload, len);
    }
}

/* ==================== FreeRTOS 任务 ==================== */

/* ---- Sensor Task ---- */
static void sensor_task(void *params)
{
    /* 大缓冲区用 static 避免占用任务栈 (json 256B + topic 48B = 304B) */
    static char json[JSON_BUF_SIZE];
    static char topic[48];
    uint32_t last_report = 0;
    int16_t temp_c;
    int json_len;

    (void)params;

    while (1) {
        /* 读取传感器 */
        s_temp_x10 = ds18b20_read_temp_x10();
        s_light_raw = light_read_raw();

        /* 温度日志 (在 sensor_task 上下文打印, 不在 ds18b20_read_temp 深层调用中) */
        if (s_temp_x10 > -9000) {
            DBG("[DS18B20] temp = %d.%d C\r\n",
                s_temp_x10 / 10, s_temp_x10 % 10);
        } else {
            DBG("[DS18B20] no device\r\n");
        }

        /* 更新 UI 数据 */
        g_ui_data.temp_x10 = s_temp_x10;
        g_ui_data.light_raw = s_light_raw;
        g_ui_data.motor_speed = s_motor_speed;

        /* 温度报警检查 */
        temp_c = s_temp_x10 / 10;
        if (temp_c >= TEMP_ALARM_HIGH) {
            g_ui_data.alarm_active = 1;
            buzzer_on_nonblock(200);  /* 短鸣 200ms */
        } else {
            g_ui_data.alarm_active = 0;
        }

        /* 更新蜂鸣器定时关闭 (buzzer_on_nonblock 依赖此调用) */
        buzzer_update();

        /* 定时上报遥测 */
        if (s_mqtt_connected && (sys_tick() - last_report >= TELEMETRY_INTERVAL * 1000)) {
            last_report = sys_tick();

            json_len = json_build_telemetry(json, sizeof(json),
                                            DEVICE_ID, s_temp_x10,
                                            s_light_raw, s_motor_speed,
                                            g_ui_data.led1_on,
                                            g_ui_data.led2_on);
            if (json_len > 0) {
                snprintf(topic, sizeof(topic), "factory/telemetry/%s", DEVICE_ID);
                mqtt_publish(topic, json);
                DBG("[MQTT] Telemetry published\r\n");
            }
        }

        vTaskDelay(200);  /* 200ms 采集间隔 */
    }
}

/* ---- MQTT Task ---- */
static void mqtt_task(void *params)
{
    /* 大缓冲区用 static 减少栈占用 (4×48=192B) */
    static char will_topic[48];
    static char ctrl_topic[48];
    static char ota_topic[48];
    static char status_topic[48];
    uint32_t last_ping = 0;
    const char *will_msg;
    const char *online_msg;

    (void)params;

    /* 等待 LVGL 初始化完成 */
    vTaskDelay(500);

    while (1) {
        /* 1. 连接 WiFi */
        g_ui_data.mqtt_online = 0;
        if (!esp8266_connect_wifi(WIFI_SSID, WIFI_PASSWORD)) {
            DBG("[MQTT] WiFi failed, retry in 5s\r\n");
            vTaskDelay(5000);
            continue;
        }

        /* 2. 连接 TCP */
        if (!esp8266_connect_tcp(BROKER_IP, BROKER_PORT)) {
            DBG("[MQTT] TCP failed, retry in 5s\r\n");
            vTaskDelay(5000);
            continue;
        }

        /* 3. MQTT 连接 (带 LWT) */
        snprintf(will_topic, sizeof(will_topic), "factory/status/%s", DEVICE_ID);
        will_msg = "{\"status\":\"offline\"}";

        if (!mqtt_connect(DEVICE_ID, will_topic, will_msg)) {
            DBG("[MQTT] Connect failed, retry in 5s\r\n");
            esp8266_disconnect_tcp();
            vTaskDelay(5000);
            continue;
        }

        /* 4. 订阅控制 topic 和 OTA topic */
        snprintf(ctrl_topic, sizeof(ctrl_topic), "factory/control/%s", DEVICE_ID);
        mqtt_subscribe(ctrl_topic);

        snprintf(ota_topic, sizeof(ota_topic), "factory/ota/%s", DEVICE_ID);
        mqtt_subscribe(ota_topic);

        /* 发布在线状态 */
        snprintf(status_topic, sizeof(status_topic), "factory/status/%s", DEVICE_ID);
        online_msg = "{\"status\":\"online\"}";
        mqtt_publish(status_topic, online_msg);

        s_mqtt_connected = 1;
        g_ui_data.mqtt_online = 1;
        DBG("[MQTT] Connected to broker %s:%d\r\n", BROKER_IP, BROKER_PORT);

        /* 5. 主循环: 处理 MQTT + 心跳 */
        while (s_mqtt_connected) {
            /* 处理 MQTT 消息 */
            if (!mqtt_loop(mqtt_msg_cb)) {
                DBG("[MQTT] Loop returned 0, reconnecting\r\n");
                s_mqtt_connected = 0;
                g_ui_data.mqtt_online = 0;
                break;
            }

            /* 心跳 */
            if (sys_tick() - last_ping >= MQTT_KEEPALIVE * 1000) {
                last_ping = sys_tick();
                if (!mqtt_ping()) {
                    DBG("[MQTT] Ping failed, reconnecting\r\n");
                    s_mqtt_connected = 0;
                    g_ui_data.mqtt_online = 0;
                    break;
                }
            }

            vTaskDelay(50);  /* 50ms 处理间隔 */
        }

        /* 断线重连 */
        esp8266_disconnect_tcp();
        vTaskDelay(3000);
    }
}

/* ---- LVGL Task ---- */
static void lvgl_task(void *params)
{
    uint32_t last_ui_update = 0;

    (void)params;

    while (1) {
        /* 调用 LVGL 定时器处理 (渲染 + 输入处理) */
        lv_timer_handler();

        /* 每 200ms 更新一次仪表盘数值 */
        if (sys_tick() - last_ui_update >= 200) {
            last_ui_update = sys_tick();
            if (ui_get_current_screen() == 0) {
                ui_update_dashboard();
            }
        }

        vTaskDelay(5);  /* ~5ms 间隔, 约 200fps 上限, 实际受渲染限制约 30fps */
    }
}

/* ---- Control Task ---- */
static void control_task(void *params)
{
    control_cmd_t cmd;
    control_cmd_t c;
    uint8_t v;

    (void)params;

    while (1) {
        /* 从队列获取控制指令 (阻塞等待) */
        if (xQueueReceive(s_ctrl_queue, &cmd, portMAX_DELAY) == pdTRUE) {
            switch (cmd.cmd_type) {
                case 0:  /* LED1 */
                    if (cmd.value) { led1_on(); g_ui_data.led1_on = 1; }
                    else            { led1_off(); g_ui_data.led1_on = 0; }
                    DBG("[CTRL] LED1 %s\r\n", cmd.value ? "ON" : "OFF");
                    break;
                case 1:  /* LED2 */
                    if (cmd.value) { led2_on(); g_ui_data.led2_on = 1; }
                    else            { led2_off(); g_ui_data.led2_on = 0; }
                    DBG("[CTRL] LED2 %s\r\n", cmd.value ? "ON" : "OFF");
                    break;
                case 2:  /* Motor */
                    s_motor_speed = cmd.value;
                    motor_set_speed(cmd.value);
                    DBG("[CTRL] Motor %d%%\r\n", cmd.value);
                    break;
                case 3:  /* Buzzer */
                    if (cmd.value) { buzzer_on_nonblock(2000); g_ui_data.buzzer_on = 1; }
                    else            { buzzer_off(); g_ui_data.buzzer_on = 0; }
                    DBG("[CTRL] Buzzer %s\r\n", cmd.value ? "ON" : "OFF");
                    break;
            }
        }

        /* 检查本地按钮请求 (UI 触发) */
        if (ui_get_led1_req()) {
            c.cmd_type = 0;
            c.value = !g_ui_data.led1_on;
            xQueueSend(s_ctrl_queue, &c, 0);
        }
        if (ui_get_led2_req()) {
            c.cmd_type = 1;
            c.value = !g_ui_data.led2_on;
            xQueueSend(s_ctrl_queue, &c, 0);
        }
        if (ui_get_motor_inc_req()) {
            v = (s_motor_speed + 10 > 100) ? 100 : s_motor_speed + 10;
            c.cmd_type = 2;
            c.value = v;
            xQueueSend(s_ctrl_queue, &c, 0);
        }
        if (ui_get_motor_dec_req()) {
            v = (s_motor_speed < 10) ? 0 : s_motor_speed - 10;
            c.cmd_type = 2;
            c.value = v;
            xQueueSend(s_ctrl_queue, &c, 0);
        }
        if (ui_get_buzzer_req()) {
            c.cmd_type = 3;
            c.value = !g_ui_data.buzzer_on;
            xQueueSend(s_ctrl_queue, &c, 0);
        }
    }
}

/* ---- OTA Task ---- */
/* ota_task 已在 ota_update.c 中实现 */

/* ==================== Main ==================== */

int main(void)
{
    /* 1. 初始化所有硬件 */
    hw_init_all();

    /* 2. 创建控制指令队列 */
    s_ctrl_queue = xQueueCreate(16, sizeof(control_cmd_t));

    /* 3. 创建 FreeRTOS 任务 */
    xTaskCreate(sensor_task,   "sensor",  STACK_SENSOR,  NULL, PRIO_SENSOR,  NULL);
    xTaskCreate(mqtt_task,     "mqtt",    STACK_MQTT,     NULL, PRIO_MQTT,    NULL);
    xTaskCreate(lvgl_task,     "lvgl",    STACK_LVGL,     NULL, PRIO_LVGL,    NULL);
    xTaskCreate(control_task,  "ctrl",    STACK_CONTROL,  NULL, PRIO_CONTROL, NULL);
    xTaskCreate(ota_task,      "ota",     STACK_OTA,      NULL, PRIO_OTA,     NULL);

    DBG("[SYS] FreeRTOS tasks created, starting scheduler\r\n");

    /* 4. 启动调度器 */
    vTaskStartScheduler();

    /* 不应该到达这里 */
    DBG("[FATAL] Scheduler failed to start\r\n");
    while (1);
}
