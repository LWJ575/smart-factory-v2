#ifndef ACTUATORS_H
#define ACTUATORS_H

#include <stdint.h>

/* ============================================================
 * 执行器驱动 — LED + 蜂鸣器 + PWM 电机
 * ============================================================ */

/* 初始化执行器 (启动 PWM, 熄灭 LED, 关蜂鸣器) */
void actuators_init(void);

/* LED 控制
 * idx: 0=LED1, 1=LED2
 * on:  1=点亮, 0=熄灭
 */
void led_set(int idx, int on);

/* 蜂鸣器即时开关 */
void buzzer_set(int on);

/* 蜂鸣器定时报警 (非阻塞)
 * duration_ms: 报警时长(毫秒), 超时后自动关闭
 * 调用 buzzer_update() 在主循环中检查
 */
void buzzer_alarm(uint32_t duration_ms);

/* 便捷函数: 单个 LED 直接开关 */
void led1_on(void);
void led1_off(void);
void led2_on(void);
void led2_off(void);

/* 便捷函数: 蜂鸣器 */
void buzzer_off(void);
void buzzer_on_nonblock(uint32_t duration_ms); /* = buzzer_alarm */

/* 蜂鸣器定时检查 (在主循环中调用) */
void buzzer_update(void);

/* 电机转速控制
 * speed: 0-100 (百分比)
 */
void motor_set_speed(int speed);

/* 获取电机当前转速 */
int motor_get_speed(void);

/* 获取 LED 当前状态 */
int led_get_state(int idx);

#endif /* ACTUATORS_H */
