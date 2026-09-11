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
| `int bios_init(void)` | Initialize hardware; `EOK` (0) or negative errno |
| `void bios_conout(int c)` | Write a character to the console |
| `int bios_conin(void)` | Blocking console read |
| `int bios_constat(void)` | Console status (0xFF = key ready) |
| `void bios_consize(uint8_t *cw, uint8_t *ch)` | Console dimensions |
| `int bios_read(uint16_t sec, uint8_t *buf)` | Read one 512-byte sector |
| `int bios_write(uint16_t sec, const uint8_t *buf)` | Write one sector |
| `uint32_t bios_time(void)` | platform-defined time service |

## Configuring the system

Resource usage is tuned per-platform in `platform/<ID>/config.sh`, where every
parameter carries the `CONFIG_` prefix and the four software knobs are
required (there are no defaults; the optional fields are `CONFIG_XIP_BASE`,
the boot budgets `CONFIG_BOOT_SIZE` / `CONFIG_BOOT_RAM_SIZE`, and the app
selections `CONFIG_SYS_APPS` / `CONFIG_EXTRA_APPS`).  `build_disk.sh` reads
them and writes the
effective values to `build/gen/config.h`, which every kernel/CCP/SDK/app
compile includes.  The sysgen host tool is compiled once and sizes its arrays
to the *ceilings* in `sysgen/include/config.h` (16 volumes / 32 MB total disk
size / 8 FCBs — the maximum any platform may declare); it applies each
platform's active values at runtime and rejects a platform that exceeds them.

| Knob | Example (vemu) | Kernel RAM cost (roughly) |
|------|----------------|---------------------------|
| `CONFIG_VOL_MAX` | 4 | `MAX_VOLUMES`-backed arrays and `SysInfo.vol_mounted[]` |
| `CONFIG_DISK_SIZE` | 2048 | alloc bitmap covers the whole grid (ceil(KB/8) bytes); total image size in KB, overhead included |
| `CONFIG_FCB_MAX` | 4 | `CONFIG_FCB_MAX` open-file control blocks |
| `CONFIG_STACK_SIZE` | 0x1000 | single shared kernel/CCP/app stack (top of TPA) |

Two further knobs are *optional* app selections rather than hardware facts
(they make no compile-time array, so are exempt from the "all required"
rule): `CONFIG_SYS_APPS` and `CONFIG_EXTRA_APPS` — space-separated lists of
the `apps/sys` commands and `apps/extra` tools `sysgen new` installs.  For
both, unset or `*` = install all of them, empty `""` = install none, and a
list installs only those.  A tiny-flash port (see below) typically declares
`CONFIG_EXTRA_APPS=""` plus only the `apps/sys` commands it has room for:

```sh
CONFIG_SYS_APPS="copy stat set"
CONFIG_EXTRA_APPS=""
```

An unknown name in either list is a hard build error (a typo must never
silently drop an app). See [Bundled Apps](bundled-apps.md).

`CONFIG_VOL_MAX` and `CONFIG_DISK_SIZE` are on-disk *format* parameters (the
volume count, and the total image size including the kernel/CCP overhead), so
changing them must be paired with a fresh image:
`sysgen new --platform=<name>`. `CONFIG_STACK_SIZE` is consumed by the
linker (linker scripts cannot include C headers); `build_disk.sh` passes the
platform's value to the kernel links as `--defsym=__stack_size`, with the
`PROVIDE` in `linker_kernel_common.ld` serving as a hand-link fallback only.

A small-RAM port (for example a 32 KB ROM / ~2.5 KB SRAM target) shrinks
the data footprint by dropping the disk layer's big arrays: `CONFIG_FCB_MAX 2`,
`CONFIG_VOL_MAX 2`, `CONFIG_DISK_SIZE 512`, and a tighter
`CONFIG_STACK_SIZE`, and by trimming the bundled apps it installs
(`CONFIG_SYS_APPS` list, `CONFIG_EXTRA_APPS=""`) so the flash budget is
not spent on `.COM`s that cannot fit. The v1 pattern is to keep one tuned configuration
checked in per port under `platform/<name>/config.sh`; `sysgen/include/config.h`
holds the host ceilings a platform must stay within.

## Adding a platform

A platform is a self-contained `platform/<name>/` directory:

