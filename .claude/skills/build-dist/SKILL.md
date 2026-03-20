---
name: build-dist
description: Build MINIX distribution (obj + distribution targets)
disable-model-invocation: true
argument-hint: "[-j N]"
allowed-tools: Bash, Read, Grep
---

Build the full MINIX distribution after tools have been built.

This runs two steps to avoid nbmkdep path errors:
1. `./build.sh -m i386 -U obj` — create all object directories
2. `./build.sh -m i386 -U -j$(nproc) distribution` — cross-compile everything

```bash
HOST_CFLAGS="-fcommon -Wno-error=implicit-function-declaration -Wno-error=incompatible-pointer-types -Wno-error=int-conversion" \
  ./build.sh -m i386 -U obj && \
HOST_CFLAGS="-fcommon -Wno-error=implicit-function-declaration -Wno-error=incompatible-pointer-types -Wno-error=int-conversion" \
  ./build.sh -m i386 -U -j$(nproc) distribution
```

If `$ARGUMENTS` contains `-j N`, use that parallelism instead.

Steps:
1. Verify TOOLDIR exists (tools must be built first)
2. Run obj target
3. Run distribution target
4. Report success/failure and DESTDIR path
