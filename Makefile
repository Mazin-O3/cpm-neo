# CP/M Neo — builds sysgen and generates a disk image + bootloader for a
# given platform/arch.
#
# Usage:
#   make sysgen - build sysgen/build/sysgen (the disk/bootloader generator)
#   make disk   - build sysgen, then run "sysgen new" with DISK_SIZE/
#                 MEM_SIZE/PLATFORM/ARCH. Produces sysgen/build/disk.img
#                 and sysgen/build/core/int/bootloader.elf
#   make clean  - remove sysgen/build
#

SYSGEN_DIR := sysgen
SYSGEN_BIN := $(SYSGEN_DIR)/build/sysgen

DISK_SIZE ?= 128K
MEM_SIZE  ?= 32K
PLATFORM  ?= tinymcu
ARCH      ?= tinymcu-riscv32

.PHONY: sysgen disk clean

sysgen: $(SYSGEN_BIN)

$(SYSGEN_BIN):
	$(MAKE) -C $(SYSGEN_DIR)

disk: $(SYSGEN_BIN)
	./$(SYSGEN_BIN) new \
		--disk-size=$(DISK_SIZE) --mem=$(MEM_SIZE) \
		--no-sys --no-extra \
		--platform=$(PLATFORM) --arch=$(ARCH)

clean:
	rm -rf $(SYSGEN_DIR)/build
