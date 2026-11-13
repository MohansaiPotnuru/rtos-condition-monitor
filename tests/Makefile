# Host unit tests for the hardware-independent library (lib/).
#   make -C tests                         # gcc/clang with ASan + UBSan
#   make -C tests SAN=                    # no sanitizers (e.g. Windows/zig)
#   make -C tests CC="zig cc"

CC     ?= cc
SAN    ?= -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer
CFLAGS ?= -std=c11 -O1 -g -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion -Werror
LDLIBS ?= -lm

LIB  := ../lib
SRCS := test_main.c test_codec.c test_signal.c test_system.c \
        $(LIB)/crc16.c $(LIB)/cobs.c $(LIB)/sigstats.c $(LIB)/alarm.c \
        $(LIB)/adc_conv.c $(LIB)/rta.c $(LIB)/health.c $(LIB)/cli.c $(LIB)/telemetry.c

BIN := build/unit_tests$(EXE)

.PHONY: all run clean
all: run

$(BIN): $(SRCS) unit.h $(wildcard $(LIB)/include/*.h)
	@mkdir -p build
	$(CC) $(CFLAGS) $(SAN) -I$(LIB)/include $(SRCS) -o $@ $(LDLIBS)

run: $(BIN)
	./$(BIN)

clean:
	rm -rf build
