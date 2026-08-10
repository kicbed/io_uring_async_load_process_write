#!/usr/bin/env python3

"""Run bounded AsyncDataLoader parameter sweeps as separate processes."""

from __future__ import annotations

import argparse
import csv
import os
from pathlib import Path
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from itertools import product
from typing import Sequence


ALIGNMENT = 4096
VALID_BACKENDS = ("sync", "threadpool", "uring", "auto")
CSV_FIELDS = (
    "environment_id",
    "input_path",
    "input_bytes",
    "requested_backend",
    "selected_backend",
    "block_size",
    "max_inflight_buffers",
    "queue_depth",
    "thread_workers",
    "sample_index",
    "buffer_pool_bytes",
    "blocks_written",
    "bytes_written",
    "inflight_peak",
    "read_process_queue_peak",
    "process_write_queue_peak",
    "read_average_us",
    "process_average_us",
    "write_average_us",
    "stage_average_us",
    "elapsed_ms",
    "throughput_mib_s",
    "peak_rss_kib",
    "rss_limit_mib",
    "rss_within_limit",
    "verification",
)


class SweepError(RuntimeError):
    """Reports a failed child run or an invalid child result."""


@dataclass(frozen=True)
class RunConfig:
    block_size: int
    max_inflight_buffers: int
    queue_depth: int
    backend: str
    sample_index: int


def positive_integer(text: str) -> int:
    try:
        value = int(text)
    except ValueError as error:
        raise argparse.ArgumentTypeError("requires a positive integer") from error
    if value <= 0:
        raise argparse.ArgumentTypeError("requires a positive integer")
    return value


def positive_integer_list(text: str) -> list[int]:
    values: list[int] = []
    for item in text.split(","):
        value = positive_integer(item.strip())
        if value not in values:
            values.append(value)
    if not values:
        raise argparse.ArgumentTypeError("requires at least one integer")
    return values


def backend_list(text: str) -> list[str]:
    values: list[str] = []
    for item in text.split(","):
        backend = item.strip()
        if backend not in VALID_BACKENDS:
            raise argparse.ArgumentTypeError(
                "backends must be sync, threadpool, uring, or auto"
            )
        if backend not in values:
            values.append(backend)
    if not values:
        raise argparse.ArgumentTypeError("requires at least one backend")
    return values


def parse_arguments(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Run preprocess_pipeline_demo once per parameter sample, record "
            "pipeline metrics and GNU time peak RSS, then publish one CSV."
        )
    )
    parser.add_argument("--executable", required=True, type=Path)
    parser.add_argument(
        "--time-executable",
        type=Path,
        default=Path("/usr/bin/time"),
    )
    parser.add_argument("--input", required=True, type=Path)
    parser.add_argument("--output-directory", required=True, type=Path)
    parser.add_argument("--csv", required=True, type=Path)
    parser.add_argument("--environment-id", required=True)
    parser.add_argument(
        "--block-sizes",
        required=True,
        type=positive_integer_list,
        help="comma-separated byte counts",
    )
    parser.add_argument(
        "--buffers",
        required=True,
        type=positive_integer_list,
        help="comma-separated BufferPool capacities",
    )
    parser.add_argument(
        "--queue-depths",
        required=True,
        type=positive_integer_list,
        help="comma-separated capacities used by both handoff queues",
    )
    parser.add_argument(
        "--backends",
        type=backend_list,
        default=backend_list("sync,threadpool,uring"),
    )
    parser.add_argument("--thread-workers", type=positive_integer, default=2)
    parser.add_argument("--iterations", type=positive_integer, default=3)
    parser.add_argument("--max-runs", type=positive_integer, default=1000)
    parser.add_argument("--rss-limit-mib", type=positive_integer)

    arguments = parser.parse_args(argv)

    if not arguments.environment_id.strip():
        parser.error("--environment-id must not be empty")
    if "\n" in arguments.environment_id or "\r" in arguments.environment_id:
        parser.error("--environment-id must be one line")
    for block_size in arguments.block_sizes:
        if block_size % ALIGNMENT != 0:
            parser.error("every block size must be a multiple of 4096")
    for buffer_count in arguments.buffers:
        if buffer_count < 3:
            parser.error("every buffer count must be at least 3 for overlap")

    total_runs = (
        len(arguments.block_sizes)
        * len(arguments.buffers)
        * len(arguments.queue_depths)
        * len(arguments.backends)
        * arguments.iterations
    )
    if total_runs > arguments.max_runs:
        parser.error(
            f"parameter matrix needs {total_runs} runs, above --max-runs="
            f"{arguments.max_runs}"
        )
    arguments.total_runs = total_runs
    return arguments


def require_executable(path: Path, label: str) -> Path:
    resolved = path.resolve()
    if not resolved.is_file() or not os.access(resolved, os.X_OK):
        raise SweepError(f"{label} is not an executable file: {path}")
    return resolved


def paths_identify_same_file(left: Path, right: Path) -> bool:
    if left.resolve() == right.resolve():
        return True
    try:
        return left.samefile(right)
    except FileNotFoundError:
        return False


