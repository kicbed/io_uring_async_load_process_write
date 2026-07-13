# AsyncDataLoader Benchmark Methodology

## Current Status

Stage 3 provides reusable latency statistics, a shared CSV schema, and three
benchmark executables. No official performance result has been collected yet.
Tiny CTest inputs verify behavior only; their timings are not benchmark data.

Official sync/threadpool/io_uring/pipeline results belong to Stage 11 and must
be produced from recorded commands and environments.

## Workload Families

| Executable | CSV name | Timed work per iteration | Writes output | Valid comparison |
|---|---|---|---|---|
| `stage3_bench_sync` | `sync_baseline` | open, block allocation, `pread`, CPU case transform, `pwrite`, `fsync`, close | yes | future end-to-end pipelines with the same transform and durability |
| `stage3_bench_pread` | `pread_scan` | sequential `pread` into one bounded buffer plus byte checksum | no | `mmap_scan` under the same file/cache conditions |
| `stage3_bench_mmap` | `mmap_scan` | `mmap`, sequential byte scan plus checksum, `munmap` | no | `pread_scan` under the same file/cache conditions |

`sync_baseline` and `mmap_scan` are not directly comparable. The former is a
complete read-process-write workload with durable output; the latter is a
read-only scan. A shared CSV format does not make different workloads equal.

## Measurement Boundaries

- `sync_baseline` measures the full baseline function, including file open,
  output truncation, block allocation, transform, write, `fsync`, and RAII
  close. Each iteration rewrites the output from offset zero.
- `pread_scan` opens the input and allocates its bounded block before the
  iteration timers. Each sample covers all `pread` calls and checksum work.
- `mmap_scan` opens and stats the input before the iteration timers. Each sample
  covers mapping, sequential access, and unmapping.
- Throughput always means input bytes scanned or processed divided by measured
  time. Written bytes are reported separately and are not added to throughput.
- Average, P50, P95, and P99 are calculated from per-iteration elapsed times.
  Percentiles use the nearest-rank definition.

## CLI

Build benchmark binaries in Release mode:

```bash
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release -j
```

Run the end-to-end synchronous baseline:

```bash
./build-release/stage3_bench_sync \
  <input> <output> <block_size_bytes> [iterations]
```

Run the comparable scan microbenchmarks:

```bash
./build-release/stage3_bench_pread \
  <input> <block_size_bytes> [iterations]

./build-release/stage3_bench_mmap \
  <input> [iterations]
```

`iterations` defaults to 1 and is limited to 1,000,000. `block_size_bytes`
must be positive and is limited to 1 GiB. These limits bound benchmark-owned
sample and block memory; they are not the final pipeline memory configuration.

## CSV Schema

```text
name,bytes_per_iteration,bytes_written_per_iteration,sample_count,total_elapsed_ms,average_ms,p50_ms,p95_ms,p99_ms,throughput_mib_s
```

| Field | Meaning |
|---|---|
| `name` | Stable workload identifier |
| `bytes_per_iteration` | Input bytes read or scanned in one iteration |
| `bytes_written_per_iteration` | Output bytes written in one iteration |
| `sample_count` | Number of completed timed iterations |
| `total_elapsed_ms` | Sum of all per-iteration elapsed times |
| `average_ms` | Arithmetic mean iteration latency |
| `p50_ms` | Nearest-rank 50th percentile latency |
| `p95_ms` | Nearest-rank 95th percentile latency |
| `p99_ms` | Nearest-rank 99th percentile latency |
| `throughput_mib_s` | Total input MiB divided by total measured seconds |

## Fair Test Protocol

1. Use the same Release build, machine, kernel, compiler, filesystem, storage
   device, and input file for all rows in one comparison.
2. Use a real allocated input file. Record whether it is sparse, compressed by
   the filesystem, or generated from another source.
3. Record exact input size and a checksum so every method receives identical
   bytes.
4. For `pread_scan` versus `mmap_scan`, use the same iteration count and run
   them close together. Record the `pread` block size.
5. State the cache policy. Repeated iterations usually mix the first page-fault
   pass with later page-cache hits. Do not label a run "cold cache" unless the
   cache-reset procedure is explicitly recorded.
6. Do not automate global Linux cache dropping in this repository. It requires
   privileges, affects other workloads, and can make results less reproducible.
7. For P95/P99 interpretation, use enough samples. With only a few samples,
   nearest-rank P99 is effectively the maximum and is statistically weak.
8. Keep background load, CPU power policy, and thermal conditions stable. Run
   methods in alternating order when practical to reduce time-order bias.
9. Place sync output on the recorded target filesystem and ensure enough free
   space. Its timing includes `fsync`, so storage durability behavior matters.
10. Save raw CSV before writing conclusions. Report regressions and cases where
    mmap or a fallback wins; do not force an io_uring-faster narrative.

## Environment Record Template

```text
date:
git_commit:
build_type: Release
compiler_and_version:
cmake_version:
kernel:
cpu:
memory:
storage_device:
filesystem:
mount_options:
cpu_governor:
input_path:
input_size_bytes:
input_checksum:
input_sparse_or_compressed:
output_path_and_filesystem:
cache_policy:
background_load_notes:
iterations:
pread_block_size_bytes:
raw_csv_path:
```

## Result Record Templates

Comparable read-only scans:

| Environment ID | Input size | Cache policy | Iterations | Method | Block size | Average ms | P95 ms | P99 ms | MiB/s |
|---|---:|---|---:|---|---:|---:|---:|---:|---:|
| pending | pending | pending | pending | `pread_scan` | pending | pending | pending | pending | pending |
| pending | pending | pending | pending | `mmap_scan` | n/a | pending | pending | pending | pending |

End-to-end preprocessing:

| Environment ID | Input size | Iterations | Method | Block size | Bytes written | Average ms | P95 ms | MiB/s | Output verified |
|---|---:|---:|---|---:|---:|---:|---:|---:|---|
| pending | pending | pending | `sync_baseline` | pending | pending | pending | pending | pending | pending |
| pending | pending | pending | future pipeline | pending | pending | pending | pending | pending | pending |

## Interpretation Guardrails

- Coroutines organize asynchronous state; they are not themselves a performance
  source.
- `io_uring` is not assumed to be fastest for every file size, queue depth,
  filesystem, or cache state.
- mmap uses demand paging and the page cache. Mapping a whole file is not the
  final bounded-buffer pipeline design.
- The serial baseline is retained as a correctness oracle and end-to-end
  reference. It is not the final pipeline architecture.
- Stage 3 establishes measurement mechanics. Stage 11 records full backend and
  pipeline results with `strace`/`perf` evidence where useful.
