# MINIX 3 Modernization Journal

## Build Environment

### Host Compiler: GCC 14 (Debian Trixie) Workarounds

GCC 14 promotes several warnings to errors that break the MINIX 3 codebase (written for GCC 4.x era). Required `HOST_CFLAGS`:

```bash
HOST_CFLAGS="-fcommon -Wno-error=implicit-function-declaration -Wno-error=incompatible-pointer-types -Wno-error=int-conversion"
```

| Flag | Reason |
|------|--------|
| `-fcommon` | GCC 10+ defaults to `-fno-common`, breaking tentative definitions in BSS (e.g., `debug_file` in nbmake) |
| `-Wno-error=implicit-function-declaration` | GCC 14 makes this an error; affects `mi_vector_hash` in tools/compat |
| `-Wno-error=incompatible-pointer-types` | GCC 14 error; affects `makewhatis` (zlib gzFile pointer types) |
| `-Wno-error=int-conversion` | GCC 14 error; various old C code patterns |

### Source Fixes Required for GCC 14

1. **`external/gpl3/binutils/dist/gold/errors.h`**: Add `#include <string>` (missing include, worked before due to transitive includes in older libstdc++)

2. **`external/bsd/llvm/dist/llvm/include/llvm/IR/ValueMap.h:104`**: Change `return MDMap;` to `return MDMap != nullptr;` (unique_ptr to bool conversion disallowed in C++17)

### Build Command

```bash
HOST_CFLAGS="-fcommon -Wno-error=implicit-function-declaration -Wno-error=incompatible-pointer-types -Wno-error=int-conversion" \
  ./build.sh -m i386 -U -j$(nproc) tools
```

The `-U` flag is required for unprivileged (non-root) builds.

### Known Build Issue: obj Directories

The `distribution` target can fail with `nbmkdep: unable to write to file ...d.tmp: No such file or directory` when obj subdirectories are missing (e.g., `external/bsd/kyua-cli/lib/utils/cmdline/`). Fix: run `obj` target explicitly before `distribution`:

```bash
./build.sh -m i386 -U obj
./build.sh -m i386 -U -j$(nproc) distribution
```

Or use a clean build with `-r` flag to reset state.

---

## Build Optimization: MKPIC=no

MINIX defaults to static linking (`LDSTATIC=-static`), so PIC objects (`.pico`) and shared libraries (`.so`) are unnecessary overhead. Setting these in `share/mk/bsd.own.mk` roughly halves library compilations:

```make
MKPIC:=     no
MKPICLIB:=  no
MKPICINSTALL:= no
```

This is the single most effective build-time optimization with no functionality loss. Committed on branch `optimize-buildtime`.

---

## Toolchain Architecture

### Current Build Flow (slow)

```
build.sh tools
  ├── nbmake (host make, BSD)          ~1%
  ├── sed, awk, yacc, lex, m4 (BSD)    ~5%
  ├── binutils (GNU: as, ld, ar, ...)  ~10%
  ├── llvm-lib (121 LLVM libraries!)   ~60-80%  ← bottleneck
  ├── llvm-clang (cross-compiler)      ~5%
  ├── llvm-lld, llvm-tblgen            ~5%
  └── mkfs, partition, installboot     ~1%

build.sh distribution
  └── Cross-compiles entire MINIX using above toolchain
```

The tools phase builds a **complete LLVM/Clang 3.6.1** from source as a cross-compiler. This is ~60-80% of tools build time.

### Bundled LLVM 3.6.1 Details

- Source: `external/bsd/llvm/dist/llvm/` and `external/bsd/llvm/dist/clang/`
- SVN revision: 237755 (branches/release_36)
- Target triple: `i586-elf32-minix`
- Components built: LLVM core, Clang, LLD, LLDB, 6 arch backends
- Build wrapper: 191 library Makefiles + 31 tool Makefiles in `external/bsd/llvm/`
- MINIX-specific patches in Clang: ~500 lines (Target info, Toolchain class, Driver)

### Proposed: Host Clang as Cross-Compiler (fast)

Clang is inherently a cross-compiler — just pass `--target=i586-elf32-minix`. This eliminates building LLVM entirely during `tools`.

```
build.sh tools (with host clang)
  ├── nbmake, sed, awk, yacc, lex, m4  ~20%
  ├── wrapper scripts for host clang     ~0%
  └── mkfs, partition, installboot       ~5%

  Skipped entirely:
  ├── llvm-lib (121 dirs)
  ├── llvm-clang, llvm-tblgen
  └── binutils (if using lld + integrated-as)
```

