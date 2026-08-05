# Stage 8: `O_DIRECT` Alignment Boundary

Stage 8 prepares aligned reusable memory; it does not yet open files with
`O_DIRECT` or claim that direct I/O is faster.

## The Three Alignment Dimensions

An `O_DIRECT` operation may constrain all three of these values:

1. the user-space buffer address;
2. the I/O length;
3. the file offset.

The exact rules vary by filesystem, file, device, and kernel version. A
misaligned request may fail with `EINVAL`, while some environments may fall
back to buffered I/O. Therefore, `4096` is a configurable conservative default,
not a universal Linux rule.

Since Linux 6.1, callers can request `STATX_DIOALIGN` through `statx(2)` when
the filesystem supports it:

- `stx_dio_mem_align` describes the required user-buffer alignment;
- `stx_dio_offset_align` describes the required offset and I/O-length
  alignment.

If those fields are unavailable, the application needs a documented
filesystem-specific rule or a safe buffered-I/O fallback.

## What Stage 8 Guarantees

`AlignedBuffer` allocates with `posix_memalign()`. `PipelineConfig` rejects an
alignment that is not a power of two or is not a multiple of `sizeof(void*)`,
and requires `block_size` to be a multiple of `buffer_alignment`.

This establishes:

```text
aligned buffer address + aligned configured block size
```

It does not yet establish:

```text
runtime filesystem support + aligned file offset + aligned final request
```

Those checks belong to the future backend/pipeline integration.

## Final Partial Block

The last logical block can contain fewer valid bytes than `block_size`. A
future direct-I/O path must not blindly submit an unaligned length or write
padding as real output. It must deliberately choose a strategy such as an
aligned request plus valid-byte tracking, or a documented buffered tail path,
while preserving the correct final file length.

## Performance Boundary

`O_DIRECT` changes page-cache behavior and alignment responsibilities. It is
not automatically faster. Stage 11 must compare it under the actual workload
and report measured results without assuming that direct I/O or io_uring wins.

## References

- Linux `open(2)`: <https://man7.org/linux/man-pages/man2/open.2.html>
- Linux `statx(2)`: <https://man7.org/linux/man-pages/man2/statx.2.html>
- Linux `posix_memalign(3)`: <https://man7.org/linux/man-pages/man3/posix_memalign.3.html>
