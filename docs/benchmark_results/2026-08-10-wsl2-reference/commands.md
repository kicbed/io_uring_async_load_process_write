# Reproduction Commands

These commands document the campaign shape. They intentionally use a scratch
directory outside the repository because benchmark inputs and outputs are
large and disposable.

```bash
campaign_root=/tmp/asyncdataloader-stage11-reference-3ba60cd
environment_id=wsl2-i7-12800hx-ext4-release-3ba60cd-20260810

cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release -j

mkdir -p "$campaign_root/raw" \
         "$campaign_root/outputs" \
         "$campaign_root/sweep" \
         "$campaign_root/analysis" \
         "$campaign_root/profiles"

dd if=/dev/zero of="$campaign_root/input-64m.bin" bs=1M count=64 status=progress
dd if=/dev/zero of="$campaign_root/input-256m.bin" bs=1M count=256 status=progress
dd if=/dev/zero of="$campaign_root/input-1g.bin" bs=1M count=1024 status=progress
dd if=/dev/zero of="$campaign_root/input-2g.bin" bs=1M count=2048 status=progress
dd if=/dev/zero of="$campaign_root/input-4g.bin" bs=1M count=4096 status=progress

sha256sum "$campaign_root"/input-*.bin > "$campaign_root/raw/input-sha256.txt"
stat -c '%n size_bytes=%s blocks_512=%b' "$campaign_root"/input-*.bin
```

The recorded campaign warmed the 256 MiB input once and did not drop caches:

```bash
./build-release/stage3_bench_pread \
  "$campaign_root/input-256m.bin" 1048576 1 >/dev/null
```

## Read Scans and Stage 3 Reference

```bash
./build-release/stage3_bench_pread \
  "$campaign_root/input-256m.bin" 1048576 10 \
  > "$campaign_root/raw/pread-scan.csv" \
  2> "$campaign_root/raw/pread-scan.stderr.txt"

./build-release/stage3_bench_mmap \
  "$campaign_root/input-256m.bin" 10 \
  > "$campaign_root/raw/mmap-scan.csv" \
  2> "$campaign_root/raw/mmap-scan.stderr.txt"

./build-release/stage3_bench_sync \
  "$campaign_root/input-256m.bin" \
  "$campaign_root/outputs/stage3-sync.bin" \
  1048576 3 \
  > "$campaign_root/raw/stage3-sync.csv" \
  2> "$campaign_root/raw/stage3-sync.stderr.txt"
```

## Stage 11.1: Overlap Ablation

```bash
./build-release/stage11_bench_end_to_end \
  "$campaign_root/input-256m.bin" \
  "$campaign_root/outputs/serial.bin" \
  "$campaign_root/outputs/no-overlap.bin" \
  "$campaign_root/outputs/overlap.bin" \
  1048576 8 4 5 \
  > "$campaign_root/raw/end-to-end.csv" \
  2> "$campaign_root/raw/end-to-end.stderr.txt"
```

## Stage 11.2: Read Backend Matrix

```bash
./build-release/stage11_bench_backends \
  "$campaign_root/input-256m.bin" \
  "$campaign_root/outputs" \
  1048576 8 4 2 5 \
  > "$campaign_root/raw/backends.csv" \
  2> "$campaign_root/raw/backends.stderr.txt"
```

## Stage 11.3/11.4: Parameter Matrix and Analysis

```bash
python3 benchmark/stage11_parameter_sweep.py \
  --executable ./build-release/preprocess_pipeline_demo \
  --input "$campaign_root/input-256m.bin" \
  --output-directory "$campaign_root/sweep" \
  --csv "$campaign_root/raw/parameter-matrix.csv" \
  --environment-id "$environment_id" \
  --block-sizes 1048576,4194304 \
  --buffers 3,8 \
  --queue-depths 1,4 \
  --backends sync,threadpool,uring \
  --thread-workers 2 \
  --iterations 5 \
  --rss-limit-mib 300

python3 benchmark/stage11_analyze_results.py \
  --input-csv "$campaign_root/raw/parameter-matrix.csv" \
  --output-directory "$campaign_root/analysis" \
  --minimum-samples 5 \
  --title "AsyncDataLoader WSL2 256 MiB Reference Matrix"
```

The bounded-RSS scale check repeated this command once for each input size,
changing `<size>` to `64m`, `256m`, `1g`, `2g`, and `4g`:

```bash
size=1g
python3 benchmark/stage11_parameter_sweep.py \
  --executable ./build-release/preprocess_pipeline_demo \
  --input "$campaign_root/input-$size.bin" \
  --output-directory "$campaign_root/sweep-rss-$size" \
  --csv "$campaign_root/raw/rss-scale-$size.csv" \
  --environment-id "$environment_id" \
  --block-sizes 8388608 \
  --buffers 24 \
  --queue-depths 8 \
  --backends auto \
  --thread-workers 2 \
  --iterations 1 \
  --rss-limit-mib 300
```

The five scale CSVs were then passed as separate `--input-csv` arguments to
`stage11_analyze_results.py`.

## Stage 11.5: System Evidence

The same command shape was captured separately for `sync`, `threadpool`, and
`uring`; this example shows Sync:

```bash
python3 benchmark/stage11_capture_profile.py \
  --tool strace \
  --output-directory "$campaign_root/profiles" \
  --label sync-b1m-b8-q4-strace \
  --environment-id "$environment_id" \
  -- \
  ./build-release/preprocess_pipeline_demo \
  "$campaign_root/input-256m.bin" \
  "$campaign_root/outputs/sync-profile.bin" \
  --backend=sync \
  --block-size=1048576 \
  --buffers=8 \
  --queue-depth=4 \
  --thread-workers=2 \
  --report-ms=0
```

The recorded perf attempt used the same pipeline command with `--tool perf`
and a unique label. It failed because the active WSL2 kernel's perf tools were
not installed; the wrapper's failure evidence was retained instead of being
reported as a profile.
