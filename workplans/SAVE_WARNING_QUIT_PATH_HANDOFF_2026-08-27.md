# The save-warning modal never fires on the quit path — Handoff (2026-08-27)

**For:** the checking agent.
**Base:** `040a8de` (the GUI half of the embedded-section data-loss fix).
**Engine counterpart:** `7d43a1ff` in `openswmm.engine`.
**Standing findings:** engine lessons 1–163.

**This is a review finding on `040a8de`, not a rewrite of it.** That commit is
correct and I did not touch its design. It has one residual hole, in the one
save a user is most likely to make last.

---

## 0. Why this round exists at all

I came to this repo to *implement* the GUI half and found it already
committed by a concurrent session. So I reviewed it instead. Recording the
review, because the parts that are right matter as much as the part that
isn't:

- **The funnel claim is TRUE, and I checked it rather than trusting it.**
  `040a8de`'s message says "every GUI save path funnels through `saveAs`".
  Swept: the only production engine-write call site in the whole repo is
  `swmmvisprojectwindow.cpp:1439`. Every other hit is a test or a comment.
  One connection really does cover every path.
- **No captured delta can be stranded.** Between the write (1439) and the
  emit (1623) there is exactly one `return false` (1462) and it is the
  `rc != 0` branch, where nothing was captured. Checked by enumerating the
  returns in that span, because "emitted last" is only true if nothing
  leaves early.
- **The emit is guarded on non-empty**, so `cleanSave_staysSilent` is
  pinning real behaviour, not an accident.

## 1. The defect

`SWMMVis::closeEvent` walks the dirty sub-windows and calls `pw->close()`.
That runs the *"Save before closing?"* prompt, and **Save** calls
`save()` → `saveAs()` → the engine write → `emit
saveCompletedWithEngineWarnings(...)`.

That emit is **queued** — correctly so; `040a8de` made it queued precisely
so a modal cannot re-enter `auto-save-before-run`. But a queued emit is a
metacall **posted** to `SWMMVis`, delivered only when control next reaches
the event loop.

**On the quit path control never reaches the event loop.** `closeEvent`
continues: close dialogs → cancel runners → `saveSettings()` →
`QMainWindow::closeEvent` accepts → the app exits.

So: **user edits a deck with an embedded `[REACTION_*]` system, quits, answers
Save, and the reaction system is destroyed with no message.** That is the
original defect, alive in the exit path, after both halves of the fix landed.
The engine warned, the GUI captured it, and the notice was posted into a queue
nobody would ever drain.

Closing a *single* window while the app stays up is fine — control does
return to the loop there. It is specifically **quit** that drops it.

## 2. The fix

```
mod: src/swmmvis.cpp   (one call + comment, in SWMMVis::closeEvent)
```

```cpp
QCoreApplication::sendPostedEvents(this, QEvent::MetaCall);
```

Placed immediately after the sub-window walk: every window has settled, and
we are still ahead of dialog teardown, so a modal parented to `this` comes up
against a fully live main window.

**⚠ The premise is a claim about Qt teardown ordering that I have NOT
measured — and per engine lesson 159 I am not going to let the hedge stand in
for the check. Step 1 of §3 is that measurement.** What I can say
independently of the answer: `sendPostedEvents` **consumes** the pending
metacall, so if Qt *would* have drained the queue anyway the only effect is
that the modal appears slightly earlier. The change is safe under both
outcomes; only its *necessity* is unmeasured.

No new include: `QCoreApplication::` already resolves in this TU (used at
`:732` and `:5311`) and `<QEvent>` is included at `:181`.

## 3. Validation protocol

1. **Measure the premise first — revert the one line and quit with a dirty
   embedded deck.** Open `tests/gui/data/…` or any deck carrying an embedded
   `[REACTION_OPTIONS]` + `[REACTION_SPECIES]`, edit anything, ⌘Q, answer
   **Save**. **Report whether the modal appears.**
   - If it does NOT appear: the defect is confirmed and the fix is load-bearing.
   - **If it DOES appear, my §1 is wrong** — Qt drains the queue before exit,
     this round is unnecessary, and the right outcome is to revert it rather
     than keep a change whose justification did not survive contact. Say so.
2. **With the fix in, repeat.** Modal appears, names the dropped section, app
   quits normally afterwards.
