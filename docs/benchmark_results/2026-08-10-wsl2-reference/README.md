# Stage 11 WSL2 Reference Campaign

This directory archives one real Stage 11 benchmark campaign. All commands
used the Release binaries from commit
`3ba60cdc4c4d3f64c8b5fda6ff417a56209717e3` on the same WSL2/ext4
environment.

This is a reference result, not a universal backend ranking. The cache was
warmed once and was not reset between samples. The machine was an interactive
WSL2 environment, and several samples had large storage-related tails. The raw
rows are retained so those tails are visible rather than averaged away.

## Experiment Shape

- Main comparison input: 256 MiB.
- Parameter matrix: 1/4 MiB blocks, 3/8 buffers, queue capacities 1/4, and
  Sync/ThreadPool/io_uring; five fresh processes per exact configuration.
- Bounded-RSS scale check: 64 MiB, 256 MiB, 1 GiB, 2 GiB, and 4 GiB with an
  8 MiB block, 24 buffers, queue capacity 8, and a 300 MiB RSS limit.
- Processing: `ByteIncrementStage`, which changes every valid byte by `+1`
  modulo 256.
- Every accepted pipeline sample verified its complete output and checked that
  observed queue and in-flight peaks did not exceed configuration.
- Inputs were zero-filled but physically allocated files. Byte increment is
  data-independent, so zeros do not bypass the processing loop.

See [environment.md](environment.md) for the complete environment and cache
policy and [commands.md](commands.md) for reproduction commands.

## Read-Only Scan Reference

| Method | Samples | Average ms | P95 ms | MiB/s |
|---|---:|---:|---:|---:|
| bounded 1 MiB `pread` scan | 10 | 84.415 | 88.192 | 3032.647 |
| `mmap` sequential scan | 10 | 69.345 | 73.057 | 3691.713 |

In this warm read-only scan, mmap observed about 1.217x the throughput of the
bounded pread scan. This does not make mmap the production pipeline: the scan
does not process or durably write output, and mapping the input is a different
ownership model from the bounded BufferPool path.

The older Stage 3 synchronous end-to-end baseline measured 215.705 ms average
and 1186.806 MiB/s over three samples. It uses the Stage 3 case transform, not
Stage 11's byte-increment transform, so its absolute value is not merged with
the Stage 11 comparisons.

Raw evidence: [pread-scan.csv](raw/pread-scan.csv),
[mmap-scan.csv](raw/mmap-scan.csv), and
[stage3-sync.csv](raw/stage3-sync.csv).

## Serial, No-Overlap, and Overlap

All three rows use blocking pread and pwrite. The two pipeline rows keep the
same reader/processor/writer threads and bounded queues; only the BufferPool
capacity changes from one buffer (no cross-block overlap) to eight buffers
(overlap permitted).

| Execution | Samples | Average ms | P50 ms | P95 ms | MiB/s |
|---|---:|---:|---:|---:|---:|
| one-thread serial oracle | 5 | 171.303 | 157.449 | 217.043 | 1494.425 |
| three-stage, one buffer | 5 | 1416.868 | 175.967 | 6319.895 | 180.680 |
| three-stage, eight buffers | 5 | 465.935 | 158.101 | 1719.984 | 549.433 |

The median values were close, while one long sample strongly affected each
pipeline aggregate. The overlap row had a much smaller long tail than the
one-buffer control, but this five-sample WSL2 run does **not** establish a
reliable end-to-end speedup over serial. It establishes a fair runnable
ablation and preserves the unexpected result for later investigation.

Raw evidence: [end-to-end.csv](raw/end-to-end.csv) and
[end-to-end.stderr.txt](raw/end-to-end.stderr.txt).

## Read Backend Matrix

The processor, writer, BufferPool, queues, transform, block size, and output
verification stayed fixed. Only the reader mechanism changed.

| Requested -> selected | Samples | Average ms | P50 ms | P95 ms | MiB/s |
|---|---:|---:|---:|---:|---:|
| Sync -> Sync | 5 | 836.391 | 146.687 | 3535.484 | 306.077 |
| ThreadPool -> ThreadPool | 5 | 1056.333 | 197.760 | 4567.817 | 242.348 |
| io_uring -> io_uring | 5 | 156.815 | 144.473 | 210.075 | 1632.500 |
| Auto -> io_uring | 5 | 155.886 | 146.301 | 198.349 | 1642.227 |

