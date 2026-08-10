#!/usr/bin/env python3

"""Turn Stage 11.3 raw samples into a summary, charts, and a report."""

from __future__ import annotations

import argparse
from collections import defaultdict
import csv
from html import escape
import io
import math
import os
from pathlib import Path
import statistics
import sys
import tempfile
from typing import Iterable, Sequence


MEBIBYTE = 1024 * 1024
MAX_ROWS = 100_000
MAX_GROUPS = 200
FINDING_MINIMUM_DIFFERENCE = 0.03

REQUIRED_FIELDS = (
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

GROUP_FIELDS = (
    "environment_id",
    "input_path",
    "input_bytes",
    "requested_backend",
    "selected_backend",
    "block_size",
    "max_inflight_buffers",
    "queue_depth",
    "thread_workers",
    "buffer_pool_bytes",
)

SUMMARY_FIELDS = GROUP_FIELDS + (
    "sample_count",
    "average_elapsed_ms",
    "median_elapsed_ms",
    "p95_elapsed_ms",
    "aggregate_throughput_mib_s",
    "max_peak_rss_kib",
    "max_inflight_peak",
    "max_read_process_queue_peak",
    "max_process_write_queue_peak",
    "rss_limit_mib",
    "rss_limit_status",
    "speedup_vs_sync_same_config",
)

EXPLICIT_SELECTION = {
    "sync": "sync",
    "threadpool": "thread_pool",
    "uring": "io_uring",
}
BACKEND_ORDER = {"sync": 0, "threadpool": 1, "uring": 2, "auto": 3}


class AnalysisError(RuntimeError):
    """The raw evidence is invalid or cannot be compared safely."""


def positive_integer(text: str) -> int:
    try:
        value = int(text)
    except ValueError as error:
        raise argparse.ArgumentTypeError("requires a positive integer") from error
    if value <= 0:
        raise argparse.ArgumentTypeError("requires a positive integer")
    return value


def parse_arguments(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Validate Stage 11.3 CSV samples and generate summary.csv, "
            "throughput.svg, peak_rss.svg, and analysis.md."
        )
    )
    parser.add_argument(
        "--input-csv",
        required=True,
        action="append",
        type=Path,
        help="raw Stage 11.3 CSV; repeat only for the same environment",
    )
    parser.add_argument("--output-directory", required=True, type=Path)
    parser.add_argument("--minimum-samples", type=positive_integer, default=5)
    parser.add_argument(
        "--title",
        default="AsyncDataLoader Stage 11.4 Benchmark Analysis",
    )
    arguments = parser.parse_args(argv)
    if not arguments.title.strip() or "\n" in arguments.title or "\r" in arguments.title:
        parser.error("--title must be one non-empty line")
    return arguments


def same_file(left: Path, right: Path) -> bool:
    if left.resolve() == right.resolve():
        return True
    try:
        return left.samefile(right)
    except FileNotFoundError:
        return False


def validate_paths(arguments: argparse.Namespace) -> list[Path]:
    output_directory = arguments.output_directory.resolve()
    if not output_directory.is_dir():
        raise AnalysisError(
            f"output directory must already exist: {arguments.output_directory}"
        )

    inputs: list[Path] = []
    for path in arguments.input_csv:
        resolved = path.resolve()
        if not resolved.is_file():
            raise AnalysisError(f"input CSV is not a regular file: {path}")
        if any(same_file(resolved, old) for old in inputs):
            raise AnalysisError(f"input CSV was supplied more than once: {path}")
        inputs.append(resolved)

    output_names = ("summary.csv", "throughput.svg", "peak_rss.svg", "analysis.md")
    outputs = [output_directory / name for name in output_names]
    if any(same_file(input_path, output) for input_path in inputs for output in outputs):
        raise AnalysisError("an output path must not overwrite an input CSV")
    return inputs


def text_value(row: dict[str, str | None], field: str, where: str) -> str:
    value = row.get(field)
    if value is None:
        raise AnalysisError(f"{where}: {field} has no value")
    return value.strip()


def integer_value(
    row: dict[str, str | None], field: str, where: str, minimum: int = 0
) -> int:
    text = text_value(row, field, where)
    try:
        value = int(text)
    except ValueError as error:
        raise AnalysisError(f"{where}: invalid integer {field}={text!r}") from error
    if value < minimum:
        raise AnalysisError(f"{where}: {field} must be at least {minimum}")
    return value


