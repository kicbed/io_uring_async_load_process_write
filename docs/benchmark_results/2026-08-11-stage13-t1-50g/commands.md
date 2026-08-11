# Commands

The run used one unique temporary root on `/tmp`, which is on the same ext4
filesystem as the workspace.

```bash
mkdir /tmp/asyncdataloader-stage13-t1-50g-20260811
mkdir /tmp/asyncdataloader-stage13-t1-50g-20260811/output
fallocate -l 50G \
  /tmp/asyncdataloader-stage13-t1-50g-20260811/input.bin

stat --format='input_bytes=%s allocated_512b_blocks=%b' \
  /tmp/asyncdataloader-stage13-t1-50g-20260811/input.bin
du -B1 /tmp/asyncdataloader-stage13-t1-50g-20260811/input.bin

/usr/bin/time \
  -f 'checksum_wall_seconds=%e checksum_max_rss_kib=%M' \
  sha256sum \
  /tmp/asyncdataloader-stage13-t1-50g-20260811/input.bin

/usr/bin/time \
  -f 'sweep_wall_seconds=%e sweep_launcher_max_rss_kib=%M' \
  python3 benchmark/stage11_parameter_sweep.py \
  --executable ./build-release/preprocess_pipeline_demo \
  --input /tmp/asyncdataloader-stage13-t1-50g-20260811/input.bin \
  --output-directory /tmp/asyncdataloader-stage13-t1-50g-20260811/output \
  --csv /tmp/asyncdataloader-stage13-t1-50g-20260811/t1-50g.csv \
  --environment-id stage13-20260811-wsl2-ext4 \
  --block-sizes 8388608 \
  --buffers 24 \
  --queue-depths 8 \
  --backends auto \
  --thread-workers 2 \
  --iterations 1 \
  --rss-limit-mib 300
```

The sweep tool removed its generated output in a `finally` block. After the
CSV was inspected and archived, the exact temporary input/CSV paths and their
now-empty directories were removed.
