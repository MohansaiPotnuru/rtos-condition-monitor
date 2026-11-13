/**
 * @file    startup.c
 * @brief   Reset handler and vector table for STM32F401xE, in C.
 *
 * No vendor assembly startup: the vector table is a const array placed in
 * .isr_vector, unused IRQs land in Default_Handler (which records a fault
 * instead of spinning silently), and the FPU is enabled before any C code
 * that might use it runs.
 */
#include <stdint.h>
#include <string.h>

#include "fault.h"
#include "stm32f4xx.h"

extern uint32_t _estack, _sidata, _sdata, _edata, _sbss, _ebss;
extern int      main(void);

void Reset_Handler(void);
void Default_Handler(void);
void HardFault_Handler(void);
void fault_hardfault_c(uint32_t *frame, uint32_t exc_return);

/* Kernel handlers (names mapped in FreeRTOSConfig.h). */
void SVC_Handler(void);
void PendSV_Handler(void);
void SysTick_Handler(void);

/* Peripheral handlers implemented by the BSP. */
void USART2_IRQHandler(void);
void EXTI15_10_IRQHandler(void);

void NMI_Handler(void) __attribute__((weak, alias("Default_Handler")));
void MemManage_Handler(void) __attribute__((weak, alias("HardFault_Handler")));
void BusFault_Handler(void) __attribute__((weak, alias("HardFault_Handler")));
void UsageFault_Handler(void) __attribute__((weak, alias("HardFault_Handler")));
void DebugMon_Handler(void) __attribute__((weak, alias("Default_Handler")));

#define IRQ(n) (16 + (n))
typedef void (*vector_t)(void);

/* Every slot defaults to Default_Handler (GCC range initializer), then the
 * used ones are overridden, so no vector can ever be NULL. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Woverride-init"
#pragma GCC diagnostic ignored "-Wpedantic"
__attribute__((section(".isr_vector"), used)) const vector_t g_vectors[16 + 85] = {
    [1 ... 16 + 84] = Default_Handler,
    [0]  = (vector_t)(uintptr_t)&_estack,
    [1]  = Reset_Handler,
    [2]  = NMI_Handler,
    [3]  = HardFault_Handler,
    [4]  = MemManage_Handler,
    [5]  = BusFault_Handler,
    [6]  = UsageFault_Handler,
    [11] = SVC_Handler,
    [12] = DebugMon_Handler,
    [14] = PendSV_Handler,
    [15] = SysTick_Handler,
    [IRQ(USART2_IRQn)]    = USART2_IRQHandler,
    [IRQ(EXTI15_10_IRQn)] = EXTI15_10_IRQHandler,
};
#pragma GCC diagnostic pop

_Static_assert(sizeof(g_vectors) / sizeof(g_vectors[0]) == 16 + 85, "F401 has 85 IRQs");

__attribute__((noreturn)) void Reset_Handler(void)
{
    /* .data from flash, .bss zeroed. Sizes come from linker-symbol addresses
     * (distinct objects as far as C is concerned, so compare integers). */
    const size_t data_words = ((uintptr_t)&_edata - (uintptr_t)&_sdata) / sizeof(uint32_t);
    const size_t bss_words  = ((uintptr_t)&_ebss - (uintptr_t)&_sbss) / sizeof(uint32_t);
    const uint32_t *src = &_sidata;
    uint32_t       *dst = &_sdata;
    for (size_t i = 0; i < data_words; i++) {
        dst[i] = src[i];
    }
    dst = &_sbss;
    for (size_t i = 0; i < bss_words; i++) {
        dst[i] = 0u;
    }

    /* Full access to CP10/CP11 (FPU) before any floating-point code. */
    SCB->CPACR |= (0xFu << 20);
    __DSB();
    __ISB();

    /* Vector table in flash; enable precise fault reporting. */
    SCB->VTOR = (uint32_t)(uintptr_t)g_vectors;
    SCB->SHCSR |= SCB_SHCSR_USGFAULTENA_Msk | SCB_SHCSR_BUSFAULTENA_Msk | SCB_SHCSR_MEMFAULTENA_Msk;
    SCB->CCR |= SCB_CCR_DIV_0_TRP_Msk;

    (void)main();
    for (;;) {
    }
}

void Default_Handler(void)
{
    /* An interrupt fired with no handler: report the active vector. */
    const uint32_t vect = SCB->ICSR & SCB_ICSR_VECTACTIVE_Msk;
    fault_fatal(FAULT_HARDFAULT, "unhandled IRQ", vect);
}

/* Pick the stack that was active when the fault hit (MSP or PSP) and hand
 * the exception frame to C. Naked: no prologue may touch the stack first. */
__attribute__((naked)) void HardFault_Handler(void)
{
    __asm volatile(
        "tst   lr, #4        \n"
        "ite   eq            \n"
        "mrseq r0, msp       \n"
        "mrsne r0, psp       \n"
        "mov   r1, lr        \n"
        "b     fault_hardfault_c \n");
}
