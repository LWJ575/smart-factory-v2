#include "sensors.h"
#include "config.h"
#include "stm32f10x.h"

/* ============================================================
 * 传感器驱动实现 (标准库/SPL 版本)
 *
 * DS18B20 1-Wire 时序 (标准速度):
 *   Reset:  低 480us -> 放开 -> 等 70us -> 检测 -> 总 960us
 *   Write 1: 低 6us -> 放开 -> 等 64us
 *   Write 0: 低 60us -> 放开 -> 等 10us
 *   Read:   低 6us -> 放开 -> 等 9us -> 采样 -> 等 55us
 *
 * 微秒延时使用 bsp.c 的 sys_delay_us() (DWT 周期计数器)
 * ============================================================ */

/* ---- 1-Wire 底层操作 (开漏模式: 写1=释放, 写0=拉低) ---- */

#define OW_LOW()   GPIO_ResetBits(DS18B20_PORT, DS18B20_PIN)
#define OW_HIGH()  GPIO_SetBits(DS18B20_PORT, DS18B20_PIN)
#define OW_READ()  GPIO_ReadInputDataBit(DS18B20_PORT, DS18B20_PIN)

/* 1-Wire 复位 + 存在检测
 * 返回: 1=检测到器件, 0=无器件 */
static int ow_reset(void)
{
    int presence;

    OW_LOW();
    sys_delay_us(480);
    OW_HIGH();
    sys_delay_us(70);

    presence = (OW_READ() == Bit_RESET) ? 1 : 0;
    sys_delay_us(410);  /* 完成复位时序 */
    return presence;
}

/* 写一个 bit */
static void ow_write_bit(int bit)
{
    if (bit) {
        OW_LOW();
        sys_delay_us(6);
        OW_HIGH();
        sys_delay_us(64);
    } else {
        OW_LOW();
        sys_delay_us(60);
        OW_HIGH();
        sys_delay_us(10);
    }
}

/* 读一个 bit */
static int ow_read_bit(void)
{
    int bit;

    OW_LOW();
    sys_delay_us(6);
    OW_HIGH();
    sys_delay_us(9);

    bit = (OW_READ() == Bit_SET) ? 1 : 0;
    sys_delay_us(55);
    return bit;
}

/* 写一个字节 (LSB first) */
static void ow_write_byte(uint8_t byte)
{
    int i;
    for (i = 0; i < 8; i++) {
        ow_write_bit(byte & 0x01);
        byte >>= 1;
    }
}

/* 读一个字节 (LSB first) */
static uint8_t ow_read_byte(void)
{
    uint8_t val = 0;
    int i;
    for (i = 0; i < 8; i++) {
        val >>= 1;
        if (ow_read_bit()) val |= 0x80;
    }
    return val;
}

/* ---- DS18B20 公共接口 ---- */

float ds18b20_read_temp(void)
{
    /* 1. 复位 + 检测存在 */
    if (!ow_reset()) {
        return -999.0f;
    }

    /* 2. Skip ROM + Convert T */
    ow_write_byte(0xCC);
    ow_write_byte(0x44);

    /* 3. 等待转换完成 (12-bit 模式约 750ms) */
    sys_delay_ms(750);

    /* 4. 复位 + Skip ROM + Read Scratchpad */
    if (!ow_reset()) {
        return -999.0f;
    }

    ow_write_byte(0xCC);
    ow_write_byte(0xBE);

    /* 5. 读取前 2 字节 (温度数据) */
    {
        uint8_t temp_l = ow_read_byte();
        uint8_t temp_h = ow_read_byte();
        int16_t raw;
        float temp;

        /* 消费剩余 7 字节 scratchpad (CRC 等), 不使用但需读取 */
        int i;
        for (i = 0; i < 7; i++) {
            ow_read_byte();
        }

        /* 6. 计算温度
         * 12-bit 分辨率: raw = (temp_h << 8) | temp_l
         * 温度 = raw / 16.0
         * DS18B20 上电默认值 = 0x0550 (85°C), 全零 = 无器件 */
        raw = (int16_t)((temp_h << 8) | temp_l);
        if (raw == 0) {
            return -999.0f;  /* 无器件: 总线浮空读到全零 */
        }
        temp = raw * 0.0625f;

        /* 注意: 不在此函数内调用 DBG (snprintf 栈消耗大, 交给 caller 打印) */
        return temp;
    }
}

/* ---- 便捷函数 ---- */

int16_t ds18b20_read_temp_x10(void)
{
    float t = ds18b20_read_temp();
    if (t < -900.0f) return -9990;
    return (int16_t)(t * 10.0f + 0.5f);
}

uint16_t light_read_raw(void)
{
    return light_read();
}

/* ---- 光敏 ADC (标准库) ---- */

uint16_t light_read(void)
{
    uint16_t val;
    uint32_t timeout;

    /* 配置规则通道 */
    ADC_RegularChannelConfig(LIGHT_ADC, LIGHT_ADC_CHANNEL, 1,
                             ADC_SampleTime_55Cycles5);

    /* 启动软件转换 */
    ADC_SoftwareStartConvCmd(LIGHT_ADC, ENABLE);

    /* 等待转换完成 (超时 10ms) */
    timeout = sys_tick() + 10;
    while (ADC_GetFlagStatus(LIGHT_ADC, ADC_FLAG_EOC) == RESET) {
        if (sys_tick() >= timeout) return 0;
    }

    val = (uint16_t)ADC_GetConversionValue(LIGHT_ADC);
    return val;
}

/* ---- 初始化 ---- */

void sensors_init(void)
{
    GPIO_InitTypeDef gpio;
    ADC_InitTypeDef adc;

    /* ---- DS18B20: PG11, 开漏输出 (1-Wire 总线, 需外部上拉 4.7kΩ) ---- */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOG, ENABLE);
    gpio.GPIO_Pin = DS18B20_PIN;
    gpio.GPIO_Mode = GPIO_Mode_Out_OD;   /* 开漏: 写1=释放, 写0=拉低 */
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(DS18B20_PORT, &gpio);

    /* 初始状态释放总线 (高) */
    OW_HIGH();

    /* ---- 光敏 ADC: ADC1 Channel 1 (PA1 模拟输入) ---- */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_ADC1, ENABLE);
    gpio.GPIO_Pin = GPIO_Pin_1;
    gpio.GPIO_Mode = GPIO_Mode_AIN;      /* 模拟输入 */
    GPIO_Init(GPIOA, &gpio);

    /* ADC 时钟: PCLK2/6 = 72/6 = 12MHz (不超过 14MHz) */
    RCC_ADCCLKConfig(RCC_PCLK2_Div6);

    adc.ADC_Mode = ADC_Mode_Independent;
    adc.ADC_ScanConvMode = DISABLE;
    adc.ADC_ContinuousConvMode = DISABLE;
    adc.ADC_ExternalTrigConv = ADC_ExternalTrigConv_None;
    adc.ADC_DataAlign = ADC_DataAlign_Right;
    adc.ADC_NbrOfChannel = 1;
    ADC_Init(LIGHT_ADC, &adc);

    ADC_Cmd(LIGHT_ADC, ENABLE);

    /* ADC 校准 (上电后必须执行) */
    ADC_ResetCalibration(LIGHT_ADC);
    while (ADC_GetResetCalibrationStatus(LIGHT_ADC));
    ADC_StartCalibration(LIGHT_ADC);
    while (ADC_GetCalibrationStatus(LIGHT_ADC));

    DBG("[SENSORS] initialized (DWT %d MHz)\r\n",
        SystemCoreClock / 1000000);
}
