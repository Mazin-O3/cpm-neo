# Disk Format

← [README](../README.md)

A CP/M Neo disk image is a sequence of 512-byte sectors.

## Disk layout

| Area | Sectors |
|---|---|
| Boot sector | 0 |
| VMAP | 1 |
| Kernel | 2..K |
| CCP | K+1..K+S |
| Block area | K+S+1..end |

The block area is divided into fixed 1 KB blocks. A volume consists of up to
four ordered block runs. Each run is a contiguous range of blocks.
The runs define the volume's logical block order. Unallocated blocks form
the free pool.

<img src="images/disk-format.png" alt="Disk format" width="100%">

## Sector 0: Boot sector

| Offset | Size | Field | Constant | Value |
|---|---:|---|---|---|
| 0x000 | u16 | Magic | `S0_MAGIC` / `DISK_MAGIC` | `0x4350` (`'CP'`) |
| 0x002 | u16 | Disk format version | `S0_DISK_VER` | `0` |
| 0x004 | u16 | Disk size KB | `S0_DISK_SIZE_KB` | varies |
| 0x006 | u32 | Kernel RAM load address | `S0_KERN_LOAD` | varies |
| 0x00A | u32 | Kernel byte count | `S0_KERN_SIZE` | varies |
| 0x00E | u16 | Kernel sectors | `S0_KERN_SECTORS` | varies |
| 0x010 | u16 | Kernel start LBA | `S0_KERN_LBA` | `2` |
| 0x012 | u16 | OS version | `S0_OS_VER` | `0x0100` |
| 0x014 | u16 | Kernel version | `S0_KERN_VER` | `0x0100` |
| 0x016 | u16 | CCP version | `S0_CCP_VER` | `0x0100` |
| 0x018 | u16 | Reserved sectors | `S0_KERN_SECS` | `K + S` |
| 0x01A | u16 | CCP start LBA | `S0_CCP_LBA` | `2 + K` |
| 0x01C | u16 | CCP sector count | `S0_CCP_SIZE` | `S` |
| 0x01E | u8[8] | Platform name | `S0_PLATFORM` | varies |
| 0x1FE | u16 | Boot signature | `S0_SIG` | `0xAA55` |

## Volume map: sector 1

The VMAP is the authoritative record of volume layout. It is written as a
single sector when volume layout or volume attributes change.

| Offset | Size | Field | Constant | Value |
|---|---:|---|---|---|
| 0x000 | u16 | Total blocks | `VMAP_NUM_BLOCKS` | varies |
| 0x002 | u16 | Block-0 LBA | `VMAP_BASE_LBA` | `K + S + 2` |
| 0x004 | u16 | Magic | `VMAP_MAGIC_OFF` / `VMAP_MAGIC` | `0x4350` |
| 0x006 | 4×18 B | Volume records | `VMAP_VOLREC` | see below |
| 0x1FE | u16 | Signature | `VMAP_SIG` | `0xAA55` |

Block `i` occupies:

```text
[base_lba + i * BD_BLOCK_SECS, base_lba + (i + 1) * BD_BLOCK_SECS)
```

A single volume can be grown at runtime (`bd_resize` / `SET RZ +N`) to consume
blocks freed by shrinking (`SET RZ -N`) or unmounting (`SET UM`) the others, up
to and including the entire grid. Because of that, the grid itself can never
be provisioned larger than what one volume is allowed to address. Any blocks
beyond a single volume's cap would be permanently unreachable by every volume,
no matter how the others are resized. The maximum grid size is therefore:

```text
BD_VOL_MAX_BLOCKS
= 2048 blocks
```

## Volume record

Each volume record is 18 bytes:

```text
Bytes  0-15   Runs[4]
               Each run: u16 start, u16 count

Byte      16   run_count
Byte      17   attr
```

`run_count == 0` means unmounted. Otherwise the runs are stored in logical
order.

A volume's logical sector space is the concatenation of its runs. Logical
LBA 0 is the first sector of the first run.

The kernel enforces:

- Runs do not overlap.
- Runs do not exceed `num_blocks`.
- A mounted volume has at least one run.
- A volume has at most four runs.
- `attr` contains the volume read-only state.

## Volume header

The volume header occupies logical LBA 0.

| Offset | Size | Field | Constant | Value |
|---|---:|---|---|---|
| 0x000 | u16 | Magic | `VHDR_MAGIC_OFF` | `0x4350` (`DISK_MAGIC`) |
| 0x002 | u16 | Format version | `VHDR_VER_OFF` | `0x0001` |
| 0x004 | u16 | Volume size KB | `VHDR_SIZE_KB_OFF` | varies |
| 0x006 | u16 | Root directory LBA | `VHDR_ROOT_LBA_OFF` | `1` |
| 0x008 | u16 | Data area start LBA | `VHDR_DATA_LBA_OFF` | `17` |
| 0x00A | u16 | Total data blocks | `VHDR_TOT_BLKS_OFF` | varies |
| 0x1FE | u16 | Signature | - | `0xAA55` |

The volume layout is:

```text
Logical LBA 0      Volume header
Logical LBA 1-16   Root directory
Logical LBA 17+    Data area
```

`BD_ROOT_ENTRIES` is 256 entries occupying 16 sectors. Directory block numbers
are indexes into the data area.

`Block 0` of the data area is reserved as the empty sentinel. Usable capacity is
therefore `BD_TOT_BLKS - 1` blocks.

## Initial volume layout

`mkdisk_build` divides the block grid between A:, B:, C:, and D:. Each volume
gets `floor(num_blocks / 4)` blocks, with the remainder distributed one block
at a time to the first volumes.

## Directory entry

Each directory entry is 32 bytes:

| Offset | Size | Field | Constant |
|---|---:|---|---|
| 0x00 | 8 B | Name, space-padded, uppercase | `BD_DIR_NAME` |
| 0x08 | 3 B | Extension | `BD_DIR_EXT` |
| 0x0B | 1 B | Attributes | `BD_DIR_ATTRIB` |
| 0x0C | 1 B | User area | `BD_DIR_USER` |
| 0x0D | 1 B | Extent index | `BD_DIR_EXTENT_IDX` |
| 0x0E | 2 B | Extent byte count | `BD_DIR_EXTENT_BYTES` |
| 0x10 | 16 B | Block pointers, 8 × u16 | `BD_DIR_BLOCKS` |

Sentinel values:

```text
BD_ENTRY_EMPTY    0x00   End of directory
BD_ENTRY_DELETED  0xE5   Deleted entry
```

Block number 0 is the empty pointer. A file receives its first data block on
its first write.

Each directory extent contains up to eight blocks
(`BD_BLOCKS_PER_EXTENT = 8`). With 1 KB blocks, an extent is at most 8 KB.

Files larger than one extent use additional directory entries with increasing
`BD_DIR_EXTENT_IDX`.

The maximum file size is:

```text
BD_MAX_EXTENTS * 8 KB = 256 * 8 KB = 2 MB
```

File sizes are reported in allocation blocks; a non-empty 1-byte file therefore
uses 1 KB.

## File attributes

| Bit | Value | Meaning |
|---:|---:|---|
| 0 | `0x01` | `FILE_ATTR_READ_ONLY`: rejects writes/delete |
| 1 | `0x02` | `FILE_ATTR_SYSTEM`: hidden from `DIR`; found by command search in user 0 of the current drive and `A:` |