def float_value(
    row: dict[str, str | None], field: str, where: str, minimum: float = 0.0
) -> float:
    text = text_value(row, field, where)
    try:
        value = float(text)
    except ValueError as error:
        raise AnalysisError(f"{where}: invalid number {field}={text!r}") from error
    if not math.isfinite(value) or value < minimum:
        raise AnalysisError(f"{where}: {field} must be finite and >= {minimum}")
    return value


def parse_sample(row: dict[str, str | None], where: str) -> dict[str, object]:
    sample: dict[str, object] = {
        "environment_id": text_value(row, "environment_id", where),
        "input_path": text_value(row, "input_path", where),
        "input_bytes": integer_value(row, "input_bytes", where),
        "requested_backend": text_value(row, "requested_backend", where),
        "selected_backend": text_value(row, "selected_backend", where),
        "block_size": integer_value(row, "block_size", where, 1),
        "max_inflight_buffers": integer_value(
            row, "max_inflight_buffers", where, 1
        ),
        "queue_depth": integer_value(row, "queue_depth", where, 1),
        "thread_workers": integer_value(row, "thread_workers", where, 1),
        "sample_index": integer_value(row, "sample_index", where, 1),
        "buffer_pool_bytes": integer_value(row, "buffer_pool_bytes", where, 1),
        "blocks_written": integer_value(row, "blocks_written", where),
        "bytes_written": integer_value(row, "bytes_written", where),
        "inflight_peak": integer_value(row, "inflight_peak", where),
        "read_process_queue_peak": integer_value(
            row, "read_process_queue_peak", where
        ),
        "process_write_queue_peak": integer_value(
            row, "process_write_queue_peak", where
        ),
        "elapsed_ms": float_value(row, "elapsed_ms", where, 0.000001),
        "throughput_mib_s": float_value(row, "throughput_mib_s", where),
        "peak_rss_kib": integer_value(row, "peak_rss_kib", where),
    }
    if not sample["environment_id"] or not sample["input_path"]:
        raise AnalysisError(f"{where}: environment_id and input_path must not be empty")
    for field in (
        "read_average_us",
        "process_average_us",
        "write_average_us",
        "stage_average_us",
    ):
        float_value(row, field, where)

    requested = str(sample["requested_backend"])
    selected = str(sample["selected_backend"])
    if requested not in (*EXPLICIT_SELECTION, "auto"):
        raise AnalysisError(f"{where}: unknown requested backend {requested!r}")
    if selected not in set(EXPLICIT_SELECTION.values()):
        raise AnalysisError(f"{where}: unknown selected backend {selected!r}")
    if requested != "auto" and EXPLICIT_SELECTION[requested] != selected:
        raise AnalysisError(f"{where}: explicit backend selection was mislabeled")

    input_bytes = int(sample["input_bytes"])
    block_size = int(sample["block_size"])
    buffers = int(sample["max_inflight_buffers"])
    queue_depth = int(sample["queue_depth"])
    expected_blocks = (input_bytes + block_size - 1) // block_size
    checks = (
        (int(sample["bytes_written"]) == input_bytes, "wrong bytes_written"),
        (int(sample["blocks_written"]) == expected_blocks, "wrong blocks_written"),
        (
            int(sample["buffer_pool_bytes"]) == block_size * buffers,
            "wrong buffer_pool_bytes",
        ),
        (int(sample["inflight_peak"]) <= buffers, "in-flight peak exceeded bound"),
        (
            int(sample["read_process_queue_peak"]) <= queue_depth
            and int(sample["process_write_queue_peak"]) <= queue_depth,
            "queue peak exceeded bound",
        ),
        (text_value(row, "verification", where) == "passed", "output verification did not pass"),
    )
    for passed, message in checks:
        if not passed:
            raise AnalysisError(f"{where}: {message}")

    rss_limit_text = text_value(row, "rss_limit_mib", where)
    rss_flag = text_value(row, "rss_within_limit", where)
    if not rss_limit_text:
        if rss_flag:
            raise AnalysisError(f"{where}: RSS result exists without an RSS limit")
        sample["rss_limit_mib"] = None
        sample["rss_within_limit"] = None
    else:
        rss_limit = integer_value(row, "rss_limit_mib", where, 1)
        expected = int(sample["peak_rss_kib"]) <= rss_limit * 1024
        if rss_flag not in ("true", "false") or (rss_flag == "true") != expected:
            raise AnalysisError(f"{where}: RSS limit result contradicts peak RSS")
        sample["rss_limit_mib"] = rss_limit
        sample["rss_within_limit"] = expected
    return sample


