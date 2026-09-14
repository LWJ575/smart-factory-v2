#include "stm32f10x.h"
#include "config.h"
#include "bsp.h"
#include "lcd.h"
#include "lv_port_disp.h"
#include "ui_main.h"
#include "lvgl.h"
#include "esp8266.h"
#include "mqtt_packet.h"
#include "mqtt_client.h"
#include "json_helper.h"
#include "sensors.h"
#include "actuators.h"
#include "ota_update.h"
#include <string.h>
#include <stdio.h>

/* ============================================================
 * Smart Factory v2.2 — Main Application (超级循环版)
 * 纯 LVGL (无 FreeRTOS) + MQTT + OTA
 *
 * 架构说明:
 *   - v2.0 的 5 个 FreeRTOS 任务 (sensor/mqtt/lvgl/control/ota)
 *     在这里变成 5 个 *_poll() 函数, 由 while(1) 主循环轮询
 *   - 任务间通信 (队列/互斥锁) 全部取消:
 *       * 控制指令: MQTT 回调直接执行 (回调跑在 main 上下文)
 *       * ESP8266: 单线程顺序访问, ISR 只写 ring buffer
 *   - 阻塞等待不再冻结 UI: sys_delay_ms() 内部调用
 *     bsp_idle_hook() → lv_timer_handler(), 所以 DS18B20 的
 *     750ms 转换等待、WiFi 20s 连接等待期间屏幕照常刷新
 *   - 所有代码跑在 MSP 上: 启动文件 Stack_Size 必须 ≥ 0x1000!
 * ============================================================ */

/* ==================== 全局状态 ==================== */

/* MQTT 连接状态 */
static volatile int s_mqtt_connected = 0;

/* 传感器数据 */
static volatile int16_t  s_temp_x10 = 0;
static volatile uint16_t s_light_raw = 0;
static volatile uint8_t  s_motor_speed = 0;

/* 时间到达辅助 (uint32_t 回绕安全) */
static int time_reached(uint32_t deadline)
{
    return (int32_t)(sys_tick() - deadline) >= 0;
}

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

/* ==================== 空闲钩子: 泵 LVGL ==================== */

/* lv_timer_handler 不可重入:
 * 若某次 LVGL 回调内部又调用了 sys_delay_ms(), 会形成递归泵。
 * 用标志位防重入 (递归调用直接返回, 等外层继续泵)。 */
static volatile uint8_t s_lv_pumping = 0;

void bsp_idle_hook(void)
{
    if (s_lv_pumping) return;
    s_lv_pumping = 1;
    lv_timer_handler();
    s_lv_pumping = 0;
}

/* ==================== 外设初始化 ==================== */

static void hw_init_all(void)
{
    /* OTA: App 链接在 0x08004000 (bootloader 占前 16KB), 必须最先重定位向量表 */
    SCB->VTOR = 0x08004000;

    /* 时钟确认 (72MHz) */
    sys_clock_init();

    /* SysTick 1ms —— 必须最先初始化, sys_delay_ms 依赖它计时 */
    bsp_systick_init();

    /* DWT 微秒延时 (DS18B20 1-Wire 时序依赖) */
    dwt_init();

    /* 调试串口 */
    debug_init();

    /* BKP (OTA 标志) */
    bkp_init();

    DBG("[SYS] Smart Factory v2.2 booting...\r\n");
    DBG("[SYS] FW: %s, Device: %s\r\n", FW_VERSION, DEVICE_ID);

    /* LED / 蜂鸣器 / 电机 PWM */
    actuators_init();

    /* DS18B20 / 光敏 ADC */
    sensors_init();

    /* ESP8266 */
    esp8266_init();
    DBG("[SYS] ESP8266 UART initialized\r\n");

    /* LCD + LVGL
     * LV_TICK_CUSTOM=1: LVGL 时间源直读 sys_tick(), 无需手动 lv_tick_inc */
    DBG("[SYS] Initializing LCD + LVGL...\r\n");
    lv_port_disp_init();
    {
        uint32_t id = lcd_read_id();
        if (id == 0x9341) {
            DBG("[SYS] LCD ID=0x%04X (ILI9341 OK)\r\n", id);
        } else {
            DBG("[SYS] LCD ID=0x%04X (CHECK WIRING!)\r\n", id);
        }
    }
    /* LCD 写入自检: 红屏 500ms (sys_delay_us 忙等, 不泵 LVGL)
     * 红屏出现   → FSMC 写入 OK, 后续白屏 = LVGL 渲染问题
     * 从未出现   → LCD 写入路径本身不通 */
    lcd_fill(0xF800);
    sys_delay_us(500000);
    DBG("[SYS] LCD write self-test done (red flash)\r\n");
    DBG("[SYS] LCD + LVGL initialized\r\n");

    /* UI */
    ui_init();
    DBG("[SYS] UI initialized\r\n");

    DBG("[SYS] Hardware init complete, entering super loop\r\n");
}

