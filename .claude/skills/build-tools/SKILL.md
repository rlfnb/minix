---
name: build-tools
description: Build MINIX cross-compilation tools (with or without --host-clang)
disable-model-invocation: true
argument-hint: "[--host-clang] [-j N]"
allowed-tools: Bash, Read, Grep
---

Build the MINIX cross-compilation toolchain. Arguments are passed to build.sh.

Default (host clang, parallel):
```
HOST_CFLAGS="-fcommon -Wno-error=implicit-function-declaration -Wno-error=incompatible-pointer-types -Wno-error=int-conversion" \
  ./build.sh -m i386 -U --host-clang -j$(nproc) tools
```

If `$ARGUMENTS` is provided, incorporate those flags. Common variants:
- `/build-tools` — host clang, parallel build (default)
- `/build-tools --no-host-clang` — build LLVM from source (slow, ~60 min)
- `/build-tools -j1` — sequential build for debugging errors

Steps:
1. Ensure we are in /workspace
2. Run the build command with HOST_CFLAGS set
3. If the build fails, read the error output and suggest fixes
4. On success, report the TOOLDIR path and time taken
