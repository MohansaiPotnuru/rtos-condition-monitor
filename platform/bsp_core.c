/**
 * @file    bsp_core.c
 * @brief   Clock tree, reset cause, microsecond timebase, IWDG, LED and button.
 *          Register-level CMSIS only, no HAL.
 */
#include "bsp.h"

#include "stm32f4xx.h"

uint32_t SystemCoreClock = 16000000u; /* HSI after reset */

static bool              s_clock_fallback;
static bsp_reset_cause_t s_reset_cause = RESET_UNKNOWN;
static bool              s_reset_read;

/* Bounded wait: a stuck oscillator must never hang the boot. */
__attribute__((unused)) static bool wait_set(volatile uint32_t *reg, uint32_t mask, uint32_t want)
{
    for (uint32_t i = 0; i < 100000u; i++) {
        if ((*reg & mask) == want) {
            return true;
        }
    }
    return false;
}

/* ------------------------------------------------------------------ clock -- */

void bsp_clock_init(void)
{
#if defined(TARGET_QEMU)
    /* netduinoplus2 runs its SysTick from a fixed 168 MHz system clock and
     * does not model RCC, so there is nothing to program. */
    SystemCoreClock = 168000000u;
#else
    /* Voltage scale 2 is sufficient up to 84 MHz on the F401. */
    RCC->APB1ENR |= RCC_APB1ENR_PWREN;
    (void)RCC->APB1ENR;
    PWR->CR = (PWR->CR & ~PWR_CR_VOS) | PWR_CR_VOS_1;

    RCC->CR |= RCC_CR_HSION;
    if (!wait_set(&RCC->CR, RCC_CR_HSIRDY, RCC_CR_HSIRDY)) {
        s_clock_fallback = true;
        return;
    }

    /* HSI 16 MHz / M=16 -> 1 MHz VCO input; x N=336 -> 336 MHz VCO;
     * / P=4 -> 84 MHz SYSCLK; / Q=7 -> 48 MHz for USB OTG FS. */
    RCC->CR &= ~RCC_CR_PLLON;
    (void)wait_set(&RCC->CR, RCC_CR_PLLRDY, 0u);
    RCC->PLLCFGR = (16u << RCC_PLLCFGR_PLLM_Pos) | (336u << RCC_PLLCFGR_PLLN_Pos) |
                   (((4u / 2u) - 1u) << RCC_PLLCFGR_PLLP_Pos) | RCC_PLLCFGR_PLLSRC_HSI |
                   (7u << RCC_PLLCFGR_PLLQ_Pos);
    RCC->CR |= RCC_CR_PLLON;
    if (!wait_set(&RCC->CR, RCC_CR_PLLRDY, RCC_CR_PLLRDY)) {
        s_clock_fallback = true; /* stay on HSI, everything still works, just slower */
        return;
    }

    /* 2 wait states at 84 MHz / 3.3 V, with prefetch and both caches. */
    FLASH->ACR = FLASH_ACR_PRFTEN | FLASH_ACR_ICEN | FLASH_ACR_DCEN | FLASH_ACR_LATENCY_2WS;
    if (!wait_set(&FLASH->ACR, FLASH_ACR_LATENCY, FLASH_ACR_LATENCY_2WS)) {
        s_clock_fallback = true;
        return;
    }

    /* AHB 84 MHz, APB1 42 MHz (max 42), APB2 84 MHz. APB1 timers run at 84 MHz. */
    RCC->CFGR = (RCC->CFGR & ~(RCC_CFGR_HPRE | RCC_CFGR_PPRE1 | RCC_CFGR_PPRE2)) | RCC_CFGR_PPRE1_DIV2;
    RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW) | RCC_CFGR_SW_PLL;
    if (!wait_set(&RCC->CFGR, RCC_CFGR_SWS, RCC_CFGR_SWS_PLL)) {
        s_clock_fallback = true;
        return;
    }
    SystemCoreClock = 84000000u;
