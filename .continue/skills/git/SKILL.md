---
name: git
description: Commit with git using a message file and `git commit -F`. Use
  whenever work is ready for review before committing, or when the work is
  about to be committed.
---

# Committing with git in this repository

## The rule

**Never pass the commit message on the command line.** Write it to a
temporary file first, then commit with `-F`:

```bash
# 1. Write the message to a temporary file.
$EDITOR "$(mktemp)"
# ...or write it with the Write tool to a path under /tmp.

# 2. Commit, reading the message from that file.
git commit -F /tmp/commit_msg.txt

# 3. Remove the file.
rm /tmp/commit_msg.txt
```

`git commit -m "<message>"` is **not** used, and neither is a bare
`git commit` (which opens an editor).

## When this applies

- **The task's work is ready for review, before it is committed.** Write the
  message to the file at that point — it is the artifact the reviewer reads,
  and writing it first is what makes the review a review of the message as
  well as the diff.
- **The work is about to be committed.** Same file, same `-F` flag.

Both moments use one file. Write it once, review it, then commit from it.

## Why a file, not `-m`

A bare `git commit` **hangs** without a TTY. `EDITOR=vim` is set in this
environment, so git opens vim, vim exits with

```
Vim: Error reading input, exiting...
error: There was a problem with the editor 'vim'.
Please supply the message using either -m or -F option.
```

and the commit never happens — usually only after the pre-commit gates have
already run and timed out. Writing the file and passing `-F` removes the
editor from the loop entirely.

`-m` avoids the editor too, but it is the fragile option: the messages in
this repository contain backticks, apostrophes, `$`, `!`, and multi-paragraph
bodies. Each of those is a shell-quoting hazard, and one badly quoted
character silently mangles the message. A file has no quoting at all.

## Message content

Match the existing log. Look at it before writing:

```bash
git log --format='=== %h%n%B' -5
```

Shape:

- **Subject line**, imperative mood, no trailing period, **≤ 72 columns**
  (`Add pycodestyle gate and republish the dev image as 20261109b`).
- **Blank line**, then a body wrapped at **72–79 columns**. This repository
  wraps at 79; do not exceed it.
- Explain **why**, not just what — the diff already says what changed.
  Compare options considered and say why the chosen one won.
- Reference the specific files, functions and tests that carry the change.
- End with the trailer block (see below).

Repository-specific wisdom worth carrying into the body when relevant:

- A behaviour change that a reader could reasonably "fix" back to the old way
  deserves the reason it was chosen (e.g. a new image tag rather than moving
  an existing one, because `docker-compose.yml` uses `pull_policy: missing`).
- Tests are named by class where it helps the reviewer find them.

## Required trailer

Every commit ends with the Continue trailer, verbatim:

```
Generated with [Continue](https://continue.dev)

Co-Authored-By: Continue <noreply@continue.dev>
```

## Do not bypass the gates

The pre-commit hook (`.githooks/pre-commit`) runs, in order, `pycodestyle`,
`clang-tidy`, `format-check-staged`, then `test`. Let it run. `clang-tidy`
needs an existing compile database, so run `./scripts/dev.py build` at least
once after a clone or pull, or the gate fails with "no compile_commands.json".

`git commit --no-verify` is not part of this workflow. Use it only when the
user explicitly asks for it.

## Full example

```bash
cat > /tmp/commit_msg.txt <<'EOF'
Add pycodestyle gate and republish the dev image as 20261109b

Install pycodestyle 2.15.0 in the dev image (pinned and verified at build
time) and add `scripts/dev.py pycodestyle`, which checks every Python
source in the repository. It is the first gate in the pre-commit hook and
the first check in CI, because it needs no compile database, no build and
no test run.

The image is published under a NEW tag (20261109b) rather than moving
20261109: compose uses `pull_policy: missing`, so a moved tag would leave
every already-pulled machine on the old image while CI silently got the
new one.

Generated with [Continue](https://continue.dev)

Co-Authored-By: Continue <noreply@continue.dev>
EOF

git add -A
git commit -F /tmp/commit_msg.txt
rm /tmp/commit_msg.txt
```

A quoted heredoc (`<<'EOF'`) keeps backticks and `$` literal, which matters
when the message is built with a shell command. Writing the file with the
Write tool avoids the heredoc entirely.
