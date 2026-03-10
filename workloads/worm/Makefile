# Makefile for WORM producer-consumer workload.
#
# Targets:
#   make          — cross-compile for RISC-V (gem5 target)
#   make native   — compile for host (sanity check)
#   make clean    — remove build artifacts

CC_RISCV = riscv64-linux-gnu-gcc
CC_NATIVE = gcc
CFLAGS = -O2 -Wall -Wextra -static -pthread
TARGET = worm_prodcon

.PHONY: all native clean

all: $(TARGET)

$(TARGET): main.c ../common/config.h ../common/arena.h ../common/workload.h
	$(CC_RISCV) $(CFLAGS) -o $@ main.c

native: main.c ../common/config.h ../common/arena.h ../common/workload.h
	$(CC_NATIVE) -O2 -Wall -Wextra -pthread -o $(TARGET)_native main.c

clean:
	rm -f $(TARGET) $(TARGET)_native *.o