/* ==================== 控制执行 (原 Control Task 的队列消费者) ==================== */

/* v2.2 无队列: MQTT 回调和 UI 按钮都直接调这个函数 (都在 main 上下文) */
static void control_exec(uint8_t cmd_type, uint8_t value)
{
    switch (cmd_type) {
        case 0:  /* LED1 */
            if (value) { led1_on();  g_ui_data.led1_on = 1; }
            else       { led1_off(); g_ui_data.led1_on = 0; }
            DBG("[CTRL] LED1 %s\r\n", value ? "ON" : "OFF");
            break;
        case 1:  /* LED2 */
            if (value) { led2_on();  g_ui_data.led2_on = 1; }
            else       { led2_off(); g_ui_data.led2_on = 0; }
            DBG("[CTRL] LED2 %s\r\n", value ? "ON" : "OFF");
            break;
        case 2:  /* Motor */
            s_motor_speed = value;
            motor_set_speed(value);
            DBG("[CTRL] Motor %d%%\r\n", value);
            break;
        case 3:  /* Buzzer */
            if (value) { buzzer_on_nonblock(2000); g_ui_data.buzzer_on = 1; }
            else       { buzzer_off();             g_ui_data.buzzer_on = 0; }
            DBG("[CTRL] Buzzer %s\r\n", value ? "ON" : "OFF");
            break;
    }
}

/* ==================== MQTT 回调 (在 main 上下文执行) ==================== */

static void mqtt_msg_cb(const char *topic, const char *payload, int len)
{
    /* 1. 控制指令: 解析后直接执行 (v2.2 无队列) */
    if (strstr(topic, "control")) {
        static char cmd_buf[128];  /* static: 不占栈 */
        char cmd_type[16] = {0};
        char action[16] = {0};
        int value = 0;
        int copy_len;

        /* 解析 JSON: {"cmd":"led1","action":"on"} 或 {"cmd":"motor","value":50} */
        copy_len = (len < (int)sizeof(cmd_buf) - 1) ? len : (int)sizeof(cmd_buf) - 1;
        memcpy(cmd_buf, payload, copy_len);
        cmd_buf[copy_len] = '\0';

        json_parse_control(cmd_buf, len, cmd_type, sizeof(cmd_type),
                          action, sizeof(action), &value);

        if (strcmp(cmd_type, "led1") == 0) {
            control_exec(0, (strcmp(action, "on") == 0) ? 1 : 0);
        } else if (strcmp(cmd_type, "led2") == 0) {
            control_exec(1, (strcmp(action, "on") == 0) ? 1 : 0);
        } else if (strcmp(cmd_type, "motor") == 0) {
            control_exec(2, (uint8_t)value);
        } else if (strcmp(cmd_type, "buzzer") == 0) {
            control_exec(3, (strcmp(action, "on") == 0) ? 1 : 0);
        }

        DBG("[MQTT] Control: %s %s val=%d\r\n", cmd_type, action, value);
    }

    /* 2. OTA 指令 */
    if (strstr(topic, "ota")) {
        ota_handle_mqtt_msg(topic, payload, len);
    }
}

