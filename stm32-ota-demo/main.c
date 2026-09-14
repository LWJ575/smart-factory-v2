/* ============================================================
 * OTA Demo App — STM32F103ZET6 正点原子精英版 (SPL)
 * v1.0.0: LED0 慢闪 (500ms)
 * v1.1.0: LED0 快闪 (100ms)  ← 改下面两个宏重新编译, 用作升级固件
 *
 * Flash 布局: App 位于 0x08004000 (bootloader 占 0x08000000-0x08003FFF)
 * Keil 配置: IROM1 = 0x08004000, Size = 0x7C000; RAM = 0x20000000, 0x10000
 *
 * 功能:
 *   - LED0 按 BLINK_MS 闪烁, 串口打印版本号 (证明固件版本)
 *   - 按下 KEY0 (PE4) → 写 BKP_DR1 = 0xA5A5 → 复位 → bootloader 执行 OTA
 *
 * 生成升级包: fromelf --bin -o firmware.bin .\OBJ\ota_demo_app.axf
 * ============================================================ */

#include "stm32f10x.h"
#include "sys.h"
#include "delay.h"
#include "usart.h"
#include <stdio.h>

/* ---- 升级演示用的版本标识 ---- */
#define FW_VER      "1.1.0"
#define BLINK_MS    100     /* v1.0.0 是 500, 肉眼可辨 */

#define APP_START   0x08004000
#define OTA_MAGIC   0xA5A5

/* LED0 = PB5 (低电平点亮), KEY0 = PE4 (按下为低) */
#define LED0_ON()   GPIO_ResetBits(GPIOB, GPIO_Pin_5)
#define LED0_OFF()  GPIO_SetBits(GPIOB, GPIO_Pin_5)
#define LED0_TOGGLE()  do { \
    if (GPIO_ReadOutputDataBit(GPIOB, GPIO_Pin_5)) LED0_ON(); \
    else LED0_OFF(); } while (0)
#define KEY0_PRESSED()  (GPIO_ReadInputDataBit(GPIOE, GPIO_Pin_4) == Bit_RESET)

static void led_key_init(void)
{
    GPIO_InitTypeDef gpio;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB | RCC_APB2Periph_GPIOE, ENABLE);

    /* LED0: 推挽输出 */
    gpio.GPIO_Pin = GPIO_Pin_5;
    gpio.GPIO_Mode = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &gpio);
    LED0_OFF();

    /* KEY0: 上拉输入 */
    gpio.GPIO_Pin = GPIO_Pin_4;
    gpio.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(GPIOE, &gpio);
}

/* 触发 OTA: 写 BKP 标志 + 复位 (与正式项目 ota_trigger_update 相同机制) */
static void trigger_ota(void)
{
    printf("[APP] KEY0 pressed -> OTA requested\r\n");
    printf("[APP] Writing BKP flag 0x%04X and rebooting...\r\n", OTA_MAGIC);

    /* 使能 PWR/BKP 时钟 + 备份域写访问 */
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_PWR | RCC_APB1Periph_BKP, ENABLE);
    PWR_BackupAccessCmd(ENABLE);
    BKP_WriteBackupRegister(BKP_DR1, OTA_MAGIC);

    /* 小延时让串口输出完 */
    delay_ms(100);
    NVIC_SystemReset();
    while (1);
}

int main(void)
{
    /* 1. 向量表重定位到 App 区 (bootloader 跳转过来的前提) */
    SCB->VTOR = APP_START;

    /* 2. 模板初始化 */
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);
    delay_init();               /* 正点原子 SysTick 轮询延时 */
    uart_init(115200);          /* printf 重定向到 USART1 */

    led_key_init();

    printf("\r\n========================================\r\n");
    printf("[APP] OTA Demo App  FW %s\r\n", FW_VER);
    printf("[APP] Running at 0x%08X (VTOR relocated)\r\n", APP_START);
    printf("[APP] LED0 blink %dms | Press KEY0 for OTA\r\n", BLINK_MS);
    printf("========================================\r\n");

    while (1) {
        LED0_TOGGLE();
        delay_ms(BLINK_MS);

        /* KEY0 按下 (消抖) → 触发 OTA */
        if (KEY0_PRESSED()) {
            delay_ms(20);                       /* 消抖 */
            if (KEY0_PRESSED()) {
                /* 等待松手, 防止复位后立即再触发 */
                while (KEY0_PRESSED());
                trigger_ota();
            }
        }
    }
}
