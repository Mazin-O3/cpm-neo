# User Guide

← [README](../README.md)

This guide covers building CP/M Neo, creating a disk image, and using `sysgen`
to inspect or modify an image.

## Prerequisites

- A bare-metal cross-toolchain for your target ISA (the bundled default is
  riscv32: `riscv64-unknown-elf-*`)
- `make`
- `sh`
- Standard POSIX tools

## Build sysgen

Build the host-side `sysgen` tool:

```sh
$ make -C sysgen
```

## Create a disk image

Create a disk image of exactly `CONFIG_DISK_SIZE` KB (e.g. 2048 for `vemu`).
The platform's `config.sh` supplies the total image size plus the memory
layout (`CONFIG_RAM_SIZE`, e.g. `0x10000` = 64 KB):

```sh
$ ./sysgen/build/sysgen new \
    --platform=vemu
```

| Option | Description |
|---|---|
| `--platform` | Target platform id (8-char max `CONFIG_ID` from `config.sh`). `vemu` is included with the repository |

`sysgen new` always writes the image to:

```text
$ sysgen/build/disk.img
```

The image is `CONFIG_DISK_SIZE` KB total, overhead included: the boot/VMAP
and kernel/CCP sectors come out of that budget first, and the remaining 1 KB
block grid is divided evenly between the `CONFIG_VOL_MAX` formatted volumes
(A:..).

## Inspect an image

### List files

```sh
$ sysgen dir
```

Lists files on A:.

## Modify an image

### Add a file

```sh
$ sysgen add myprog.com
```

Adds a file to A:. Existing files are skipped.

Use `--dst=Vn` to select a volume and user area:

```sh
$ sysgen add myprog.com --dst=B0
```

Use `--attr` to set attributes. The default for `add` is `RW`.

### Delete a file

```sh
$ sysgen era myprog.com
```

Deletes a file from A:. Read-only files are supported.

Use `--dst=Vn` to select a volume and user area:

```sh
$ sysgen era myprog.com --dst=B0
```

### Install a program

```sh
$ sysgen install myapp
```

Compiles a source folder and installs the resulting program.

Use:

```sh
$ sysgen install myapp --dst=A0 --attr=RW
```

`install` accepts a source folder and scans it for `.c`, `.s`, and `.S` files.

## Bundled applications

Which bundled apps land on a fresh image is a platform decision made in
`config.sh` (`CONFIG_SYS_APPS` / `CONFIG_EXTRA_APPS`), not a CLI option —
see [Create a disk image](#create-a-disk-image). See
[Bundled Apps](bundled-apps.md) for the applications included with CP/M Neo.

## Extract files

Extract all files from an image:

```sh
$ sysgen extract
```

Files are written to:

```text
sysgen/build/extract/
```

Extraction includes files regardless of their attributes and searches all
volumes and user areas.

## Recover files from a damaged image

`sysgen extract` can recover files without relying on the normal boot/kernel
area.

```sh
$ sysgen extract

$ ./sysgen/build/sysgen new \
    --platform=vemu

$ sysgen add sysgen/build/extract
```

The extracted files are restored to A: user 0. Files already installed in the
new image are skipped.
