# Developer Guide

← [README](../README.md)

This page covers the SDK, building your own programs, and adding a new hardware
platform.

### `sysgen install`: Compile and install a source file or folder

Put your program's sources in a folder (or use a single source file) and let
`sysgen` compile and install it:

```sh
$ ./sysgen/build/sysgen install myapp --dst=A0 --attr=RW
```

- `install` accepts `.c`, `.s`, and `.S` sources only.

### `sysgen add`: Copy any file

`add` copies any file into the image named by its 8.3 basename.

```sh
$ ./sysgen/build/sysgen add hello.txt --dst=A0 --attr=RW
```

### SDK surface

- `cpm.h`: umbrella header (syscalls + kernel ABI types).
- `syscall.h`: plain `sys_open`/`sys_read`/… declarations — the kernel
  functions themselves, called directly (see
  [Syscall Reference](syscall-reference.md)).
- `abi.h`: the user-facing ABI: `SysInfo`, `FsContext`, `VolStat`,
  env slot layout, `FD_*` handles, `DISK_SECTOR_SIZE` (sourced from
  `disk_format.h`). Tunable parameters come from `config.h`; on-disk layout
  constants in `core/kernel/disk_format.h`.
- `core/kernel/bios.h`: BIOS interface (see below).

## The BIOS layer

| Function | Role |
|----------|------|
| `int bios_init(void)` | Initialize hardware; 0 on success |
| `void bios_conout(int c)` | Write a character to the console |
| `int bios_conin(void)` | Blocking console read |
| `int bios_constat(void)` | Console status (0xFF = key ready) |
| `void bios_consize(uint8_t *cw, uint8_t *ch)` | Console dimensions |
| `int bios_read(uint16_t sec, uint8_t *buf)` | Read one 512-byte sector |
| `int bios_write(uint16_t sec, const uint8_t *buf)` | Write one sector |
| `uint32_t bios_time(void)` | platform-defined time service |

## Configuring the system

Resource usage is tuned per-platform in `platform/<ID>/config.sh`, where every
parameter carries the `CONFIG_` prefix and all four software knobs are
required (there are no defaults).  `build_disk.sh` reads them and writes the
effective values to `build/gen/config.h`, which every kernel/CCP/SDK/app
compile includes.  The sysgen host tool is compiled once and sizes its arrays
to the *ceilings* in `sysgen/include/config.h` (16 volumes / 32 MB per volume
/ 8 FCBs — the maximum any platform may declare); it applies each platform's
active values at runtime and rejects a platform that exceeds them.

| Knob | Default | Kernel RAM cost (roughly) |
|------|---------|---------------------------|
| `CONFIG_VOL_MAX` | 4 | `MAX_VOLUMES`-backed arrays and `SysInfo.vol_mounted[]` |
| `CONFIG_DISK_SIZE` | 2048 | alloc bitmap bytes per volume (KB/8); block cap `BD_VOL_MAX_BLOCKS` = KB |
| `CONFIG_FCB_MAX` | 4 | `CONFIG_FCB_MAX` open-file control blocks |
| `CONFIG_STACK_SIZE` | 0x1000 | single shared kernel/CCP/app stack (top of TPA) |

`CONFIG_VOL_MAX` and `CONFIG_DISK_SIZE` (the per-volume block cap) are also
on-disk *format* parameters, so changing them must be paired with a fresh
image: `sysgen new --platform=<name>`. `CONFIG_STACK_SIZE` is consumed by the
linker (linker scripts cannot include C headers); `build_disk.sh` passes the
platform's value to the kernel links as `--defsym=__stack_size`, with the
`PROVIDE` in `linker_kernel_common.ld` serving as a hand-link fallback only.

A small-RAM port (for example a 32 KB ROM / ~2.5 KB SRAM target) shrinks
the data footprint by dropping the disk layer's big arrays: `CONFIG_FCB_MAX 2`,
`CONFIG_VOL_MAX 2`, `CONFIG_DISK_SIZE 512`, and a tighter
`CONFIG_STACK_SIZE`. The v1 pattern is to keep one tuned configuration
checked in per port under `platform/<name>/config.sh`; `sysgen/include/config.h`
holds the host ceilings a platform must stay within.

## Adding a platform

A platform is a self-contained `platform/<name>/` directory:

