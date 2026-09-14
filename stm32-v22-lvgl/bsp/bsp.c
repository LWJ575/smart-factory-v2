#include "bsp.h"
#include "config.h"
#include <string.h>

/* DBG 宏的全局缓冲区 (BSS 段) */
char g_dbg_buf[128];

/* ---- __get_IPSR: 读取 IPSR 寄存器 (判断是否在中断中) ----
 * ARMCC v5 的正点原子 core_cm3.h 可能未提供此 intrinsic, 手动实现
 * 注意: ARMCC v5 不支持 GCC 扩展内联汇编语法 ( __asm volatile("": "=r"(v)) ),
 * 必须使用 ARMCC 原生的 __asm { } 块语法, 直接引用 C 变量名 */
#ifndef __get_IPSR
static __inline uint32_t __get_IPSR(void)
{
    uint32_t result;
    __asm { MRS result, IPSR }
    return result;
}
#endif

/* ============================================================
 * BSP 实现 — STM32F103ZET6 (SPL, 无 FreeRTOS, 纯 LVGL 版)
 * ============================================================ */

/* ---- DWT 寄存器 (Cortex-M3 调试追踪) ---- */
#define DWT_CR      (*(volatile uint32_t *)0xE0001000)
#define DWT_CYCCNT  (*(volatile uint32_t *)0xE0001004)
#define DEM_CR      (*(volatile uint32_t *)0xE000EDFC)

/* ==================== 系统时钟 ==================== */

void sys_clock_init(void)
{
    /* 正点原子精英版默认 72MHz:
     * HSE 8MHz → PLL ×9 = 72MHz
     * 这里假设时钟已经由 startup 文件中的 SystemInit() 配置
     * 但如果从 bootloader 跳转, 需要重新初始化
     */
    /* 确保在 72MHz 运行 */
    SystemCoreClock = 72000000UL;
}

/* ==================== 毫秒计时 (SysTick) ==================== */

/* SysTick 毫秒计数器 (覆盖 startup 文件中的弱符号 SysTick_Handler) */
static volatile uint32_t s_tick_ms = 0;

void SysTick_Handler(void)
{
    s_tick_ms++;
}

void bsp_systick_init(void)
{
    /* 72MHz / (72000-1+1) = 1kHz → 1ms 中断
     * SysTick_Config 同时把 SysTick 优先级设为最低 (不影响 USART2 中断) */
    SysTick_Config(SystemCoreClock / 1000);
}

uint32_t sys_tick(void)
{
    return s_tick_ms;
}

/* ---- 空闲钩子弱实现 (main.c 提供强实现: 泵 LVGL) ---- */
__weak void bsp_idle_hook(void)
{
    /* 默认什么都不做 */
}

void sys_delay_ms(uint32_t ms)
{
    /* 1. 在 ISR 中: 不能等 SysTick (中断优先级可能低于当前中断),
     *    用 DWT 忙等 */
    if (0 != __get_IPSR()) {
        sys_delay_us(ms * 1000);
        return;
    }

    /* 2. 线程模式 (超级循环): 等 SysTick 毫秒计数,
     *    等待期间调用空闲钩子泵 LVGL —— DS18B20 750ms 转换、
     *    WiFi 20s 连接等阻塞等待期间 UI 保持刷新 */
    {
        uint32_t start = s_tick_ms;
        while ((s_tick_ms - start) < ms) {
            bsp_idle_hook();
        }
    }
}

/* ==================== DWT 微秒延时 ==================== */

void dwt_init(void)
{
    DEM_CR |= (1 << 24);           /* TRCENA: 开启 DWT */
    DWT_CYCCNT = 0;                /* 清零计数器 */
    DWT_CR |= (1 << 0);            /* CYCCNTENA: 使能计数器 */
}

void sys_delay_us(uint32_t us)
{
    uint32_t start = DWT_CYCCNT;
    uint32_t ticks = us * (SystemCoreClock / 1000000);
    while ((DWT_CYCCNT - start) < ticks);
}

/* ==================== 调试串口 (USART1) ==================== */

void debug_init(void)
{
    GPIO_InitTypeDef gpio;
    USART_InitTypeDef uart;

    /* 开启时钟 */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1 | RCC_APB2Periph_GPIOA, ENABLE);

    /* PA9 = TX, 复用推挽 */
    gpio.GPIO_Pin = GPIO_Pin_9;
    gpio.GPIO_Mode = GPIO_Mode_AF_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &gpio);

    /* PA10 = RX, 浮空输入 */
    gpio.GPIO_Pin = GPIO_Pin_10;
    gpio.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOA, &gpio);

    /* USART1: 115200 8N1 */
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
    /* 开启 PWR 和 BKP 时钟 */
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_PWR | RCC_APB1Periph_BKP, ENABLE);
    /* 允许写入 BKP 域 */
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

void flash_unlock(void)
{
    /* 解锁序列 */
    FLASH_UnlockBank1();
}

void flash_lock(void)
{
    FLASH_LockBank1();
}

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
    /* STM32 Flash 必须半字 (16-bit) 写入 */
    for (uint32_t i = 0; i < len; i += 2) {
        uint16_t hw;
        if (i + 1 < len) {
            hw = buf[i] | ((uint16_t)buf[i + 1] << 8);
        } else {
            hw = buf[i] | 0xFF00;  /* 最后一字节补 0xFF */
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
    /* 读取 app 的初始栈指针和复位向量 */
    uint32_t app_sp = *(volatile uint32_t *)(app_addr);
    uint32_t app_pc = *(volatile uint32_t *)(app_addr + 4);

    /* 关闭所有中断 */
    __disable_irq();

    /* 停止 SysTick (v2.2: bsp 接管了 SysTick, 跳转前必须关闭) */
    SysTick->CTRL = 0;
    SysTick->LOAD = 0;
    SysTick->VAL  = 0;

    /* 关闭外设 (简化: 关 USART/TIM/ADC 等) */
    USART_Cmd(USART1, DISABLE);
    USART_Cmd(USART2, DISABLE);

    /* 设置 MSP (主栈指针) */
    __set_MSP(app_sp);

    /* 跳转 */
    ((void (*)(void))app_pc)();

    /* 不应该到达这里 */
    while (1);
}

/* ==================== 系统复位 ==================== */

void system_reset(void)
{
    NVIC_SystemReset();
}
