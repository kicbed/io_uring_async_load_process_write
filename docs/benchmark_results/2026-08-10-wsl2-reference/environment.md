# Recorded Environment

## Identity

| Field | Recorded value |
|---|---|
| Date | 2026-08-10 (Asia/Shanghai) |
| Environment ID | `wsl2-i7-12800hx-ext4-release-3ba60cd-20260810` |
| Git commit | `3ba60cdc4c4d3f64c8b5fda6ff417a56209717e3` |
| Build type | Release |
| Compiler | GCC 11.4.0 |
| CMake | 3.22.1 |
| liburing | 2.0 |
| strace | 5.16 |
| Kernel | Linux 6.18.33.2-microsoft-standard-WSL2 |
| CPU | 12th Gen Intel Core i7-12800HX, 24 logical CPUs exposed |
| Memory exposed to WSL2 | 16,625,881,088 bytes |
| Root storage | `/dev/sdd`, ext4, mounted at `/` |
| Mount options | `rw,relatime,discard,errors=remount-ro,data=ordered` |
| CPU governor | Not exposed by this WSL2 kernel |

Input, scratch output, and the repository build were all on the recorded ext4
filesystem. Background load was not isolated; this was a normal interactive
WSL2 session.

## Input Files

The files were zero-filled and physically allocated. `stat` reported at least
the logical size allocated for every input; the extra 4096 bytes shown for
some large files are filesystem allocation granularity, not input data.

| File | Logical bytes | Allocated bytes | SHA-256 |
|---|---:|---:|---|
| `input-64m.bin` | 67,108,864 | 67,108,864 | `3b6a07d0d404fab4e23b6d34bc6696a6a312dd92821332385e5af7c01c421351` |
| `input-256m.bin` | 268,435,456 | 268,435,456 | `a6d72ac7690f53be6ae46ba88506bd97302a093f7108472bd9efc3cefda06484` |
| `input-1g.bin` | 1,073,741,824 | 1,073,745,920 | `49bc20df15e412a64472421e13fe86ff1c5165e18b2afccf160d4dc19fe68a14` |
| `input-2g.bin` | 2,147,483,648 | 2,147,487,744 | `a7c744c13cc101ed66c29f672f92455547889cc586ce6d44fe76ae824958ea51` |
| `input-4g.bin` | 4,294,967,296 | 4,294,971,392 | `8479e43911dc45e89f934fe48d01297e16f51d17aa561d4d1c216b1ae0fcddca` |

The generated inputs and outputs are intentionally not stored in Git. The
checksums and raw CSV preserve their identity without committing gigabytes of
benchmark data.

## Cache and Run Policy

- The 256 MiB input was read once before the recorded scans.
- The harness did not drop, evict, or otherwise reset the Linux page cache.
- Scan and backend method order was rotated where supported by the executable.
- Stage 11.3 launched a fresh C++ process for every raw sample so maximum RSS
  belonged to one configuration.
- Stage 11.1/11.2 samples include processing, read/write, and output `fsync` in
  their timers; file open/close and bounded verification are outside them.
- Stage 11.3 includes reliable temporary-file publication in elapsed time, and
  GNU `time` observes the complete process including bounded verification.
- No claim of cold-cache behavior, isolated CPU frequency, or statistically
  stable tail latency is made.

## Backend Availability

An explicit io_uring 64 MiB smoke run completed and reported
`selected_backend=io_uring`; its output verification passed. Auto selected
io_uring throughout the recorded scale check.

`strace` worked. `/usr/bin/perf` was present only as a wrapper and could not
find tools matching the active WSL2 kernel. Its exit status 2 and diagnostic
text are archived under `profiles/sync-b1m-b8-q4-perf-unavailable/`.
