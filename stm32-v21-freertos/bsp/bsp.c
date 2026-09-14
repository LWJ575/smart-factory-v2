#include "bsp.h"
#include "config.h"
#include "FreeRTOS.h"
#include "task.h"
#include <string.h>

/* DBG 宏的全局缓冲区 (BSS 段, 不占任务栈) */
char g_dbg_buf[128];

/* ---- __get_IPSR: 读取 IPSR 寄存器 ---- */
#ifndef __get_IPSR
static __inline uint32_t __get_IPSR(void)
{
    uint32_t result;
    __asm { MRS result, IPSR }
    return result;
}
#endif

/* ============================================================
 * BSP 实现 — STM32F103ZET6 (SPL, FreeRTOS, 无 LVGL)
 * ============================================================ */

/* ---- DWT 寄存器 ---- */
#define DWT_CR      (*(volatile uint32_t *)0xE0001000)
#define DWT_CYCCNT  (*(volatile uint32_t *)0xE0001004)
#define DEM_CR      (*(volatile uint32_t *)0xE000EDFC)

/* ==================== 系统时钟 ==================== */

void sys_clock_init(void)
{
    SystemCoreClock = 72000000UL;
}

/* ==================== DWT 微秒延时 ==================== */

void dwt_init(void)
{
    DEM_CR |= (1 << 24);
    DWT_CYCCNT = 0;
    DWT_CR |= (1 << 0);
}

void sys_delay_us(uint32_t us)
{
    uint32_t start = DWT_CYCCNT;
    uint32_t ticks = us * (SystemCoreClock / 1000000);
    while ((DWT_CYCCNT - start) < ticks);
}

/* ==================== 毫秒计时 (FreeRTOS tick) ==================== */

static volatile uint32_t s_tick_ms = 0;

void bsp_tick_hook(void)
{
    s_tick_ms++;
}

uint32_t sys_tick(void)
{
    if (0 == __get_IPSR()) {
        extern uint32_t xTaskGetTickCount(void);
        return xTaskGetTickCount();
    }
    return s_tick_ms;
}

void sys_delay_ms(uint32_t ms)
{
    /* 1. 在 ISR 中: 忙等 */
    if (0 != __get_IPSR()) {
        while (ms--) {
            for (volatile int i = 0; i < 7200; i++);
        }
        return;
    }
    /* 2. 调度器已启动: 用 vTaskDelay (让出 CPU) */
    if (taskSCHEDULER_RUNNING == xTaskGetSchedulerState()) {
        vTaskDelay(ms);
        return;
    }
    /* 3. 调度器未启动 (main() 初始化阶段): 忙等 */
    while (ms--) {
        for (volatile int i = 0; i < 7200; i++);
    }
}

/* ==================== 调试串口 (USART1) ==================== */

void debug_init(void)
{
    GPIO_InitTypeDef gpio;
    USART_InitTypeDef uart;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1 | RCC_APB2Periph_GPIOA, ENABLE);

    gpio.GPIO_Pin = GPIO_Pin_9;
    gpio.GPIO_Mode = GPIO_Mode_AF_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &gpio);

    gpio.GPIO_Pin = GPIO_Pin_10;
    gpio.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOA, &gpio);

    uart.USART_BaudRate = 115200;
    uart.USART_WordLength = USART_WordLength_8b;
    uart.USART_StopBits = USART_StopBits_1;
    uart.USART_Parity = USART_Parity_No;
    uart.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    uart.USART_Mode = USART_Mode_Tx | USART_Mode_Rx;
    USART_Init(USART1, &uart);

    USART_Cmd(USART1, ENABLE);
}

void debug_send(const uint8_t *data, int len)
{
    while (len--) {
        while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET);
        USART_SendData(USART1, *data++);
    }
}

/* ==================== BKP 备份寄存器 ==================== */

void bkp_init(void)
{
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_PWR | RCC_APB1Periph_BKP, ENABLE);
    PWR_BackupAccessCmd(ENABLE);
}

void bkp_write(uint16_t dr, uint16_t val)
{
    switch (dr) {
        case BKP_DR1: BKP_WriteBackupRegister(BKP_DR1, val); break;
        case BKP_DR2: BKP_WriteBackupRegister(BKP_DR2, val); break;
        case BKP_DR3: BKP_WriteBackupRegister(BKP_DR3, val); break;
        case BKP_DR4: BKP_WriteBackupRegister(BKP_DR4, val); break;
        default: break;
    }
}

uint16_t bkp_read(uint16_t dr)
{
    switch (dr) {
        case BKP_DR1: return BKP_ReadBackupRegister(BKP_DR1);
        case BKP_DR2: return BKP_ReadBackupRegister(BKP_DR2);
        case BKP_DR3: return BKP_ReadBackupRegister(BKP_DR3);
        case BKP_DR4: return BKP_ReadBackupRegister(BKP_DR4);
        default: return 0;
    }
}

/* ==================== Flash 操作 ==================== */

void flash_unlock(void)  { FLASH_UnlockBank1(); }
void flash_lock(void)     { FLASH_LockBank1(); }

int flash_erase_page(uint32_t addr)
{
    FLASH_Status st = FLASH_ErasePage(addr);
    return (st == FLASH_COMPLETE) ? 0 : -1;
}

int flash_write_halfword(uint32_t addr, uint16_t data)
{
    FLASH_Status st = FLASH_ProgramHalfWord(addr, data);
    return (st == FLASH_COMPLETE) ? 0 : -1;
}

int flash_write_buf(uint32_t addr, const uint8_t *buf, uint32_t len)
{
    for (uint32_t i = 0; i < len; i += 2) {
        uint16_t hw;
        if (i + 1 < len) {
            hw = buf[i] | ((uint16_t)buf[i + 1] << 8);
        } else {
            hw = buf[i] | 0xFF00;
        }
        FLASH_Status st = FLASH_ProgramHalfWord(addr + i, hw);
        if (st != FLASH_COMPLETE) return -1;
    }
    return 0;
}

int flash_verify(uint32_t addr, const uint8_t *expected, uint32_t len)
{
    const uint8_t *flash = (const uint8_t *)addr;
    for (uint32_t i = 0; i < len; i++) {
        if (flash[i] != expected[i]) return -1;
    }
    return 0;
}

/* ==================== 跳转 ==================== */

void jump_to_app(uint32_t app_addr)
{
    uint32_t app_sp = *(volatile uint32_t *)(app_addr);
    uint32_t app_pc = *(volatile uint32_t *)(app_addr + 4);

    __disable_irq();
    USART_Cmd(USART1, DISABLE);
    USART_Cmd(USART2, DISABLE);
    __set_MSP(app_sp);
    ((void (*)(void))app_pc)();
    while (1);
}

/* ==================== 系统复位 ==================== */

void system_reset(void)
{
    NVIC_SystemReset();
}
