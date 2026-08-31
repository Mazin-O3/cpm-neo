# CCP Reference

← [README](../README.md)

CP/M Neo provides an enhanced Console Command Processor (CCP). The resident
commands are `DIR`, `DIRS`, `ERA`, `REN`, `TYPE`, `USER`, `ECHO`, `CLS`, and
`SYNC`.
Other commands are transient `.COM` programs installed by `sysgen`.

The CCP implementation lives in `core/ccp/`.

## Prompt and navigation

| Action | Example |
|---|---|
| Switch volume | `B:` |
| Switch volume and user area | `B5:` |
| Switch user area | `USER 3`, `3:` |
| Run a program | `PROG`, `PROG.COM`, `5:PROG` or `B5:PROG` |

A bare program name is resolved using these searches, in order:

1. Search the **current volume and user area** for any matching file.
2. If not found, search **user area 0 of the current volume** for a `SYS` file.
3. If still not found, search **user area 0 of `A:`** for a `SYS` file.

`SYS` files are hidden from `DIR` but can be run from any user area.

## File reference syntax

```text
A:FILE.TXT      volume A, current user area
A5:FILE.TXT     volume A, user area 5
5:FILE.TXT      current volume, user area 5
FILE.TXT        current volume and user area
```

In command syntax, `[ ]` encloses optional parts. For example, `[v:]` means
the volume may be omitted.

## Wildcards

Matching follows CP/M FCB semantics: the filename and filetype are
separate fields, and a pattern never crosses the `.` boundary.

| Symbol | Meaning |
|---|---|
| `?` | Matches any single character in its field, including blanks |
| `*` | Fills the rest of its field (`name` or `type`) with `?`; characters after a `*` are ignored |

Fields left unspecified stay blank, and blanks match only blanks.
Consequences worth remembering:

- `*.*` matches **all** files (a `?` also matches blank padding).
- A bare `*`, `*.`, or `X*` leaves the type blank, so it matches only
  files **without an extension**. On disks where every file has a type
  these list nothing.
- `D*S` means `D???????`: everything after `*` in the same field is
  discarded.


| Pattern | Selects | Why |
|---|---|---|
| `DIR *.*` | everything | `?` also matches blank padding |
| `DIR *.COM` | PIP.COM, STAT.COM | type fixed, name all-wild |
| `DIR D*.*` | DATA1.TXT | name starts with D, any type |
| `DIR *` | MAKEFILE only | blank type matches blank type |
| `DIR *.` | MAKEFILE only | same as bare `*` |
| `DIR D*` | nothing here | D-names without a type don't exist |
| `DIR L*O.DAT` | - | `*` ends the field: reads as `L???????.DAT` |
| `ERA *.TXT` | erases DATA1.TXT | wildcards work in ERA |
| `STAT ?ATA1.TXT` | DATA1.TXT | `?` fills single positions |

The classic CP/M habit follows from the table: to act on a whole disk,
type `*.*`, never a bare `*`.

## Commands

### DIR: List directory entries

```text
DIR [v[u]:][filespec]
```

Lists files on the specified volume and user area. Supports `?` and `*`.

```text
A> DIR
A> DIR *.COM
A> DIR B5:
```

### DIRS: List system files

```text
DIRS [v[u]:][filespec]
```

Lists `SYS` files, which are hidden from normal `DIR`. Supports the same
wildcards and volume/user syntax as `DIR`.

```text
A> DIRS
A> DIRS B0:*.COM
```

### ERA: Delete files

```text
ERA [v[u]:]filespec
```

Deletes matching files. Supports `?` and `*`. Read-only files are rejected.

```text
A> ERA OLD.BAK
A> ERA B:*.TMP
```

### REN: Rename files

```text
REN [v[u]:]old [v[u]:]new
```

Renames files. Supports `?` and `*` in both names. Both names must resolve to
the same volume and user area.

```text
A> REN OLD.TXT NEW.TXT
```

### TYPE: Display file contents

```text
TYPE [v[u]:]filespec
```

Displays file contents as text.

```text
A> TYPE HELLO.TXT
```

### DUMP: Display a hex dump

```text
DUMP [v[u]:]filespec
```

Displays a formatted hex dump with offsets, hexadecimal bytes, and text.

```text
A> DUMP BOOT.BIN
```

### COPY: Copy files

```text
COPY [v[u]:]src [v[u]:][name]
```

Copies one or more files. The source supports `?` and `*`. Files can be copied
between volumes and user areas.

```text
A> COPY A:FILE.TXT B:
A> COPY A:*.TXT B:
A> COPY B5:FILE.TXT D7:HI.TXT
```

### SUBMIT: Run a batch file

```text
SUBMIT filename.sub [$1..$9]
```

Executes a `.SUB` batch file. `$1`–`$9` are replaced by the supplied arguments;
parameters not supplied substitute as empty.

Lines beginning with `;` are comments. Use `$$` for a literal `$`. A line
beginning with `:` runs only if the previous command returned `0`.

Press ESC between commands to abort the batch. Missing source files
report `No SUB file found`; failures while building the batch report
`Cannot build $$$.SUB`.

### STAT: Show file or disk status

```text
STAT [v[u]:][filespec]
```

With a filespec, shows size, allocation, and attributes for each match:

```text
Secs  Bytes  Ext Attributes      Name
   1     2k    1 Dir RW         B:HELLO   .S
 213    28k    2 Sys RO         B:STAT    .COM
```

With no argument, shows free space on the current volume (`A: RW, Free: 421K`).
`STAT DSK:` shows all volumes:

```text
Vol  Attr  Used  Total
 A:  RW     81k   502k
 B:  -       -     -
 C:  RW     12k   502k
 D:  RO     28k   502k
Total: 2048K  (502K Unalloc)
```

### SET: Set file or volume attributes

```text
SET [v:]file ATTR
```

```text
RO      Read-only
RW      Read-write
SYS     System file
DIR     Normal file

MT      Mount volume
EX N    Extend volume by N KB
UM      Unmount volume
UM N    Shrink volume by N KB
```

`RO` and `RW` apply to files and volumes. `SYS` and `DIR` apply to files.
`MT`, `EX`, and `UM` apply to volumes.

Examples:

```text
A> SET DATA.TXT RO
A> SET B: RW
A> SET B: MT
A> SET B: EX 8
A> SET B: UM 4
A> SET B: UM
```

### USER: Show or set user area

```text
USER [u]
```

Shows or sets the current user area, from 0 to 15.

```text
A> USER
A> USER 3
```

### SYS: Show system information

```text
SYS
```

Displays OS, kernel, and CCP versions, TPA size, disk capacity, and mounted
volumes.

### CLS: Clear screen

```text
CLS
```

Clears the console screen and resets the cursor.

### ECHO: Display arguments

```text
ECHO [arg ...]
```

Displays the arguments, separated by a single space character and followed by
a newline.

```text
ECHO Hello World
Hello World
```

### SYNC: Flush disk writes to persistent storage

```text
SYNC
```

Commits all pending disk writes to durable storage. Walks the sync chain
(`sys_sync` → `bd_sync` → `disk_sync` → `bios_sync`), which flushes the disk
layer's write-back cache and then invokes the platform's persistence barrier.
The flush also happens automatically on program exit, so `SYNC` is only needed
to force durability before a power loss or a platform eject.

### HELP: Show help

```text
HELP [command]
```

With no argument, lists commands. With a command name, shows detailed help.

```text
A> HELP
A> HELP COPY
```
