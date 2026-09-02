# The `handover` branch — communication channel between the two Claude agents

This is an **orphan branch**. It shares no history with `main`, `mtk-genio-dev` or `mtk-v4.4.2`, and
it contains **no Zephyr source**. Nothing here is ever merged into a code branch or submitted
upstream.

```bash
git fetch origin handover
git show origin/handover:STATUS.md          # current state, read this first
git log --oneline origin/handover           # history of the conversation
```

Or check it out somewhere harmless:

```bash
git worktree add /tmp/handover origin/handover
```

## Why an orphan branch and not a commit on the work branch

`mtk-genio-dev` is the branch upstream PRs are cut from, it is force-pushed wholesale on every drop,
and every verification gate counts its commits (`git rev-list --count origin/main..HEAD` must equal
the drop's patch count). A handover commit riding on that tip would have to be stripped before every
PR, would break those counts, and would be destroyed and recreated on each force-push. Keeping the
channel on a disconnected branch avoids all of it.

## Layout

| Path | Contents |
|---|---|
| `STATUS.md` | Always-current snapshot: branch tips, what is verified, what is blocked. Overwritten every update. |
| `to-authoring/` | Reports from the build machine to the authoring side, one file per drop, named by date. |
| `artifacts/` | Raw tool output — `compliance.xml`, config diffs, failure tracebacks. Overwritten per drop. |

## Direction of travel

**This channel is one-directional: build machine → authoring side.**

The authoring side cannot push to GitHub. Its drops still reach the build machine by manual file
transfer through Aary. That is a fixed constraint, not something this branch solves — so there is
deliberately no `to-build/` directory here. Patches continue to arrive as they always have.

## Why `artifacts/` matters

The authoring side has no Python and cannot run `west`, `check_compliance.py`, `checkpatch.pl` or
`clang-format`. Without raw output it has to take the build machine's prose summaries on trust.
Shipping the actual tool output lets it check the reasoning instead — including cases where a check
*crashed* rather than passing, which a summary can easily blur.

## Commit convention on this branch

Plain descriptive subjects, **no `GENIO: ` prefix**. That prefix marks commits destined for upstream
stripping; nothing here is ever upstreamed, so using it would be misleading.

## Companion: `verified/*` tags

Annotated tags record exactly which SHA was tested, because the work branches are force-pushed and a
tested SHA may not survive under any branch name:

```bash
git fetch origin 'refs/tags/verified/*:refs/tags/verified/*'
git tag -l 'verified/*'
git show verified/2026-09-02          # full report, pinned to the SHA it describes
```
