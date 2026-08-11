# Environment

```text
date: 2026-08-11
git_commit: 6777cfb8f8d501a8a4c952490d39a70054287d62
working_tree: dirty with the in-progress Stage 13 changes documented here
build_type: Release
os: Ubuntu 22.04.5 LTS under WSL2
kernel: 6.18.33.2-microsoft-standard-WSL2
architecture: x86_64
compiler: g++ (Ubuntu 11.4.0-1ubuntu1~22.04.3) 11.4.0
cmake: 3.22.1
ctest: 3.22.1
liburing: 2.0
cpu: 12th Gen Intel(R) Core(TM) i7-12800HX
logical_cpus: 24
memory: 15 GiB
swap: 4.0 GiB
workspace_and_tmp_device: /dev/sdd
filesystem: ext4
mount_options: rw,relatime,discard,errors=remount-ro,data=ordered
cpu_governor: unavailable in this WSL2 guest
input_size_bytes: 53687091200
input_creation: fallocate -l 50G; preallocated zero-filled extents
input_checksum: ab743e145f643a1f6237b7390baf2e6edc71d83997f5bf4ed40d975fb50ba423
output_filesystem: same /dev/sdd ext4 filesystem
cache_policy: no cache drop; SHA-256 scan ran immediately before the pipeline
background_load: no intentional competing benchmark was started
iterations: 1
```

Because the working tree contained the Stage 13 implementation under test,
the commit hash alone is not sufficient to reconstruct this run. The Stage 13
diff and final summary define the additional source state.
