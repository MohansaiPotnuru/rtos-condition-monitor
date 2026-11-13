/* STM32F401RE: 512 KiB flash, 96 KiB SRAM.
 * The same layout also boots on QEMU's netduinoplus2 (STM32F405: 1 MiB / 128 KiB). */

ENTRY(Reset_Handler)

MEMORY
{
    FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 512K
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 96K
}

/* Main stack: used only before the scheduler starts and by ISRs afterwards. */
_Min_Stack_Size = 0x800;
_estack = ORIGIN(RAM) + LENGTH(RAM);

SECTIONS
{
    .isr_vector :
    {
        . = ALIGN(4);
        KEEP(*(.isr_vector))
        . = ALIGN(4);
    } > FLASH

    .text :
    {
        . = ALIGN(4);
        *(.text)
        *(.text*)
        *(.glue_7)
        *(.glue_7t)
        *(.eh_frame)
        KEEP(*(.init))
        KEEP(*(.fini))
        . = ALIGN(4);
        _etext = .;
    } > FLASH

    .rodata :
    {
        . = ALIGN(4);
        *(.rodata)
        *(.rodata*)
        . = ALIGN(4);
    } > FLASH

    .ARM.extab : { *(.ARM.extab* .gnu.linkonce.armextab.*) } > FLASH
    .ARM.exidx :
    {
        __exidx_start = .;
        *(.ARM.exidx*)
        __exidx_end = .;
    } > FLASH

    _sidata = LOADADDR(.data);

    .data :
    {
        . = ALIGN(4);
        _sdata = .;
        *(.data)
        *(.data*)
        . = ALIGN(4);
        _edata = .;
    } > RAM AT > FLASH

    .bss (NOLOAD) :
    {
        . = ALIGN(4);
        _sbss = .;
        __bss_start__ = _sbss;
        *(.bss)
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
        __bss_end__ = _ebss;
    } > RAM

    /* Survives resets (not zeroed): fault records and the boot counter. */
    .noinit (NOLOAD) :
    {
        . = ALIGN(4);
        *(.noinit)
        *(.noinit*)
        . = ALIGN(4);
    } > RAM

    /* newlib's _sbrk() grows from here; nothing in this firmware allocates. */
    . = ALIGN(8);
    PROVIDE(end = .);
    PROVIDE(_end = .);

    ._main_stack (NOLOAD) :
    {
        . = ALIGN(8);
        . = . + _Min_Stack_Size;
        . = ALIGN(8);
    } > RAM

    /* Guard: fail the link if static data + main stack do not fit in RAM. */
    ASSERT(. <= ORIGIN(RAM) + LENGTH(RAM), "RAM overflow: static data and main stack exceed 96 KiB")

    .ARM.attributes 0 : { *(.ARM.attributes) }
}
