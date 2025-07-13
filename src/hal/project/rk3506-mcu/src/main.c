/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2022 Rockchip Electronics Co., Ltd.
 */

#include "hal_bsp.h"
#include "hal_base.h"

/********************* Private MACRO Definition ******************************/
//#define TEST_DEMO
extern uint32_t __linux_share_memory_start__[];
extern uint32_t __linux_share_memory_end__[];
#define SHMEM_LINUX_MEM_BASE ((uint32_t)&__linux_share_memory_start__)
#define SHMEM_LINUX_MEM_END  ((uint32_t)&__linux_share_memory_end__)
#define SHMEM_LINUX_MEM_SIZE (2UL * RL_VRING_OVERHEAD)

#define MCULOG_OFFSET 32
#define SOFTPWM_OFFSET 0

//#define STEREO

/********************* Private Structure Definition **************************/
struct mculog_buffer {
    unsigned int init_flag;
    unsigned int max_size;
    size_t head;
    size_t tail;
    unsigned char buf[0];
};

struct softpwm_config {
    unsigned int left_duty;
    unsigned int right_duty;
};

/********************* Private Variable Definition ***************************/
volatile struct mculog_buffer *logbuf;
volatile struct softpwm_config *softpwm;

#ifdef STEREO
static struct TIMER_REG *left_timer = TIMER4;
static uint32_t left_timer_irq = TIMER4_IRQn;
static bool left_enable = false;
static bool left_flag = true;

static struct TIMER_REG *right_timer = TIMER0;
static uint32_t right_timer_irq = TIMER0_IRQn;
static bool right_enable = false;
static bool right_flag = true;
#else
static struct TIMER_REG *timer = TIMER4;
static uint32_t timer_irq = TIMER4_IRQn;
static bool enable = false;
static bool flag = true;
#endif

/********************* Public Function Definition ****************************/
#ifdef __GNUC__
__USED int _write(int fd, char *ptr, int len)
{
    size_t free, tail;
    size_t to_end;
    /*
     * write "len" of char from "ptr" to file id "fd"
     * Return number of char written.
     *
    * Only work for STDOUT, STDIN, and STDERR
     */
    if (fd > 2) {
        return -1;
    }

    tail = logbuf->tail;
    free = (tail > logbuf->head) ? (tail - logbuf->head - 1) : (logbuf->max_size - logbuf->head + tail - 1);

    len = len < free ? len : free;
    to_end = logbuf->max_size - logbuf->head;

    if (len > to_end) {
        memcpy((unsigned char*)logbuf->buf + logbuf->head, ptr, to_end);
        memcpy((unsigned char*)logbuf->buf, ptr + to_end, len - to_end);
    } else {
        memcpy((unsigned char*)logbuf->buf + logbuf->head, ptr, len);
    }

    logbuf->head = (logbuf->head + len) % logbuf->max_size;

    return len;
}
#else
int fputc(int ch, FILE *f)
{
    return 0;
}
#endif

/********************* Private Function Definition ***************************/
#ifdef STEREO
static void left_timer_isr(long unsigned int irq, void *args)
{
    HAL_TIMER_Stop_IT(left_timer);
    if (left_flag) {
        HAL_GPIO_SetPinLevel(GPIO4, GPIO_PIN_B2, GPIO_HIGH);
        HAL_TIMER_SetCount(left_timer, softpwm->left_duty);
        if(!softpwm->left_duty) {
            left_enable = false;
            HAL_GPIO_SetPinLevel(GPIO4, GPIO_PIN_B2, GPIO_LOW);
        }
    } else {
        HAL_GPIO_SetPinLevel(GPIO4, GPIO_PIN_B2, GPIO_LOW);
        HAL_TIMER_SetCount(left_timer, 375 - softpwm->left_duty);
    }
    left_flag = !left_flag;
    HAL_TIMER_ClrInt(left_timer);
    HAL_TIMER_Start_IT(left_timer);
}