def read_samples(paths: Iterable[Path]) -> list[dict[str, object]]:
    samples: list[dict[str, object]] = []
    for path in paths:
        seen: set[tuple[object, ...]] = set()
        with path.open("r", encoding="utf-8-sig", newline="") as input_file:
            reader = csv.DictReader(input_file)
            missing = [
                field
                for field in REQUIRED_FIELDS
                if reader.fieldnames is None or field not in reader.fieldnames
            ]
            if missing:
                raise AnalysisError(f"{path}: CSV header is missing {', '.join(missing)}")
            for line_number, row in enumerate(reader, start=2):
                where = f"{path}:{line_number}"
                if None in row:
                    raise AnalysisError(f"{where}: too many columns")
                sample = parse_sample(row, where)
                identity = tuple(sample[field] for field in GROUP_FIELDS) + (
                    sample["sample_index"],
                )
                if identity in seen:
                    raise AnalysisError(f"{where}: duplicate sample index for config")
                seen.add(identity)
                samples.append(sample)
                if len(samples) > MAX_ROWS:
                    raise AnalysisError(f"raw sample count exceeds {MAX_ROWS}")

    if not samples:
        raise AnalysisError("no raw sample rows were found")
    if len({sample["environment_id"] for sample in samples}) != 1:
        raise AnalysisError("different environment_id values must be analyzed separately")
    sizes_by_path: dict[str, set[int]] = defaultdict(set)
    for sample in samples:
        sizes_by_path[str(sample["input_path"])].add(int(sample["input_bytes"]))
    if any(len(sizes) != 1 for sizes in sizes_by_path.values()):
        raise AnalysisError("one input_path has conflicting input_bytes values")
    return samples


def nearest_rank(values: Sequence[float], percentile: float) -> float:
    ordered = sorted(values)
    return ordered[max(0, math.ceil(percentile * len(ordered)) - 1)]


def comparison_signature(row: dict[str, object]) -> tuple[object, ...]:
    return (
        row["environment_id"],
        row["input_path"],
        row["input_bytes"],
        row["block_size"],
        row["max_inflight_buffers"],
        row["queue_depth"],
        row["thread_workers"],
        row["buffer_pool_bytes"],
    )


def summarize(samples: Sequence[dict[str, object]]) -> list[dict[str, object]]:
    groups: dict[tuple[object, ...], list[dict[str, object]]] = defaultdict(list)
    for sample in samples:
        groups[tuple(sample[field] for field in GROUP_FIELDS)].append(sample)
    if len(groups) > MAX_GROUPS:
        raise AnalysisError(f"split reports into at most {MAX_GROUPS} groups")

    summaries: list[dict[str, object]] = []
    for group in groups.values():
        elapsed = [float(sample["elapsed_ms"]) for sample in group]
        limits = {sample["rss_limit_mib"] for sample in group}
        if limits == {None}:
            limit_text, limit_status = "", "not_set"
        elif len(limits) != 1 or None in limits:
            limit_text, limit_status = "mixed", "mixed"
        else:
            limit_text = str(next(iter(limits)))
            limit_status = (
                "passed"
                if all(sample["rss_within_limit"] is True for sample in group)
                else "failed"
            )
        summary = {field: group[0][field] for field in GROUP_FIELDS}
        summary.update(
            {
                "sample_count": len(group),
                "average_elapsed_ms": statistics.fmean(elapsed),
                "median_elapsed_ms": statistics.median(elapsed),
                "p95_elapsed_ms": nearest_rank(elapsed, 0.95),
                "aggregate_throughput_mib_s": (
                    int(group[0]["input_bytes"])
                    * len(group)
                    / MEBIBYTE
                    / (sum(elapsed) / 1000.0)
                ),
                "max_peak_rss_kib": max(int(row["peak_rss_kib"]) for row in group),
                "max_inflight_peak": max(int(row["inflight_peak"]) for row in group),
                "max_read_process_queue_peak": max(
                    int(row["read_process_queue_peak"]) for row in group
                ),
                "max_process_write_queue_peak": max(
                    int(row["process_write_queue_peak"]) for row in group
                ),
                "rss_limit_mib": limit_text,
                "rss_limit_status": limit_status,
                "speedup_vs_sync_same_config": None,
            }
        )
        summaries.append(summary)

    summaries.sort(
        key=lambda row: (
            str(row["input_path"]),
            int(row["input_bytes"]),
            int(row["block_size"]),
            int(row["max_inflight_buffers"]),
            int(row["queue_depth"]),
            int(row["thread_workers"]),
            BACKEND_ORDER[str(row["requested_backend"])],
            str(row["selected_backend"]),
        )
    )
    sync_rates = {
        comparison_signature(row): float(row["aggregate_throughput_mib_s"])
        for row in summaries
        if row["requested_backend"] == "sync" and row["selected_backend"] == "sync"
    }
    for row in summaries:
        sync_rate = sync_rates.get(comparison_signature(row))
        if sync_rate is not None and sync_rate > 0.0:
            row["speedup_vs_sync_same_config"] = (
                float(row["aggregate_throughput_mib_s"]) / sync_rate
            )
    return summaries


