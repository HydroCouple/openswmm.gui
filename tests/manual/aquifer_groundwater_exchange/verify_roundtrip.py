"""Engine-level check of the manual round-trip (VERIFY_HANDOFF step 4/5).

Opens gw_roundtrip.inp with the openswmm Python bindings, exercises the new
[GWF] API (GwfType, get/set/validate, vocabulary), writes the model back out,
reopens it and checks the [AQUIFERS] pattern, [GROUNDWATER] and [GWF] rows
survive, then deletes the aquifer and confirms the rows disappear.

Run from this directory:  conda run -n openswmm python verify_roundtrip.py
Artifacts stay here (gw_roundtrip_saved.inp, gw_roundtrip_noaq.inp).
"""
import os
import sys

from openswmm.engine import Solver, GwfType

HERE = os.path.dirname(os.path.abspath(__file__))
INP = os.path.join(HERE, "gw_roundtrip.inp")
SAVED = os.path.join(HERE, "gw_roundtrip_saved.inp")
NOAQ = os.path.join(HERE, "gw_roundtrip_noaq.inp")
RPT = os.path.join(HERE, "gw_roundtrip_py.rpt")

failures = []


def check(cond, msg):
    print(("PASS " if cond else "FAIL ") + msg)
    if not cond:
        failures.append(msg)


def section(text, name):
    # Match a section header at line start (the [TITLE] comment mentions [GWF]).
    start = text.find("\n[" + name + "]")
    if start < 0:
        return ""
    end = text.find("\n[", start + 1)
    return text[start:end if end > 0 else None]


print("GwfType members:", list(GwfType))
check([m.name for m in GwfType] == ["LATERAL", "DEEP"], "GwfType enum")

s = Solver(INP, RPT)
s.open()
sub = s.subcatchments

check(sub.get_gwf_expression("S1", GwfType.LATERAL) == "0.001*(HGW-10)",
      "LATERAL expression loaded from [GWF]")
check(sub.get_gwf_expression("S1", GwfType.DEEP) == "", "DEEP empty")

ok, msg, col = sub.validate_gwf_expression("0.001*(HGW-10)")
check(ok and msg == "" and col == -1, "validator accepts manual expression")
ok, msg, col = sub.validate_gwf_expression("0.001*(HGWW-10)")
check(not ok and "HGWW" in msg and col == 7, f"validator rejects HGWW at col {col}: {msg}")
ok, msg, col = sub.validate_gwf_expression("MIN(HGW, HCB) * KS")
check(ok, "validator accepts two-argument MIN with a comma")

vars_ = sub.gwf_variables()
funcs = sub.gwf_functions()
check(len(vars_) == 11 and vars_[0][0] == "HGW" and all(d for _, d in vars_),
      "11 variables with descriptions")
check(len(funcs) == 21 and funcs[0] == "abs" and "min" in funcs, "21 functions")

sub.set_gwf_expression("S1", GwfType.DEEP, "MIN(HGW, HCB) * KS")
check(sub.get_gwf_expression("S1", GwfType.DEEP) == "MIN(HGW, HCB) * KS",
      "DEEP expression set via API")

s.write(SAVED)
text = open(SAVED).read()
aq = section(text, "AQUIFERS")
check("EVAP1" in aq, "[AQUIFERS] row ends with the pattern name")
check("S1" in section(text, "GROUNDWATER"), "[GROUNDWATER] row present")
gwf = section(text, "GWF")
check("LATERAL" in gwf and "0.001*(HGW-10)" in gwf, "[GWF] LATERAL row written")
check("DEEP" in gwf and "MIN(HGW, HCB) * KS" in gwf, "[GWF] DEEP row written with comma")
s.close()

# Reopen: everything reloads (the comma must survive the reader).
s2 = Solver(SAVED, RPT)
s2.open()
sub2 = s2.subcatchments
check(sub2.get_gwf_expression("S1", GwfType.LATERAL) == "0.001*(HGW-10)", "LATERAL reloads")
check(sub2.get_gwf_expression("S1", GwfType.DEEP) == "MIN(HGW, HCB) * KS", "DEEP reloads")
aq_idx = sub2.get_aquifer("S1") if hasattr(sub2, "get_aquifer") else None
print("aquifer index after reopen:", aq_idx)

# Step 5: delete the aquifer -> S1 loses it; saved file has no GW rows.
ed = s2.edit if hasattr(s2, "edit") else None
if ed is not None and hasattr(ed, "delete_aquifer"):
    ed.delete_aquifer("AQ1")
    s2.write(NOAQ)
    t2 = open(NOAQ).read()
    check("[AQUIFERS]" not in t2, "no [AQUIFERS] after delete")
    check("[GROUNDWATER]" not in t2, "no [GROUNDWATER] after delete")
    check("[GWF]" not in t2, "no [GWF] after delete")
else:
    print("SKIP aquifer delete via Python (no edit.delete_aquifer binding); "
          "covered by test_aquifer_editor_dialog deleteReportsImpactAndReachesEngine")
s2.close()

print("\n%d failure(s)" % len(failures))
sys.exit(1 if failures else 0)
