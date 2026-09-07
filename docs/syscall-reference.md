# Syscall Reference

← [README](../README.md)

CP/M Neo applications access OS services through system calls. The SDK
provides `sys_<name>()` wrappers in `syscall.h`.

## Calling convention

System calls are plain kernel functions called directly: a user program's
call to `sys_open()` is a direct jump to the kernel's implementation, bound
at link time via `--just-symbols=kernel.elf`. There is no jump table, trap,
or `ecall`.

Arguments are passed in `a0`–`a3`.

Most syscalls return `0` or a positive result on success and a negative errno
on failure. File handles are non-negative values; standard handles such as
`FD_STDIN`, `FD_STDOUT`, and `FD_STDERR` are defined by the SDK.

## Syscall catalog

| # | Name | Description | Arguments | Returns |
|---:|---|---|---|---|
| 0 | `open` | Open an existing file | `path*`, `writable` | Handle ≥ 3, or negative errno |
| 1 | `read` | Read from a file | `fh`, `buf*`, `size` | Bytes read, or negative errno if nothing was read |
| 2 | `write` | Write to a file | `fh`, `buf*`, `size` | Bytes written, or negative errno if nothing was written |
| 3 | `close` | Close a file | `fh` | `0`, or negative errno |
| 4 | `exit` | Terminate the program | `code` | Never returns |
| 5 | `args` | Get command-line arguments | `out*` (`ArgBlock`) | Argument count, 0–8 |
| 6 | `findfile` | Search for a file | `pattern*`, `out*`, `start_pos` | Directory index, or negative errno |
| 7 | `getsize` | Get file size | `fh` | File size |
| 8 | `create` | Create a file | `path*` | Handle ≥ 3, or negative errno |
| 9 | `delete` | Delete a file | `path*` | `0`, or negative errno |
| 10 | `rename` | Rename a file | `old*`, `new*` | `0`, or negative errno |
| 11 | `mount` | Mount a volume | `volid` | `0`, or negative errno |
| 12 | `unmount` | Unmount a mounted volume | `volid` | `0`, or negative errno |
| 13 | `resize` | Resize a mounted volume | `volid`, `delta` | `0`, or negative errno |
| 14 | `vstat` | Get volume information | `volid`, `out*` | `0`, or negative errno |
| 15 | `exec` | Load and execute a program | `path*`, `argc`, `argv*` | Never returns on success; errno on failure |
| 16 | `fsetattr` | Get or set file attributes | `name*`, `attr` | `0`, or negative errno |
| 17 | `info` | Get system information | `out*` | `0`, or negative errno |
| 18 | `seek` | Set file position | `fh`, `pos` | `0`, or negative errno |
| 19 | `getctx` | Get filesystem context | `out*` | `0` |
| 20 | `setctx` | Restore filesystem context | `ctx` | `0`, or negative errno |
| 21 | `getenv` | Read an environment slot | `slot` | Slot value, or `-1` if invalid |
| 22 | `setenv` | Write an environment slot | `slot`, `value` | `0`, or `-1` if invalid or protected |
| 23 | `vsetattr` | Get or set volume attributes | `volid`, `attr` | `0`, or negative errno |
| 24 | `time` | Get platform-specific time | - | Platform-defined |
| 25 | `sync` | Flush filesystem changes | - | `0`, or negative errno |
| 26 | `consize` | Get console dimensions | `cw*`, `ch*` | `0` |

## Volume operations

Volumes are A:–D:. Each volume is composed of ordered block runs recorded
in the VMAP.

| Syscall | Operation |
|---|---|
| `mount(volid)` | Mount an unmounted volume and format it |
| `resize(volid, delta)` | Resize a mounted volume by `delta` KB (positive grows, negative shrinks) |
| `unmount(volid)` | Unmount the volume |

Volume layout is stored in the VMAP; see [Disk Format](disk-format.md) for the
on-disk representation and volume limits.

`setctx()` cannot bind to an unmounted volume. Filesystem validation rejects
volume layouts containing overlapping or out-of-range file blocks.

## SysInfo

`info()` fills the `SysInfo` structure.

| Field | Description |
|---|---|
| `tpa` | Available Transient Program Area in KB |
| `os_version` | Operating system version |
| `kern_version` | Kernel version |
| `ccp_version` | CCP version |
| `vol_mounted[4]` | 1 if mounted, 0 otherwise |
| `disk_size_kb` | Disk block-grid capacity in KB |
| `disk_unalloc_kb` | Unallocated block pool in KB |
| `xip` | Set to 1 when the disk image is XIP-formatted |

## Environment slots

CP/M Neo provides three environment slots. Slots 0–1 are reserved by the
system; slot 2 is available to applications.

| Slot | Purpose |
|---:|---|
| 0 | Return code of the last program/command; read-only to programs |
| 1 | CCP batch offset; writable only by the CCP |
| 2 | User-defined |
