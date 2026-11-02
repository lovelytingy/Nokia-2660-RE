# Filesystem

Status: **stub — help wanted**

MOCOR feature-phone images are not a typical Android `system.img`. Likely candidates after deeper RE of `EXEC_USER_IMAGE.bin` / `EXEC_KERNEL_IMAGE.bin`:

- custom flash layout / FTL volumes
- resource packs (see `mmi_res_240X320_MH.bin`)

Record mount points, volume names, and tools that can unpack them once found.
