# Bundled Apps

← [README](../README.md)

`sysgen new` installs bundled apps in two groups, selected per platform by
`CONFIG_SYS_APPS` / `CONFIG_EXTRA_APPS` in `config.sh`:

- `apps/sys`: the transient system commands (`COPY`, `DUMP`, `HELP`, `SET`, `STAT`,
  `SUBMIT`, `SYS`). Each top-level `.c`/`.s`/`.S` file is compiled to a
  `.COM`. When `CONFIG_SYS_APPS` is unset or `*` all of them are installed;
  an empty `""` installs none; otherwise only the named ones are.
- `apps/extra`: optional tools, named in `CONFIG_EXTRA_APPS`. The rules are
  the same as above: unset or `*` = install all extras, `""` = install none,
  a list = only those.  A small-flash platform sets `CONFIG_EXTRA_APPS=""` to
  ship only its selected `apps/sys` commands.

### Optional tools (apps/extra)

| App | Purpose | Invoke |
| --- | --- | --- |
| **ED** | Line-oriented text editor | `ED <file>` |
| **BASIC** | Tiny BASIC interpreter | `BASIC [prog.bas]` |

## BASIC

A small interactive MS-BASIC-inspired interpreter for writing and running BASIC programs.

### Commands & Statements

| Keyword | Purpose | Example |
| --- | --- | --- |
| `LIST`, `RUN`, `NEW` | Program management | `RUN` |
| `LOAD`, `SAVE` | File operations | `SAVE "GAME"` |
| `FRE`, `EXIT` | Free memory / Quit | `EXIT` |
| `LET`, `DIM` | Variables and arrays | `LET A=5` |
| `PRINT`, `INPUT` | Console I/O | `PRINT "X=";X` |
| `GOTO`, `GOSUB`, `RETURN` | Control flow | `GOTO 100` |
| `IF ... THEN` | Conditional execution | `IF X>5 THEN 200` |
| `FOR ... TO [STEP]` / `NEXT` | Loops | `FOR I=1 TO 10 STEP 2` |
| `END`, `REM` | Terminate program / comment | `END` |
| `AND`, `OR` | Logical operators | `IF A>1 AND B<2 THEN 50` |
| `ABS`, `SGN`, `RND` | Math functions | `X = RND(100)` |
| `CLR` | Clear all variables | `CLR` |
| `DEF FN` | User-defined functions | `DEF FNA(X)=X*2` |
| `PEEK`, `POKE` | Memory access | `POKE addr,val` |

**Limits**

- 64-character strings
- Maximum 8 nested `FOR` loops
- Maximum 32 nested `GOSUB`s

### Example : `GUESS.BAS`

```basic
10 N = RND(100)
20 INPUT "Guess 1-100"; G
30 IF G = N THEN 60
40 IF G < N THEN PRINT "Too low" : GOTO 20
50 PRINT "Too high" : GOTO 20
60 PRINT "You got it!"
70 END
```

## ED Text Editor

A line-oriented text editor inspired by CP/M ED.

### Commands

| Command | Description |
| --- | --- |
| `Enter` | Advance to next line |
| `N` | Jump to line `N` |
| `B` | Beginning of file |
| `H` | Beginning of file and clear modified flag |
| `L` | List 10 lines |
| `NL` | List `N` lines |
| `-NL` | List backwards |
| `F,TL` | List line range |
| `ND` | Delete line `N` |
| `F,TD` | Delete line range |
| `NI` | Insert before line `N` (terminate with **Ctrl+Z**) |
| `S/old/new/` | Replace text on current line |
| `#` | Toggle verify flag |
| `R <file>` | Read a file into the buffer |
| `E` | Save and exit |
| `Q` | Quit (prompts when the buffer is modified) |

### Example Session

```text
> ED HELLO.BAS
  NEW FILE
     : *I
     1: 10 PRINT "HELLO"
     2: 20 END
     3: (Ctrl+Z)
     : *
     : *1S/HELLO/HI/
     : *L
     1: 10 PRINT "HI"
     2: 20 END
      : *E
```