#endif
}

bool bsp_clock_fallback(void)
{
    return s_clock_fallback;
}

static uint32_t apb1_hz(void)
{
    return (SystemCoreClock >= 84000000u) ? SystemCoreClock / 2u : SystemCoreClock;
}

__attribute__((unused)) static uint32_t apb1_timer_hz(void)
{
    /* Timers on a divided APB run at twice the bus clock. */
    return (apb1_hz() != SystemCoreClock) ? apb1_hz() * 2u : apb1_hz();
}

/* -------------------------------------------------------------- reset cause */

bsp_reset_cause_t bsp_reset_cause(void)
{
    if (s_reset_read) {
        return s_reset_cause;
    }
    s_reset_read = true;
#if defined(TARGET_QEMU)
    s_reset_cause = RESET_POWER_ON;
#else
    const uint32_t csr = RCC->CSR;
    /* Order matters: a pin reset is also flagged on every other reset. */
    if (csr & RCC_CSR_LPWRRSTF) {
        s_reset_cause = RESET_LOW_POWER;
    } else if (csr & RCC_CSR_WWDGRSTF) {
        s_reset_cause = RESET_WWDG;
    } else if (csr & RCC_CSR_IWDGRSTF) {
        s_reset_cause = RESET_IWDG;
    } else if (csr & RCC_CSR_SFTRSTF) {
        s_reset_cause = RESET_SOFTWARE;
    } else if (csr & RCC_CSR_PORRSTF) {
        s_reset_cause = RESET_POWER_ON;
    } else if (csr & RCC_CSR_BORRSTF) {
        s_reset_cause = RESET_BROWN_OUT;
    } else if (csr & RCC_CSR_PINRSTF) {
        s_reset_cause = RESET_PIN;
    }
    RCC->CSR |= RCC_CSR_RMVF;
#endif
    return s_reset_cause;
}

const char *bsp_reset_cause_str(bsp_reset_cause_t c)
{
    static const char *const names[] = {"power-on", "reset pin", "software", "watchdog (IWDG)",
                                        "window watchdog", "low-power", "brown-out", "unknown"};
    return ((unsigned)c < sizeof names / sizeof names[0]) ? names[c] : "unknown";
}

void bsp_system_reset(void)
{
    NVIC_SystemReset();
}

/* ---------------------------------------------------------------- timebase */

#if defined(TARGET_QEMU)
#define TIM2_INPUT_HZ 1000000000u /* QEMU's STM32F2xx timer model counts at 1 GHz */
#else
#define TIM2_INPUT_HZ apb1_timer_hz()
#endif

void bsp_timebase_init(void)
{
    static bool done;
    if (done) {
        return; /* also called by the kernel via portCONFIGURE_TIMER_FOR_RUN_TIME_STATS */
    }
    done = true;

    RCC->APB1ENR |= RCC_APB1ENR_TIM2EN;
    (void)RCC->APB1ENR;
    TIM2->CR1 = 0u;
    TIM2->PSC = (TIM2_INPUT_HZ / 1000000u) - 1u; /* 1 MHz: one count per microsecond */
    TIM2->ARR = 0xFFFFFFFFu;                    /* TIM2 is 32-bit: wraps after 71.6 min */
    TIM2->CNT = 0u;
    TIM2->EGR = TIM_EGR_UG;                      /* load PSC now, not at the first overflow */
    TIM2->SR  = 0u;
    TIM2->CR1 = TIM_CR1_CEN;
}

uint32_t bsp_time_us(void)
{
    return TIM2->CNT;
}

/* -------------------------------------------------------------- watchdog -- */

