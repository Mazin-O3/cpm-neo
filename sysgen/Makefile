# CP/M Neo SYSGEN — the project's only makefile.

CC      ?= cc
CFLAGS  ?= -Os -Wall -Wextra

INC_LOCAL := -Iinclude -I ../core/kernel -I ../core -I .. -iquote ../sdk/include
# Host bdos.c/disk.c: the ceiling config (sysgen/include/config.h) provides
# CONFIG_* to the host build, sizing the arrays to the maximum any platform
# may use.
INC_BDOS  := -I include -DSYSGEN_HOST -I ../core/kernel -I ../core -I ../sdk/include -I .. -Wno-builtin-declaration-mismatch

# cmd_new.o keeps INC_LOCAL so the host libc (not the sdk's freestanding
# headers) resolves <stdio.h>/<stdlib.h>/<string.h>.
INC_CMD   := $(INC_LOCAL) -DSYSGEN_HOST

BUILD_DIR := build
OBJ_DIR   := $(BUILD_DIR)/obj
BIN       := $(BUILD_DIR)/sysgen

OBJS := $(OBJ_DIR)/bios.o $(OBJ_DIR)/mkdisk.o \
        $(OBJ_DIR)/utils.o \
        $(OBJ_DIR)/cmd_common.o $(OBJ_DIR)/cmd_new.o \
        $(OBJ_DIR)/cmd_apps.o $(OBJ_DIR)/cmd_files.o \
        $(OBJ_DIR)/main.o $(OBJ_DIR)/bdos.o $(OBJ_DIR)/disk.o

CROSS_COMPILE ?= $(error CROSS_COMPILE is not set; set it in arch/<isa>/config.sh)
OBJDUMP = $(CROSS_COMPILE)objdump

.PHONY: all help debug check-map clean

all: $(BIN)

$(BIN): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

$(OBJ_DIR)/bios.o: src/bios.c include/sysgen.h ../core/kernel/bios.h ../sdk/include/abi.h
	@mkdir -p $(OBJ_DIR)
	$(CC) $(CFLAGS) $(INC_LOCAL) -c $< -o $@

$(OBJ_DIR)/mkdisk.o: src/mkdisk.c include/sysgen.h ../core/kernel/bdos.h ../core/kernel/disk_format.h include/config.h
	@mkdir -p $(OBJ_DIR)
	$(CC) $(CFLAGS) $(INC_LOCAL) -c $< -o $@

$(OBJ_DIR)/utils.o: src/utils.c include/utils.h include/sysgen.h ../core/kernel/bdos.h ../core/kernel/disk.h include/config.h
	@mkdir -p $(OBJ_DIR)
	$(CC) $(CFLAGS) $(INC_LOCAL) -c $< -o $@

$(OBJ_DIR)/cmd_common.o: src/cmd_common.c include/cmd.h include/utils.h include/sysgen.h ../core/kernel/bdos.h ../core/kernel/disk.h include/config.h
	@mkdir -p $(OBJ_DIR)
	$(CC) $(CFLAGS) $(INC_LOCAL) -c $< -o $@

$(OBJ_DIR)/cmd_new.o: src/cmd_new.c include/cmd.h include/commands.h include/utils.h include/sysgen.h ../core/kernel/bdos.h ../core/kernel/disk.h include/config.h
	@mkdir -p $(OBJ_DIR)
	$(CC) $(CFLAGS) $(INC_CMD) -c $< -o $@

$(OBJ_DIR)/cmd_apps.o: src/cmd_apps.c include/cmd.h include/utils.h include/sysgen.h ../core/kernel/bdos.h ../core/kernel/disk.h include/config.h
	@mkdir -p $(OBJ_DIR)
	$(CC) $(CFLAGS) $(INC_LOCAL) -c $< -o $@

$(OBJ_DIR)/cmd_files.o: src/cmd_files.c include/cmd.h include/commands.h include/utils.h include/sysgen.h ../core/kernel/bdos.h ../core/kernel/disk.h include/config.h
	@mkdir -p $(OBJ_DIR)
	$(CC) $(CFLAGS) $(INC_LOCAL) -c $< -o $@

$(OBJ_DIR)/main.o: src/main.c include/commands.h include/utils.h
	@mkdir -p $(OBJ_DIR)
	$(CC) $(CFLAGS) $(INC_LOCAL) -c $< -o $@

$(OBJ_DIR)/bdos.o: ../core/kernel/bdos.c ../core/kernel/bdos.h ../core/kernel/disk.h \
        ../sdk/include/abi.h ../core/kernel/bios.h include/config.h
	@mkdir -p $(OBJ_DIR)
	$(CC) $(CFLAGS) $(INC_BDOS) -c $< -o $@

$(OBJ_DIR)/disk.o: ../core/kernel/disk.c ../core/kernel/disk.h ../core/kernel/disk_format.h \
        ../sdk/include/abi.h ../core/kernel/bios.h include/config.h
	@mkdir -p $(OBJ_DIR)
	$(CC) $(CFLAGS) $(INC_BDOS) -c $< -o $@

help:
	@echo "CP/M Neo SYSGEN — project makefile"

debug:
	@[ -f $(BUILD_DIR)/core/int/bootloader.elf ] || { echo "run 'sysgen new ...' first" >&2; exit 1; }
	$(OBJDUMP) -S $(BUILD_DIR)/core/int/bootloader.elf > $(BUILD_DIR)/bootloader.lst
	$(OBJDUMP) -S $(BUILD_DIR)/core/int/kernel.elf     > $(BUILD_DIR)/kernel.lst
	$(OBJDUMP) -S $(BUILD_DIR)/core/int/ccp.elf       > $(BUILD_DIR)/ccp.lst
	@echo "  Listings generated in $(BUILD_DIR)/"

check-map:
	@[ -f $(BUILD_DIR)/core/int/kernel.elf ] || { echo "run 'sysgen new ...' first" >&2; exit 1; }
	@$(OBJDUMP) -t $(BUILD_DIR)/core/int/kernel.elf | awk \
	  '/(_text_start|_text_end|_kend|__kernel_total|_stack_top)$$/ \
	  { printf "  %-16s : 0x%s\n", $$NF, $$1 }' | sort -k3

clean:
	rm -rf $(BUILD_DIR)