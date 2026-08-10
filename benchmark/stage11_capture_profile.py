#!/usr/bin/env python3

"""Capture one strace or perf-stat run without treating it as benchmark timing."""

from __future__ import annotations

import argparse
from datetime import datetime, timezone
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
from typing import Sequence


LABEL_PATTERN = re.compile(r"[A-Za-z0-9][A-Za-z0-9._-]{0,63}")
PROFILE_FILENAMES = {
    "strace": "strace-summary.txt",
    "perf": "perf-stat.csv",
}


class ProfileError(RuntimeError):
    """The profiling request cannot be executed or safely published."""


def parse_arguments(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Run one exact command under strace -f -c or perf stat and save "
            "the raw evidence in a new directory."
        )
    )
    parser.add_argument("--tool", required=True, choices=("strace", "perf"))
    parser.add_argument("--output-directory", required=True, type=Path)
    parser.add_argument("--label", required=True)
    parser.add_argument("--environment-id", required=True)
    parser.add_argument("--strace-executable", default="strace")
    parser.add_argument("--perf-executable", default="perf")
    parser.add_argument("command", nargs=argparse.REMAINDER)
    arguments = parser.parse_args(argv)

    if arguments.command[:1] == ["--"]:
        arguments.command = arguments.command[1:]
    if not arguments.command:
        parser.error("an exact command is required after --")
    if LABEL_PATTERN.fullmatch(arguments.label) is None:
        parser.error(
            "--label must be 1-64 characters using letters, digits, dot, "
            "underscore, or hyphen"
        )
    if (
        not arguments.environment_id.strip()
        or "\n" in arguments.environment_id
        or "\r" in arguments.environment_id
    ):
        parser.error("--environment-id must be one non-empty line")
    return arguments


def resolve_executable(name: str, description: str) -> Path:
    candidate = shutil.which(name) if "/" not in name else name
    if candidate is None:
        raise ProfileError(f"{description} was not found: {name}")
    resolved = Path(candidate).resolve()
    if not resolved.is_file() or not os.access(resolved, os.X_OK):
        raise ProfileError(f"{description} is not executable: {name}")
    return resolved


def validate_request(
    arguments: argparse.Namespace,
) -> tuple[Path, Path, list[str]]:
    output_directory = arguments.output_directory.resolve()
    if not output_directory.is_dir():
        raise ProfileError(
            "output directory must already exist: "
            f"{arguments.output_directory}"
        )
    final_directory = output_directory / arguments.label
    if os.path.lexists(final_directory):
        raise ProfileError(f"evidence directory already exists: {final_directory}")

    profiler_name = (
        arguments.strace_executable
        if arguments.tool == "strace"
        else arguments.perf_executable
    )
    profiler = resolve_executable(profiler_name, arguments.tool)
    command_executable = resolve_executable(arguments.command[0], "profiled command")
    command = [str(command_executable), *arguments.command[1:]]
    return final_directory, profiler, command


def profiler_command(
    tool: str,
    profiler: Path,
    profile_output: Path,
    command: Sequence[str],
) -> list[str]:
    if tool == "strace":
        return [
            str(profiler),
            "-f",
            "-c",
            "-o",
            str(profile_output),
            "--",
            *command,
        ]
    return [
        str(profiler),
        "stat",
        "-x",
        ",",
        "-o",
        str(profile_output),
        "--",
        *command,
    ]


def run_profiled_command(
    command: Sequence[str],
    stdout_path: Path,
    stderr_path: Path,
) -> int:
    environment = os.environ.copy()
    environment["LC_ALL"] = "C"
    with stdout_path.open("wb") as stdout_file, stderr_path.open("wb") as stderr_file:
        try:
            completed = subprocess.run(
                command,
                check=False,
                stdout=stdout_file,
                stderr=stderr_file,
                env=environment,
            )
            return completed.returncode
        except OSError as error:
            stderr_file.write(f"failed to start profiler: {error}\n".encode())
            return 127


def fsync_file(path: Path) -> None:
    file_descriptor = os.open(path, os.O_RDONLY)
    try:
        os.fsync(file_descriptor)
    finally:
        os.close(file_descriptor)


def fsync_directory(path: Path) -> None:
    directory_descriptor = os.open(path, os.O_RDONLY | os.O_DIRECTORY)
    try:
        os.fsync(directory_descriptor)
    finally:
        os.close(directory_descriptor)


def write_manifest(
    path: Path,
    arguments: argparse.Namespace,
    profiler: Path,
    command: Sequence[str],
    wrapped_command: Sequence[str],
    exit_code: int,
    profile_output_present: bool,
) -> str:
    status = "complete" if exit_code == 0 and profile_output_present else "failed"
    content = "\n".join(
        (
            "format=asyncdataloader-stage11-profile-v1",
            f"status={status}",
            f"label={arguments.label}",
            f"environment_id={arguments.environment_id}",
            f"captured_at_utc={datetime.now(timezone.utc).isoformat()}",
            f"working_directory={Path.cwd().resolve()}",
            f"tool={arguments.tool}",
            f"profiler_executable={profiler}",
            f"command={shlex.join(command)}",
            f"profiler_command={shlex.join(wrapped_command)}",
            f"exit_code={exit_code}",
            f"profile_output_present={str(profile_output_present).lower()}",
            "timing_scope=diagnostic_only_not_benchmark_csv",
            "note=profilers perturb execution; use this evidence only to explain "
            "a separately recorded benchmark observation",
            "",
        )
    )
    with path.open("w", encoding="utf-8", newline="") as manifest:
        manifest.write(content)
        manifest.flush()
        os.fsync(manifest.fileno())
    return status


def capture(arguments: argparse.Namespace) -> int:
    final_directory, profiler, command = validate_request(arguments)
    output_directory = final_directory.parent
    temporary = tempfile.TemporaryDirectory(
        prefix=f".{arguments.label}.tmp.",
        dir=output_directory,
        ignore_cleanup_errors=True,
    )
    temporary_directory = Path(temporary.name)
    try:
        profile_output = temporary_directory / PROFILE_FILENAMES[arguments.tool]
        stdout_path = temporary_directory / "command.stdout.txt"
        stderr_path = temporary_directory / "command.stderr.txt"
        wrapped_command = profiler_command(
            arguments.tool,
            profiler,
            profile_output,
            command,
        )
        exit_code = run_profiled_command(
            wrapped_command,
            stdout_path,
            stderr_path,
        )
        profile_output_present = (
            profile_output.is_file() and profile_output.stat().st_size > 0
        )
        status = write_manifest(
            temporary_directory / "manifest.txt",
            arguments,
            profiler,
            command,
            wrapped_command,
            exit_code,
            profile_output_present,
        )

        for path in temporary_directory.iterdir():
            if path.is_file():
                fsync_file(path)
        fsync_directory(temporary_directory)
        os.rename(temporary_directory, final_directory)
        fsync_directory(output_directory)
    finally:
        temporary.cleanup()

    print(
        f"status={status} tool={arguments.tool} "
        f"evidence_directory={final_directory}"
    )
    return 0 if status == "complete" else 2


def main(argv: Sequence[str] | None = None) -> int:
    try:
        arguments = parse_arguments(sys.argv[1:] if argv is None else argv)
        return capture(arguments)
    except (OSError, ProfileError) as error:
        print(f"stage11 profile capture failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
