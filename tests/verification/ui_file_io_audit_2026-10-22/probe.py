"""Read/write audit probes. Writes only beneath this directory; never simulates."""
import ctypes as C
import json
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent
GUI = ROOT.parents[2]
ENGINE = GUI.parent / "openswmm.engine"
LIBRARY = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else ENGINE / "build/hotstart-format/src/engine/libopenswmm.engine.6.dylib"
lib = C.CDLL(str(LIBRARY))
P, I, D, S = C.c_void_p, C.c_int, C.c_double, C.c_char_p

def bind(name, args, result=I):
    f = getattr(lib, name)
    f.argtypes, f.restype = args, result
    return f

create = bind("swmm_engine_create", [], P)
destroy = bind("swmm_engine_destroy", [P], None)
open_model = bind("swmm_engine_open", [P, S, S, S, S])
close = bind("swmm_engine_close", [P])
write = bind("swmm_model_write_with_plugin", [P, S, S])
get = bind("swmm_options_get", [P, S, S, I])
setopt = bind("swmm_options_set", [P, S, S])
getext = bind("swmm_options_get_ext", [P, S, S, I])
setext = bind("swmm_options_set_ext", [P, S, S])
last_error = bind("swmm_get_last_error_msg", [P], S)
warning_count = bind("swmm_get_warning_count", [P])
warning_at = bind("swmm_get_warning_at", [P, I], S)

def enc(v):
    return str(v).encode()

def readopt(e, key, extended=False, size=65536):
    b = C.create_string_buffer(size)
    rc = (getext if extended else get)(e, enc(key), b, size)
    return b.value.decode() if rc == 0 else {"error": rc}

def check(rc, e=None):
    assert rc == 0, (rc, last_error(e) if e else None)

def load(path, plugin=None):
    e = create()
    rc = open_model(e, enc(path), enc(path.with_suffix(".audit.rpt")), b"", enc(plugin) if plugin else None)
    if rc:
        error = last_error(e)
        destroy(e)
        raise RuntimeError((path, rc, error))
    return e

def release(e):
    close(e)
    destroy(e)

def snapshot(e, keys, extkeys):
    return {"options": {k: readopt(e, k) for k in keys}, "2d_options": {k: readopt(e, k, True) for k in extkeys}}

OPTIONS = {
    "VARIABLE_STEP": "0.755", "ROUTING_STEP": "0.0005", "RULE_STEP": "37",
    "FLOW_ROUTING": "FV", "FV_CELL_LENGTH": "17.25", "FV_MIN_CELLS": "7", "FV_CFL": "0.63",
    "FV_RIEMANN": "HLLC", "FV_ORDER": "2", "FV_LIMITER": "SUPERBEE", "FV_SCALAR_SCHEME": "MUSCL",
    "FV_TIME_INTEGRATION": "RK2", "FV_SLOT_CELERITY": "122.5", "FV_BACKEND": "CPU",
    "FV_MIN_PARALLEL_CELLS": "1234", "FV_LTS": "NO", "FV_LTS_MAX_TIERS": "3", "FV_CFL_CENSUS_INTERVAL": "4",
    "FV_STRUCTURE_COUPLING": "ROUTING_STEP", "FV_PRESSURIZED_IMPLICIT": "YES", "FV_PRESSURE_CLOSURE": "TPA",
    "QUALITY_SOLVER": "LAGRANGIAN", "QUALITY_STEP": "0.25", "MAX_SEGMENTS_PER_LINK": "73",
    "DISPERSION": "RWPT", "RWPT_SEED": "123", "WATER_AGE": "ON", "HEAT_TRANSPORT": "ON",
    "OUTFALL_BACKFLOW_QUALITY": "ZERO", "SKIP_STEADY_STATE": "YES",
    "WATER_DENSITY": "997", "WATER_SPECIFIC_HEAT_CAPACITY": "4200", "WIND_FUNC_COEFF_A": "11",
    "WIND_FUNC_COEFF_B": "13", "PRESSURE_RATIO": "0.9",
    "SURCHARGE_METHOD": "EXTRAN", "DPS_CELERITY": "42", "DPS_ALPHA": "0.2", "DPS_DECAY_TIME": "17", "TPA_CELERITY": "777",
}
EXT = {"RECONSTRUCTION_ORDER": "2", "THETA_REFERENCE_STEP": "7.5", "INFIL_STEP": "120", "THETA": "0.8"}