/* ==================== MQTT 状态机 (原 MQTT Task) ==================== */

/* v2.0 的 mqtt_task 是阻塞式 while 循环; v2.2 改成状态机,
 * 每次 mqtt_poll() 前进一步, 让主循环能穿插传感器采集和 UI 刷新。
 * 注: WiFi/TCP 连接步骤内部仍是阻塞等待 (最长 ~20s),
 *     但 sys_delay_ms 会泵 LVGL, 屏幕不会冻结, 只是传感器暂停刷新 */
typedef enum {
    MQ_WAIT_BOOT = 0,   /* 上电延迟, 让 UI 先显示出来 */
    MQ_WIFI,            /* 连接 WiFi */
    MQ_CONNECT,         /* MQTT CONNECT + 订阅 + 上线 */
    MQ_RUN,             /* 正常运行: 收消息 + 心跳 */
    MQ_RETRY_WAIT,      /* 断线后等待重试 */
} mq_state_t;

static void mqtt_poll(void)
{
    /* 大缓冲区用 static (原来在任务栈, 现在挪到 BSS) */
    static char will_topic[48];
    static char ctrl_topic[48];
    static char ota_topic[48];
    static char status_topic[48];
    static mq_state_t st = MQ_WAIT_BOOT;
    static uint32_t s_boot_at = 0;
    static uint32_t s_wait_until = 0;
    static uint32_t s_last_ping = 0;
    static int s_inited = 0;

    if (!s_inited) {
        s_inited = 1;
        s_boot_at = sys_tick();
        mqtt_init();
    }

    switch (st) {
    case MQ_WAIT_BOOT:
        if (time_reached(s_boot_at + 500)) {
            st = MQ_WIFI;
        }
        break;

    case MQ_WIFI:
        g_ui_data.mqtt_online = 0;
        if (esp8266_connect_wifi(WIFI_SSID, WIFI_PASSWORD)) {
            st = MQ_CONNECT;
        } else {
            DBG("[MQTT] WiFi failed, retry in 5s\r\n");
            s_wait_until = sys_tick() + 5000;
            st = MQ_RETRY_WAIT;
        }
        break;

    case MQ_CONNECT:
        snprintf(will_topic, sizeof(will_topic), "factory/status/%s", DEVICE_ID);

        if (!mqtt_connect(DEVICE_ID, will_topic, "{\"status\":\"offline\"}")) {
            DBG("[MQTT] Connect failed, retry in 5s\r\n");
            esp8266_disconnect_tcp();
            s_wait_until = sys_tick() + 5000;
            st = MQ_RETRY_WAIT;
            break;
        }

        /* 订阅控制 topic 和 OTA topic */
        snprintf(ctrl_topic, sizeof(ctrl_topic), "factory/control/%s", DEVICE_ID);
        mqtt_subscribe(ctrl_topic);

        snprintf(ota_topic, sizeof(ota_topic), "factory/ota/%s", DEVICE_ID);
        mqtt_subscribe(ota_topic);

        /* 发布在线状态 */
        snprintf(status_topic, sizeof(status_topic), "factory/status/%s", DEVICE_ID);
        mqtt_publish(status_topic, "{\"status\":\"online\"}");

        s_mqtt_connected = 1;
        g_ui_data.mqtt_online = 1;
        s_last_ping = sys_tick();
        DBG("[MQTT] Connected to broker %s:%d\r\n", BROKER_IP, BROKER_PORT);
        st = MQ_RUN;
        break;

    case MQ_RUN:
        /* 处理 MQTT 消息 */
        if (!mqtt_loop(mqtt_msg_cb)) {
            DBG("[MQTT] Loop returned 0, reconnecting\r\n");
            s_mqtt_connected = 0;
            g_ui_data.mqtt_online = 0;
            s_wait_until = sys_tick() + 3000;
            st = MQ_RETRY_WAIT;
            break;
        }

        /* 心跳 */
        if (time_reached(s_last_ping + MQTT_KEEPALIVE * 1000)) {
            s_last_ping = sys_tick();
            if (!mqtt_ping()) {
                DBG("[MQTT] Ping failed, reconnecting\r\n");
                s_mqtt_connected = 0;
                g_ui_data.mqtt_online = 0;
                s_wait_until = sys_tick() + 3000;
                st = MQ_RETRY_WAIT;
            }
        }
        break;

    case MQ_RETRY_WAIT:
        esp8266_disconnect_tcp();
        if (time_reached(s_wait_until)) {
            st = MQ_WIFI;
        }
        break;
    }
}

