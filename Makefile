# rtos-condition-monitor firmware
#
#   make                    # NUCLEO-F401RE  -> build/nucleo/rtos-condition-monitor.{elf,bin,hex,map}
#   make TARGET=qemu        # QEMU netduinoplus2 build used by the CI smoke test
#   make qemu-run           # build and run under QEMU (UART2 on stdio)
#   make test               # host unit tests (lib/), ASan + UBSan
#   make flash              # st-flash / STM32_Programmer_CLI, if installed
#
# Toolchain: arm-none-eabi-gcc 10+ (CI uses Ubuntu's 13.2; locally tested with GNU Tools for STM32 14.3).

TARGET ?= nucleo
PREFIX ?= arm-none-eabi-
CC      := $(PREFIX)gcc
OBJCOPY := $(PREFIX)objcopy
SIZE    := $(PREFIX)size

NAME   := rtos-condition-monitor
BUILD  := build/$(TARGET)
KERNEL := third_party/FreeRTOS-Kernel

ifeq ($(TARGET),qemu)
  TDEFS := -DTARGET_QEMU
else ifeq ($(TARGET),nucleo)
  TDEFS := -DTARGET_NUCLEO
else
  $(error TARGET must be nucleo or qemu)
endif

SRCS := \
  platform/startup.c platform/bsp_core.c platform/bsp_uart.c platform/bsp_adc.c platform/fault.c \
  app/main.c app/app_tasks.c app/app_cli.c \
  lib/crc16.c lib/cobs.c lib/sigstats.c lib/alarm.c lib/adc_conv.c lib/rta.c lib/health.c lib/cli.c lib/telemetry.c \
  $(KERNEL)/tasks.c $(KERNEL)/queue.c $(KERNEL)/list.c $(KERNEL)/timers.c \
  $(KERNEL)/event_groups.c $(KERNEL)/stream_buffer.c \
  $(KERNEL)/portable/GCC/ARM_CM4F/port.c

INCS := -Iconfig -Iapp -Iplatform/include -Ilib/include \
        -I$(KERNEL)/include -I$(KERNEL)/portable/GCC/ARM_CM4F \
        -Ivendor/cmsis/core -Ivendor/cmsis/device

ARCH  := -mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16
DEFS  := -DSTM32F401xE $(TDEFS)
WARN  := -Wall -Wextra -Wshadow -Wdouble-promotion -Wformat=2 -Wundef -Wno-unused-parameter
CFLAGS := $(ARCH) $(DEFS) $(INCS) -std=gnu11 -Os -g3 -ffunction-sections -fdata-sections \
          -fno-common -fstack-usage $(WARN) -MMD -MP
# Our code is held to -Werror; the vendored kernel is compiled with warnings only.
OWN_CFLAGS := -Werror
LDFLAGS := $(ARCH) -Tplatform/stm32f401re.ld -nostartfiles --specs=nano.specs --specs=nosys.specs \
           -Wl,--gc-sections -Wl,-Map=$(BUILD)/$(NAME).map -Wl,--print-memory-usage -Wl,--no-warn-rwx-segments

OBJS := $(SRCS:%.c=$(BUILD)/obj/%.o)
ELF  := $(BUILD)/$(NAME).elf

.PHONY: all clean test qemu-run flash size
all: $(ELF) $(BUILD)/$(NAME).bin $(BUILD)/$(NAME).hex size

$(BUILD)/obj/$(KERNEL)/%.o: $(KERNEL)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/obj/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(OWN_CFLAGS) -c $< -o $@

$(ELF): $(OBJS) platform/stm32f401re.ld
	$(CC) $(LDFLAGS) $(OBJS) -o $@

$(BUILD)/$(NAME).bin: $(ELF)
	$(OBJCOPY) -O binary $< $@

$(BUILD)/$(NAME).hex: $(ELF)
	$(OBJCOPY) -O ihex $< $@

size: $(ELF)
	$(SIZE) $<

test:
	$(MAKE) -C tests

qemu-run:
	$(MAKE) TARGET=qemu
	qemu-system-arm -M netduinoplus2 -display none -serial null -serial stdio -monitor none \
	  -kernel build/qemu/$(NAME).elf

flash: all
	st-flash --reset write $(BUILD)/$(NAME).bin 0x08000000 || \
	STM32_Programmer_CLI -c port=SWD -w $(BUILD)/$(NAME).hex -v -rst

clean:
	rm -rf build tests/build

-include $(OBJS:.o=.d)
