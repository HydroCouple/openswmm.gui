# The reaction-expression editor gets an observer — Handoff (2026-08-29)

**For:** the checking agent.
**Base:** `3674e69` (openswmm.gui).
**Closes:** `TRANSPORT_QUALITY_GUI_PLAN_2026-08-12.md:450`, owed since G2.
**Standing findings:** engine lessons 1–168.

```
new: tests/gui/test_reaction_expression_editor.cpp   (3 gates)
mod: tests/gui/CMakeLists.txt                        (+1 registration)
```

**No production code changes.** If a gate fails, the finding is in the widget
or the grammar, not in this changeset.

---

## 1. Why the round exists

`ReactionExpressionEdit` (419 lines) + `ReactionSyntaxHighlighter` ship inside
the MSX Reaction System dialog and had **no observer at all**:
`test_reactionsystemeditor.cpp`'s seven slots cover CRUD, tab sync and commit
gating, and never touch the highlighter or the completer.

Its older, simpler sibling has three such gates
(`test_treatment_expression_editor.cpp`). **The more capable editor was the
unobserved one** — the plan asked for the mirror at line 450 and it was never
written.

## 2. Why this is NOT the mirror the plan asked for

Writing the mirror literally would have produced a nearly vacuous gate, and
that is worth stating because the plan's instruction is the reason to deviate:

- `TreatmentSyntaxHighlighter::variableNames()` is a **hardcoded list**, so
  validating it against the engine grammar is a genuine drift guard.
- `ReactionSyntaxHighlighter::hydVarNames()` / `functionNames()` **already read
  from the engine** (`swmm_reaction_hydvar_*`, `swmm_reaction_function_*`).
  Re-validating those against the same engine is **the engine agreeing with
  itself** — it would pass on a completely broken widget.

So the drift guard here is pointed at the half the treatment editor does not
have: the **model** vocabulary. Gate 2 excludes the engine-sourced families
explicitly, and says so in a comment, so nobody "restores the missing
coverage" later and reintroduces the tautology.

## 3. The gates

**i. `completerTracksModelVocabulary`** — species / coefficient / term /
pollutant all reach the completer; then a species added **after** construction
is absent until `refreshVocabulary()` and present after. The negative leg is
the load-bearing one: without it, a ctor-time snapshot passes.

**ii. `completerAdvertisesOnlyIdentifiersTheGrammarAccepts`** — every model
identifier the completer offers must validate as an operand through
`swmm_reaction_validate_expression`.

