ARCH     := riscv64
CC       := riscv64-unknown-elf-gcc
OBJCOPY  := riscv64-unknown-elf-objcopy
SRC_DIR  := kernel
BUILD_DIR:= build
OBJ_DIR  := $(BUILD_DIR)/obj

CFLAGS  := -march=rv64gc -mabi=lp64 -mcmodel=medany -nostdlib -nostartfiles -ffreestanding -O0 -g -Wall -Wextra -I ./include -I ./kernel
LDFLAGS := -nostdlib -static

C_SRCS := \
  boot/start.c \
  core/main.c \
  core/panic.c \
  core/test.c \
  drivers/console.c \
  drivers/uart.c \
  fs/bio.c \
  fs/log.c \
  fs/fs.c \
  fs/file.c \
  proc/proc.c \
  proc/sysproc.c \
  lib/printf.c \
  lib/string.c \
  mm/kalloc.c \
  mm/vm.c \
  trap/trap.c

S_SRCS := \
  boot/entry.S \
  proc/swtch.S \
  trap/kernelvec.S \
  trap/trampoline.S

C_OBJS := $(addprefix $(OBJ_DIR)/,$(C_SRCS:.c=.o))
S_OBJS := $(addprefix $(OBJ_DIR)/,$(S_SRCS:.S=.o))
OBJS   := $(C_OBJS) $(S_OBJS)

all: $(BUILD_DIR)/kernel.elf $(BUILD_DIR)/kernel.bin

$(BUILD_DIR)/kernel.elf: $(OBJS) $(SRC_DIR)/kernel.ld | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $(OBJS) -T $(SRC_DIR)/kernel.ld $(LDFLAGS)

$(BUILD_DIR)/kernel.bin: $(BUILD_DIR)/kernel.elf | $(BUILD_DIR)
	$(OBJCOPY) -O binary $< $@

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.S
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR):
	mkdir -p $@

clean:
	rm -rf $(BUILD_DIR)

run: $(BUILD_DIR)/kernel.elf
	qemu-system-riscv64 -machine virt -nographic -serial mon:stdio -bios none -kernel $<

.PHONY: all clean run