1. `config.sh` declares the platform facts:
   - `CONFIG_ID` — the 8-char max platform id, required and stamped into sector 0
     (`S0_PLATFORM`). e.g. `platform/blackpill-411fe/` with
     `CONFIG_ID="BPF411E"`.
   - `CONFIG_ARCH` — the ISA directory under `arch/` (selects the toolchain)
   - `CONFIG_RAM_SIZE` — total RAM in bytes (hex), e.g. `0x10000` = 64 KB
   - `CONFIG_RAM_BASE` — base address of the RAM region holding CP/M Neo
   - `CONFIG_IO_BASE` — base address of the peripheral MMIO window
   - `CONFIG_XIP_BASE` — consulted only when the build passes `--xip` (see below);
      there is no configured window size. Under `--xip`, `CONFIG_XIP_BASE` is the
      base of the execute-in-place window, which extends over the disk image
      itself (see [Architecture](architecture.md#execute-in-place-xip)); the
      kernel/CCP run in place from it and whether they fit the produced disk
      is validated at build time.
   - the four software knobs `CONFIG_VOL_MAX`, `CONFIG_DISK_SIZE`,
     `CONFIG_FCB_MAX`, `CONFIG_STACK_SIZE` (all required, no defaults) — see
     [Configuring the system](#configuring-the-system).
2. `bios.c` implements the functions in `bios.h`.
3. Build with `sysgen new ... --platform=<id> [--xip]` — `--xip` selects an
   XIP disk; omit it for a plain (RAM-loading) disk. The flag alone selects
   the mode: a plain build ignores `CONFIG_XIP_BASE` entirely.

### Platform lookup

`--platform` addresses a platform purely by its `CONFIG_ID`:

- `build_disk.sh` scans every `platform/*/config.sh` and a platform matches
  when its `CONFIG_ID` equals the argument;
- a `CONFIG_ID` declared by more than one platform is an error
  (`duplicate platform ID ...`);
- an unmatched id fails with `unknown platform '<id>'`.

### The BIOS contract

Each platform implements the functions declared in `core/kernel/bios.h`
(console: `bios_conout`, `bios_conin`, `bios_constat`, `bios_consize`,
`bios_init`; storage: `bios_read`, `bios_write`, `bios_sync`; time:
`bios_time`) directly in `bios.c`.

Storage semantics follow a write-back contract:
`bios_write` only *accepts* a sector (the platform may cache it); `bios_sync` is
the persistence barrier that commits all previously accepted writes to durable
storage and must return success only once they are durable. `bios_read` must
observe all prior successful writes (read-after-write). The disk layer and
`SYNC` command drive this chain via `bd_sync` -> `disk_sync` -> `bios_sync`.

A platform that supports several storage devices can select one at build time
inside the storage functions:

```c
int bios_read(uint16_t sec, uint8_t *buf)
{
#ifdef USE_SDCARD
    return sdcard_read(sec, buf);
#else
    return disk_read(sec, buf);
#endif
}
```

Driver code may be organized within `bios.c` however the platform likes. See
[Architecture](architecture.md) for the boot and build flow.

## Adding an architecture

An architecture is a self-contained `arch/<isa>/` directory. The build scripts
source `arch/<isa>/config.sh` automatically.

The `arch/<isa>/` directory needs four files:

| File | Purpose |
| --- | --- |
| `config.sh` | Toolchain metadata for this ISA |
| `boot.S` | Architecture bootloader (loads the kernel and jumps to it) |
| `linker_boot.ld` | Bootloader memory layout (first 1 KB of RAM) |
| `crt0.S` | C runtime startup (kernel, CCP, and apps): sets the stack pointer to the top of the shared stack, clears `.bss`, and jumps to `_start` |

### `config.sh` contract

`config.sh` is sourced by `build_disk.sh` and `app_build.sh`. It must set:

| Variable | Meaning |
| --- | --- |
| `CONFIG_CROSS_COMPILE` | Cross-compiler prefix, e.g. `riscv64-unknown-elf-`. Required; the build fails if unset |
| `CONFIG_ARCH_CFLAGS` | `-march`/`-mabi` flags for the target, e.g. `-march=rv32im -mabi=ilp32`. Must include the ISA's code-model flag (RISC-V: `-mcmodel=medany`); every binary runs at its fixed link origin — the kernel and CCP in place from flash on XIP disks, apps from the TPA |
| `CONFIG_LD_EMULATION` | Linker emulation for the target, e.g. `elf32lriscv` |
| `CONFIG_BOOT_BASE` | Address where the bootloader is placed and executed (reset vector origin) |
| `CONFIG_BOOT_SIZE` | Maximum bootloader code image bytes. Bounds the boot code `MEMORY` region and the `build_disk.sh` size check |
| `CONFIG_BOOT_RAM_SIZE` | Boot runtime RAM bytes (scratch buffer + stack + bios `.bss`). Forms the `BRAM` region at `CONFIG_RAM_BASE + CONFIG_BOOT_SIZE` |

The RISC-V example (`arch/riscv32/config.sh`):

```sh
CONFIG_CROSS_COMPILE=riscv64-unknown-elf-
CONFIG_ARCH_CFLAGS="-march=rv32im -mabi=ilp32 -mcmodel=medany"
CONFIG_LD_EMULATION="elf32lriscv"

CONFIG_BOOT_BASE=0x0000
CONFIG_BOOT_SIZE=1024
CONFIG_BOOT_RAM_SIZE=0x400
```

Every component (bootloader, kernel, CCP, SDK library, and each app) sources
this file and compiles with `CONFIG_ARCH_CFLAGS`; `CONFIG_BOOT_BASE`/
`CONFIG_BOOT_SIZE`/`CONFIG_BOOT_RAM_SIZE` keep boot code and its runtime RAM
in separate regions. To target another ISA, edit these here before running
`sysgen new`.

### Bootloader conventions

`boot.S` uses the platform BIOS (`bios_read`, `bios_conout`) to load the kernel.
`bios_init()` must successfully initialize the required BIOS services before they
are used; failure halts silently. Sector-0 field offsets are shared by the
bootloader, kernel, and sysgen via `core/kernel/disk_format.h`. The toolchain must
produce images with `ld -m $CONFIG_LD_EMULATION`, as used by `build_disk.sh` and
`app_build.sh`.

## Building a program with the SDK

Compile a program from a source folder and install it with `sysgen install`
as shown above. See [syscall-reference.md](syscall-reference.md) for the API
and the [User Guide](user-guide.md) for the `sysgen` command reference.
