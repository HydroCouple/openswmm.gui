# Phase 20 — Parse the prepared save before publication

This W1 checkpoint adds a disposable, read-only engine parse between built-in
Save preparation and publication. The publishable model retains its final-path
references. A separate sibling validation input redirects mesh and component
references to their prepared files, so validation sees the proposed combination
rather than previously saved companion files.

## Behavior

- Explicit mesh and process-component references must name readable, nonempty
  regular files. Prepared outputs take precedence over existing final files.
  This also catches missing configurations on unknown component registrations,
  which the engine can otherwise diagnose before attempting the file read.
- The validation input stays in the final model directory. Unchanged relative
  rainfall, time-series and other engine-read references retain their normal
  anchor. The engine's existing file-format and reference checks are reused.
- Settings must parse as a JSON object with a positive schema version and a
  project session whose input-path string resolves to the final model.
- A new engine parses the validation input with lenient editor semantics.
  Hard parse failures, external resource failures and component-configuration
  diagnostics block publication. Node, link and subcatchment counts must match
  the source engine; mesh vertex, cell and quadrilateral counts must match the
  selected GUI mesh when supplied, otherwise the source engine.
- Other model diagnostics remain visible as draft warnings. An unresolved
  authoring reference does not by itself prevent saving an unfinished model.
- External `[PLUGINS]` declarations are removed only from the disposable
  validation input. Their saved declarations are preserved and the user receives
  a warning that external plugins were not loaded or validated. This is needed
  because normal engine open loads plugin code, even with no report/output path.
- Validation never calls engine initialize, start or run. Configured result,
  report, hotstart and ARD detailed-output destinations are not opened by this
  validation path. Temporary names are translated back to final names in
  diagnostics; the disposable validation file is removed on every return path.

## Implementation

- `include/project/projectsavevalidation.h`: bounded header-only validation.
- `ProjectSaveOutputs::preparedOutputs()`: read-only final/staged/role manifest.
- `SWMMVisProjectWindow::saveAs`: validates the completed staged set before the
  existing journal/publication transaction.
- `tests/gui/test_projectsavevalidation.cpp`: focused QtCore/engine coverage.

The helper deliberately does not modify the live engine or any saved destination.
Existing earlier Save synchronization still updates the live authoring engine;
that separate operation is not made transactional by this checkpoint.

## Regression coverage

The focused suite checks prepared companions overriding old invalid final bytes,
malformed/missing/empty companions, unknown components with missing configs,
nested Unicode paths and new directories, unchanged relative time-series files,
missing time-series files, unfinished drafts (including a missing object named
`config`), outside-directory component diagnostics with final-path names,
mixed triangle/quad counts, source
object-count mismatch, invalid settings, plugin isolation and untouched runtime
output sentinels. The integration suite covers the production Save caller and
its publication/dirty-state behavior.

Fixtures are retained under `SWMMVIS_SAVE_VALIDATION_OUTPUT`, defaulting to
`tests/gui/data/projectsavevalidation_output`; phase build and test evidence is
recorded by the integrating task. Final integrated validation passed **26 focused
validation checks**, **112 production Save checks**, **30 journal/recovery checks**
and **72 mesh-writer checks**, with no failures or skips (QtTest fixture checks
included). Combined with terrain and dialog coverage, Phase 20 passed **315 checks
across nine suites** against the current installed engine. Evidence:
`workplans/artifacts/phase_20_parallel/final_core_tests.log` and
`final_gui_tests.log`. Native acceptance remains separate.

## Limits and follow-up

This is built-in parse/reference and count validation, not a solver-readiness
certificate or field-by-field semantic equivalence proof. Full hydraulic mesh
geometry preparation, boundary resolution, numerical initialization and external
plugins require their own acceptance checks. The current built-in components
have no secondary input includes; ARD's config-relative `DETAILED_OUTPUT` token
is resolved during parse but opened only during runtime initialization. This
checkpoint does not certify runtime output-path resolution from relocated configs.

Unchanged external resources are read during validation but are not locked
against later external edits. Unknown/skipped model sections, all saved field
values, cancellation, stale asynchronous work and durable filesystem barriers
remain separate W1/W8 work. Existing recovery checks protect publication targets;
they do not provide a snapshot of every referenced external input.

The current engine splits whitespace inside `config="value"` component tokens.
Validation therefore refuses such final references with an explicit diagnostic
rather than concealing a subsequent Open failure. A project folder may contain
spaces: relative validation-stage tokens avoid introducing this parser limitation
when the published component token itself has no whitespace. Fixing quoted
component argument parsing in the engine remains a follow-up.

Additional regression gates cover unfinished references whose object name is
`config`, and component read failures outside the model directory whose engine
diagnostics retain `..` path segments. Both forms keep final-path diagnostics
and ordinary draft behavior.
