#!/usr/bin/env bash
# Commit the still-pending openswmm.gui work — RUN ON THE HOST, NOT IN A SANDBOX.
#
# REWRITTEN 2026-08-27 (second pass). The first version of this script also
# committed the "Add 2D Results…" feature; the concurrent session committed
# that itself as 95f81ec while this was sitting unrun, so that step is gone.
# workplans/artifacts/twod_results.patch is stale and now holds only a notice.
#
# What is left un-committed and is MINE:
#   * the quit-path flush in SWMMVis::closeEvent (review finding on 040a8de)
#   * the two mesh-savedirty .oswp fixtures a COMMITTED test already reads
#
# ⚠ src/swmmvis.cpp currently carries 7 uncommitted hunks: 1 mine and 6 from
# the concurrent session's IN-FLIGHT work. This script stages ONLY my hunk, by
# patch. It never runs `git commit -a` and never touches the other six.
#
# ⚠ Because that session is live, re-verify before trusting the patch: step 1
# re-checks that quit_flush.patch is still an exact subset of the current diff
# and ABORTS if the file has moved under it.
#
# Usage:  bash workplans/artifacts/commit_pending_gui.sh
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"

# ---------------------------------------------------------------------------
# 0. Locks and litter
# ---------------------------------------------------------------------------
if [ -e .git/index.lock ]; then
    echo "!! .git/index.lock exists:"; ls -la .git/index.lock
    if pgrep -l git >/dev/null 2>&1; then
        echo "!! A git process is RUNNING — do not remove the lock. Stopping."
        pgrep -l git; exit 1
    fi
    echo ">> No git process running; removing the stale lock."
    rm -f .git/index.lock
fi
# Empty file my sandbox created probing writability and could not unlink.
rm -f .git/_probe

# ---------------------------------------------------------------------------
# 1. Re-verify the patch still matches the file
# ---------------------------------------------------------------------------
if ! git apply --check -R workplans/artifacts/quit_flush.patch 2>/dev/null; then
    echo "!! quit_flush.patch no longer matches src/swmmvis.cpp."
    echo "!! The concurrent session has moved the file. Regenerate the patch:"
    echo "     git diff -- src/swmmvis.cpp   # find the sendPostedEvents hunk"
    echo "!! Stopping rather than staging something unverified."
    exit 1
fi
echo ">> quit_flush.patch verified against the current worktree."

# ---------------------------------------------------------------------------
# 2. The quit-path flush  (review finding on 040a8de)
# ---------------------------------------------------------------------------
git apply --cached workplans/artifacts/quit_flush.patch

git commit -F - <<'MSG'
fix(save): the data-loss warning never reached a user who saved on the way out

The save-time engine warning (040a8de) is emitted over a queued connection so
the modal cannot re-enter auto-save-before-run. On the quit path that emit is
posted and never delivered: SWMMVis::closeEvent runs each dirty window's "Save
before closing?" prompt, then closes dialogs, cancels runners, saves settings
and accepts -- control never returns to the event loop, so the metacall dies in
the queue.

A user who edited a deck carrying an embedded [REACTION_*] system, quit, and
answered Save therefore destroyed the reaction system with no message: the
original silent loss (engine 7d43a1ff) surviving in the exit path after both
halves of the fix had landed. Closing a single window while the app stays up
was never affected -- control reaches the loop there. It is specifically quit.

closeEvent now flushes pending metacalls to itself immediately after the
sub-window walk: every window settled, still ahead of dialog teardown, so the
modal comes up against a live main window. sendPostedEvents consumes the event,
so the notice cannot double-fire on a path that would have drained the queue
on its own -- which is also why this is safe if Qt's teardown would have
delivered it anyway.

That premise is NOT measured. Step 1 of the protocol is to revert this hunk,
quit with a dirty embedded deck and report whether the modal appears; if it
does, this commit is unnecessary and should be reverted rather than kept for
tidiness.

No automated gate: the defect sits behind a modal prompt and an application
quit, and the test harness drives SWMMVisProjectWindow directly, so it cannot
reach SWMMVis::closeEvent. A gate that flushed the queue itself would assert
the mechanism against itself and pass on a broken build -- worse than an
acknowledged manual step, because it reports coverage that is not there.

Manual protocol: workplans/SAVE_WARNING_QUIT_PATH_HANDOFF_2026-08-27.md
MSG

# ---------------------------------------------------------------------------
# 3. Fixtures a COMMITTED test needs and does not have
# ---------------------------------------------------------------------------
# NOTE: the tracked .inp siblings of these files are also modified right now by
# the concurrent session. Only the untracked .oswp are staged here.
git add tests/gui/data/mesh_savedirty_clean.oswp \
        tests/gui/data/mesh_savedirty_edited.oswp

git commit -F - <<'MSG'
test(gui): commit the mesh-savedirty fixtures the test already reads

tests/gui/test_meshsavedirty.cpp is tracked and reads
mesh_savedirty_clean.oswp / mesh_savedirty_edited.oswp, which were not. The
gate therefore passed only on the machine where the fixtures happened to
exist, and a fresh clone could not run it at all -- the failure mode is a test
that looks green because nobody's checkout is clean.
MSG

echo
echo "=== done; the concurrent session's 6 hunks in src/swmmvis.cpp are untouched ==="
git --no-pager log --oneline -3
git status --short
