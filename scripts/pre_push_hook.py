#!/usr/bin/env python3
"""Pre-push safety gate: disable auto-merge on GitHub PRs.

Git invokes this hook before updating remote refs. For pushes to a
recognized GitHub remote, the hook inspects each pushed branch, finds
open pull requests for that branch, and disables auto-merge on any PR
that has it enabled. GitHub interaction failures block the push.
"""

from __future__ import annotations

import json
import re
import subprocess
import sys
from typing import Callable, Sequence, TextIO
from urllib.parse import urlsplit


# subprocess.run-compatible callable used for command execution. Tests
# inject a fake runner; production uses subprocess.run.
Runner = Callable[..., subprocess.CompletedProcess[str]]

_ZERO_SHA = "0" * 40
_GITHUB_HOST = "github.com"
_SCP_LIKE_PREFIX = "git@github.com:"
_URL_SCHEMES = {"http", "https", "ssh", "git"}
_OWNER_REPO_RE = re.compile(r"^([^/\s]+)/([^/\s]+?)(?:\.git)?/?$")


class HookError(RuntimeError):
    """A safety-gate failure that must block the push."""


def _stderr(message: str, err: TextIO) -> None:
    err.write(f"pre-push: {message}\n")


def parse_github_repo(remote_url: str) -> tuple[str, str] | None:
    """Return (owner, repo) for a recognized GitHub remote, else None.

    Detection is strict: only known GitHub URL forms that reach the
    owner/repo part are accepted. Unrecognized URLs return None. A
    recognized GitHub prefix with an unparseable owner/repo raises
    HookError because the destination is GitHub but the hook cannot
    safely identify the repository.
    """
    url = remote_url.strip()
    if not url:
        return None

    scp_prefix = _SCP_LIKE_PREFIX
    if url.lower().startswith(scp_prefix):
        return _parse_owner_repo(url[len(scp_prefix):], url)

    try:
        parts = urlsplit(url)
    except ValueError:
        return None

    scheme = parts.scheme.lower()
    hostname = (parts.hostname or "").lower()
    if scheme not in _URL_SCHEMES or hostname != _GITHUB_HOST:
        return None

    # Recognized GitHub URL form: ssh://git@github.com/OWNER/REPO,
    # ssh://github.com/OWNER/REPO, https://github.com/OWNER/REPO, etc.
    path = parts.path or ""
    if path.startswith("/"):
        path = path[1:]
    return _parse_owner_repo(path, url)


def _parse_owner_repo(path: str, remote_url: str) -> tuple[str, str]:
    match = _OWNER_REPO_RE.match(path)
    if not match:
        raise HookError(
            f"recognized GitHub remote but could not parse OWNER/REPO: "
            f"{remote_url!r}"
        )
    owner, repo = match.group(1), match.group(2)
    if not owner or not repo:
        raise HookError(
            f"recognized GitHub remote but OWNER/REPO is empty: "
            f"{remote_url!r}"
        )
    return owner, repo


def is_github_remote(remote_url: str) -> bool:
    """True when the remote URL is a recognized GitHub form."""
    return parse_github_repo(remote_url) is not None


def iter_pushed_branches(lines: Sequence[str]) -> list[str]:
    """Extract destination branch names from pre-push stdin lines.

    Each line is: <local ref> <local sha> <remote ref> <remote sha>.
    Only branch updates to refs/heads/* are returned. Deletions (local
    sha all zeros) are ignored. Malformed lines fail closed.
    """
    branches: list[str] = []
    seen: set[str] = set()
    for raw in lines:
        line = raw.rstrip("\n")
        if not line.strip():
            continue
        fields = line.split()
        if len(fields) != 4:
            raise HookError(
                f"malformed pre-push stdin line (expected 4 fields): "
                f"{raw!r}"
            )
        local_ref, local_sha, remote_ref, _remote_sha = fields
        if local_sha == _ZERO_SHA:
            continue
        if not remote_ref.startswith("refs/heads/"):
            continue
        branch = remote_ref[len("refs/heads/"):]
        if not branch:
            raise HookError(
                f"malformed pre-push remote ref (empty branch): {raw!r}"
            )
        if branch not in seen:
            seen.add(branch)
            branches.append(branch)
    return branches