def render_summary_csv(summaries: Sequence[dict[str, object]]) -> str:
    output = io.StringIO(newline="")
    writer = csv.DictWriter(output, fieldnames=SUMMARY_FIELDS, lineterminator="\n")
    writer.writeheader()
    for summary in summaries:
        row = dict(summary)
        for field in (
            "average_elapsed_ms",
            "median_elapsed_ms",
            "p95_elapsed_ms",
            "aggregate_throughput_mib_s",
        ):
            row[field] = f"{float(summary[field]):.3f}"
        speedup = summary["speedup_vs_sync_same_config"]
        row["speedup_vs_sync_same_config"] = (
            "" if speedup is None else f"{float(speedup):.3f}"
        )
        writer.writerow(row)
    return output.getvalue()


def backend_name(name: object) -> str:
    return {
        "sync": "Sync",
        "threadpool": "ThreadPool",
        "thread_pool": "ThreadPool",
        "uring": "io_uring",
        "io_uring": "io_uring",
        "auto": "Auto",
    }.get(str(name), str(name))


def human_bytes(value: int) -> str:
    if value >= MEBIBYTE:
        return f"{value / MEBIBYTE:.2f} MiB"
    if value >= 1024:
        return f"{value / 1024:.2f} KiB"
    return f"{value} B"


def chart_label(row: dict[str, object], multiple_inputs: bool) -> str:
    requested = backend_name(row["requested_backend"])
    selected = backend_name(row["selected_backend"])
    backend = requested if requested == selected else f"{requested}->{selected}"
    parts = [
        backend,
        f"block={human_bytes(int(row['block_size']))}",
        f"buffers={row['max_inflight_buffers']}",
        f"queue={row['queue_depth']}",
    ]
    if multiple_inputs:
        parts.insert(0, Path(str(row["input_path"])).name)
    return " | ".join(parts)


def render_chart(
    summaries: Sequence[dict[str, object]], title: str, metric: str
) -> str:
    is_rss = metric == "max_peak_rss_kib"
    values = [
        float(row[metric]) / 1024.0 if is_rss else float(row[metric])
        for row in summaries
    ]
    pool_values = [int(row["buffer_pool_bytes"]) / MEBIBYTE for row in summaries]
    maximum = max(values + (pool_values if is_rss else []) + [1.0])
    width, label_width, plot_width = 1080, 410, 530
    row_height = 38 if is_rss else 30
    top = 58
    height = top + row_height * len(summaries) + 24
    unit = "MiB" if is_rss else "MiB/s"
    heading = "whole-process peak RSS" if is_rss else "observed aggregate throughput"
    multiple_inputs = len({row["input_path"] for row in summaries}) > 1
    lines = [
        '<?xml version="1.0" encoding="UTF-8"?>',
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}" role="img">',
        f"  <title>{escape(title)}: {heading}</title>",
        '  <rect width="100%" height="100%" fill="white"/>',
        '  <style>text { font-family: sans-serif; fill: #1f2937; } .label { font-size: 11px; } .value { font-size: 11px; font-weight: 600; }</style>',
        f'  <text x="12" y="27" font-size="18" font-weight="700">{escape(title)}: {heading}</text>',
    ]
    if is_rss:
        lines.append(
            '  <text x="410" y="45" font-size="10">gray = BufferPool payload, colored = process RSS</text>'
        )
    for index, row in enumerate(summaries):
        y = top + index * row_height
        value = values[index]
        bar_width = plot_width * value / maximum
        if is_rss:
            pool_width = plot_width * pool_values[index] / maximum
            color = "#dc2626" if row["rss_limit_status"] == "failed" else "#0891b2"
            lines.append(
                f'  <rect x="{label_width}" y="{y}" width="{pool_width:.1f}" height="8" fill="#cbd5e1"/>'
            )
            bar_y, bar_height, text_y = y + 11, 12, y + 22
        else:
            color = {"sync": "#64748b", "threadpool": "#2563eb", "uring": "#7c3aed", "auto": "#d97706"}[str(row["requested_backend"])]
            bar_y, bar_height, text_y = y, 17, y + 13
        lines.extend(
            [
                f'  <text class="label" x="6" y="{text_y}">{escape(chart_label(row, multiple_inputs))}</text>',
                f'  <rect x="{label_width}" y="{bar_y}" width="{bar_width:.1f}" height="{bar_height}" rx="2" fill="{color}"/>',
                f'  <text class="value" x="{label_width + bar_width + 5:.1f}" y="{text_y}">{value:.3f} {unit}</text>',
            ]
        )
    lines.append("</svg>")
    return "\n".join(lines) + "\n"


