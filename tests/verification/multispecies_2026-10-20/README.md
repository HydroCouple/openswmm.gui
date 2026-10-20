# Wet-subcatchment reaction verification — 2026-10-20

The preceding increment was committed first: engine `770fe19d`, GUI
`ef493e3f`, benchmarks `406ab68`. This record covers the subsequent BULK wet
non-LID subcatchment scope, its C/Python interface, File editing, and rain-fed
example. `verification.json` records the tested source and binary hashes.
The build includes unrelated pre-existing checkout changes; it is not a
clean-checkout qualification.

Results: 10 native regression targets, 56 Python reaction checks and 15 GUI
checks pass. The surface-quality target contains 41 cases, including wet-pool
conversion, rain/run-on, infiltration/runoff/storage allocation, evaporation,
tiny/dry pools, failure retention, clamp accounting, algebraic expressions,
temperature and declared mass basis. End-to-end native output checks use
LEGACY, EULERIAN_ARD and LAGRANGIAN network transport.

The benchmark suite passes 24 closed-form network cells and three rain-fed
wet-pool self-consistency cells. The latter require A+B=tracer and positive B
at the subcatchment and receiving node; they make no time-history accuracy
claim. Synthetic zero output and broken sums both produce three FAIL cells
and a nonzero gate. A missing executable produces three UNAVAILABLE cells.

From `openswmm.engine`, rebuild the selected native targets in the existing
`build/component-catalogue-20261016` configuration, then run:

```sh
ctest --test-dir build/component-catalogue-20261016 \
  -R '^test_engine_(reactions_config|reactions_api|reaction_integrators|reaction_legacy_binding|reaction_ard_binding|output_quality|msx_parity|msx_buildup_washoff|lard_transport|lard_wiring)$' \
  --output-on-failure
```

The Python check used the existing complete staging package with the current
enum files and a freshly compiled `engine__reactions` extension. That extension
was built against copied current public headers and the native library in a
private prebuilt-engine prefix. The older staged extension omitted existing
rainfall properties; refreshing it resolved those two initial failures.

```sh
PYTHONPATH="$PWD/build/component-catalogue-python-20261016/stage:$PWD/python" \
DYLD_LIBRARY_PATH="$PWD/build/component-catalogue-20261016/src/engine:$PWD/build/component-catalogue-20261016/bin/Release" \
  .venv/bin/python -m pytest --import-mode=importlib -o pythonpath= \
  python/tests/engine/test_reactions.py -q
```

For the GUI, configure the accompanying `gui-check` project with Qt 6 Core,
Gui, Widgets and Test, build `reaction_editor_check`, and run with
`QT_QPA_PLATFORM=offscreen`. It links the same native build directly.
From `openswmm.engine.benchmarks`, run:

```sh
python run_regression.py --suite quality/reactions \
  --exe ../openswmm.engine/build/component-catalogue-20261016/src/cli/openswmm
```

Remaining work includes porous LID inventories and reactor adapters,
geometry-dependent WALL/BULK conversion, exact native checkpoint/output
continuation, dry buildup chemistry, selective subcatchment laws, and
SETPOINT/FLOWPACED sources. The wet-pool method mixes incoming mass before
reacting for the full runoff interval; rapid kinetics require timestep
sensitivity checks. Unsupported LID combinations and unavailable expression
contexts remain refused.
