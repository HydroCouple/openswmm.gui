"""Compare the Boston rain file, loaded gages, and sampled original mesh cells.

The reduced deck preserves every gage and its position, but replaces the 1D
network and samples the large mesh. The original project is read only.
"""
import ctypes as C
import csv
import hashlib
import json
import shutil
import sys
from collections import Counter
from datetime import datetime, timedelta
from pathlib import Path

ROOT = Path(__file__).resolve().parent
MODEL = Path('/Volumes/BaseI/Boston/BWSC_Combined_Model_July 2026_New GAAR_T Storm 2070 - v1/BWSC_Combined_Model_July 2026_New GAAR_T Storm 2070.inp')
LIB = Path(sys.argv[1]).resolve()
sections = {'[RAINGAGES]': [], '[SYMBOLS]': []}
section = ''
for line in MODEL.open():
    s = line.strip()
    if s.startswith('['):
        section = s.upper()
    elif s and not s.startswith(';') and section in sections:
        sections[section].append(s)
rain_path = MODEL.parent / 'Precipication/1kmPixels_2023-12-18_PCSWMM_15min.tsf'
shutil.copyfile(rain_path, ROOT / 'rain.tsf')
with rain_path.open() as f:
    rows = list(csv.reader(f, delimiter='\t'))
ids = rows[0][1:]
dates = [datetime.strptime(row[0], '%m/%d/%Y %I:%M:%S %p') for row in rows[3:]]
raw = [[float(x) for x in row[1:]] for row in rows[3:]]
epoch = datetime(1899, 12, 30)
oa_dates = [(d - epoch).total_seconds() / 86400 for d in dates]
col = {name: i for i, name in enumerate(ids)}

vertices = []
samples = []
counts = Counter()
section = ''
for line in MODEL.with_suffix('.2dm').open():
    s = line.strip()
    if s.startswith('['):
        section = s.upper()
        continue
    if not s or s.startswith(';'):
        continue
    counts[section] += 1
    fields = s.split()
    if section == '[2D_VERTICES]':
        vertices.append(tuple(map(float, fields[:3])))
    elif section == '[2D_TRIANGLES]' and (counts[section] - 1) % 2500 == 0:
        points = [vertices[int(i)] for i in fields[:3]]
        samples.append((counts[section] - 1, points))
    elif section == '[2D_QUADS]' and (counts[section] - 1) % 100 == 0:
        points = [vertices[int(i)] for i in fields[:4]]
        samples.append((counts['[2D_TRIANGLES]'] + counts[section] - 1, points))

inp = '''[OPTIONS]
FLOW_UNITS CFS
FLOW_ROUTING DYNWAVE
START_DATE 12/17/2023
START_TIME 17:30:00
END_DATE 12/18/2023
END_TIME 20:54:00
REPORT_STEP 00:00:10
WET_STEP 00:00:01
DRY_STEP 00:00:01
ROUTING_STEP 1
[JUNCTIONS]
J 0 1 0 0 0
[OUTFALLS]
O -0.5 FREE NO
[CONDUITS]
C J O 30 0.013 0 0 0
[XSECTIONS]
C CIRCULAR 0.3 0 0 0 1
[2D_OPTIONS]
INTEGRATOR EXPLICIT
LTS_TIERS 1
MAX_TIMESTEP 1
REPORT_2D NO
RAINFALL_MODE NATURAL_NEIGHBOUR
[RAINGAGES]
'''
inp += '\n'.join(s.replace('Precipication/1kmPixels_2023-12-18_PCSWMM_15min.tsf', 'rain.tsf') for s in sections['[RAINGAGES]'])
inp += '\n[SYMBOLS]\n' + '\n'.join(sections['[SYMBOLS]'])
inp += '\n[2D_VERTICES]\n'
for _, points in samples:
    inp += ''.join(f'{x:.17g} {y:.17g} 0\n' for x, y, _ in points)
vertex_offset = 0
cell_section = ''
for _, points in samples:
    wanted = '[2D_TRIANGLES]' if len(points) == 3 else '[2D_QUADS]'
    if wanted != cell_section:
        inp += wanted + '\n'
        cell_section = wanted
    inp += ' '.join(str(vertex_offset+j) for j in range(len(points))) + ' 0.03 0 sample\n'
    vertex_offset += len(points)
deck = ROOT / 'sampled.inp'
deck.write_text(inp)

lib = C.CDLL(str(LIB))
P, I, D = C.c_void_p, C.c_int, C.c_double
def bind(name, args, result=I):
    fn = getattr(lib, name)
    fn.argtypes, fn.restype = args, result
    return fn
create = bind('swmm_engine_create', [], P)
open_ = bind('swmm_engine_open', [P, C.c_char_p, C.c_char_p, C.c_char_p, C.c_char_p])
init = bind('swmm_engine_initialize', [P])
start = bind('swmm_engine_start', [P, I])
step = bind('swmm_engine_step', [P, C.POINTER(D)])
count = bind('swmm_gage_get_rainfall_series_count', [P, I, C.POINTER(I)])
series = bind('swmm_gage_get_rainfall_series', [P, I, C.POINTER(D), C.POINTER(D), I])
weights = bind('swmm_2d_get_rainfall_weights', [P, I, C.POINTER(I), C.POINTER(I), C.POINTER(D), I, C.POINTER(I)])
rain = bind('swmm_2d_get_rainfall_bulk', [P, C.POINTER(D)])
cum = bind('swmm_2d_get_rain_volume_bulk', [P, C.POINTER(D)])
area = bind('swmm_2d_triangle_get_area', [P, I, C.POINTER(D)])
warn_count = bind('swmm_get_warning_count', [P])
warn = bind('swmm_get_warning_at', [P, I], C.c_char_p)
error_count = bind('swmm_get_error_count', [P])
error = bind('swmm_get_error_at', [P, I], C.c_char_p)
e = create()
def check(status):
    if status:
        raise RuntimeError((status, [error(e, i).decode() for i in range(error_count(e))]))