**Estimated tools build: 15-30% of current time.**

### Implementation Path: Host Clang Cross-Compilation

#### Mechanism: `EXTERNAL_TOOLCHAIN`

The build system already supports this (build.sh line ~1660, bsd.own.mk line ~400):

```bash
# When EXTERNAL_TOOLCHAIN is set, tools come from there instead of TOOLDIR
# Expected: ${EXTERNAL_TOOLCHAIN}/bin/${MACHINE_GNU_PLATFORM}-{tool}
# Example:  /opt/llvm/bin/i586-elf32-minix-clang
```

#### Wrapper Scripts Needed

Create in `${TOOLDIR}/bin/`:

```bash
# i586-elf32-minix-clang
exec /usr/bin/clang --target=i586-elf32-minix --sysroot=${DESTDIR} "$@"

# i586-elf32-minix-clang++
exec /usr/bin/clang++ --target=i586-elf32-minix --sysroot=${DESTDIR} "$@"

# i586-elf32-minix-ld → lld
exec /usr/bin/ld.lld "$@"

# i586-elf32-minix-ar → llvm-ar
exec /usr/bin/llvm-ar "$@"

# i586-elf32-minix-ranlib → llvm-ranlib
exec /usr/bin/llvm-ranlib "$@"

# i586-elf32-minix-nm → llvm-nm
exec /usr/bin/llvm-nm "$@"

# i586-elf32-minix-objcopy → llvm-objcopy
exec /usr/bin/llvm-objcopy "$@"

# i586-elf32-minix-objdump → llvm-objdump
exec /usr/bin/llvm-objdump "$@"

# i586-elf32-minix-strip → llvm-strip
exec /usr/bin/llvm-strip "$@"

# i586-elf32-minix-size → llvm-size
exec /usr/bin/llvm-size "$@"

# i586-elf32-minix-readelf → llvm-readobj
exec /usr/bin/llvm-readobj "$@"

# i586-elf32-minix-strings (no LLVM equivalent — use host strings)
exec /usr/bin/strings "$@"
```

#### Make Variable Changes (share/mk/bsd.own.mk)

```make
MKLLVM=no           # Don't build LLVM from source
MKBINUTILS=no       # Don't build GNU binutils
# EXTERNAL_TOOLCHAIN or wrapper scripts provide the tools
```

#### Bootstrap Problem

CRT files (`crt0.o`, `crti.o`, `crtn.o`) and libc are built by the cross-compiler, but `--sysroot` needs them to link. Solution:

1. Install headers first (no compiler needed)
2. Build CRT files + libc with `-nostdlib`
3. From then on, normal `--sysroot` works

#### genassym Special Case

`genassym` forces `-no-integrated-as` (bsd.own.mk line ~1113). This requires an external assembler (currently GNU `as`). Fix: patch genassym to work with integrated assembler, or keep just `as` from binutils.

#### Linker: elf_i386_minix

GNU ld uses a custom emulation `elf_i386_minix` (shell script in `external/gpl3/binutils/dist/ld/emulparams/`). For lld:

- lld doesn't use emulation scripts — it has native ELF support
- The MINIX emulation only sets: `OUTPUT_FORMAT="elf32-i386-minix"` and `ELF_INTERPRETER_NAME="/libexec/ld-elf.so.1"`
- Both can be passed as lld flags: `-m elf_i386` and `--dynamic-linker=/libexec/ld-elf.so.1`
- MINIX links statically by default, so the dynamic linker path is usually irrelevant

#### MINIX Triple in Upstream LLVM

MINIX is defined as an `OSType` in upstream LLVM (`Triple.h`), so host clang should recognize `--target=...-minix`. Verify with:

```bash
echo | clang --target=i586-elf32-minix -E -dM - | grep minix
# Should output: #define __minix 3 (etc.)
```

If the host clang is too old or doesn't have MINIX support, fallback: use `--target=i586-elf32-unknown` and define `__minix` manually via `-D`.

---

## Going GNU-Free

### Current GNU Dependencies

The **only** GNU component in the MINIX build process is **binutils**. Everything else is already BSD:

