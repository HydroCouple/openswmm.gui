"""Compare fixed native rain-rule behavior with the unchanged legacy engine."""
import ctypes as c
import importlib.util
import json
import os
from pathlib import Path

HERE = Path(__file__).resolve().parent
ENGINE = HERE.parents[3] / "openswmm.engine"
OUT = HERE / "fixed"
os.environ.setdefault("OPENSWMM_RAIN_NEW_LIB", str(ENGINE / "build/debug/bin/Debug/libopenswmm.engine.6.0.0.dylib"))
os.environ["OPENSWMM_RAIN_PROBE_OUT"] = str(OUT)
spec = importlib.util.spec_from_file_location("probe", HERE / "probe.py")
p = importlib.util.module_from_spec(spec)
spec.loader.exec_module(p)
base_model = p.model
p.bind(p.old, "swmm_setValue", [c.c_int, c.c_int, c.c_double])
p.bind(p.new, "swmm_gage_set_rainfall", [p.ptr, c.c_int, c.c_double])
p.bind(p.new, "swmm_forcing_gage_rainfall", [p.ptr, c.c_int, c.c_double, c.c_int, c.c_int])

cases = []
for step in [60, 300, 3600, 7200]:
    cases.append(dict(name=f"constant_{step}", step=step))
for hours in [1, 24, 48]:
    cases.append(dict(name=f"pulse_{hours}", hours=hours, pulse=True, long=True))
for form in ["VOLUME", "CUMULATIVE"]:
    cases.append(dict(name=form.lower(), form=form, reset=form == "CUMULATIVE"))
cases += [dict(name="non_midnight", start="00:37:00"),
          dict(name="si", units="CMS"), dict(name="scale", scale=2),
          dict(name="monthly", monthly=2), dict(name="api_override", api=2),
          dict(name="api_monthly", api=2, monthly=2),
          dict(name="forcing_override", forcing=1), dict(name="forcing_add", forcing=2),
          dict(name="forcing_add_intensity", forcing=2, intensity=True),
          dict(name="forcing_ignored_intensity", forcing=2, intensity=True, ignore=True),
          dict(name="co_gage", co=True), dict(name="unused", unused=True),
          dict(name="rain_file", rain_file=True), dict(name="runoff_interface", interface=True),
          dict(name="ignore", ignore=True), dict(name="dry_expiration", pulse=True, long=True),
          dict(name="numeric_action", numeric=True)]
