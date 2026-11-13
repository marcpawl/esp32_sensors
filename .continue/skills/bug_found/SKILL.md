## Existing Bug Found

While working on a task and an existing bug is found 

-  If **bug fix is not needed for this task**
   Look to see if there is an exising issue for the bug, if
   not create a github issue.
   Have a note indicating the new bug found in the pull request for
   the task being worked on, linking to the issue.

- If the **bug is not needed for the task.**
  1. **Stash** the in-progress changes with a message that describes the work
     done so far and what is still pending for the task, e.g.
     `git stash push -m "WIP <task>: done ...; pending ..."`.
  2. Create a new issue for the bug found.
  2. **Branch** from the base of the current branch (the commit the current
     branch was started from, not the current branch tip):
     `git switch -c fix/<short-name> <base-commit>`.
  4. Create a failing test case that demonstrates the bug. 
  5. **Fix** the problem, add tests where the bug is host-testable, 
     then **pull and rebase** the current branch (`git pull --rebase`).
  6  **push** the branch.
  7. Create a PR
  8. **Wait** for the fix to be merged.
     Do not continue as if the fix
     were available on the original branch.
  9. Once merged, **delete** the fix branch and re-sync the original branch
     (`git pull --rebase`) so it includes the merged fix.
 10. If there is an issue for the task, then add a comment referencing
     the bug found, including the URL to the bugs issue.
 11. If there is a PR for the task, have the PR block until the
     bug fix has been merged.
 12. **Return** to the original branch and `git stash pop`; read the stash
     message to recall what was done and what was pending. 
     Rebase to get the fixes.  Then **resume** the original task.


An existing bug is defined as a bug that occurs in the base of branch being
worked on.