def _run_command(
    args: Sequence[str],
    runner: Runner,
    *,
    context: str,
) -> subprocess.CompletedProcess[str]:
    try:
        proc = runner(
            list(args),
            capture_output=True,
            text=True,
            check=False,
        )
    except FileNotFoundError as exc:
        raise HookError(f"{context}: command not found: {args[0]}") from exc
    except OSError as exc:
        raise HookError(f"{context}: failed to run {args[0]}: {exc}") from exc

    if proc.returncode != 0:
        detail = (proc.stderr or proc.stdout or "").strip()
        if detail:
            detail = f"; {detail}"
        raise HookError(
            f"{context}: command failed with exit {proc.returncode}: "
            f"{' '.join(args)}{detail}"
        )
    return proc


def list_open_prs(
    repo: str,
    branch: str,
    *,
    runner: Runner = subprocess.run,
) -> list[dict]:
    """Return open PRs for HEAD branch in OWNER/REPO.

    Fails closed on any gh interaction or JSON parsing problem.
    """
    args = [
        "gh", "pr", "list",
        "-R", repo,
        "--head", branch,
        "--state", "open",
        "--json", "number,autoMergeRequest",
    ]
    proc = _run_command(
        args, runner, context=f"listing open PRs for {repo} {branch}"
    )
    try:
        data = json.loads(proc.stdout or "[]")
    except json.JSONDecodeError as exc:
        raise HookError(
            f"listing open PRs for {repo} {branch}: invalid JSON from gh: "
            f"{exc}"
        ) from exc
    if not isinstance(data, list):
        raise HookError(
            f"listing open PRs for {repo} {branch}: expected JSON array "
            f"from gh"
        )
    for item in data:
        if not isinstance(item, dict) or "number" not in item:
            raise HookError(
                f"listing open PRs for {repo} {branch}: unexpected PR "
                f"entry from gh: {item!r}"
            )
    return data


def disable_auto_merge(
    repo: str,
    number: int,
    *,
    runner: Runner = subprocess.run,
) -> None:
    """Disable auto-merge for a PR. Fails closed on any gh failure."""
    args = [
        "gh", "pr", "merge", str(number),
        "-R", repo,
        "--disable-auto",
    ]
    _run_command(
        args,
        runner,
        context=f"disabling auto-merge for {repo}#{number}",
    )


def run_hook(
    remote_name: str,
    remote_url: str,
    stdin: TextIO,
    *,
    runner: Runner = subprocess.run,
    err: TextIO = sys.stderr,
) -> int:
    """Pre-push hook entry point. Returns process exit code."""
    try:
        repo_info = parse_github_repo(remote_url)
        if repo_info is None:
            return 0
        owner, repo_name = repo_info
        repo = f"{owner}/{repo_name}"

        branches = iter_pushed_branches(stdin.readlines())
        if not branches:
            return 0

        for branch in branches:
            prs = list_open_prs(repo, branch, runner=runner)
            for pr in prs:
                number = pr.get("number")
                if not isinstance(number, int):
                    raise HookError(
                        f"listing open PRs for {repo} {branch}: PR number "
                        f"is not an int: {pr!r}"
                    )
                if pr.get("autoMergeRequest") is None:
                    continue
                disable_auto_merge(repo, number, runner=runner)
                _stderr(
                    f"disabled auto-merge for {repo}#{number} "
                    f"(branch {branch}, remote {remote_name})",
                    err,
                )
        return 0
    except HookError as exc:
        _stderr(str(exc), err)
        _stderr(
            "push blocked: resolve the GitHub/gh issue and retry; "
            "this hook is a safety gate for GitHub pushes.",
            err,
        )
        return 1
    except Exception as exc:  # pragma: no cover - defensive safety gate
        _stderr(f"unexpected error: {exc}", err)
        _stderr(
            "push blocked: resolve the error and retry; this hook is a "
            "safety gate for GitHub pushes.",
            err,
        )
        return 1


def main(argv: Sequence[str] | None = None,
         stdin: TextIO | None = None,
         err: TextIO | None = None) -> int:
    """CLI entry point for the pre-push hook."""
    args = list(sys.argv[1:] if argv is None else argv)
    if len(args) != 2:
        stream = sys.stderr if err is None else err
        _stderr(
            "expected remote name and remote URL arguments; "
            "refusing to allow push",
            stream,
        )
        return 1
    remote_name, remote_url = args[0], args[1]
    return run_hook(
        remote_name,
        remote_url,
        sys.stdin if stdin is None else stdin,
        err=sys.stderr if err is None else err,
    )


if __name__ == "__main__":
    raise SystemExit(main())