try:
    check(open_(e, str(deck).encode(), str(ROOT/'sampled.rpt').encode(), None, None))
    max_value_error = max_date_error = 0
    worst_value = None
    for j, s in enumerate(sections['[RAINGAGES]']):
        n = I()
        check(count(e, j, C.byref(n)))
        assert n.value == len(raw), (j, n.value)
        times, values = (D*n.value)(), (D*n.value)()
        check(series(e, j, times, values, n.value))
        column = col[s.split()[0].removeprefix('RG_')]
        max_date_error = max(max_date_error, max(abs(t-expected) for t, expected in zip(times, oa_dates)))
        for k,v in enumerate(values):
            difference = abs(v-raw[k][column]*4)
            if difference > max_value_error:
                max_value_error = difference
                worst_value = {'gage':s.split()[0], 'time':str(dates[k]), 'raw_inches':raw[k][column], 'expected_inhr':raw[k][column]*4, 'loaded_inhr':v}
    print('Loaded all gages; max intensity/date error:', max_value_error, max_date_error, flush=True)
    check(init(e))
    gages, ws = (I*len(ids))(), (D*len(ids))()
    assignments = []
    method_counts = Counter()
    max_sum_error = 0
    for i, (original, points) in enumerate(samples):
        method, n = I(), I()
        check(weights(e, i, C.byref(method), gages, ws, len(ids), C.byref(n)))
        assert n.value > 0
        assignment = list(zip(list(gages)[:n.value], list(ws)[:n.value]))
        max_sum_error = max(max_sum_error, abs(sum(w for _,w in assignment)-1))
        method_counts[method.value] += 1
        assignments.append(assignment)
    names = [s.split()[0].removeprefix('RG_') for s in sections['[RAINGAGES]']]
    totals = [sum(row[col[name]] for row in raw) for name in names]
    sample_totals_mm = [sum(totals[g]*w for g,w in a)*25.4 for a in assignments]
    wet_period_zeros = []
    for k,row in enumerate(raw):
        if any(v > 0 for v in row):
            zeros = sum(all(row[col[names[g]]] == 0 or w == 0 for g,w in a) for a in assignments)
            if zeros:
                wet_period_zeros.append({'time':str(dates[k]), 'valid_dry_sample_cells':zeros})
    wet_index = dates.index(datetime(2023,12,17,17,30))
    expected = [sum(raw[wet_index][col[names[g]]]*4*w for g,w in a)*0.0254/3600 for a in assignments]
    check(start(e,0))
    elapsed = D()
    check(step(e,C.byref(elapsed)))
    actual = (D*len(samples))()
    check(rain(e,actual))
    runtime_error = max(abs(x-y) for x,y in zip(actual,expected))
    reported_error = None
    if hasattr(lib, 'swmm_2d_get_report_rainfall_bulk'):
        reported = (D*len(samples))()
        report_rain = bind('swmm_2d_get_report_rainfall_bulk', [P, D, C.POINTER(D)])
        check(report_rain(e, oa_dates[wet_index] + elapsed.value, reported))
        reported_error = max(abs(x-y) for x,y in zip(reported,expected))
    while 0 < elapsed.value * 86400 < 60 - 1e-7:
        check(step(e,C.byref(elapsed)))
    volumes = (D*len(samples))()
    check(cum(e,volumes))
    max_depth_error = 0
    for i in range(len(samples)):
        a = D()
        check(area(e,i,C.byref(a)))
        max_depth_error = max(max_depth_error,abs(volumes[i]/a.value-expected[i]*60))
    output = {'library':str(LIB), 'sha256':hashlib.sha256(LIB.read_bytes()).hexdigest(),
        'model':str(MODEL), 'mesh_counts':dict(counts), 'sample_count':len(samples), 'gages':len(ids),
        'records_per_gage':len(raw), 'max_loaded_inhr_error':max_value_error,'worst_loaded_value':worst_value,'max_loaded_date_error_days':max_date_error,
        'method_counts':dict(method_counts), 'max_weight_sum_error':max_sum_error,
        'sampled_total_rain_mm_range':[min(sample_totals_mm),max(sample_totals_mm)],
        'sampled_zero_total_cells':sum(x==0 for x in sample_totals_mm),
        'wet_periods_with_valid_local_zeros':wet_period_zeros,
        'runtime_first_step_max_rain_ms_error':runtime_error,'reported_max_rain_ms_error':reported_error,
        'runtime_60s_max_depth_m_error':max_depth_error,
        'warnings':[warn(e,i).decode() for i in range(warn_count(e))]}
    (ROOT/(LIB.parent.name+'_audit.json')).write_text(json.dumps(output,indent=2))
    with (ROOT/'sampled_cells.csv').open('w') as f:
        w = csv.writer(f)
        w.writerow(['original_cell_id','total_rain_mm','rain_1730_mmhr','contributor_count'])
        for (original,_),t,r,a in zip(samples,sample_totals_mm,expected,assignments):
            w.writerow([original,t,r*3600000,len(a)])
    print(json.dumps(output,indent=2))
finally:
    bind('swmm_engine_end',[P])(e)
    bind('swmm_engine_close',[P])(e)
    bind('swmm_engine_destroy',[P],None)(e)
