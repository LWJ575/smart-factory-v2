#include "lcd.h"
#include "config.h"

/* ============================================================
 * LCD 驱动实现 — ILI9341 via FSMC 16-bit 并行
 * 正点原子精英版: PB0 背光, FSMC NE4 + A16 做 RS
 * ============================================================ */

/* ==================== FSMC 初始化 ==================== */

static void fsmc_init(void)
{
    FSMC_NORSRAMInitTypeDef fsmc;
    FSMC_NORSRAMTimingInitTypeDef readTiming;
    FSMC_NORSRAMTimingInitTypeDef writeTiming;
    GPIO_InitTypeDef gpio;

    /* 开启 FSMC 和 GPIO 时钟 */
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_FSMC, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOD | RCC_APB2Periph_GPIOE |
                           RCC_APB2Periph_GPIOB | RCC_APB2Periph_GPIOG, ENABLE);

    /* FSMC 引脚全部配置为复用推挽 */
    gpio.GPIO_Mode = GPIO_Mode_AF_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;

    /* GPIOD: D0=PD14, D1=PD15, D2=PD0, D3=PD1, D13=PD8, D14=PD9, D15=PD10,
     *        NOE=PD4, NWE=PD5, A16=PD11 (RS 信号) */
    gpio.GPIO_Pin = GPIO_Pin_0 | GPIO_Pin_1 | GPIO_Pin_4 | GPIO_Pin_5 |
                    GPIO_Pin_8 | GPIO_Pin_9 | GPIO_Pin_10 | GPIO_Pin_11 |
                    GPIO_Pin_14 | GPIO_Pin_15;
    GPIO_Init(GPIOD, &gpio);

    /* GPIOE: D4=PE7, D5=PE8, D6=PE9, D7=PE10, D8=PE11,
     *        D9=PE12, D10=PE13, D11=PE14, D12=PE15 */
    gpio.GPIO_Pin = GPIO_Pin_7 | GPIO_Pin_8 | GPIO_Pin_9 | GPIO_Pin_10 |
                    GPIO_Pin_11 | GPIO_Pin_12 | GPIO_Pin_13 | GPIO_Pin_14 |
                    GPIO_Pin_15;
    GPIO_Init(GPIOE, &gpio);

    /* GPIOG: NE4=PG12 (LCD 片选) */
    gpio.GPIO_Pin = GPIO_Pin_12;
    GPIO_Init(GPIOG, &gpio);

    /* 读时序 (5 个 HCLK, DataSetupTime=15) */
    readTiming.FSMC_AddressSetupTime = 0;
    readTiming.FSMC_AddressHoldTime = 0;
    readTiming.FSMC_DataSetupTime = 15;
    readTiming.FSMC_BusTurnAroundDuration = 0;
    readTiming.FSMC_CLKDivision = 0;
    readTiming.FSMC_DataLatency = 0;
    readTiming.FSMC_AccessMode = FSMC_AccessMode_B;

    /* 写时序 (2 个 HCLK, DataSetupTime=2) */
    writeTiming.FSMC_AddressSetupTime = 0;
    writeTiming.FSMC_AddressHoldTime = 0;
    writeTiming.FSMC_DataSetupTime = 2;
    writeTiming.FSMC_BusTurnAroundDuration = 0;
    writeTiming.FSMC_CLKDivision = 0;
    writeTiming.FSMC_DataLatency = 0;
    writeTiming.FSMC_AccessMode = FSMC_AccessMode_B;

    /* FSMC 配置 */
    fsmc.FSMC_Bank = FSMC_Bank1_NORSRAM4;
    fsmc.FSMC_DataAddressMux = FSMC_DataAddressMux_Disable;
    fsmc.FSMC_MemoryType = FSMC_MemoryType_SRAM;
    fsmc.FSMC_MemoryDataWidth = FSMC_MemoryDataWidth_16b;
    fsmc.FSMC_BurstAccessMode = FSMC_BurstAccessMode_Disable;
    fsmc.FSMC_AsynchronousWait = FSMC_AsynchronousWait_Disable;
    fsmc.FSMC_WaitSignalPolarity = FSMC_WaitSignalPolarity_Low;
    fsmc.FSMC_WrapMode = FSMC_WrapMode_Disable;
    fsmc.FSMC_WaitSignalActive = FSMC_WaitSignalActive_BeforeWaitState;
    fsmc.FSMC_WriteOperation = FSMC_WriteOperation_Enable;
    fsmc.FSMC_WaitSignal = FSMC_WaitSignal_Disable;
    fsmc.FSMC_ExtendedMode = FSMC_ExtendedMode_Disable;
    fsmc.FSMC_WriteBurst = FSMC_WriteBurst_Disable;
    fsmc.FSMC_ReadWriteTimingStruct = &readTiming;
    fsmc.FSMC_WriteTimingStruct = &writeTiming;

    FSMC_NORSRAMInit(&fsmc);
    FSMC_NORSRAMCmd(FSMC_Bank1_NORSRAM4, ENABLE);
}