results, failures = [], []
for case in cases:
    p.ROOT = OUT / case["name"]
    p.ROOT.mkdir(parents=True, exist_ok=True)
    hours = case.get("hours", 24)
    step = case.get("step", 300)
    def model(attr, runoff_step=60, control_only=False, numeric_action=False):
        deck = base_model(attr, runoff_step, control_only, numeric_action)
        if case.get("intensity"):
            deck = deck.replace(f"GAGE RG {attr}", "GAGE RG INTENSITY")
        if case.get("start"):
            deck = deck.replace("START_TIME 00:00:00", "START_TIME " + case["start"])
        deck = deck.replace("END_TIME 06:05:00", "END_TIME 06:05:17")
        if case.get("units"):
            deck = deck.replace("FLOW_UNITS CFS", "FLOW_UNITS " + case["units"])
        form = case.get("form", "INTENSITY")
        deck = deck.replace("RG INTENSITY", "RG " + form)
        if case.get("scale"):
            deck = deck.replace("TIMESERIES RAIN\n", f"TIMESERIES RAIN {case['scale']}\n")
        if case.get("monthly"):
            deck += "\n[ADJUSTMENTS]\nRAINFALL " + " ".join([str(case["monthly"])] * 12) + "\n"
        if case.get("rain_file"):
            rain = p.ROOT / "rain.dat"
            rain.write_text("\n".join(f"STA 2026 1 1 {hour} 0 1" for hour in range(7)) + "\n")
            deck = deck.replace("RG INTENSITY 1:00 1 TIMESERIES RAIN", f'RG VOLUME 1:00 1 FILE "{rain}" STA IN')
        if case.get("interface"):
            mode = "SAVE" if case.get("saving") else "USE"
            deck += f'\n[FILES]\n{mode} RUNOFF "{p.ROOT / "runoff.dat"}"\n'
        if case.get("ignore"):
            deck = deck.replace("[OPTIONS]\n", "[OPTIONS]\nIGNORE_RAINFALL YES\n")
        if case.get("co"):
            deck = deck.replace("RG INTENSITY 1:00 1 TIMESERIES RAIN", "RG INTENSITY 1:00 1 TIMESERIES RAIN\nRG2 INTENSITY 1:00 1 TIMESERIES RAIN 2")
            deck = deck.replace("S1 RG OUT1", "S1 RG2 OUT1")
            # Ensure the primary is used too, matching legacy co-gage validation.
            deck = deck.replace("[SUBAREAS]", "S2 RG OUT1 1 100 100 1 0\n[SUBAREAS]")
            deck = deck.replace("[INFILTRATION]", "S2 0.015 0.1 0 0 100 OUTLET\n[INFILTRATION]")
            deck = deck.replace("[STORAGE]", "S2 3 0.5 4 7 0\n[STORAGE]")
            deck = deck.replace("IF GAGE RG ", "IF GAGE RG2 ")
        if case.get("reset"):
            for hour, value in enumerate([1, 2, 3, .5, 1.5, 2.5, 3.5]):
                deck = deck.replace(f"RAIN 01/01/2026 {hour:02}:00 1\n", f"RAIN 01/01/2026 {hour:02}:00 {value}\n")
        if case.get("long"):
            deck = deck.replace("END_DATE 01/01/2026", "END_DATE 01/03/2026")
            deck = deck.replace("END_TIME 06:05:17", "END_TIME 04:05:17")
        if case.get("pulse"):
            for hour in range(1, 7):
                deck = deck.replace(f"RAIN 01/01/2026 {hour:02}:00 1\n", f"RAIN 01/01/2026 {hour:02}:00 0\n")
        return deck
    p.model = model
    def prescribe(kind, engine):
        if case.get("api"):
            rc = p.old.swmm_setValue(101, 0, case["api"]) if kind == "legacy" else p.new.swmm_gage_set_rainfall(engine, 0, case["api"])
            assert rc == 0, rc
        if case.get("forcing"):
            if kind == "legacy":
                rc = p.old.swmm_setValue(101, 0, 2)
            else:
                value = 2 if case["forcing"] == 1 else 1
                rc = p.new.swmm_forcing_gage_rainfall(engine, 0, value, case["forcing"], 1)
            assert rc == 0, rc
    samples = [0.5, 1, 1.5, 2, 3, 4.5, 6]
    if case.get("long"):
        samples += [23.5, 24, 24.5, 25, 47.5, 48, 48.5, 49, 50, 52]
    if case.get("interface"):
        case["saving"] = True
        saved = p.run("legacy", f"{hours}-HR_DEPTH", step, True)
        assert saved.get("start_rc") == 0
        del case["saving"]
    pair = [p.run(kind, f"{hours}-HR_DEPTH", step, True,
                  control_only=case.get("unused", False), numeric_action=case.get("numeric", False),
                  sample_hours=samples, before_step=prescribe)
            for kind in ["legacy", "refactored"]]
    result = {"case": case, "runs": pair, "matches": True}
    if any(r.get("start_rc") != 0 or not r["samples"] for r in pair):
        result["matches"] = False
    else:
        field = "orifice_setting" if case.get("numeric") else "rain_depth_inches"
        if len(pair[0]["samples"]) != len(pair[1]["samples"]):
            result["matches"] = False
        for a, b in zip(pair[0]["samples"], pair[1]["samples"]):
            if abs(a["hour"] - b["hour"]) > 1e-8 or abs(a[field] - b[field]) > 1e-9:
                result["matches"] = False
    results.append(result)
    print(case["name"], "PASS" if result["matches"] else "FAIL", flush=True)
    if not result["matches"]:
        failures.append(result)

payload = {"new_library": str(p.NEW_PATH), "legacy_library": str(p.OLD_PATH), "results": results}
(OUT / "parity.json").write_text(json.dumps(payload, indent=2) + "\n")
assert not failures, [r["case"]["name"] for r in failures]
print(f"All {len(results)} native/legacy scenario pairs match.")