static void right_timer_isr(long unsigned int irq, void *args)
{
    HAL_TIMER_Stop_IT(right_timer);
    if (right_flag) {
        HAL_GPIO_SetPinLevel(GPIO4, GPIO_PIN_B3, GPIO_HIGH);
        HAL_TIMER_SetCount(right_timer, softpwm->right_duty);
        if(!softpwm->right_duty) {
            right_enable = false;
            HAL_GPIO_SetPinLevel(GPIO4, GPIO_PIN_B3, GPIO_LOW);
        }
    } else {
        HAL_GPIO_SetPinLevel(GPIO4, GPIO_PIN_B3, GPIO_LOW);
        HAL_TIMER_SetCount(right_timer, 375 - softpwm->right_duty);
    }
    right_flag = !right_flag;
    HAL_TIMER_ClrInt(right_timer);
    HAL_TIMER_Start_IT(right_timer);
}
#else
static void timer_isr(long unsigned int irq, void *args)
{
    HAL_TIMER_Stop_IT(timer);
    if (flag) {
        HAL_GPIO_SetPinLevel(GPIO4, GPIO_PIN_B2, GPIO_HIGH);
        HAL_GPIO_SetPinLevel(GPIO4, GPIO_PIN_B3, GPIO_HIGH);
        HAL_TIMER_SetCount(timer, softpwm->left_duty);
        if(!softpwm->left_duty) {
            enable = false;
            HAL_GPIO_SetPinLevel(GPIO4, GPIO_PIN_B2, GPIO_LOW);
            HAL_GPIO_SetPinLevel(GPIO4, GPIO_PIN_B3, GPIO_LOW);
            HAL_DBG("Sound Stop\n");
        }
    } else {
        HAL_GPIO_SetPinLevel(GPIO4, GPIO_PIN_B2, GPIO_LOW);
        HAL_GPIO_SetPinLevel(GPIO4, GPIO_PIN_B3, GPIO_LOW);
        HAL_TIMER_SetCount(timer, 375 - softpwm->left_duty);
    }
    flag = !flag;
    HAL_TIMER_ClrInt(timer);
    HAL_TIMER_Start_IT(timer);
}
#endif

int main(void)
{
    uint64_t start, end;
    uint32_t count;

    /* HAL BASE Init */
    HAL_Init();

    /* BSP Init */
    BSP_Init();

    /* INTMUX Init */
    HAL_INTMUX_Init();
    
    /* LOG SHARE MEMORY Init */
    logbuf = (struct mculog_buffer*)((void*)SHMEM_LINUX_MEM_BASE + MCULOG_OFFSET);
    while(logbuf->init_flag != 0x4d43554c);
    logbuf->init_flag = 0x554C4F47;
    HAL_DBG("Load mculog_buffer on: 0x%x\n", (unsigned int)(logbuf));

    /* PWM CONFIG SHARE MEMORY Init */
    softpwm = (struct softpwm_config*)((void*)SHMEM_LINUX_MEM_BASE + SOFTPWM_OFFSET);
    HAL_DBG("Load softpwm_config on: 0x%x\n", (unsigned int)(softpwm));

    /* GPIO Init */
    HAL_GPIO_SetPinDirection(GPIO4, GPIO_PIN_B2, GPIO_OUT);
    HAL_GPIO_SetPinDirection(GPIO4, GPIO_PIN_B3, GPIO_OUT);
    HAL_GPIO_SetPinLevel(GPIO4, GPIO_PIN_B2, GPIO_LOW);
    HAL_GPIO_SetPinLevel(GPIO4, GPIO_PIN_B3, GPIO_LOW);

    /* TIMER Init */
    softpwm->left_duty = 0;
    softpwm->right_duty = 0;
#ifdef STEREO
    HAL_NVIC_SetIRQHandler(left_timer_irq, left_timer_isr);
    HAL_NVIC_EnableIRQ(left_timer_irq);
    HAL_TIMER_SetCount(left_timer, 0);
    HAL_TIMER_Init(left_timer, TIMER_FREE_RUNNING);

    HAL_INTMUX_SetIRQHandler(right_timer_irq, right_timer_isr, NULL);
    HAL_INTMUX_EnableIRQ(right_timer_irq);
    HAL_TIMER_SetCount(right_timer, 0);
    HAL_TIMER_Init(right_timer, TIMER_FREE_RUNNING);
#else
    HAL_NVIC_SetIRQHandler(timer_irq, timer_isr);
    HAL_NVIC_EnableIRQ(timer_irq);
    HAL_TIMER_SetCount(timer, 0);
    HAL_TIMER_Init(timer, TIMER_FREE_RUNNING);
#endif

    HAL_DBG("Hello RK3506 mcu\n");

    while (1) {
#ifdef STEREO
        if (softpwm->left_duty && !left_enable)
        {
            HAL_TIMER_SetCount(left_timer, softpwm->left_duty);
            HAL_TIMER_Start_IT(left_timer);
            left_enable = true;
        }
        if (softpwm->right_duty && !right_enable)
        {
            HAL_TIMER_SetCount(right_timer, softpwm->right_duty);
            HAL_TIMER_Start_IT(right_timer);
            right_enable = true;
        }
#else
        if (softpwm->left_duty && !enable)
        {
            HAL_TIMER_SetCount(timer, softpwm->left_duty);
            HAL_TIMER_Start_IT(timer);
            HAL_DBG("Sound Start\n");
            enable = true;
        }
#endif
        HAL_DelayUs(1);
    }
}

int entry(void)
{
    return main();
}
