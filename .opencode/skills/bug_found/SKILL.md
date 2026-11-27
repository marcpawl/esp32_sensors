---
name: bug_found
description: Procedure to follow when an existing bug is found while
  working on a task.
---

## Existing Bug Found

While working on a task, an existing bug is found.

An existing bug is a bug that occurs in the base of the branch being worked
on.

Run all `git` and `gh` commands through `./scripts/dev.py shell`, as
described in the `git` skill. The commands below are shown without that
wrapper.

### If the bug fix is not needed for this task

- Look for an existing issue for the bug.
- If none exists, create a GitHub issue.
- Add a note to the pull request for the current task.
- Link the note to the bug issue.

### If the bug fix is needed for this task

1. Find or create a GitHub issue for the bug.

2. Record the base branch and base commit of the current branch: the branch
   the fix PR will target and the commit the current branch was started
   from, not the branch tip.

3. **Interrupt the work with a commit, not a stash.** Stage tracked
   changes, then handle untracked files explicitly:

   ```bash
   git status --short
   git ls-files --others --exclude-standard
   git add --update
   ```

   `--update` stages modified and deleted tracked files. It does not add
   untracked files, and ignored files are never added. If the untracked
   list is not empty, prompt before staging any of those files:

   - Stage an untracked file only when it belongs to the task.
   - If it is generated or local-only, add a rule to `.gitignore` and
     stage that change instead of staging the file.
   - Do not use `git add -A`, `git add .`, or `git add -f`.

   Once the untracked files are resolved, review the staged change and
   commit:

   ```bash
   git diff --cached
   git commit -F .git/commit_msg.txt
   rm .git/commit_msg.txt
   ```

   Write `.git/commit_msg.txt` before the commit. Keep the file inside the
   repository, because the container cannot see a host file under `/tmp`.
   The message is the handoff for resuming the task, so include:

   - That the task is being interrupted to fix the bug.
   - The original task: goal, issue or pull request, branch name, base
     branch, and base commit.
   - What has been done: files changed, behavior implemented, tests added,
     and test results.
   - What remains: pending steps, open questions, and decisions already
     made.
   - The bug found: symptom, reproduction, expected and actual behavior,
     base commit, and issue URL.
   - Where to resume: the base branch and commit, the fix branch name, and
     any commands or notes needed.

   Use an imperative subject of at most 72 columns, such as
   `Interrupt <task> to fix <bug>`. Wrap the body at 72-79 columns and end
   with the trailers required by the `git` skill.

4. Create the fix branch from the recorded base commit, not from the tip of
   the current branch and not from the interrupt commit:

   ```bash
   git switch -c fix/<short-name> <base-commit>
   ```

5. Create a failing test case that demonstrates the bug.

6. **Fix** the problem, adding host-testable tests where possible, then
   fetch and rebase the fix branch onto the latest base branch:

   ```bash
   git fetch
   git rebase origin/<base-branch>
   ```

7. **Push** the fix branch.

8. Create a pull request for the fix.

9. **Rebase onto the fix branch instead of waiting for the fix PR to
   merge.** From the original branch:

   ```bash
   git fetch
   git branch -f fix/<short-name> origin/fix/<short-name>
   git rebase fix/<short-name>
   ```

   The interrupt commit and any later task commits now sit on top of the
   fix. Keep the local fix branch; the final rebase uses it as the upper
   bound.

10. If there is an issue for the original task, add a comment referencing
    the bug fix, including the URL of the bug issue and fix pull request.

11. If there is a pull request for the original task, have it block until
    the bug fix has been merged. Do not merge the original task while it
    depends on an unmerged fix branch.

12. **Resume** from the interrupt commit:

    - Find it with `git log --grep='^Interrupt '`.
    - Read its message with `git show -s --format=%B <commit>`.
    - Check the done and pending lists against the current code and tests.
    - Continue the original task from there.
    - Keep the interrupt commit until the original task is complete.

13. Once the fix PR has merged, fetch and rebase again to move the task
    commits from the fix branch onto the merged base branch:

    ```bash
    git fetch
    git rebase --onto origin/<base-branch> fix/<short-name>
    ```

    Then delete the local fix branch.
