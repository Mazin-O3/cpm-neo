# CP/M Neo — builds sysgen and generates a disk image + bootloader for
# the TinyMCU platform (platform/tinymcu/config.sh).
#
# Usage:
#   make sysgen - build sysgen/build/sysgen (the disk/bootloader generator)
#   make disk   - build sysgen, then run "sysgen new" for PLATFORM (XIP=1
#                 to additionally pass --xip). Produces sysgen/build/disk.img,
#                 sysgen/build/bootloader.bin and sysgen/build/core/int/{kernel,ccp}.bin
#   make clean  - remove sysgen/build
#
# See README.md's "Quick Start" for the underlying sysgen invocation this
# wraps, and platform/tinymcu/config.sh for every TinyMCU-specific value
# (RAM/IO base, disk size, ...) sysgen reads instead of a --mem/--disk-size
# command line flag.

SYSGEN_DIR := sysgen
SYSGEN_BIN := $(SYSGEN_DIR)/build/sysgen

PLATFORM ?= tinymcu
XIP      ?= 0

ifeq ($(XIP),1)
XIP_FLAG := --xip
else
XIP_FLAG :=
endif

.PHONY: sysgen disk clean

sysgen: $(SYSGEN_BIN)

$(SYSGEN_BIN):
	$(MAKE) -C $(SYSGEN_DIR)

disk: $(SYSGEN_BIN)
	./$(SYSGEN_BIN) new --platform=$(PLATFORM) $(XIP_FLAG)

clean:
	rm -rf $(SYSGEN_DIR)/build
