---
name: test-qemu
description: Boot MINIX in QEMU for testing (UEFI or BIOS)
disable-model-invocation: true
argument-hint: "[uefi|bios]"
allowed-tools: Bash, Read, Grep
---

Boot the MINIX HD image in QEMU. Default: BIOS mode.

UEFI boot:
```bash
qemu-system-i386 -bios /usr/share/OVMF/OVMF.fd -m 256M \
  -drive file=minix_x86.img,if=ide,format=raw -serial stdio -nographic
```

BIOS boot:
```bash
qemu-system-i386 -m 256 -hda minix_x86.img -serial stdio -nographic
```

If `$ARGUMENTS` contains "uefi", use UEFI mode. Otherwise BIOS.

Steps:
1. Find the HD image (check releasetools/ and current directory)
2. Launch QEMU with appropriate flags
3. Note: use -nographic for headless environments
4. If the image is missing, suggest running /build-image first
