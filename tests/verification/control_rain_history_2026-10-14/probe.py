"""Reproduce rain-gage control grammar and history behavior in bundled engines.

No solver sources are changed. All generated models, reports and observations
stay beside this script. A control curve maps rainfall depth to an orifice
setting, allowing observations through each engine's public API.
"""
import ctypes as c
import hashlib
import json
import os
from pathlib import Path

ROOT = Path(os.environ.get("OPENSWMM_RAIN_PROBE_OUT", Path(__file__).resolve().parent))
ROOT.mkdir(parents=True, exist_ok=True)
GUI = Path(__file__).resolve().parents[3]
FRAMEWORKS = GUI / "build/SWMMVis.app/Contents/Frameworks"
NEW_PATH = Path(os.environ.get("OPENSWMM_RAIN_NEW_LIB", FRAMEWORKS / "libopenswmm.engine.6.dylib"))
OLD_PATH = Path(os.environ.get("OPENSWMM_RAIN_OLD_LIB", FRAMEWORKS / "libopenswmm.legacy.engine.6.dylib"))


def bind(lib, name, args, result=c.c_int):
    fn = getattr(lib, name)
    fn.argtypes, fn.restype = args, result
    return fn


old, new = c.CDLL(str(OLD_PATH)), c.CDLL(str(NEW_PATH))
ptr, string, dblptr = c.c_void_p, c.c_char_p, c.POINTER(c.c_double)
bind(old, "swmm_getVersion", [])
bind(old, "swmm_open", [string, string, string])
bind(old, "swmm_start", [c.c_int])
bind(old, "swmm_step", [dblptr])
bind(old, "swmm_end", [])
bind(old, "swmm_close", [])
bind(old, "swmm_getValue", [c.c_int, c.c_int], c.c_double)
bind(new, "swmm_engine_create", [], ptr)
bind(new, "swmm_engine_open", [ptr, string, string, string, string])
bind(new, "swmm_engine_initialize", [ptr])
bind(new, "swmm_engine_start", [ptr, c.c_int])
bind(new, "swmm_engine_step", [ptr, dblptr])
bind(new, "swmm_engine_end", [ptr])
bind(new, "swmm_engine_destroy", [ptr], None)
bind(new, "swmm_link_get_control_setting", [ptr, c.c_int, dblptr])
bind(new, "swmm_control_validate_rule", [ptr, string, string, c.c_int, c.POINTER(c.c_int)])


def model(attribute, runoff_step=60, control_only=False, numeric_action=False):
    hydrology = "" if control_only else """
[SUBCATCHMENTS]
S1 RG OUT1 1 100 100 1 0
[SUBAREAS]
S1 0.015 0.1 0 0 100 OUTLET
[INFILTRATION]
S1 3 0.5 4 7 0
"""
    step = f"00:{runoff_step // 60:02}:00"
    condition = "0.03" if numeric_action else "0"
    action = "1\nELSE ORIFICE O1 SETTING = 0" if numeric_action else "CURVE DEPTH_TO_SETTING"
    return f"""[TITLE]
Rain-gage control history probe
[OPTIONS]
FLOW_UNITS CFS
FLOW_ROUTING DYNWAVE
INFILTRATION HORTON
START_DATE 01/01/2026
START_TIME 00:00:00
END_DATE 01/01/2026
END_TIME 06:05:00
WET_STEP {step}
DRY_STEP {step}
ROUTING_STEP 60
VARIABLE_STEP 0
RULE_STEP 00:00:00
REPORT_STEP 00:05:00
[RAINGAGES]
RG INTENSITY 1:00 1 TIMESERIES RAIN
{hydrology}
[STORAGE]
ST1 10 10 0 FUNCTIONAL 0 0 1000
[OUTFALLS]
OUT1 0 FREE NO
[ORIFICES]
O1 ST1 OUT1 SIDE 0 0.65 NO 0
[XSECTIONS]
O1 CIRCULAR 1 0 0 0
[CURVES]
DEPTH_TO_SETTING CONTROL 0 0
DEPTH_TO_SETTING 100 1
[TIMESERIES]
RAIN 01/01/2026 00:00 1
RAIN 01/01/2026 01:00 1
RAIN 01/01/2026 02:00 1
RAIN 01/01/2026 03:00 1
RAIN 01/01/2026 04:00 1
RAIN 01/01/2026 05:00 1
RAIN 01/01/2026 06:00 1
[CONTROLS]
RULE ObserveDepth
IF GAGE RG {attribute} >= {condition}
THEN ORIFICE O1 SETTING = {action}
[REPORT]
INPUT NO
CONTROLS NO
NODES NONE
LINKS NONE
"""


