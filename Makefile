ARCH  := riscv64
CC    := riscv64-unknown-elf-gcc
OBJCOPY := riscv64-unknown-elf-objcopy
CFLAGS:= -march=rv64gc -mabi=lp64 -mcmodel=medany -nostdlib -nostartfiles -ffreestanding -O2 -Wall -Wextra -I ./include -I ./kernel
LDFLAGS := -T kernel.ld -nostdlib -static

OBJS := kernel/entry.o kernel/start.o kernel/trampoline.o kernel/main.o kernel/uart.o kernel/console.o kernel/printf.o kernel/string.o kernel/panic.o kernel/kalloc.o kernel/vm.o kernel/test.o

all: kernel.elf kernel.bin

kernel.elf: $(OBJS) kernel/kernel.ld
	$(CC) $(CFLAGS) -o $@ $(OBJS) -T kernel/kernel.ld -nostdlib -static

kernel.bin: kernel.elf
	$(OBJCOPY) -O binary $< $@

kernel/entry.o: kernel/entry.S
	$(CC) $(CFLAGS) -c -o $@ $<

kernel/trampoline.o: kernel/trampoline.S
	$(CC) $(CFLAGS) -c -o $@ $<

kernel/%.o: kernel/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

run: kernel.elf
	qemu-system-riscv64 -machine virt -nographic -serial mon:stdio -bios none -kernel kernel.elf

clean:
	rm -f kernel/*.o kernel.elf kernel.bin

.PHONY: all run clean
