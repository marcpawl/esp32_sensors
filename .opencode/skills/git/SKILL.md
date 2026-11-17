---
name: git
description: Commit with git using a message file and `git commit -F`. Use
  whenever work is ready for review before committing, or when the work is
  about to be committed.
---

# Committing with git in this repository

## Quality checks

Applicable quality checks should be run prior to pushing

There are no quality checks for .md files.
Skill cpp_qc is used for C++ and CMake changes.
Skill python_qc is used for C++ and CMake changes.

Ask if the quality checks should be applied after
changes have been made  when the user has asked
for a code change.


## Commit Message

**Never pass the commit message on the command line.** Write it to a temporary
file first, then commit with `-F`:

```bash
# 1. Write the message to a temporary file.
$EDITOR "$(mktemp)"
# ...or write it with the Write tool to a path under /tmp.

# 2. Commit, reading the message from that file.
git commit -F /tmp/commit_msg.txt

# 3. Remove the file.
rm /tmp/commit_msg.txt
```

Do not stop to ask for approval of the message. Commit with it; rewording
before a push is cheap, and the message does not need to be final first.

`git commit -m "<message>"` is **not** used, and neither is a bare `git commit`
(which opens an editor).

### Why a file, not `-m`

A bare `git commit` **hangs** without a TTY. `EDITOR=vim` is set in this
environment, so git opens vim, vim exits with

```
Vim: Error reading input, exiting...
error: There was a problem with the editor 'vim'.
Please supply the message using either -m or -F option.
```

and the commit never happens — usually only after the quality gates have
already run and timed out. Writing the file and passing `-F` removes the editor
from the loop entirely.

`-m` avoids the editor too, but it is the fragile option: the messages in this
repository contain backticks, apostrophes, `$`, `!`, and multi-paragraph
bodies. Each of those is a shell-quoting hazard, and one badly quoted character
silently mangles the message. A file has no quoting at all.

### Message content

Match the existing log. Look at it before writing:

```bash
git log --format='=== %h%n%B' -5
```

Shape:

- **Subject line**, imperative mood, no trailing period, **≤ 72 columns**
  (`Add pycodestyle gate and republish the dev image as 20261109b`).
- **Blank line**, then a body wrapped at **72–79 columns**. This repository
  wraps at 79; do not exceed it.
- Explain **why**, not just what — the diff already says what changed. Compare
  options considered and say why the chosen one won.
- Reference the specific files, functions and tests that carry the change.
- End with the trailer block (see below).

Repository-specific wisdom worth carrying into the body when relevant:

- A behaviour change that a reader could reasonably "fix" back to the old way
  deserves the reason it was chosen (e.g. a new image tag rather than moving an
  existing one, because `docker-compose.yml` uses `pull_policy: missing`).
- Tests are named by class where it helps the reviewer find them.

### Required trailer

Every commit ends with trailers describing the AI used.

```
ai-agent: [Agent Name](Agent URL)
ai-model: [Model Name)(Model URL)
ai-session: AI agent session name
```

## Pull Request Message

Before an OpenCode session pushes to GitHub:
- Create a draft pull request if there is not already one
- rewrite the pull request's extended description.

Derive the description from:

- the issue the pull request is bound to, when there is one;
- the pull request summary;
- the git log entries for the commits that are part of the request.

Summarize it to **under 100 words**, in one paragraph, describing what the
commits solve rather than listing them. End it with a trailer, so the merge
commit carries a link to the detail instead of repeating it:

```
pull_request: https://github.com/OWNER/REPO/pull/N
```

Apply it with `gh pr edit N --body-file FILE`.