io_uring and Auto avoided the large tail observed in the other two rows in
this particular run. Auto is not a fourth I/O implementation: it selected
io_uring and then ran that backend. The wider parameter matrix below shows why
this one table must not be generalized into “io_uring is always fastest.”

Raw evidence: [backends.csv](raw/backends.csv) and
[backends.stderr.txt](raw/backends.stderr.txt).

## Parameter Matrix

The matrix contains 120 verified child-process samples in 24 exact groups.
Across the eight block/buffer/queue configurations, the fastest aggregate
mechanism was Sync three times, io_uring three times, and ThreadPool twice.
The best aggregate in this matrix was io_uring at 1801.804 MiB/s for a 1 MiB
block, eight buffers, and queue capacity one. Other groups contain multi-second
single-sample tails, including an io_uring group, so the campaign supports no
universal winner and no timing-only causal claim.

Whole-process peak RSS ranged from 6.66 to 35.76 MiB in this 256 MiB matrix;
all rows remained below the 300 MiB guard and within configured queue/buffer
bounds.

- Full generated report: [matrix/analysis.md](matrix/analysis.md)
- Grouped data: [matrix/summary.csv](matrix/summary.csv)
- Raw samples: [raw/parameter-matrix.csv](raw/parameter-matrix.csv)
- Charts: [throughput](matrix/throughput.svg) and
  [peak RSS](matrix/peak_rss.svg)

## Bounded-RSS Scale Observation

The same configuration was used for every row: Auto selected io_uring,
8 MiB blocks, 24 pool buffers (192 MiB configured payload), queue capacity 8,
two fallback workers, one fresh process, and a 300 MiB RSS limit.

| Input | Peak RSS KiB | Peak RSS MiB | In-flight peak | Verified/bounds |
|---:|---:|---:|---:|---|
| 64 MiB | 53,100 | 51.86 | 6 | passed |
| 256 MiB | 102,256 | 99.86 | 13 | passed |
| 1 GiB | 159,452 | 155.71 | 19 | passed |
| 2 GiB | 159,472 | 155.73 | 19 | passed |
| 4 GiB | 159,464 | 155.73 | 19 | passed |

RSS rose while short inputs activated more pool slots, then plateaued at about
155.7 MiB from 1 to 4 GiB. This is useful small-scale evidence that memory is
bounded by configuration rather than file size. It is **not** the checklist's
formal T1/T1b result, which still requires real 50 GiB and 200 GiB inputs under
the declared acceptance protocol.

- Generated report: [rss-scale/analysis.md](rss-scale/analysis.md)
- Raw rows: [raw/](raw/)
- Charts: [throughput](rss-scale/throughput.svg) and
  [peak RSS](rss-scale/peak_rss.svg)

The 2 GiB row had a 9.14 s elapsed-time outlier. Its RSS and correctness
observations remain valid, but its throughput must not be treated as a stable
performance estimate.

## System Evidence

`strace -f -c` ran the same 256 MiB, 1 MiB block, eight-buffer, queue-four
pipeline once per explicit backend:

| Backend | `pread64` | `pwrite64` | `fsync` | `futex` | Backend-specific calls |
|---|---:|---:|---:|---:|---|
| Sync | 775 | 256 | 2 | 1480 | none |
| ThreadPool | 775 | 256 | 2 | 2740 | none |
| io_uring | 518 | 256 | 2 | 1504 | 1 `io_uring_setup`, 257 `io_uring_enter` |

The whole command includes startup and bounded post-run verification. The
io_uring calls confirm that its reader used SQE/CQE completion; they also match
the current one-read-at-a-time design, not batched read-depth scaling. The
ThreadPool run had more futex calls, consistent with extra worker coordination,
but syscall counts alone do not prove the cause of a timing tail.

The installed `perf` wrapper exited with status 2 because tools for the active
WSL2 kernel were unavailable. The failure manifest and stderr are retained;
no hardware-counter result is claimed.

Evidence: [profiles/](profiles/).

## Technical Conclusion

This campaign demonstrates that the project can run reproducible, verified,
bounded experiments and can preserve inconvenient results. Backend choice is
environment- and configuration-dependent. Coroutines organize suspension and
resumption; they are not themselves a speedup. The current io_uring reader has
one request outstanding, so queue capacity is pipeline backpressure capacity,
not io_uring batch depth.

The result bundle closes Stage 11's measurement tooling and reference-run
deliverable. It does not claim statistical significance, cold-cache behavior,
formal 50/200 GiB acceptance, out-of-order processing, or a CPU-heavy final
workload.
