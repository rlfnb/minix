---
name: build-image
description: Build MINIX HD image from distribution
disable-model-invocation: true
allowed-tools: Bash, Read, Grep
---

Build the MINIX x86 HD image after distribution has been built.

```bash
cd /workspace/releasetools && sh x86_hdimage.sh
```

Steps:
1. Verify DESTDIR has the distribution files (`/etc/mtree/set.base`)
2. Run x86_hdimage.sh
3. Report the output image path and size