def markdown_cell(value: object) -> str:
    return str(value).replace("|", "\\|").replace("\n", " ")


def config_text(row: dict[str, object]) -> str:
    return (
        f"input={Path(str(row['input_path'])).name}, "
        f"block={human_bytes(int(row['block_size']))}, "
        f"buffers={row['max_inflight_buffers']}, queue={row['queue_depth']}"
    )


def render_report(
    summaries: Sequence[dict[str, object]],
    samples: Sequence[dict[str, object]],
    input_paths: Sequence[Path],
    title: str,
    minimum_samples: int,
) -> str:
    lines = [
        f"# {title}",
        "",
        "This report describes validated observations from the recorded environment. "
        "It does not claim a universal backend ranking or infer causes from timing alone.",
        "",
        "## Dataset",
        "",
        f"- Environment ID: `{samples[0]['environment_id']}`",
        f"- Raw samples: {len(samples)}",
        f"- Exact configuration groups: {len(summaries)}",
        f"- Minimum samples used for findings: {minimum_samples}",
        "- Sources: " + ", ".join(f"`{path}`" for path in input_paths),
        "",
        "## Results",
        "",
        "| Input | Requested -> selected | Block | Buffers | Queue | Samples | Avg ms | P95 ms | MiB/s | Max RSS MiB | vs Sync | RSS limit |",
        "|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|",
    ]
    for row in summaries:
        speedup = row["speedup_vs_sync_same_config"]
        values = (
            Path(str(row["input_path"])).name,
            f"{backend_name(row['requested_backend'])} -> {backend_name(row['selected_backend'])}",
            human_bytes(int(row["block_size"])),
            row["max_inflight_buffers"],
            row["queue_depth"],
            row["sample_count"],
            f"{float(row['average_elapsed_ms']):.3f}",
            f"{float(row['p95_elapsed_ms']):.3f}",
            f"{float(row['aggregate_throughput_mib_s']):.3f}",
            f"{int(row['max_peak_rss_kib']) / 1024.0:.2f}",
            "n/a" if speedup is None else f"{float(speedup):.3f}x",
            row["rss_limit_status"],
        )
        lines.append("| " + " | ".join(markdown_cell(value) for value in values) + " |")

    lines.extend(
        [
            "",
            "![Observed aggregate throughput](throughput.svg)",
            "",
            "![Peak RSS and BufferPool payload](peak_rss.svg)",
            "",
            "Aggregate throughput is total group bytes divided by total group time. "
            "Peak RSS is the maximum for the whole process, not BufferPool memory alone.",
            "",
            "## Same-configuration observations",
            "",
        ]
    )
    comparison_groups: dict[tuple[object, ...], list[dict[str, object]]] = defaultdict(list)
    for row in summaries:
        if row["requested_backend"] != "auto" and int(row["sample_count"]) >= minimum_samples:
            comparison_groups[comparison_signature(row)].append(row)

    observations: list[str] = []
    counterintuitive: list[str] = []
    for group in comparison_groups.values():
        if len(group) < 2:
            continue
        fastest = max(group, key=lambda row: float(row["aggregate_throughput_mib_s"]))
        slowest = min(group, key=lambda row: float(row["aggregate_throughput_mib_s"]))
        fastest_rate = float(fastest["aggregate_throughput_mib_s"])
        slowest_rate = float(slowest["aggregate_throughput_mib_s"])
        ratio_text = (
            "the slowest observed rate was zero"
            if slowest_rate == 0.0
            else f"{fastest_rate / slowest_rate:.3f}x the slowest observed mechanism"
        )
        observations.append(
            f"- For {config_text(fastest)}, the fastest observed mechanism was "
            f"**{backend_name(fastest['requested_backend'])}** at {fastest_rate:.3f} "
            f"MiB/s ({ratio_text})."
        )
        uring = next((row for row in group if row["requested_backend"] == "uring"), None)
        if uring is not None:
            uring_rate = float(uring["aggregate_throughput_mib_s"])
            if (
                fastest["requested_backend"] != "uring"
                and fastest_rate > uring_rate * (1.0 + FINDING_MINIMUM_DIFFERENCE)
            ):
                counterintuitive.append(
                    f"- io_uring was not the fastest observed mechanism for "
                    f"{config_text(uring)}; {backend_name(fastest['requested_backend'])} "
                    f"measured {fastest_rate:.3f} versus {uring_rate:.3f} MiB/s. "
                    "No cause is claimed without system-level evidence."
                )
    lines.extend(observations or ["- No exact config had two adequately sampled explicit backends."])
    lines.extend(["", "## Counterintuitive findings", ""])
    lines.extend(
        counterintuitive
        or [
            "- No io_uring reversal crossed the 3% reporting threshold. This does "
            "not prove that counterintuitive behavior is absent."
        ]
    )

    undersampled = sum(
        int(row["sample_count"]) < minimum_samples for row in summaries
    )
    failed_limits = sum(row["rss_limit_status"] == "failed" for row in summaries)
    lines.extend(
        [
            "",
            "## Evidence boundaries",
            "",
            "- Every accepted row passed output verification and its queue/in-flight bounds.",
            f"- Groups below the finding threshold: {undersampled}.",
            f"- Groups with an RSS-limit failure: {failed_limits}.",
            "- Auto rows record fallback policy behavior. They are not ranked as a fourth I/O mechanism.",
            "- The CSV cannot explain a performance cause. Profile the exact same command first:",
            "",
            "```bash",
            "strace -f -c -o strace-summary.txt -- <exact pipeline command>",
            "perf stat -r 5 -o perf-stat.txt -- <exact pipeline command>",
            "```",
            "",
            "The current reader has one read request outstanding. Handoff queue depth "
            "must not be described as io_uring submission-depth scaling. Coroutines "
            "organize suspension and resumption; they are not themselves a speedup source.",
            "",
        ]
    )
    return "\n".join(lines)