def run(kind, attribute, runoff_step=60, runtime=False, control_only=False, numeric_action=False, sample_hours=None, before_step=None):
    label = f"{kind}_{attribute}_{runoff_step}" + ("_control_only" if control_only else "") + ("_numeric_action" if numeric_action else "")
    inp, rpt, out = (ROOT / (label + ext) for ext in (".inp", ".rpt", ".out"))
    inp.write_text(model(attribute, runoff_step, control_only, numeric_action))
    result = {"engine": kind, "attribute": attribute, "runoff_seconds": runoff_step,
              "control_only": control_only, "numeric_action": numeric_action, "samples": []}
    engine = None
    started = False
    try:
        if kind == "legacy":
            rc = old.swmm_open(str(inp).encode(), str(rpt).encode(), str(out).encode())
        else:
            engine = new.swmm_engine_create()
            rc = new.swmm_engine_open(engine, str(inp).encode(), str(rpt).encode(), str(out).encode(), None)
        result["open_rc"] = rc
        if rc:
            return result
        if kind == "refactored":
            error, line = c.create_string_buffer(512), c.c_int(-1)
            rule = f"RULE Check\nIF GAGE RG {attribute} >= 0\nTHEN ORIFICE O1 SETTING = 1"
            result["validate_rc"] = new.swmm_control_validate_rule(engine, rule.encode(), error, len(error), c.byref(line))
            result["validate_message"] = error.value.decode(errors="replace")
            rc = new.swmm_engine_initialize(engine)
            result["initialize_rc"] = rc
            if rc:
                return result
        rc = old.swmm_start(0) if kind == "legacy" else new.swmm_engine_start(engine, 0)
        result["start_rc"] = rc
        if rc:
            return result
        started = True
        if runtime:
            targets = iter(sample_hours or [0.5, 1, 1.5, 2, 3, 4, 4.5, 5, 6])
            target = next(targets, None)
            for _ in range(10000):
                if before_step:
                    before_step(kind, engine)
                elapsed = c.c_double()
                rc = old.swmm_step(c.byref(elapsed)) if kind == "legacy" else new.swmm_engine_step(engine, c.byref(elapsed))
                if rc or elapsed.value <= 0:
                    result["step_rc"] = rc
                    break
                hours = elapsed.value * 24
                if target is not None and hours + 1e-8 >= target:
                    setting = c.c_double()
                    if kind == "legacy":
                        setting.value = old.swmm_getValue(407, 0)
                    else:
                        assert new.swmm_link_get_control_setting(engine, 0, c.byref(setting)) == 0
                    field = "orifice_setting" if numeric_action else "rain_depth_inches"
                    result["samples"].append({"hour": hours, field: setting.value if numeric_action else setting.value * 100})
                    target = next(targets, None)
            else:
                raise RuntimeError("Step limit exceeded")
        return result
    finally:
        if kind == "legacy":
            if started:
                old.swmm_end()
            old.swmm_close()
        elif engine:
            if started:
                new.swmm_engine_end(engine)
            new.swmm_engine_destroy(engine)


def main():
    results = [run(kind, attr) for attr in
               ["INTENSITY", "1-HR_DEPTH", "24-HR_DEPTH", "48-HR_DEPTH", "72-HR_DEPTH", "24", "48", "72", "3-DAY_DEPTH"]
               for kind in ["legacy", "refactored"]]
    results += [run(kind, "24", step, True) for step in [60, 300] for kind in ["legacy", "refactored"]]
    results += [run("refactored", attr, 60, True) for attr in ["1", "72"]]
    results += [run("legacy", "3-DAY_DEPTH", 60, True)]
    results += [run(kind, "24", 60, True, True) for kind in ["legacy", "refactored"]]
    results += [run(kind, "24", 60, True, numeric_action=True) for kind in ["legacy", "refactored"]]

    def observed(kind, attribute, step=60, control_only=False):
        return next(r for r in reversed(results) if r["engine"] == kind
                    and r["attribute"] == attribute and r["runoff_seconds"] == step
                    and r["control_only"] == control_only and not r["numeric_action"] and r["samples"])


    def at_hour(result, hour):
        return next(s["rain_depth_inches"] for s in result["samples"] if abs(s["hour"] - hour) < 1e-8)


    grammar = {(r["engine"], r["attribute"]): r for r in results[:18]}
    assert old.swmm_getVersion() == 53000
    assert grammar["legacy", "24-HR_DEPTH"]["start_rc"] == 0
    assert grammar["legacy", "48-HR_DEPTH"]["start_rc"] == 0
    assert grammar["legacy", "72-HR_DEPTH"]["open_rc"] != 0
    assert grammar["refactored", "24-HR_DEPTH"]["validate_rc"] != 0
    assert grammar["refactored", "72"]["start_rc"] == 0
    assert abs(at_hour(observed("legacy", "24"), 3) - 2) < 1e-10
    assert at_hour(observed("refactored", "24"), 3) < 0.04
    assert at_hour(observed("refactored", "24", 300), 3) < 0.01
    assert observed("refactored", "72")["samples"] == observed("refactored", "1")["samples"]
    assert abs(at_hour(observed("legacy", "3-DAY_DEPTH"), 4.5) - 3) < 1e-10
    assert abs(at_hour(observed("legacy", "24"), 4.5) - 4) < 1e-10
    assert all(s["rain_depth_inches"] == 0 for kind in ["legacy", "refactored"]
               for s in observed(kind, "24", control_only=True)["samples"])
    assert all(s["orifice_setting"] == 0 for s in results[-1]["samples"])
    assert any(s["orifice_setting"] == 1 for s in results[-2]["samples"])

    payload = {
        "legacy_version_int": old.swmm_getVersion(),
        "libraries": [{"path": str(p), "sha256": hashlib.sha256(p.read_bytes()).hexdigest()}
                      for p in [OLD_PATH, NEW_PATH]],
        "results": results,
        "reproduction_assertions": "passed",
    }
    (ROOT / "observations.json").write_text(json.dumps(payload, indent=2) + "\n")
    for r in results:
        print(r["engine"], r["attribute"], r["runoff_seconds"], "control_only=" + str(r["control_only"]),
              "numeric_action=" + str(r["numeric_action"]),
              "open=" + str(r["open_rc"]), "start=" + str(r.get("start_rc", "-")),
              "validate=" + str(r.get("validate_rc", "-")), r["samples"])
    print("Legacy version:", payload["legacy_version_int"])
    print("Observations:", ROOT / "observations.json")


if __name__ == "__main__":
    main()
