#include "actuators.h"
#include "config.h"
#include "stm32f10x.h"

/* ============================================================
 * 执行器驱动实现 (标准库/SPL 版本)
 * ============================================================ */

static int s_led1_state = 0;
static int s_led2_state = 0;
static int s_motor_speed = 0;
static uint32_t s_buzzer_end = 0;

/* ---- 设置 TIM 比较寄存器 (根据通道号选择) ---- */
static void set_motor_compare(uint32_t value)
{
#if MOTOR_TIM_CHANNEL == 1
    TIM_SetCompare1(MOTOR_TIM, value);
#elif MOTOR_TIM_CHANNEL == 2
    TIM_SetCompare2(MOTOR_TIM, value);
#elif MOTOR_TIM_CHANNEL == 3
    TIM_SetCompare3(MOTOR_TIM, value);
#elif MOTOR_TIM_CHANNEL == 4
    TIM_SetCompare4(MOTOR_TIM, value);
#else
    #error "Invalid MOTOR_TIM_CHANNEL"
#endif
}

void actuators_init(void)
{
    GPIO_InitTypeDef gpio;
    TIM_TimeBaseInitTypeDef tim;
    TIM_OCInitTypeDef oc;

    /* ---- LED1 (PB5) + 蜂鸣器 (PB8): 推挽输出 ---- */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
    gpio.GPIO_Mode = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    gpio.GPIO_Pin = LED1_PIN | BUZZER_PIN;
    GPIO_Init(GPIOB, &gpio);

    /* ---- LED2 (PE5): 推挽输出 ---- */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOE, ENABLE);
    gpio.GPIO_Pin = LED2_PIN;
    GPIO_Init(GPIOE, &gpio);

    /* ---- 电机 PWM: TIM3 CH1 (PA6) ---- */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    /* PA6 = TIM3_CH1, 复用推挽 */
    gpio.GPIO_Pin = GPIO_Pin_6;
    gpio.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOA, &gpio);

    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE);
    /* 72MHz / (71+1) = 1MHz, 1MHz / (999+1) = 1kHz PWM */
    tim.TIM_Prescaler = MOTOR_TIM_PRESCALER;
    tim.TIM_Period = MOTOR_TIM_PERIOD;
    tim.TIM_ClockDivision = TIM_CKD_DIV1;
    tim.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInit(MOTOR_TIM, &tim);

    /* PWM1 模式, CH1 */
    oc.TIM_OCMode = TIM_OCMode_PWM1;
    oc.TIM_OutputState = TIM_OutputState_Enable;
    oc.TIM_Pulse = 0;  /* 初始 0% */
    oc.TIM_OCPolarity = TIM_OCPolarity_High;
#if MOTOR_TIM_CHANNEL == 1
    TIM_OC1Init(MOTOR_TIM, &oc);
    TIM_OC1PreloadConfig(MOTOR_TIM, TIM_OCPreload_Enable);
#elif MOTOR_TIM_CHANNEL == 2
    TIM_OC2Init(MOTOR_TIM, &oc);
    TIM_OC2PreloadConfig(MOTOR_TIM, TIM_OCPreload_Enable);
#elif MOTOR_TIM_CHANNEL == 3
    TIM_OC3Init(MOTOR_TIM, &oc);
    TIM_OC3PreloadConfig(MOTOR_TIM, TIM_OCPreload_Enable);
#elif MOTOR_TIM_CHANNEL == 4
    TIM_OC4Init(MOTOR_TIM, &oc);
    TIM_OC4PreloadConfig(MOTOR_TIM, TIM_OCPreload_Enable);
#endif
    TIM_Cmd(MOTOR_TIM, ENABLE);

    /* ---- 初始状态: LED 熄灭, 蜂鸣器关, 电机 0% ---- */
    led_set(0, 0);
    led_set(1, 0);
    buzzer_set(0);
    motor_set_speed(0);

    DBG("[ACT] actuators initialized\r\n");
}

void led_set(int idx, int on)
{
    if (idx == 0) {
        s_led1_state = on;
#if LED_ACTIVE_LOW
        GPIO_WriteBit(LED1_PORT, LED1_PIN, on ? Bit_RESET : Bit_SET);
#else
        GPIO_WriteBit(LED1_PORT, LED1_PIN, on ? Bit_SET : Bit_RESET);
#endif
    } else {
        s_led2_state = on;
#if LED_ACTIVE_LOW
        GPIO_WriteBit(LED2_PORT, LED2_PIN, on ? Bit_RESET : Bit_SET);
#else
        GPIO_WriteBit(LED2_PORT, LED2_PIN, on ? Bit_SET : Bit_RESET);
#endif
    }
}

int led_get_state(int idx)
{
    return (idx == 0) ? s_led1_state : s_led2_state;
}

/* ---- 便捷函数 ---- */

void led1_on(void)  { led_set(0, 1); }
void led1_off(void) { led_set(0, 0); }
void led2_on(void)  { led_set(1, 1); }
void led2_off(void) { led_set(1, 0); }

void buzzer_off(void) { buzzer_set(0); }
void buzzer_on_nonblock(uint32_t duration_ms) { buzzer_alarm(duration_ms); }

void buzzer_set(int on)
{
#if BUZZER_ACTIVE_HIGH
    GPIO_WriteBit(BUZZER_PORT, BUZZER_PIN, on ? Bit_SET : Bit_RESET);
#else
    GPIO_WriteBit(BUZZER_PORT, BUZZER_PIN, on ? Bit_RESET : Bit_SET);
#endif
}

void buzzer_alarm(uint32_t duration_ms)
{
    s_buzzer_end = sys_tick() + duration_ms;
    buzzer_set(1);
    DBG("[ACT] alarm on for %u ms\r\n", (unsigned)duration_ms);
}

void buzzer_update(void)
{
    if (s_buzzer_end > 0 && sys_tick() >= s_buzzer_end) {
        buzzer_set(0);
        s_buzzer_end = 0;
    }
}

void motor_set_speed(int speed)
{
    uint32_t ccr;

    if (speed < 0) speed = 0;
    if (speed > 100) speed = 100;

    s_motor_speed = speed;
    ccr = (MOTOR_TIM_PERIOD + 1) * (uint32_t)speed / 100;
    set_motor_compare(ccr);
}

int motor_get_speed(void)
{
    return s_motor_speed;
}