/* ==================== 传感器采集 (原 Sensor Task) ==================== */

static void sensor_poll(void)
{
    static char json[JSON_BUF_SIZE];
    static char topic[48];
    static uint32_t s_last_run = 0;
    static uint32_t s_last_report = 0;
    int16_t temp_c;
    int json_len;

    /* 200ms 采集间隔 */
    if (!time_reached(s_last_run + SENSOR_PERIOD_MS)) return;
    s_last_run = sys_tick();

    /* 读取传感器 (DS18B20 内部有 750ms 等待, 期间泵 LVGL) */
    s_temp_x10 = ds18b20_read_temp_x10();
    s_light_raw = light_read_raw();

    if (s_temp_x10 > -9000) {
        DBG("[DS18B20] temp = %d.%d C\r\n",
            s_temp_x10 / 10, s_temp_x10 % 10);
    }
    /* 传感器未插入时不打印, 避免刷屏 */

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

    /* 定时上报遥测 */
    if (s_mqtt_connected && time_reached(s_last_report + TELEMETRY_INTERVAL * 1000)) {
        s_last_report = sys_tick();

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
}

/* ==================== UI 刷新 (原 LVGL Task) ==================== */

static void ui_poll(void)
{
    static uint32_t s_last_update = 0;

    /* LVGL 定时器处理 (渲染 + 输入) —— 主路径 */
    bsp_idle_hook();  /* 带防重入保护的 lv_timer_handler() */

    /* 每 200ms 更新一次仪表盘数值 */
    if (time_reached(s_last_update + UI_UPDATE_PERIOD_MS)) {
        s_last_update = sys_tick();
        if (ui_get_current_screen() == 0) {
            ui_update_dashboard();
        }
    }
}

/* ==================== 本地控制 (UI 按钮请求, 原 Control Task) ==================== */

static void control_poll(void)
{
    uint8_t v;

    if (ui_get_led1_req()) {
        control_exec(0, !g_ui_data.led1_on);
    }
    if (ui_get_led2_req()) {
        control_exec(1, !g_ui_data.led2_on);
    }
    if (ui_get_motor_inc_req()) {
        v = (s_motor_speed + 10 > 100) ? 100 : s_motor_speed + 10;
        control_exec(2, v);
    }
    if (ui_get_motor_dec_req()) {
        v = (s_motor_speed < 10) ? 0 : s_motor_speed - 10;
        control_exec(2, v);
    }
    if (ui_get_buzzer_req()) {
        control_exec(3, !g_ui_data.buzzer_on);
    }
}

/* ==================== Main (超级循环) ==================== */

int main(void)
{
    /* 1. 初始化所有硬件 */
    hw_init_all();

    /* 2. 超级循环 (替代 v2.0 的 5 个 FreeRTOS 任务) */
    while (1) {
        sensor_poll();    /* 传感器采集 + 遥测上报 (200ms 周期) */
        mqtt_poll();      /* MQTT 状态机 (连接/收消息/心跳/重连) */
        control_poll();   /* UI 按钮请求 → 本地控制 */
        ui_poll();        /* lv_timer_handler + 仪表盘刷新 */
        ota_poll();       /* OTA 请求检查 (ota_update.c) */

        /* 蜂鸣器定时关闭 (buzzer_on_nonblock 依赖) */
        buzzer_update();

        /* 主循环节拍: 约 5ms 一圈, 等待期间泵 LVGL */
        sys_delay_ms(LOOP_PERIOD_MS);
    }
}