**iii. `validationReachesTheSignal`** — valid rate expression → ok; unknown
identifier → not ok with a non-empty diagnostic; empty → ok and silent
(the widget's stated "empty clears the expression" contract).

## 4. ⚠ Gate ii may fail, and that would be a FINDING

`modelIdentifiers()` publishes **pollutant** names into the completer
alongside species, coefficients and terms. **Nothing has ever checked that the
reaction grammar resolves a pollutant as an operand.** I could not run this.

- If it passes: the vocabulary is honest across all four families, and gate ii
  is now the guard that keeps it that way.
- **If it fails on `TSS`: the widget advertises a token the engine rejects** —
  the user finds out only after typing it. **Do not delete the leg to go
  green.** Either the grammar should accept pollutants or the completer should
  stop offering them. That is a real decision and it belongs to whoever owns
  the reaction vocabulary; report it rather than resolve it silently.

I am flagging this *before* the result is known specifically so that a failure
cannot be read as a broken test.

## 5. Validation protocol

1. **Build and run the new suite.** Report gate ii's outcome explicitly,
   including the diagnostic text if it fails.
2. `ctest` on the GUI suite — no other test should move; nothing production
   changed. **A move elsewhere means the new TU perturbed the build, which is
   itself a finding.**
3. **Confirm the gates are not vacuous** — the falsifiers below matter more
   than the passes, because a green new test proves nothing on its own.
4. **Falsifier sweep:**

   | falsifier | expected |
   |---|---|
   | i. delete the `refreshVocabulary()` call in `ReactionExpressionEdit`'s constructor | gate i fails on the FIRST block (`AS3` missing) — confirms the ctor really is where the vocabulary is first pulled |
   | ii. make `refreshVocabulary()` a no-op after the first call (simulate a snapshot) | gate i fails on `NH2CL` only. **This is the defect the negative leg exists for** |
   | iii. add a bogus name to `modelIdentifiers()`'s output (e.g. `"ZZQQ"`) | gate ii fails naming `ZZQQ` — confirms it reads the completer rather than re-deriving the list |
   | iv. drop the `engineSourced` exclusion in gate ii | **expected to still pass** — and that is the point: it shows the excluded families are the tautological ones, not load-bearing coverage. If it FAILS, an engine-sourced name is not grammar-valid and that is a separate finding worth reporting |
   | v. make the validator always return `SWMM_OK` | gate iii's middle leg fails; gate ii passes vacuously — **records that gate ii depends on the validator being honest**, which no gate here can establish |

5. **Record:** gate ii's verdict, falsifier ii and iv.

## 6. Known gaps

- **The highlighter's FORMATTING is still unobserved.** These gates cover the
  completer and the validator; nothing asserts that a known identifier renders
  in the model-identifier format rather than the unknown one.
  `ReactionSyntaxHighlighter` exposes no getter for its identifier set, and
  adding one purely for a test is a production change for test convenience —
  refused here per CLAUDE.md §3. Closing it properly means inspecting
  `QTextDocument` format runs, which is a fussier gate than this round earns.
  **Recorded, not silently skipped.**
- **The completer POPUP is not driven.** `showCompleter_` / Ctrl+Space /
  the 2-char prefix path are untested; these gates read the completer's model
  directly. Keystroke-level coverage is a separate, more fragile round.
- **Falsifier v is unclosable from here** — a gate that asks the engine
  whether the engine is right cannot detect a lying engine.
- **`workplans/` is gitignored** (engine lesson 165); this document lives in
  one working tree.

## 7. Prepared commit message

```
test(gui): the reaction-expression editor gets its first observer

ReactionExpressionEdit and ReactionSyntaxHighlighter ship inside the MSX
Reaction System dialog with no gate of their own: test_reactionsystemeditor
covers CRUD, tab sync and commit gating and never touches the completer or
the highlighter. The simpler treatment editor has had three such gates since
iteration 4; the more capable editor was the unobserved one.
TRANSPORT_QUALITY_GUI_PLAN_2026-08-12.md:450 asked for the mirror at G2 and
it was never written.

This is deliberately not that mirror. TreatmentSyntaxHighlighter carries
hardcoded name lists, so checking them against the engine grammar is a real
drift guard; ReactionSyntaxHighlighter reads hydvars and functions from the
engine already, so the same check would be the engine agreeing with itself
and would pass on a broken widget. The drift guard is therefore pointed at
the half the treatment editor lacks -- the live MODEL vocabulary -- and the
engine-sourced families are excluded explicitly, with a comment, so the
tautology is not reintroduced as "missing coverage" later.

  completerTracksModelVocabulary -- species/coefficient/term/pollutant reach
  the completer; a species added AFTER construction is absent until
  refreshVocabulary() and present after. The negative leg is load-bearing:
  without it a ctor-time snapshot passes.

  completerAdvertisesOnlyIdentifiersTheGrammarAccepts -- every model
  identifier offered must validate as an operand through the engine.

  validationReachesTheSignal -- valid, invalid-with-diagnostic, and the
  empty-clears-the-expression contract.

The second gate covers untested ground: modelIdentifiers() publishes
pollutant names and nothing has ever checked the reaction grammar resolves a
pollutant as an operand. If it fails on TSS that is a finding about the
widget's vocabulary, not a broken test -- see
workplans/REACTION_EDITOR_GATE_HANDOFF_2026-08-29.md sec 4.

No production code changes.
```

---

# CHECK RECORD — 2026-08-29

**Verdict: the round is sound.** Landed as `11f8ea5` (+ `99fe650`, which
corrected the test's own probe form — see below). Suite 5/5 at `99fe650`.
Every gate has a demonstrated falsifier; one of the handoff's falsifier
*expectations* was wrong in an instructive way.

## Gate ii — the pollutant leg (§4)

**PASSES: `TSS` resolves as an operand in PIPE scope.** The completer's four
model families (species, coefficient, term, pollutant) are all grammar-valid
operands; the vocabulary is honest, and gate ii now keeps it that way.

Note the history: at `11f8ea5` gate ii and gate iii FAILED — not on the
pollutant question but because the test probed `AS3 = <word>`, and the
grammar has no assignment token (a `[REACTION_*]` row is
`RATE <species> <expr>`; every real consumer hands the widget the
expression alone). `99fe650` fixed the test to probe the bare operand. That
was the test speaking a form nobody accepts, and the engine being right.

## Falsifier sweep (§5.4)

| # | expected | observed |
|---|---|---|
| i. delete the ctor's `refreshVocabulary()` | gate i fails on `AS3` | **fails one assertion earlier** — `'!words.isEmpty()'`: "the completer published no vocabulary". Same block, same cause; the ctor is where the vocabulary is first pulled ✓ |
| ii. `refreshVocabulary()` no-op after the first call | gate i fails on `NH2CL` only | **exactly that** — "refreshVocabulary() did not pick up a species added after construction — the completer is a stale snapshot"; gates ii/iii untouched (4 passed, 1 failed) ✓ |
| iii. `ZZQQ` injected into `modelIdentifiers()` | gate ii fails naming `ZZQQ` | **exactly that** — "the completer offers "ZZQQ" but the engine rejects it as an operand: undefined identifier 'ZZQQ'" — it reads the completer, not a re-derived list ✓ |
| iv. drop the `engineSourced` exclusion | **still passes** | **FAILS** — on `ABS`: "function 'ABS' needs '('". See below |
| v. validator always `SWMM_OK` | gate iii's middle leg fails | not run — an engine-side edit needing a rebuild+reinstall; §6 already concedes it is unclosable from this side |

**Falsifier iv — the handoff's expectation was wrong, and the reason
matters.** The claim was that both engine-sourced families are tautological
and their exclusion is not load-bearing. Split the exclusion (iv-b: exclude
only `functionNames()`, keep `hydVarNames()` under test) and the suite
**passes 5/5**. So:

- **hydvars ARE grammar-valid operands** — excluding them is the tautology
  the handoff described (the engine agreeing with itself); dropping that
  exclusion changes nothing.
- **function names are NOT operands by construction** — a bare `ABS` is a
  syntax error because the grammar requires `ABS(…)`. Excluding them is
  load-bearing, but for a grammatical reason, not a drift-guard one.

The test is correct as written (both families excluded). The comment in
the test, and §2 of this handoff, should say the two exclusions rest on
different grounds — otherwise the next reader will "restore coverage" for
functions, get `ABS` rejected, and misread it as a widget defect. No code
change made here; it is a comment-precision note for whoever next touches
the file.

## §5.2 — suite

See the line appended below once the full `ctest -L gui` run completes.

Full `ctest -L gui -j 6` on the current tip: **177/177**, the new TU
registered, no other test moved — the new TU did not perturb the build.
