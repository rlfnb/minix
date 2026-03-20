# Pending Fixes & Briefing for Next Session

## Immediate Task: Apply Source Fixes for GCC 14 Compatibility

These two fixes are needed before the MINIX tools build succeeds on Debian Trixie (GCC 14). They are currently uncommitted on the `uefi-boot` branch.

### Fix 1: binutils gold — missing `#include <string>`

**File**: `external/gpl3/binutils/dist/gold/errors.h`

**Problem**: `std::string` used at line 87 without including `<string>`. Worked before due to transitive includes in older libstdc++, fails with GCC 14 / libstdc++14.

**Fix**: Add `#include <string>` after `#include <cstdarg>`:

```diff
 #include <cstdarg>
+#include <string>

 #include "gold-threads.h"
```

### Fix 2: LLVM ValueMap — unique_ptr to bool conversion

**File**: `external/bsd/llvm/dist/llvm/include/llvm/IR/ValueMap.h`

**Problem**: Line 104: `return MDMap;` tries to implicitly convert `std::unique_ptr` to `bool`, which is disallowed in C++17 / GCC 14.

**Fix**: Make the conversion explicit:

```diff
-  bool hasMD() const { return MDMap; }
+  bool hasMD() const { return MDMap != nullptr; }
```

---

## Briefing: What To Do Next

### Priority 1: Get MINIX Building in Container

1. **Build the container**: `./build-container.sh` (uses buildah, Debian Trixie base)
2. **Apply the two fixes above**
3. **Run the full build**:
   ```bash
   # In container with /workspace mounted:
   HOST_CFLAGS="-fcommon -Wno-error=implicit-function-declaration -Wno-error=incompatible-pointer-types -Wno-error=int-conversion" \
     ./build.sh -m i386 -U -j$(nproc) obj tools distribution
   ```
   Note: `obj` first to create all object directories, preventing `nbmkdep` path errors.
4. **Build the HD image** (includes UEFI EFI partition):
   ```bash
   cd releasetools && sh x86_hdimage.sh
   ```
5. **Test UEFI boot**:
   ```bash
   qemu-system-i386 -bios /usr/share/OVMF/OVMF.fd -m 256M \
     -drive file=minix_x86.img,if=ide,format=raw -serial stdio
   ```

### Priority 2: Replace Cross-Toolchain with Host Clang

This is the big build-time optimization — eliminate 60-80% of tools build by using the host's Clang as cross-compiler instead of building LLVM 3.6.1 from source.

**Approach**:
1. Modify `build.sh` to add a `--host-clang` mode
2. Create wrapper scripts in `${TOOLDIR}/bin/` that call host clang/lld/llvm-tools with `--target=i586-elf32-minix --sysroot=${DESTDIR}`
3. Set `MKLLVM=no MKBINUTILS=no` to skip building LLVM and GNU binutils
4. Handle bootstrap: headers first, then CRT files with `-nostdlib`, then full build
5. Fix `genassym` to work with integrated assembler (remove `-no-integrated-as`)

**Key files to modify**:
- `build.sh` — add host-clang mode, create wrappers
- `share/mk/bsd.own.mk` — tool selection when using external clang
- `share/mk/bsd.sys.mk` — genassym assembler flag

**Verification**: First confirm host clang recognizes MINIX triple:
```bash
echo | clang --target=i586-elf32-minix -E -dM - | grep minix
```

### Priority 3: UEFI Boot Testing & Phase 2

Once the build works, test UEFI boot (Phase 1 is already implemented on this branch). Then proceed to Phase 2 (framebuffer console) per the plan in `.claude/plans/validated-sniffing-cook.md`.

---

## Current Branch State

```
uefi-boot (2 commits ahead of master):
  3f6c27dcd  Disable PIC and shared library builds for MINIX
  7f74d2d17  Add UEFI boot support via GRUB 2 EFI with serial console

Uncommitted:
  M external/bsd/llvm/dist/llvm/include/llvm/IR/ValueMap.h  (Fix 2)
  ? JOURNAL.md
  ? FIXES.md
  ? build-container.sh
```

The `errors.h` fix (Fix 1) was applied in a previous build attempt but got lost — needs to be reapplied.

## Key Documentation

- `JOURNAL.md` — Full knowledge base (build env, toolchain architecture, UEFI boot, GNU-free path)
- `.claude/plans/validated-sniffing-cook.md` — UEFI boot implementation plan (Phases 1-3)