void bsp_iwdg_start(uint32_t timeout_ms)
{
#if defined(TARGET_QEMU)
    (void)timeout_ms;
#else
    /* Freeze the watchdog while a debugger halts the core. */
    DBGMCU->APB1FZ |= DBGMCU_APB1_FZ_DBG_IWDG_STOP;

    /* LSI ~32 kHz / 64 = 500 Hz -> 2 ms per count, 12-bit reload (max ~8 s). */
    uint32_t reload = timeout_ms / 2u;
    if (reload < 1u) {
        reload = 1u;
    } else if (reload > 0xFFFu) {
        reload = 0xFFFu;
    }
    IWDG->KR  = 0xCCCCu; /* start (also starts LSI) */
    IWDG->KR  = 0x5555u; /* unlock PR/RLR */
    IWDG->PR  = 4u;      /* /64 */
    IWDG->RLR = reload;
    (void)wait_set(&IWDG->SR, IWDG_SR_PVU | IWDG_SR_RVU, 0u);
    IWDG->KR = 0xAAAAu;
#endif
}

void bsp_iwdg_kick(void)
{
#if !defined(TARGET_QEMU)
    IWDG->KR = 0xAAAAu;
#endif
}

/* ------------------------------------------------------------ LED, button -- */

void bsp_led_init(void)
{
#if !defined(TARGET_QEMU)
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
    (void)RCC->AHB1ENR;
    GPIOA->MODER = (GPIOA->MODER & ~GPIO_MODER_MODER5) | GPIO_MODER_MODER5_0; /* PA5 output */
    GPIOA->OTYPER &= ~GPIO_OTYPER_OT5;
    GPIOA->OSPEEDR &= ~GPIO_OSPEEDR_OSPEED5;
    GPIOA->BSRR = GPIO_BSRR_BR5;
#endif
}

void bsp_led_set(bool on)
{
#if !defined(TARGET_QEMU)
    GPIOA->BSRR = on ? GPIO_BSRR_BS5 : GPIO_BSRR_BR5; /* atomic: no read-modify-write */
#else
    (void)on;
#endif
}

static void (*s_on_press)(void);
static uint32_t s_last_press_us;

void bsp_button_init(void (*on_press)(void))
{
    s_on_press = on_press;
#if !defined(TARGET_QEMU)
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOCEN;
    RCC->APB2ENR |= RCC_APB2ENR_SYSCFGEN;
    (void)RCC->APB2ENR;
    GPIOC->MODER &= ~GPIO_MODER_MODER13;                /* PC13 input (external pull-up on board) */
    SYSCFG->EXTICR[3] = (SYSCFG->EXTICR[3] & ~SYSCFG_EXTICR4_EXTI13) | SYSCFG_EXTICR4_EXTI13_PC;
    EXTI->FTSR |= EXTI_FTSR_TR13;                       /* B1 is active low: falling edge */
    EXTI->RTSR &= ~EXTI_RTSR_TR13;
    EXTI->PR = EXTI_PR_PR13;
    EXTI->IMR |= EXTI_IMR_MR13;
    NVIC_SetPriority(EXTI15_10_IRQn, BSP_IRQ_PRIO_BUTTON);
    NVIC_EnableIRQ(EXTI15_10_IRQn);
#endif
}

void EXTI15_10_IRQHandler(void)
{
    if ((EXTI->PR & EXTI_PR_PR13) == 0u) {
        return;
    }
    EXTI->PR = EXTI_PR_PR13; /* write 1 to clear */

    /* Time-based debounce: contact bounce lasts a few ms, presses are >100 ms apart. */
    const uint32_t now = bsp_time_us();
    if ((now - s_last_press_us) < 50000u) {
        return;
    }
    s_last_press_us = now;
    if (s_on_press != NULL) {
        s_on_press();
    }
}

/* ------------------------------------------------------- exported helpers -- */

uint32_t bsp_apb1_hz(void);
uint32_t bsp_apb1_hz(void)
{
#if defined(TARGET_QEMU)
    return 42000000u; /* baud rate is irrelevant in QEMU */
#else
    return apb1_hz();
#endif
}
