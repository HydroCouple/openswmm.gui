# Phase 23 — burned terrain and reports publish on Save

## Working-state contract

Channel burn-in now writes its raster and report into a private, uniquely owned generation directory beside the project. The worker reads that physical raster for terrain sampling and meshing; the completion result and adopted mesh layer share ownership of the pending files. Saved INP, 2DM, burned DEM and report files stay unchanged until project Save. A new final terrain directory is not created during generation.

Failure after burning, cancellation before work, cancellation while a completed result is waiting for adoption, stale-result rejection and discarding the owning project release the pending files when their last owner is gone. Canvas layers can outlive a project, so project destruction explicitly abandons their generated artifacts. Undo-retained layers keep their pending data while the owning project remains open. Save failure retains the pending files for correction/retry; successful publication releases them after the project settles.

The generation directory is a staging resource, not an output destination. Save protects the entire directory, its known files, original DEM dependencies reported by GDAL and required-absent raster companions. This prevents a model, mesh or component from being published into a directory that later cleanup would delete, or from replacing the source terrain.

## Publication and outside changes

Before burning, the job captures resolved identities and SHA-256 fingerprints of both intended final destinations, including absence. Save checks those original states when registering the auxiliary outputs. Publication also retains the existing destination checks, old/new journal copies, rollback and restart recovery. A file created, edited or retargeted after capture stops replacement rather than becoming the new overwrite baseline. Protected-directory checks cover ordinary and symlink-aliased descendants and are repeated before publication.

The completed payload is sealed with hashes. Save streams checked copies into transaction staging and compares the copied bytes with those hashes; missing, altered or incomplete pending files stop Save. `Auxiliary = 4` preserves the engine output-role ABI and explicitly publishes raster/report resources before project settings and the model. Existing recovery records are role-independent, so no journal format change is required.

Save As retains the intended burn destinations captured during generation, as it does an explicitly selected external mesh destination. Reports name final logical paths, never temporary job paths. The burn summary makes clear that these files are written on Save. The vertical-unit conversion description now reports the actual model-to-raster factor used by the burn.

## Checked raster/report writes

The raster writer accepts a new path or an explicitly reserved empty stage. It refuses source aliases, nonempty existing destinations, symlinks and directories before writing. GDAL copy, band/dataset flush and close results are checked. Cancellation/failure removes only an output claimed by that invocation. A reopened completed GeoTIFF must be self-contained; an unexpected required GDAL companion is refused instead of silently discarded.

The CSV writer uses checked atomic publication and verifies stream, flush and commit results. Failure to prepare the promised report now fails the generated pair. Existing `.aux.xml`, `.ovr` and `.msk` companions at the intended final raster are refused to prevent old metadata, overviews or masks being applied to a replacement. Save checks for companions that appeared afterward and protects those paths from other outputs in the same transaction. Support for intentionally publishing/deleting multi-file raster companions remains separate work.

Burned-terrain Stage-B caching is disabled because the physical raster belongs to one disposable job. Ordinary terrain and boundary caching remain enabled. No cache is treated as a project output.

## Automated evidence

The actual-worker baseline reproduced four failures: existing/new burn destinations changed before Save on both successful meshing and a later thinning failure. The writer baseline had seven failing checks, including real short-write/flush failures and existing-file destruction. Production Save reproduced six failures covering ignored auxiliary output, outside/payload edits, source aliasing and project-discard cleanup. The transaction baseline reproduced five auxiliary publication/recovery failures.

Final results: **247 checks across six suites**, no failures/skips:

| Suite | Passed |
|---|---:|
| Burned raster/report writer | 14 |
| Existing full channel-burn integration | 6 |
| Save transaction/recovery | 51 |
| Prepared-project validation | 26 |
| Actual mesh worker/ownership | 22 |
| Production Save | 128 |

The unit tests inject real POSIX file-size limits for report short-write and final raster flush failure. Transaction tests exercise auxiliary rollback and process interruption at publication boundaries. Worker tests inspect an actual lowered raster sample, preserved original DEM/sentinels, logical report paths, shared-owner cleanup and cancellation; successful raster/report review copies remain outside the job directory. Production Save verifies first/repeated publication and reopen, failure/retry, outside and payload edits, source aliases, forbidden staging-folder destinations, late companion files and discard cleanup.

Release GUI build/deployment and strict deep signature verification passed. Installed/bundled engine UUIDs match the tested runtime. New GUI PID **68994** launched at **2026-09-26 22:11:27 EDT**, preserving prior instances. See [the testing guide and logs](../../workplans/artifacts/phase_23_burn_staging/README.md) for evidence and native steps.

## Remaining boundaries

Native burn-preview, Save/Don't Save, keyboard and large-raster acceptance remain pending. A process crash can leave an abandoned generation directory; this phase does not add an orphan-directory scavenger or power-loss directory durability. Source inputs changed externally while generation runs are not fully fingerprinted as an input graph. Large-raster hashing/copy costs and cancellation latency during those synchronous file operations remain performance follow-up work. General import staging, complete topology/quality/groundwater remapping and live editing-engine refresh remain open W1 work. Optional database-driver packaging remains W9 work.
