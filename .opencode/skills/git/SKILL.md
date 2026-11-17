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

The merge commit is written by GitHub, not by the session, so the pull request
has to read the way the commit should before the merge button is pressed. Two
repository settings decide how much of the pull request reaches the commit
message:

- `merge_commit_title=PR_TITLE` — the commit subject becomes the pull request
  title plus its number, e.g.
  `Stop the on-target esp_timer tests building all of ESP-IDF (#88)`;
- `merge_commit_message=PR_BODY` — the pull request **description** becomes
  the commit body, starting on the third line.

GitHub calls this combination "Pull request title and description". Only three
combinations are accepted, and none of them keeps the older
`Merge pull request #N from OWNER/BRANCH` subject alongside a description:

- `PR_TITLE` + `PR_BODY` — title and description; what this repository uses;
- `PR_TITLE` + `BLANK` — title only;
- `MERGE_MESSAGE` + `PR_TITLE` — classic merge subject plus title, no
  description. That was the setting until 2026-11-16, which is why merge
  commit `8ac8725` carried only the title line.

Check the settings before pushing; a wrong value silently drops the
description:

```bash
gh api repos/{owner}/{repo} \
  --jq '{merge_commit_title, merge_commit_message}'
# expect {"merge_commit_title":"PR_TITLE","merge_commit_message":"PR_BODY"}
```

Repair them if they differ:

```bash
gh api -X PATCH repos/{owner}/{repo} \
  -f merge_commit_title=PR_TITLE \
  -f merge_commit_message=PR_BODY
```

Before an OpenCode session pushes to GitHub:

- create a draft pull request if there is not already one;
- rewrite the pull request's extended description;
- give the pull request a title that is a complete commit subject: imperative
  mood, no trailing period, ≤ 72 columns **including** the ` (#N)` that GitHub
  appends.

### Writing the description

The description is the merge commit's body, so write it the way the body of a
commit is written in this repository, not the way a review comment is:

- derive it from the issue the pull request is bound to, when there is one;
  from the pull request summary; and from the git log entries for the commits
  in the request;
- **under 100 words**, in one paragraph, describing what the commits solve
  rather than listing them;
- wrapped at **72–79 columns**, so the commit body matches the log;
- end with a trailer, so the merge commit points at the pull request for the
  diff, the review, and the issue it closes:

```
pull_request: https://github.com/OWNER/REPO/pull/N
```

Apply it with `gh pr edit N --body-file FILE`.