def write_atomic(path: Path, content: str) -> None:
    temporary = tempfile.NamedTemporaryFile(
        mode="w",
        encoding="utf-8",
        newline="",
        prefix=f".{path.name}.tmp.",
        dir=path.parent,
        delete=False,
    )
    temporary_path = Path(temporary.name)
    try:
        with temporary:
            temporary.write(content)
            temporary.flush()
            os.fsync(temporary.fileno())
        os.replace(temporary_path, path)
    except BaseException:
        temporary_path.unlink(missing_ok=True)
        raise


def run(arguments: argparse.Namespace) -> None:
    input_paths = validate_paths(arguments)
    samples = read_samples(input_paths)
    summaries = summarize(samples)
    output_directory = arguments.output_directory.resolve()
    outputs = {
        "summary.csv": render_summary_csv(summaries),
        "throughput.svg": render_chart(
            summaries, arguments.title, "aggregate_throughput_mib_s"
        ),
        "peak_rss.svg": render_chart(summaries, arguments.title, "max_peak_rss_kib"),
        "analysis.md": render_report(
            summaries,
            samples,
            input_paths,
            arguments.title,
            arguments.minimum_samples,
        ),
    }
    for name, content in outputs.items():
        write_atomic(output_directory / name, content)
    directory_fd = os.open(output_directory, os.O_RDONLY | os.O_DIRECTORY)
    try:
        os.fsync(directory_fd)
    finally:
        os.close(directory_fd)
    print(
        f"status=complete samples={len(samples)} groups={len(summaries)} "
        f"output_directory={output_directory}"
    )


def main(argv: Sequence[str] | None = None) -> int:
    try:
        arguments = parse_arguments(sys.argv[1:] if argv is None else argv)
        run(arguments)
        return 0
    except (OSError, AnalysisError) as error:
        print(f"stage11 analysis failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
