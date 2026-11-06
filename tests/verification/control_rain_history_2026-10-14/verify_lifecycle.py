"""Verify rainfall-rule text and zero history across model I/O and fresh runs."""
import ctypes as c
import importlib.util
import json
import os
from pathlib import Path

HERE = Path(__file__).resolve().parent
ENGINE = HERE.parents[3] / "openswmm.engine"
ROOT = HERE / "fixed/lifecycle"
ROOT.mkdir(parents=True, exist_ok=True)
os.environ["OPENSWMM_RAIN_NEW_LIB"] = str(ENGINE / "build/control-rain-history/bin/Release/libopenswmm.engine.6.0.0.dylib")
os.environ["OPENSWMM_RAIN_PROBE_OUT"] = str(ROOT)
spec = importlib.util.spec_from_file_location("probe", HERE / "probe.py")
p = importlib.util.module_from_spec(spec)
spec.loader.exec_module(p)
lib = p.new
p.bind(lib, "swmm_model_write", [p.ptr, p.string])
p.bind(lib, "swmm_model_write_with_plugin", [p.ptr, p.string, p.string])
p.bind(lib, "swmm_engine_close", [p.ptr])
p.bind(lib, "swmm_control_count", [p.ptr])
p.bind(lib, "swmm_control_get_rule", [p.ptr, c.c_int, p.string, c.c_int])
p.bind(lib, "swmm_control_find_references", [p.ptr, p.string, c.POINTER(c.c_int), c.POINTER(c.c_int)])
p.bind(lib, "swmm_control_remove_rule", [p.ptr, c.c_int])
p.bind(lib, "swmm_control_add_rule", [p.ptr, p.string])
source = ROOT / "source.inp"
source.write_text(p.model("48-HR_DEPTH"))
expected = "IF GAGE RG 48-HR_DEPTH >= 0"
records = []
engine = lib.swmm_engine_create()
try:
    assert lib.swmm_engine_open(engine, str(source).encode(), None, None, None) == 0
    for extension, plugin in [("inp", None), ("gpkg", b"org.hydrocouple.openswmm.plugins.geopackage")]:
        target = ROOT / ("roundtrip." + extension)
        for companion in [target, Path(str(target) + "-shm"), Path(str(target) + "-wal")]:
            if companion.exists():
                companion.unlink()
        assert lib.swmm_model_write_with_plugin(engine, str(target).encode(), plugin) == 0
        reopened = lib.swmm_engine_create()
        try:
            assert lib.swmm_engine_open(reopened, str(target).encode(), None, None, plugin) == 0
            assert lib.swmm_control_count(reopened) == 1
            text = c.create_string_buffer(4096)
            assert lib.swmm_control_get_rule(reopened, 0, text, len(text)) == 0
            assert expected in text.value.decode()
            assert lib.swmm_engine_initialize(reopened) == 0
            assert lib.swmm_engine_start(reopened, 0) == 0
            for _ in range(91):
                elapsed = c.c_double()
                assert lib.swmm_engine_step(reopened, c.byref(elapsed)) == 0
            setting = c.c_double()
            assert lib.swmm_link_get_control_setting(reopened, 0, c.byref(setting)) == 0
            assert abs(setting.value * 100 - 1) < 1e-9
            assert lib.swmm_engine_end(reopened) == 0
            records.append({"format": extension, "rule": text.value.decode(), "depth_after_91_steps": setting.value * 100})
        finally:
            lib.swmm_engine_destroy(reopened)
    for run in range(2):
        assert lib.swmm_engine_initialize(engine) == 0
        assert lib.swmm_engine_start(engine, 0) == 0
        elapsed = c.c_double()
        for _ in range(91):
            assert lib.swmm_engine_step(engine, c.byref(elapsed)) == 0
        setting = c.c_double()
        assert lib.swmm_link_get_control_setting(engine, 0, c.byref(setting)) == 0
        assert abs(setting.value * 100 - 1) < 1e-9, (run, setting.value * 100, elapsed.value)
        assert lib.swmm_engine_end(engine) == 0
        assert lib.swmm_engine_close(engine) == 0
        assert lib.swmm_engine_open(engine, str(source).encode(), None, None, None) == 0
    count, indices = c.c_int(4), (c.c_int * 4)()
    assert lib.swmm_control_find_references(engine, b"rg", indices, c.byref(count)) == 0
    assert count.value == 1 and indices[0] == 0
    assert lib.swmm_control_remove_rule(engine, 0) == 0
    assert lib.swmm_control_count(engine) == 0
    assert lib.swmm_control_add_rule(engine, b"RULE Replacement\nIF GAGE RG 24-HR_DEPTH > 0\nTHEN ORIFICE O1 SETTING = 0") == 0
    assert lib.swmm_engine_initialize(engine) == 0
    records.append({"fresh_runs": 2, "gage_references": 1, "rule_remove_and_replace": "passed"})
finally:
    lib.swmm_engine_destroy(engine)
(ROOT / "results.json").write_text(json.dumps(records, indent=2) + "\n")
print("INP and GeoPackage round trips, fresh runs, references and rule replacement passed.")
