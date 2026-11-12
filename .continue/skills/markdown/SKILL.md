---
name: markdown
description: How the Markdown files in this repository are written. Use before
  writing or editing any .md file — documentation, a skill, or a README.
---

# Markdown

## The two rules

1. Wrap prose at **fewer than 80 columns**, wherever wrapping does not change
   the formatting.
2. When a passage describes several items, make it a **list or a subsection**,
   not a sentence that runs the items together.

## Keep the lines short

Prose is wrapped at 79 columns or fewer, so a line is short enough to read in a
diff without re-flowing the paragraph around it.

"Where it would not affect the formatting" exempts:

- **tables** — a row is one line, and a cell is never broken;
- **fenced code blocks** — the code is never re-wrapped;
- **headings** — never broken;
- **a path, URL, command, or identifier** that is longer than the limit by
  itself — leave it whole on its own line rather than splitting it;
- **list markers and indentation** — the marker is not counted, and a
  continuation line keeps the text aligned under the item.

So this is right:

```markdown
The reusable components live in the repo-root `components/` directory and
the three HAL components live in `hal/`.
```

And this is not — one 138-column line, the kind a diff cannot show without
re-flowing the whole paragraph:

```markdown
The reusable components live in the repo-root `components/` directory and the three HAL components live in the repo-root `hal/` directory.
```

## Prefer lists and subsections to run-on sentences

A sentence that strings several items together hides them. The reader cannot
scan it, cannot tell how many items there are, and cannot link to one of them.

Bad — one sentence that strings three items together (the line break does not
make it a list):

> The repository holds the firmware in `mcgowan/`, the host tests in
> `test-logic/`, and the linux application in `donny/`.

Good — a list:

> - `mcgowan/` — the ESP32-S3 firmware
> - `test-logic/` — the host unit tests
> - `donny/` — the linux-emulated application

When each item needs its own explanation, give it a **subsection** instead of a
list item:

```markdown
### `mcgowan/`

The ESP32-S3 firmware. It links `hal_board` and produces `mcgowan.bin`.

### `donny/`

The linux-emulated application. It links `hal_stub` and produces
`donny.elf`.
```

A list is for items that need one line each. A table is for items that share
the same set of properties — the columns are the properties, the rows are the
items. A subsection is for items that need a paragraph.

## Why the rules

The prose in this repository is already written at this width, so the rule is a
description of the house style rather than a new constraint.

The list rule is about the reader. A run-on sentence makes the author choose
between a paragraph that hides the items and a list that is shorter than the
sentence would have been. Writing the list first is also how the author finds
out whether there are really three items or five.
