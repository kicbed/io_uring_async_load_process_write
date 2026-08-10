# Stage 12 Metrics Output

Stage 12 presents the metrics already produced by the real bounded pipeline.
It does not add a fourth processing stage and does not move data blocks.

```text
reader / processor / writer
          |
          | update bounded Counter, Gauge, and Histogram objects
          v
     MetricsRegistry
       |          |
       | live     | one final Snapshot after workers stop
       v          v
 terminal line   terminal summary + optional metrics.json
```

## Run the Demo

```bash
./build/preprocess_pipeline_demo \
  /path/to/input.bin \
  /path/to/output.bin \
  --backend=auto \
  --block-size=1048576 \
  --buffers=8 \
  --queue-depth=4 \
  --report-ms=250 \
  --metrics-json=/path/to/metrics.json
```

`--report-ms=N` controls the live terminal sampling interval. `0` disables
periodic progress without disabling final metrics. `--metrics-json=PATH` is
optional; its parent directory must already exist, and it must not name the
input or processed-output file.

## Terminal Output

Before work starts, the header identifies the input, output, requested and
selected backend, CPU stage, block size, pool size, and queue capacity.

While work runs, one progress sample has this shape:

```text
[############------------]  50.0% | 32.00 MiB / 64.00 MiB | 900.00 MiB/s | q 1/4,2/4 | buffers 5/8
```

- The first byte count is `pipeline.write.bytes`, so progress means bytes that
  reached the writer, not bytes merely read ahead.
- `q` shows current read-to-process and process-to-write queue depths.
- `buffers` shows current pool leases versus the configured maximum.
- The rate is a live observation from completed bytes divided by elapsed time,
  not a benchmark comparison.

On an interactive TTY, the same line is refreshed in place. When stdout is
redirected, every sample begins with `live` and receives its own line so log
files do not contain terminal cursor-control sequences. The final summary is
printed after the reporting thread has stopped.

The summary reports processed bytes and blocks, elapsed pipeline time,
throughput, buffer and queue high-water marks, average stage latencies, output
commit status, and bounded verification status. The elapsed interval covers
the pipeline and reliable processed-output publication; the later verification
pass is not added to pipeline throughput.

## Optional JSON Snapshot

The JSON document uses schema version 1:

```json
{
  "schema_version": 1,
  "status": "complete",
  "run": {
    "requested_backend": "auto",
    "selected_backend": "threadpool",
    "stage": "byte_increment",
    "input_bytes": 67108864
  },
  "pipeline_config": {
    "block_size": 1048576,
    "max_inflight_buffers": 8,
    "queue_depth": 4,
    "buffer_alignment": 4096,
    "buffer_pool_bytes": 8388608
  },
  "result": {
    "blocks_written": 64,
    "bytes_written": 67108864,
    "output_committed": true,
    "verification": "passed"
  },
  "metrics": {
    "counters": [],
    "gauges": [],
    "histograms": []
  }
}
```

The real file also includes input/output paths, elapsed time, throughput, every
registered counter, current and high-water gauge values, and fixed histogram
bounds and bucket counts. Strings are JSON-escaped and non-finite floating
point values are rejected.

The final `MetricsRegistry::Snapshot` is taken after all reader, processor, and
writer threads have stopped and after output verification succeeds. Therefore
the final counts are stable. Live samples are deliberately non-transactional:
they are a low-cost operational view while atomics are still changing.

## Reliable JSON Publication

`write_metrics_json_atomic()` follows the same reliability idea used for the
processed output:

```text
render one bounded final snapshot
  -> mkstemp beside metrics.json
  -> complete write, retrying EINTR and short writes
  -> fsync temporary file
  -> rename over metrics.json
  -> fsync parent directory
```

Before rename, RAII removes an unfinished temporary file on failure. Because
the temporary file is in the same directory, rename is atomic. If JSON
publication fails after the processed output was committed, the processed file
remains valid but the command returns an error because the explicitly requested
metrics artifact was not delivered reliably.

The JSON string is bounded by the registry limits (at most 64 metrics and fixed
histogram buckets) plus fixed run metadata. It never contains one entry per
input block, so its memory does not grow with file size.

## Compatibility and Boundaries

The original newline-delimited `key=value` records remain at the end of stdout.
Stage 11 sweep and profiling tools continue to parse them. Benchmark commands
should normally use `--report-ms=0` to avoid terminal sampling noise.

The reporter borrows stable metric references and owns only one optional
`std::jthread` plus small formatting data. It never owns a `BufferHandle`,
pushes a queue item, changes backpressure, selects a backend, or changes output
ordering. HTTP serving, dashboards, databases, and remote metric systems are
outside Stage 12.
