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

Create a 2 MB disk image with 64 KB of RAM (`CONFIG_RAM_SIZE=0x10000` in the platform's `config.sh`):

```sh
$ ./sysgen/build/sysgen new \
    --disk-size=2048K \
    --platform=vemu
```

| Option | Description |
|---|---|
| `--disk-size` | Disk size in KB. Requires a `K` suffix, e.g. `2048K`. Defaults to the maximum useful size |
| `--platform` | Target platform id: the 8-char max `CONFIG_ID=` declared by a platform's `config.sh` (platforms under `platform/`). `vemu` is included with the repository; its `config.sh` selects the ISA, `CONFIG_RAM_SIZE`, and memory layout |
| `--no-extra` | Do not install optional apps from `apps/extra` |

`sysgen new` always writes the image to:

```text
$ sysgen/build/disk.img
```

The build report identifies the target architecture and ISA variant used, taken
from the platform's `config.sh` (`ARCH`) and the arch's `config.sh`
(`-march`), e.g. `Architecture : riscv32 (rv32im)`.

The image contains four formatted volumes, A:–D:. The maximum useful disk size is the size of a single volume.

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

### Delete a file

```sh
$ sysgen era myprog.com
```

Deletes a file from the image.

## Bundled applications

Install the bundled system commands:

```sh
$ sysgen install --sys-apps
```

Install the optional applications:

```sh
$ sysgen install --extra-apps
```

`--sys-apps` installs system commands as `SYS+RO`.
`--extra-apps` installs optional applications as `RO`.

Use `--no-extra` with `sysgen new` to omit optional applications.

See [Bundled Apps](bundled-apps.md) for the applications included with CP/M Neo.

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
    --disk-size=2048K \
    --platform=vemu

$ sysgen add sysgen/build/extract
```

The extracted files are restored to A: user 0. Files already installed in the
new image are skipped.

## Build and run a program

For application development, see the [Developer Guide](developer-guide.md).

The usual workflow is:

```text
write source -> build/install with sysgen -> run from CP/M Neo
```