| Component | Current | Status |
|-----------|---------|--------|
| Compiler | Clang | Already BSD-licensed |
| Make | nbmake | BSD (NetBSD) |
| awk, sed, lex, yacc | nb-prefixed | BSD (NetBSD) |
| Linker | GNU ld | → Replace with lld |
| Assembler | GNU as | → Replace with clang integrated-as |
| ar, ranlib, nm, etc. | GNU binutils | → Replace with llvm-ar etc. |
| Autotools | Not used | BSD make system |

### LLVM Tool Availability in MINIX Tree

LLVM equivalents already have Makefiles in `external/bsd/llvm/bin/`:

- llvm-ar, llvm-nm, llvm-objdump, llvm-size, llvm-readobj: Makefiles exist
- lld: Available (controlled by `MKLLD`)
- llvm-objcopy, llvm-strip: Not in current build (would need adding, or use host tools)
- strings: No LLVM equivalent (use BSD `strings` from NetBSD)

### LLVM Custom Passes (minix/llvm/passes/)

MINIX has custom LLVM passes built as plugins:

- **ASR** (Address Space Randomization): Function/stack/heap randomization
- **Magic**: State tracking, memory instrumentation
- **Sectionify**: Custom ELF section placement
- **WeakAliasModuleOverride**: Symbol aliasing

These are independent plugins (`.so` files) and work regardless of LLVM version, **but** they use the Legacy Pass Manager API (LLVM 3.6). Modern LLVM uses the New Pass Manager — these would need porting if upgrading LLVM.

---

## UEFI Boot Support (Phase 1 implemented)

Branch: `uefi-boot`

### What Was Done

1. **EFI partition enabled**: `EFI_SIZE=10MiB` in `releasetools/x86_hdimage.sh`
2. **GRUB updated**: Tag `grub-2.06`, modules: `efi_gop serial` (replaces deprecated `efi_uga`)
3. **grub.cfg**: Serial terminal config, `novga=1` kernel parameter, serial as default
4. **Kernel early console**: `direct_con_mode` flag guards all VGA memory (0xB8000) and 6845 I/O port access; falls back to `ser_putc()` in serial-only mode
5. **pre_init.c**: Recognizes `novga=1` boot parameter, forces serial debug
6. **Console driver**: Validates BIOS Data Area VDU parameters before VGA access; sets `nr_cons=0` on UEFI (no VGA), disabling console driver entirely

### Hybrid Image

The HD image boots on both BIOS and UEFI:

| Partition | Type | Content |
|-----------|------|---------|
| MBR boot sector | — | bootxx_minixfs3 (BIOS) |
| 1 (ROOT) | 81 | MFS with kernel + modules |
| 2 (USR) | 81 | MFS |
| 3 (HOME) | 81 | MFS |
| 4 (EFI) | EF | FAT32 with GRUB EFI + grub.cfg |

### Testing

```bash
# UEFI boot
qemu-system-i386 -bios /usr/share/OVMF/OVMF.fd -m 256M \
  -drive file=minix_x86.img,if=ide,format=raw -serial stdio

# BIOS boot (regression)
qemu-system-i386 --enable-kvm -m 256 -hda minix_x86.img
```

### Phases 2+3 (planned)

- Phase 2: Early framebuffer console (pixel rendering on UEFI GOP)
- Phase 3: Full framebuffer console driver (login prompt on screen)
- See plan file: `.claude/plans/validated-sniffing-cook.md`

---

## Container Build Environment

### Dockerfile (Debian Trixie)

```bash
apt-get install -y \
  build-essential gcc g++ make git ca-certificates bc texinfo \
  bison flex zlib1g-dev \
  clang lld llvm llvm-dev \
  autoconf automake libtool python3 gettext pkg-config \
  qemu-system-x86 ovmf \
  openssh-client less vim-tiny curl
```

Build script: `build-container.sh` (uses buildah, mounts `$PWD` as `/workspace`).

### Environment Variables

```bash
HOST_CFLAGS="-fcommon -Wno-error=implicit-function-declaration -Wno-error=incompatible-pointer-types -Wno-error=int-conversion"
```

---

## Branch Overview

| Branch | Status | Content |
|--------|--------|---------|
| `master` | upstream | Clean upstream tracking |
| `optimize-buildtime` | pushed | MKPIC=no (halves library compilations) |
| `uefi-boot` | local | UEFI boot support Phase 1 (serial console) |
| `pae` | preserved | PAE experiment from 2016 (compilable, not runnable) |
