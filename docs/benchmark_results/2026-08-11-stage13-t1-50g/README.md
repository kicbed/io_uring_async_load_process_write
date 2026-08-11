# Stage 13 T1 50 GiB Acceptance Record

This bundle records the single bounded-memory acceptance run executed on
2026-08-11. It is an acceptance result, not a multi-sample performance study.

## Result

- input: one ext4-preallocated 50 GiB zero-filled file
  (`53,687,091,200` bytes, not a sparse logical-length-only file);
- input SHA-256:
  `ab743e145f643a1f6237b7390baf2e6edc71d83997f5bf4ed40d975fb50ba423`;
- configuration: 8 MiB blocks, 24 buffers, queue depth 8, two ThreadPool
  workers configured, requested backend Auto;
- selected backend: io_uring;
- result: 6,400 blocks and all 53,687,091,200 bytes written;
- correctness: bounded output verification passed;
- backpressure observations: in-flight peak 19/24; both queue peaks 8/8;
- configured BufferPool payload: 201,326,592 bytes (192 MiB);
- whole child-process peak RSS: 159,640 KiB;
- configured acceptance RSS limit: 300 MiB, passed;
- measured pipeline/commit elapsed value: 435,414.277 ms;
- complete sweep launcher wall time, including the demo's bounded verification:
  523.54 seconds.

This satisfies T1 for this recorded environment: the 50 GiB file completed,
output was verified, and RSS stayed below the declared limit. It does not
satisfy T1b because the required 200 GiB point was intentionally not run.

The one observed throughput value is retained in the raw CSV for auditability,
but it is not used as a general performance claim. The run had one sample, a
zero-filled preallocated input, no cache drop, and a WSL2 virtualized storage
path.

## Files

- `environment.md`: hardware/software/filesystem and cache boundary;
- `commands.md`: exact preparation and sweep commands;
- `raw/t1-50g.csv`: untouched header and data row printed by the sweep tool;
- `raw/input-sha256.txt`: input identity;
- `raw/command-observations.txt`: allocation and outer-command observations.

The 50 GiB input and generated output were temporary and were removed after
these records were inspected.