3. **The non-quit path must not regress:** close a single dirty window with
   other windows still open — modal still appears exactly once, not twice.
   Double-firing is the specific way this fix could go wrong.
4. `ctest` on the GUI suite, including `040a8de`'s three
   `test_savewarnings` gates. They exercise `saveAs` directly and should be
   untouched by this — **if any of them move, something is wrong with my
   understanding, not with them.**
5. **Falsifier sweep:**

   | falsifier | expected |
   |---|---|
   | i. move the flush to *after* `saveSettings()` | modal still appears (still pre-accept) — confirms the placement is about dialog liveness, not delivery |
   | ii. move it before the sub-window walk | **no modal** — nothing has been posted yet. Confirms ordering is load-bearing and not incidental |
   | iii. change the connection to `Qt::DirectConnection` and drop the flush | modal appears on quit, but now re-enters `auto-save-before-run` — the reason `040a8de` chose queued. **Do not adopt this**; it is here to show the flush is buying something the simpler change does not |
   | iv. quit with a **clean** deck | no modal, no log entry. A quit-time notice that fires unconditionally is lesson 148's failure mode |

6. **Record:** step 1's answer above all — it decides whether this round
   survives.

## 4. Known gaps

- **⚠ NO AUTOMATED GATE. This is the honest weakness of the round and I am
  not dressing it up.** The defect lives in `SWMMVis::closeEvent` behind a
  modal prompt and an application quit; the existing `test_savewarnings`
  harness drives `SWMMVisProjectWindow` directly and cannot reach it. I
  considered a gate that posts a queued signal, closes a window and then
  calls the same flush the fix calls — **that asserts the mechanism against
  itself and would pass on a broken build.** A tautological gate is worse
  than an acknowledged manual step, because it reports coverage that is not
  there. Engine lessons 162 and 163 were both paid for exactly here.
  **If you can see a gate that genuinely bites, it is worth more than the
  fix.**
- **`040a8de`'s modal keys on the literal phrase `"lost from this save"`**,
  which is pinned by gates in *both* repos that do not know about each other.
  Reword the engine's message and the GUI silently downgrades data loss to a
  log line. The GUI gate catches it only once the GUI is rebuilt against the
  new engine. Recorded, not fixed — it needs a shared constant across a repo
  boundary.
- **`workplans/` is gitignored** (`.gitignore:99`), exactly as `plans/` is in
  the engine repo (engine lesson 158). This document exists in one working
  tree on one machine. Same decision owed, now in two repos.
- **Still a mitigation, not a cure.** The user is told, not protected. IO3's
  per-component `saveData()` is what stops the loss; until then the modal
  reports data that is already gone, with no recovery offered.
- **⚠ `src/swmmvis.cpp` had uncommitted changes from a concurrent session
  when I edited it** (alongside `forms/swmmvis.ui`, `include/swmmvis.h`,
  `src/swmmvisactions.cpp` — an unrelated actions/UI feature). My hunk is in
  `closeEvent` and theirs appears not to be, but **stage this file narrowly
  and check the diff before committing.** Do not `git commit -a`.

## 5. Prepared commit message

```
fix(save): the data-loss warning never reached a user who saw it on the way out

The save-time engine warning (040a8de) is emitted over a queued connection so
a modal cannot re-enter auto-save-before-run. On the quit path that emit is
posted and never delivered: SWMMVis::closeEvent runs each dirty window's
"Save before closing?" prompt, then closes dialogs, saves settings and accepts
-- control never returns to the event loop, so the metacall dies in the queue.

A user who edited a deck with an embedded [REACTION_*] system, quit, and
answered Save therefore destroyed the reaction system with no message: the
original silent loss (engine 7d43a1ff), surviving in the exit path after both
halves of the fix had landed.

closeEvent now flushes pending metacalls to itself immediately after the
sub-window walk -- every window settled, still ahead of dialog teardown, so
the modal comes up against a live main window. sendPostedEvents consumes the
event, so the notice cannot double-fire on paths that would have drained the
queue on their own.

No automated gate: the defect sits behind a modal prompt and an application
quit, and the test harness drives SWMMVisProjectWindow directly. A gate that
flushed the queue itself would assert the mechanism against itself and pass on
a broken build. Manual protocol in
workplans/SAVE_WARNING_QUIT_PATH_HANDOFF_2026-08-27.md.
```