def validate_paths(arguments: argparse.Namespace) -> None:
    if not arguments.input.is_file():
        raise SweepError(f"input is not a regular file: {arguments.input}")
    if not arguments.output_directory.is_dir():
        raise SweepError(
            "output directory must already exist: "
            f"{arguments.output_directory}"
        )
    if not arguments.csv.parent.is_dir():
        raise SweepError(
            f"CSV parent directory must already exist: {arguments.csv.parent}"
        )
    if paths_identify_same_file(arguments.input, arguments.csv):
        raise SweepError("input and CSV paths must differ")


def build_run_plan(arguments: argparse.Namespace) -> list[RunConfig]:
    plan: list[RunConfig] = []
    base_configs = list(
        product(
            arguments.block_sizes,
            arguments.buffers,
            arguments.queue_depths,
        )
    )

    for config_index, (block_size, buffers, queue_depth) in enumerate(
        base_configs
    ):
        for sample_index in range(1, arguments.iterations + 1):
            start = (config_index + sample_index - 1) % len(arguments.backends)
            backend_order = (
                arguments.backends[start:] + arguments.backends[:start]
            )
            for backend in backend_order:
                plan.append(
                    RunConfig(
                        block_size=block_size,
                        max_inflight_buffers=buffers,
                        queue_depth=queue_depth,
                        backend=backend,
                        sample_index=sample_index,
                    )
                )
    return plan


def parse_key_value_output(output: str) -> dict[str, str]:
    values: dict[str, str] = {}
    for line in output.splitlines():
        if "=" not in line:
            continue
        key, value = line.split("=", 1)
        if key and " " not in key:
            values[key] = value
    return values


def require_value(values: dict[str, str], name: str) -> str:
    try:
        return values[name]
    except KeyError as error:
        raise SweepError(f"pipeline output is missing {name}") from error


def require_integer(values: dict[str, str], name: str) -> int:
    text = require_value(values, name)
    try:
        return int(text)
    except ValueError as error:
        raise SweepError(f"pipeline output has invalid {name}: {text}") from error


def require_float(values: dict[str, str], name: str) -> float:
    text = require_value(values, name)
    try:
        return float(text)
    except ValueError as error:
        raise SweepError(f"pipeline output has invalid {name}: {text}") from error


def run_sample(
    arguments: argparse.Namespace,
    executable: Path,
    time_executable: Path,
    scratch_output: Path,
    config: RunConfig,
) -> dict[str, object]:
    rss_fd, rss_name = tempfile.mkstemp(
        prefix=".stage11-sweep-rss-",
        suffix=".txt",
        dir=arguments.output_directory,
    )
    os.close(rss_fd)
    rss_path = Path(rss_name)

    command = [
        str(time_executable),
        "-f",
        "%M",
        "-o",
        str(rss_path),
        str(executable),
        str(arguments.input.resolve()),
        str(scratch_output),
        f"--backend={config.backend}",
        f"--block-size={config.block_size}",
        f"--buffers={config.max_inflight_buffers}",
        f"--queue-depth={config.queue_depth}",
        f"--alignment={ALIGNMENT}",
        f"--thread-workers={arguments.thread_workers}",
        "--report-ms=0",
    ]

    environment = os.environ.copy()
    environment["LC_ALL"] = "C"
    try:
        completed = subprocess.run(
            command,
            check=False,
            capture_output=True,
            text=True,
            env=environment,
        )
        if completed.returncode != 0:
            raise SweepError(
                f"backend {config.backend} failed with exit code "
                f"{completed.returncode}\nstdout:\n{completed.stdout}\n"
                f"stderr:\n{completed.stderr}"
            )

        rss_text = rss_path.read_text(encoding="utf-8").strip()
        try:
            peak_rss_kib = int(rss_text)
        except ValueError as error:
            raise SweepError(
                f"GNU time returned invalid peak RSS: {rss_text}"
            ) from error
    finally:
        rss_path.unlink(missing_ok=True)

    values = parse_key_value_output(completed.stdout)
    if require_value(values, "status") != "complete":
        raise SweepError("pipeline did not report status=complete")
    if require_value(values, "verification") != "passed":
        raise SweepError("pipeline output verification did not pass")
    if require_value(values, "output_committed") != "true":
        raise SweepError("pipeline did not reliably commit its output")

    input_bytes = require_integer(values, "input_bytes")
    bytes_written = require_integer(values, "bytes_written")
    if bytes_written != input_bytes:
        raise SweepError("written bytes differ from input bytes")
    if require_integer(values, "buffer_pool_bytes") != (
        config.block_size * config.max_inflight_buffers
    ):
        raise SweepError("reported buffer-pool bytes differ from configuration")

    inflight_peak = require_integer(
        values,
        "pipeline.buffer_pool.inflight.peak",
    )
    read_queue_peak = require_integer(
        values,
        "pipeline.queue.read_process.depth.peak",
    )
    write_queue_peak = require_integer(
        values,
        "pipeline.queue.process_write.depth.peak",
    )
    if inflight_peak > config.max_inflight_buffers:
        raise SweepError("in-flight buffer peak exceeded its configured bound")
    if read_queue_peak > config.queue_depth or write_queue_peak > config.queue_depth:
        raise SweepError("queue peak exceeded its configured bound")

    elapsed_ms = require_float(values, "elapsed_ms")
    throughput_mib_s = require_float(values, "throughput_mib_s")
    rss_limit_mib = arguments.rss_limit_mib
    rss_within_limit = ""
    if rss_limit_mib is not None:
        rss_within_limit = str(
            peak_rss_kib <= rss_limit_mib * 1024
        ).lower()

    return {
        "environment_id": arguments.environment_id,
        "input_path": str(arguments.input.resolve()),
        "input_bytes": input_bytes,
        "requested_backend": config.backend,
        "selected_backend": require_value(values, "selected_backend"),
        "block_size": config.block_size,
        "max_inflight_buffers": config.max_inflight_buffers,
        "queue_depth": config.queue_depth,
        "thread_workers": arguments.thread_workers,
        "sample_index": config.sample_index,
        "buffer_pool_bytes": require_integer(values, "buffer_pool_bytes"),
        "blocks_written": require_integer(values, "blocks_written"),
        "bytes_written": bytes_written,
        "inflight_peak": inflight_peak,
        "read_process_queue_peak": read_queue_peak,
        "process_write_queue_peak": write_queue_peak,
        "read_average_us": require_float(
            values,
            "pipeline.read.latency_ns.average_us",
        ),
        "process_average_us": require_float(
            values,
            "pipeline.process.latency_ns.average_us",
        ),
        "write_average_us": require_float(
            values,
            "pipeline.write.latency_ns.average_us",
        ),
        "stage_average_us": require_float(
            values,
            "stage.0.byte_increment.latency_ns.average_us",
        ),
        "elapsed_ms": elapsed_ms,
        "throughput_mib_s": throughput_mib_s,
        "peak_rss_kib": peak_rss_kib,
        "rss_limit_mib": "" if rss_limit_mib is None else rss_limit_mib,
        "rss_within_limit": rss_within_limit,
        "verification": require_value(values, "verification"),
    }