1. `config.sh` declares the platform facts:
   - `CONFIG_ID` — the 8-char max platform id, required and stamped into sector 0
     (`S0_PLATFORM`). e.g. `platform/blackpill-f411/` with
     `CONFIG_ID="BPF411"`.
   - `CONFIG_ARCH` — the ISA directory under `arch/` (selects the toolchain)
   - `CONFIG_RAM_SIZE` — total RAM in bytes (hex), e.g. `0x10000` = 64 KB
   - `CONFIG_RAM_BASE` — base address of the RAM region holding CP/M Neo
   - `CONFIG_IO_BASE` — base address of the peripheral MMIO window
   - `CONFIG_XIP_BASE` — optional: pins the execute-in-place window base for
      `sysgen new --xip`. The window extends over the disk image itself (see
      [Architecture](architecture.md#execute-in-place-xip)); the kernel/CCP
      run in place from it and whether they fit the produced disk is
      validated at build time. When omitted and `--xip` is given, the base
      is auto-derived as `CONFIG_BOOT_BASE` + boot size (right past the
      bootloader); declare it only when the window lives elsewhere (e.g. a
      gap between boot and the disk).
- the four software knobs `CONFIG_VOL_MAX`, `CONFIG_DISK_SIZE`,
      `CONFIG_FCB_MAX`, `CONFIG_STACK_SIZE` (all required, no defaults) — see
      [Configuring the system](#configuring-the-system).
   - `CONFIG_BOOT_BASE` (required) — where the bootloader goes (the flash/reset
      vector origin).  `CONFIG_BOOT_SIZE` and `CONFIG_BOOT_RAM_SIZE` are
      optional: the linker script provides defaults via `PROVIDE()`; a platform
      that needs a different budget overrides them (see [Boot placement lives in
      the platform](#boot-placement-lives-in-the-platform)).
2. `bios.c` implements the functions in `bios.h`.
3. Build with `sysgen new --platform=<id>` — disk images are plain
   (RAM-loading) by default; pass `--xip` to link the kernel/CCP into the
   flash window (using `CONFIG_XIP_BASE` if declared, else auto-derived).

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

### The Blackpill F411 platform (`BPF411`)

The reference ARM port shares the on-chip flash three ways:

| Region | Address | Contents |
| --- | --- | --- |
| Boot | `0x08000000` | `bootloader.bin`, `0xFF`-padded to `__boot_size` (4 KB default from linker) |
| XIP / disk | `0x08001000` | `disk.img` verbatim (VMAP, kernel, CCP, apps) |
| RAM | `0x20000000` (128 KB) | boot runtime RAM at `0x20001000`, kernel data near the top |

Key platform choices:

- **Console — USB CDC-ACM** on the native USB-C port (OTG_FS, PA11/PA12),
  polled by the kernel's `bios_*` console calls; no interrupts. It presents as
  `0483:5740` and opens as `/dev/ttyACM0` (64-byte bulk IN/OUT ×2 endpoints
  sharing one TX FIFO, 8-byte notify IN).
- **Clock — 96 MHz** from the 25 MHz HSE (PLL M=25 N=192 P=2; PLL48 for USB).
  `clock_init()` is idempotent: the register state survives the boot→kernel
  handoff, so a second call just confirms the PLL.
- **Storage — read-only (phase 1).** `bios_read()` copies 512-byte sectors out
  of the flash image at the XIP base (auto-derived `0x08001000`);
  `bios_write()`/`bios_sync()` report
  no write path yet (`EVOLRO` protects the volumes at the FSD level).
- **Time — DWT cycle counter** ÷ 96 (kHz). **LED — PC13** blinks at 4 Hz.

Assembling and flashing (blackpill always builds an XIP disk):

```sh
sh platform/blackpill-f411/build.sh     # sysgen new --xip + pack -> BPF411.bin
sh platform/blackpill-f411/flash.sh     # ST-Link (SWD) flash
```

The bootloader is `0xFF`-padded to `__boot_size` (4 KB) so the disk image always
lands exactly at the auto-derived XIP base `0x08001000`, making
`bios_read(sec)` = `flash[0x08001000 + sec*512]`.

## Adding an architecture

An architecture is a self-contained `arch/<isa>/` directory. The build scripts
source `arch/<isa>/config.sh` automatically.

The `arch/<isa>/` directory needs five files:

| File | Purpose |
| --- | --- |
| `config.sh` | Toolchain metadata for this ISA |
| `boot.S` | Architecture bootloader (initializes the platform BIOS and jumps to the kernel) |
| `linker_boot.ld` | Bootloader memory layout (boot code region + boot runtime RAM) |
| `crt0.S` | C runtime startup (kernel, CCP, and apps): sets the stack pointer to the top of the shared stack, copies `.data`, clears `.bss`, and jumps to `_start` |
| `kjump.S` | Optional. `void kjump(uintptr_t addr)` — transfer control to a freshly loaded program. Only needed when the ISA encodes execution-state in the address (e.g. the Cortex-M Thumb bit); without it the generic weak C fallback in `kernel.c` is used. The build compiles it only when the file is present |

### `config.sh` contract

`config.sh` is sourced by `build_disk.sh` and `app_build.sh`. It must set:

| Variable | Meaning |
| --- | --- |
| `CONFIG_CROSS_COMPILE` | Cross-compiler prefix, e.g. `riscv64-unknown-elf-`. Required; the build fails if unset |
| `CONFIG_ARCH_CFLAGS` | `-march`/`-mabi` flags for the target, e.g. `-march=rv32im -mabi=ilp32`. Must include the ISA's code-model flag (RISC-V: `-mcmodel=medany`); every binary runs at its fixed link origin — the kernel and CCP in place from flash on XIP disks, apps from the TPA |
| `CONFIG_LD_EMULATION` | Linker emulation for the target, e.g. `elf32lriscv` |

The RISC-V example (`arch/riscv32/config.sh`):

```sh
CONFIG_CROSS_COMPILE=riscv64-unknown-elf-
CONFIG_ARCH_CFLAGS="-march=rv32im -mabi=ilp32 -mcmodel=medany"
CONFIG_LD_EMULATION="elf32lriscv"
```

Every component (bootloader, kernel, CCP, SDK library, and each app) sources
this file and compiles with `CONFIG_ARCH_CFLAGS`. The arch owns only ISA
facts: the toolchain prefix, the CFLAGS, the linker emulation, and the boot
memory budget defaults (`__boot_size`, `__boot_ram_size` via `PROVIDE()` in
`linker_boot.ld`). To target another ISA, add an `arch/<isa>/` directory and
set `CONFIG_ARCH` in the platform before running `sysgen new`.

### Boot placement lives in the platform

Where the bootloader lives is a *memory-map* fact, not an ISA fact, so the
boot base is declared by `platform/<name>/config.sh`, not the arch:

| Variable | Meaning |
| --- | --- |
| `CONFIG_BOOT_BASE` | Address where the bootloader is placed and executed (reset vector origin) |
| `CONFIG_BOOT_SIZE` | Optional. Boot code budget override (arch linker script provides default via `PROVIDE()`) |
| `CONFIG_BOOT_RAM_SIZE` | Optional. Boot runtime RAM budget override (arch linker script provides default via `PROVIDE()`) |

`CONFIG_BOOT_BASE` flows into the boot link as `--defsym=__boot_base`; the
boot size and RAM size are consumed by the linker script directly (overridden
only if the platform sets `CONFIG_BOOT_SIZE` / `CONFIG_BOOT_RAM_SIZE`). On an
emulated platform the boot base is the emulator's reset vector (`vemu`:
`0x0000`); on real hardware it is the flash base, and the XIP window auto-derives
to sit right past the boot code, e.g.:

```sh
CONFIG_BOOT_BASE=0x08000000     # flash base = reset vector
# XIP base auto-derived: 0x08001000 = BOOT_BASE + __boot_size (4 KB)
# __boot_size = 0x1000 (4 KB) comes from linker script PROVIDE() default
```

XIP is a per-build choice (`sysgen new --xip`), not a platform property:

| `sysgen new ...` | `CONFIG_XIP_BASE` | Resulting disk |
| --- | --- | --- |
| no `--xip` | ignored | plain non-XIP (RAM-loaded); `S0_XIP` = 0 |
| `--xip` | declared | XIP; window base = `CONFIG_XIP_BASE` |
| `--xip` | omitted | XIP; window base = `CONFIG_BOOT_BASE` + boot size |

### Bootloader conventions

 `boot.S` uses the platform BIOS (`bios_read`, `bios_conout`) to load the kernel.
`bios_init()` must successfully initialize the required BIOS services before they
are used; failure halts silently. Sector-0 field offsets are shared by the
bootloader, kernel, and sysgen via `core/kernel/disk_format.h`. The toolchain must
produce images with `ld -m $CONFIG_LD_EMULATION`, as used by `build_disk.sh` and
`app_build.sh`.

### Transferring control: `kjump`

The kernel hands off to the CCP and loaded `.com` files via `void kjump(uintptr_t
addr)`. The default implementation (in `kernel.c`) is a weak C function-pointer
call — fine on any ISA with plain branchable addresses. An ISA whose addresses
encode execution-state overrides it with a strong `arch/<isa>/kjump.S`.

`kjump()` is called at the kernel's three hand-off sites (two RAM `__tpa_base`
loads for `.com` programs and the XIP CCP entry).


## Building a program with the SDK

Compile a program from a source folder and install it with `sysgen install`
as shown above. See [syscall-reference.md](syscall-reference.md) for the API
and the [User Guide](user-guide.md) for the `sysgen` command reference.