/* ==================== ILI9341 初始化序列 ==================== */

static void ili9341_init_seq(void)
{
    /* 软件复位 */
    lcd_write_cmd(0x01);
    sys_delay_ms(100);

    /* 退出睡眠 */
    lcd_write_cmd(0x11);
    sys_delay_ms(120);

    /* MADCTL: 0x28 = 横屏模式
     * Bit5(MV)=1: 行列交换 (240x320 -> 320x240)
     * Bit3(BGR)=1: BGR 色序 (正点原子面板需要)
     * 若颜色异常 (红蓝互换), 改为 0x20 (BGR=0) */
    lcd_write_cmd(0x36);
    lcd_write_data(0x28);

    /* 像素格式: RGB565 (16-bit/pixel) */
    lcd_write_cmd(0x3A);
    lcd_write_data(0x55);

    /* 帧率控制 */
    lcd_write_cmd(0xB1);
    lcd_write_data(0x00);
    lcd_write_data(0x1B);

    /* 显示开 */
    lcd_write_cmd(0x29);
}

/* ==================== 公共接口 ==================== */

void lcd_init(void)
{
    /* 1. 初始化 FSMC */
    fsmc_init();

    /* 2. 初始化 DWT (延时用) */
    dwt_init();

    /* 3. 背光引脚 PB0, 推挽输出 (GPIOB 时钟已在 fsmc_init 中开启) */
    GPIO_InitTypeDef gpio;
    gpio.GPIO_Pin = GPIO_Pin_0;
    gpio.GPIO_Mode = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &gpio);

    /* 4. ILI9341 初始化序列 */
    ili9341_init_seq();

    /* 5. 开背光 */
    lcd_backlight_on();
}

void lcd_write_cmd(uint16_t cmd)
{
    LCD_REG = cmd;
}

void lcd_write_data(uint16_t data)
{
    LCD_RAM = data;
}

void lcd_write_reg(uint16_t reg, uint16_t val)
{
    LCD_REG = reg;
    LCD_RAM = val;
}

uint16_t lcd_read_reg(uint16_t reg)
{
    LCD_REG = reg;
    return LCD_RAM;
}

void lcd_backlight_on(void)
{
    /* PB0 高电平点亮背光 */
    GPIO_SetBits(GPIOB, GPIO_Pin_0);
}

void lcd_backlight_off(void)
{
    GPIO_ResetBits(GPIOB, GPIO_Pin_0);
}

void lcd_set_window(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2)
{
    /* 设置列地址 */
    LCD_REG = 0x2A;
    LCD_RAM = x1 >> 8;
    LCD_RAM = x1 & 0xFF;
    LCD_RAM = x2 >> 8;
    LCD_RAM = x2 & 0xFF;

    /* 设置行地址 */
    LCD_REG = 0x2B;
    LCD_RAM = y1 >> 8;
    LCD_RAM = y1 & 0xFF;
    LCD_RAM = y2 >> 8;
    LCD_RAM = y2 & 0xFF;

    /* 开始写 GRAM */
    LCD_REG = 0x2C;
}

void lcd_fill(uint16_t color)
{
    uint32_t total = (uint32_t)LCD_W * LCD_H;
    lcd_set_window(0, 0, LCD_W - 1, LCD_H - 1);
    while (total--) {
        LCD_RAM = color;
    }
}

void lcd_flush_area(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2,
                    const uint16_t *color_data)
{
    uint32_t total = (uint32_t)(x2 - x1 + 1) * (y2 - y1 + 1);
    lcd_set_window(x1, y1, x2, y2);
    while (total--) {
        LCD_RAM = *color_data++;
    }
}