def write_csv_atomic(path: Path, rows: list[dict[str, object]]) -> None:
    temporary_file = tempfile.NamedTemporaryFile(
        mode="w",
        encoding="utf-8",
        newline="",
        prefix=f".{path.name}.tmp.",
        dir=path.parent,
        delete=False,
    )
    temporary_path = Path(temporary_file.name)
    try:
        with temporary_file:
            writer = csv.DictWriter(temporary_file, fieldnames=CSV_FIELDS)
            writer.writeheader()
            writer.writerows(rows)
            temporary_file.flush()
            os.fsync(temporary_file.fileno())
        os.replace(temporary_path, path)
        directory_fd = os.open(path.parent, os.O_RDONLY | os.O_DIRECTORY)
        try:
            os.fsync(directory_fd)
        finally:
            os.close(directory_fd)
    except BaseException:
        temporary_path.unlink(missing_ok=True)
        raise


def execute_sweep(arguments: argparse.Namespace) -> int:
    validate_paths(arguments)
    executable = require_executable(arguments.executable, "pipeline executable")
    time_executable = require_executable(
        arguments.time_executable,
        "GNU time executable",
    )
    plan = build_run_plan(arguments)

    scratch_fd, scratch_name = tempfile.mkstemp(
        prefix=".stage11-sweep-output-",
        suffix=".bin",
        dir=arguments.output_directory,
    )
    os.close(scratch_fd)
    scratch_output = Path(scratch_name)

    rows: list[dict[str, object]] = []
    try:
        for run_index, config in enumerate(plan, start=1):
            print(
                f"run={run_index}/{len(plan)} backend={config.backend} "
                f"block_size={config.block_size} "
                f"buffers={config.max_inflight_buffers} "
                f"queue_depth={config.queue_depth} "
                f"sample={config.sample_index}",
                file=sys.stderr,
                flush=True,
            )
            rows.append(
                run_sample(
                    arguments,
                    executable,
                    time_executable,
                    scratch_output,
                    config,
                )
            )
    finally:
        scratch_output.unlink(missing_ok=True)

    write_csv_atomic(arguments.csv, rows)
    limit_failures = [
        row for row in rows if row["rss_within_limit"] == "false"
    ]
    print(f"status=complete runs={len(rows)} csv={arguments.csv}")
    if limit_failures:
        print(
            f"rss limit exceeded by {len(limit_failures)} sample(s)",
            file=sys.stderr,
        )
        return 3
    return 0


def main(argv: Sequence[str] | None = None) -> int:
    try:
        arguments = parse_arguments(sys.argv[1:] if argv is None else argv)
        return execute_sweep(arguments)
    except (OSError, SweepError) as error:
        print(f"stage11 parameter sweep failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
