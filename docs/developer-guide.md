# Developer Guide

← [README](../README.md)

This page covers the SDK, building your own programs, and adding a new hardware
platform.

### `sysgen install`: Compile and install a source file or folder

Put your program's source files in a folder, or provide a single source file, and let `sysgen` compile and install it:

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
- `syscall.h`: `sys_open`/`sys_read`/… wrappers over the kernel's
  `SyscallTable` (see [Syscall Reference](syscall-reference.md)).
- `kernel_abi.h`: shared ABI: the `SyscallTable` type, `SysInfo`, disk
  constants (VMAP/block layout), env slot layout, `FD_*` handles.
- `bios.h`: BIOS interface (see below).

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

A program that needs to touch hardware directly can use the SDK's `sys_dev()`
helper, which reads/writes a 32-bit memory-mapped I/O register in the window
at `__io_base` (the platform's MMIO base from `platform/<name>/config.sh`).
The register and command offsets are encoded with the `IOCTL_*` macros in
`kernel_abi.h`; the in/out `data` pointer carries the value being written or
read (it is required, and may not be `NULL`).

## Adding a platform

A platform is a self-contained `platform/<name>/` directory:

1. `config.sh` declares the platform facts:
   - `ID` — the 8-char max platform id shown by the OS and stamped into
     sector 0 (`S0_PLATFORM`). It is the platform's identity for `--platform`,
     so it is required. e.g. `platform/blackpill-411fe/` with `ID="BPF411E"`.
   - `ARCH` — the ISA directory under `arch/` (selects the toolchain)
   - `RAM_SIZE` — total RAM in bytes (hex), e.g. `0x10000` = 64 KB
   - `RAM_BASE` — base address of the RAM region holding CP/M Neo
   - `IO_BASE` — base address of the peripheral MMIO window
2. `bios.c` implements the functions in `bios.h`.
3. Build with `sysgen new ... --platform=<id>`.

### Platform lookup

`--platform` addresses a platform purely by its `ID`:

- `build_disk.sh` scans every `platform/*/config.sh` and a platform matches
  when its `ID` equals the argument;
- an `ID` declared by more than one platform is an error
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
`SYNC` command drive this chain via `bd_sync` → `disk_sync` → `bios_sync`.

A platform that supports several storage devices can select one at build time
inside the storage functions:

```c
int bios_read(uint16_t sec, uint8_t *buf)
{
#ifdef USE_SDCARD
    return sdcard_read(sec, buf);
#else
    return flash_read(sec, buf);
#endif
}
```

Driver code may be organized within `bios.c` however the platform likes.

See [Architecture](architecture.md) for the boot and build flow.

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
| `CROSS_COMPILE` | Cross-compiler prefix, e.g. `riscv64-unknown-elf-` |
| `ARCH_CFLAGS` | `-march`/`-mabi` flags for the target, e.g. `-march=rv32im -mabi=ilp32` |
| `LD_EMULATION` | Linker emulation for the target, e.g. `elf32lriscv` |

The RISC-V example (`arch/riscv32/config.sh`):

```sh
CROSS_COMPILE=${CROSS_COMPILE:-riscv64-unknown-elf-}
ARCH_CFLAGS="-march=rv32im -mabi=ilp32"
LD_EMULATION="elf32lriscv"
```

`arch/<isa>/config.sh` is the per-build ISA override point. Every component
(bootloader, kernel, CCP, SDK library, and each app — all of which source this
file) is compiled with `ARCH_CFLAGS`. To target another ISA, edit
`ARCH_CFLAGS` here before running `sysgen new`; the build report reflects the
flags actually used.

### Bootloader conventions

`boot.S` calls the platform BIOS (`bios_read`, `bios_conout`) to load the
kernel. Sector-0 field offsets are shared between the bootloader, kernel, and
sysgen via `core/kernel/s0_layout.h`. The toolchain must produce an image with
`ld -m $LD_EMULATION` as done by `build_disk.sh` and `app_build.sh`.

## Building a program with the SDK

Any program can be compiled in a source folder and installed with `sysgen
install`:

```sh
$ ./sysgen/build/sysgen install myapp --dst=A0 --attr=RW
```

See [syscall-reference.md](syscall-reference.md) for the API.

See the [User Guide](user-guide.md) for the `sysgen` command reference.
