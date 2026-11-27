#!/usr/bin/env python3
"""Run GCC's analyzer over one ESP-IDF project's compile database.

`dev.py build --analyzer` configures the project with
`-DTHERMO_GCC_ANALYZER=ON`, which adds `-fanalyzer` and `-Werror` to this
repository's own C++ targets. This driver then replays only those compile
commands with `-S -o <devnull>` instead of `-c -o <object>`.

`-fsyntax-only` would exit earlier, but GCC's analyzer runs on GIMPLE and
reports nothing with it. `-S` is the first stage that keeps the analyzer's
diagnostics; it skips the assembler, object emission, archiving and linking.

Every source is analyzed in its own compiler invocation, so no build outputs
are produced or refreshed. A source that fails makes the whole check fail.
"""

from __future__ import annotations

from concurrent.futures import ThreadPoolExecutor
import argparse
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys

ANALYZER_FLAG = "-fanalyzer"


def _command_tokens(entry: dict) -> list[str]:
    """The argv of a compile database entry, without running a shell."""
    if "arguments" in entry:
        return list(entry["arguments"])
    return shlex.split(entry["command"])


def _analyzer_entries(entries: list[dict]) -> list[dict]:
    """Entries whose compiler command enables GCC's analyzer."""
    return [
        entry for entry in entries
        if ANALYZER_FLAG in _command_tokens(entry)
    ]


def _analysis_command(tokens: list[str]) -> list[str]:
    """Rewrite one compile command into an analyzer-only invocation.

    Pure: the returned argv replaces the object output with assembly to the
    null device and adds `-S`. All preprocessor, language and analysis flags
    are kept, so the command still sees exactly what the real build sees.
    """
    if tokens.count("-c") != 1 or tokens.count("-o") != 1:
        raise ValueError("expected exactly one -c and one -o")
    rewritten = list(tokens)
    rewritten.remove("-c")
    output = rewritten.index("-o")
    rewritten[output + 1] = os.devnull
    rewritten.insert(output, "-S")
    return rewritten


def _analyze(entry: dict) -> tuple[str, int, str]:
    """Run one rewritten compile command; return source, status, output."""
    command = _analysis_command(_command_tokens(entry))
    result = subprocess.run(
        command,
        cwd=entry["directory"],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        check=False,
    )
    return entry["file"], result.returncode, result.stdout


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument(
        "build_dir",
        type=Path,
        help="ESP-IDF build directory holding compile_commands.json",
    )
    parser.add_argument(
        "--jobs",
        type=int,
        default=os.cpu_count() or 1,
        help="parallel compiler invocations (default: CPU count)",
    )
    args = parser.parse_args(argv)

    database = args.build_dir / "compile_commands.json"
    try:
        entries = json.loads(database.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        print(f"cannot read {database}: {exc}", file=sys.stderr)
        return 1

    selected = _analyzer_entries(entries)
    if not selected:
        print(f"no {ANALYZER_FLAG} entries in {database}", file=sys.stderr)
        return 1

    failures = 0
    with ThreadPoolExecutor(max_workers=max(1, args.jobs)) as pool:
        # map preserves the database order, so diagnostics stay grouped and
        # reproducible even though the compiler invocations are parallel.
        for source, rc, output in pool.map(_analyze, selected):
            if rc == 0:
                continue
            failures += 1
            print(f"{source}: analyzer failed ({rc})", file=sys.stderr)
            if output:
                end = "" if output.endswith("\n") else "\n"
                print(output, end=end, file=sys.stderr)

    if failures:
        print(
            f"GCC analyzer: {failures} of {len(selected)} source(s) failed",
            file=sys.stderr,
        )
        return 1
    print(f"GCC analyzer: {len(selected)} source(s), no findings")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