def main():
    fixture = ROOT / "source.inp"
    fixture.write_text((GUI / "tests/gui/data/mini_2d.inp").read_text() + "\n[POLLUTANTS]\nP1 MG/L 1 2 3 0.04 NO * 0 5 6\n")
    e = load(fixture)
    refused = {}
    for k, v in OPTIONS.items():
        rc = setopt(e, enc(k), enc(v))
        if rc: refused[k] = rc
    for k, v in EXT.items():
        rc = setext(e, enc(k), enc(v))
        if rc: refused[k] = rc
    mwtset = bind("swmm_pollutant_set_mwt", [P, I, D])
    mwtget = bind("swmm_pollutant_get_mwt", [P, I, C.POINTER(D)])
    check(mwtset(e, 0, 58.44), e)
    # Tiny but positive values accepted by the pollutant property adapter.
    rainset = bind("swmm_pollutant_set_rain_conc", [P, I, D])
    rainget = bind("swmm_pollutant_get_rain_conc", [P, I, C.POINTER(D)])
    check(rainset(e, 0, 0.000012345), e)
    add = bind("swmm_transect_add", [P, S])
    check(add(e, b"T1"), e)
    rough = bind("swmm_transect_set_roughness", [P, I, D, D, D])
    check(rough(e, 0, .03, .03, .03), e)
    station = bind("swmm_transect_add_station", [P, I, D, D])
    for x, z in [(0., 3.), (2., 0.), (8., 0.), (10., 3.)]: check(station(e, 0, x, z), e)
    banks = bind("swmm_transect_set_bank_stations", [P, I, D, D])
    check(banks(e, 0, 2., 8.), e)
    encset = bind("swmm_transect_set_encroachment_stations", [P, I, D, D])
    encget = bind("swmm_transect_get_encroachment_stations", [P, I, C.POINTER(D), C.POINTER(D)])
    check(encset(e, 0, 1.25, 8.75), e)
    comments = bind("swmm_transect_set_comments", [P, I, S])
    getcomments = bind("swmm_transect_get_comments", [P, I, S, I])
    check(comments(e, 0, b"Audit transect comment"), e)
    def extra(engine, gpkg=False):
        mw, rc, xl, xr = D(), D(), D(), D()
        if not gpkg:
            check(mwtget(engine, 0, C.byref(mw)), engine)
        check(rainget(engine, 0, C.byref(rc)), engine)
        check(encget(engine, 0, C.byref(xl), C.byref(xr)), engine)
        b = C.create_string_buffer(1024)
        check(getcomments(engine, 0, b, len(b)), engine)
        return {"pollutant_mwt": "getter crashes: GeoPackage reader does not populate mwt" if gpkg else mw.value, "pollutant_rain_conc": rc.value, "transect_encroachment": [xl.value,xr.value], "transect_comment": b.value.decode()}
    before = snapshot(e, OPTIONS, EXT)
    before.update(extra(e))
    results = {"library": str(LIBRARY), "refused_values": refused, "before": before, "formats": {}}
    micro = ROOT / "microstep.inp"
    check(write(e, enc(micro), None), e)
    try:
        micro_engine = load(micro)
        results["microstep"] = readopt(micro_engine, "ROUTING_STEP")
        release(micro_engine)
    except Exception as err:
        results["microstep"] = {"before": readopt(e, "ROUTING_STEP"), "reopen_error": str(err)}
    check(setopt(e, b"ROUTING_STEP", b"0.125"), e)
    before["options"]["ROUTING_STEP"] = readopt(e, "ROUTING_STEP")
    for ext, plugin in [("inp", None), ("gpkg", "org.hydrocouple.openswmm.plugins.geopackage"), ("h5", "org.hydrocouple.openswmm.plugins.hdf5")]:
        dest = ROOT / ("saved." + ext)
        if dest.exists(): dest.unlink()
        n = warning_count(e)
        rc = write(e, enc(dest), enc(plugin) if plugin else None)
        r = {"write_rc": rc, "warnings": [warning_at(e, j).decode() for j in range(n, warning_count(e))]}
        if rc == 0:
            try:
                e2 = load(dest, plugin)
                r["after"] = snapshot(e2, OPTIONS, EXT)
                r["after"].update(extra(e2, ext == "gpkg"))
                release(e2)
                r["differences"] = {}
                for group in ("options", "2d_options"):
                    r["differences"][group] = {k: {"before": v, "after": r["after"][group][k]} for k,v in before[group].items() if v != r["after"][group][k]}
                for k in extra(e):
                    if before[k] != r["after"][k]: r["differences"][k] = {"before": before[k], "after": r["after"][k]}
            except Exception as err:
                r["reopen_error"] = str(err)
        results["formats"][ext] = r
    # Report selectors use exactly 256 bytes in SimOptionsContext.
    names = " ".join(f"node_{i:04d}" for i in range(50))
    check(setopt(e, b"RPT_NODES", enc(names)), e)
    results["report_list"] = {"full": readopt(e, "RPT_NODES"), "ui_256": readopt(e, "RPT_NODES", size=256)}
    release(e)
    (ROOT / "probe_results.json").write_text(json.dumps(results, indent=2) + "\n")
    print(json.dumps({"refused": refused, "microstep": results["microstep"], "formats": {k: {a:b for a,b in v.items() if a != "after"} for k,v in results["formats"].items()}, "report_list_lengths": {k:len(v) for k,v in results["report_list"].items()}}, indent=2))

if __name__ == "__main__": main()
