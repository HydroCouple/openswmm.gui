"""Generate the self-contained T10 decks. Run from any directory."""
from pathlib import Path
import argparse
ROOT = Path(__file__).resolve().parents[4]
DEST = ROOT / 'docs/manual/tutorials/models/lid_active_chain'
p=argparse.ArgumentParser();p.add_argument('--richards',action='store_true');p.add_argument('--cells',type=int,default=8);p.add_argument('--atol',type=float,default=1e-7);p.add_argument('--rtol',type=float,default=1e-5);p.add_argument('--destination',type=Path);a=p.parse_args()
if a.richards:DEST=ROOT/'docs/manual/tutorials/models/lid_richards_chain'
if a.destination:DEST=a.destination
DEST.mkdir(parents=True, exist_ok=True)
BASE = '''[TITLE]
Chained storage-node LIDs: {case}
Synthetic research example; parameters are not calibrated design values.
[OPTIONS]
FLOW_UNITS CFS
FLOW_ROUTING DYNWAVE
START_DATE 01/01/2004
START_TIME 00:00:00
REPORT_START_DATE 01/01/2004
REPORT_START_TIME 00:00:00
END_DATE 01/02/2004
END_TIME 00:00:00
ROUTING_STEP 0.5
VARIABLE_STEP 0
REPORT_STEP 00:05:00
QUALITY_SOLVER LEGACY
OUTFALL_BACKFLOW_QUALITY ZERO
LINK_OFFSETS DEPTH
[STORAGE]
; constant footprint 1000 ft2; maximum depth = authored thickness sum
A .3 2.5 0 FUNCTIONAL 0 0 1000 0 0
B 0 2.5 0 FUNCTIONAL 0 0 1000 0 0
[OUTFALLS]
R 0 TIMESERIES Tailwater NO
FA 0 FREE NO
FB 0 FREE NO
[ORIFICES]
; A-to-B uses an authored orifice offset; no single-ended LID anchor
V_AB A B SIDE .05 .6 NO 0
V_BR B R SIDE 0 .6 NO 0
[XSECTIONS]
V_AB CIRCULAR .15 0 0 0
V_BR CIRCULAR .10 0 0 0
W_A RECT_OPEN .5 .5 0 0
W_B RECT_OPEN .5 .5 0 0
[WEIRS]
W_A A FA TRANSVERSE 2.0 3.3 NO 0 0 NO
W_B B FB TRANSVERSE 2.0 3.3 NO 0 0 NO
[POLLUTANTS]
; REACTIVE is a hypothetical first-order treatable constituent, not calibrated TSS/N/P
REACTIVE MG/L 0 0 0 0 NO * 0 0 0
TRACER MG/L 0 0 0 0 NO * 0 0 0
[INFLOWS]
A FLOW Pulse FLOW 1 1 0
A REACTIVE Load CONCEN 1 1 0
A TRACER Load CONCEN 1 1 0
[LID_CONTROLS]
Train NODE
Train SURFACE 6 .1
Train MEDIA 12 .45 .20 .08 5 3 3
Train AGGREGATE 12 .40 100
Train BOTTOM 0 0
[LID_NODES]
A Train 10
B Train 10
[LID_NODE_OUTLETS]
; anchors select a single LID endpoint; V_AB must use an explicit offset
V_BR 3 BOTTOM
W_A 1 BOTTOM
W_B 1 BOTTOM
[LID_LAYER_TREATMENT]
Train 2 REACTIVE 0 2 -
Train 3 REACTIVE 0 2 -
[CONTROLS]
{controls}
[TIMESERIES]
Pulse 00:00 0
Pulse 01:00 .30
Pulse 02:00 0
{pulse}
Load 00:00 20
Load 24:00 20
{tailwater}
[REPORT]
INPUT NO
CONTROLS YES
NODES ALL
LINKS ALL
[COORDINATES]
A 0 30
B 100 30
R 200 30
FA 0 -30
FB 100 -30
'''
TIMED = '''RULE HoldA
IF SIMULATION TIME < 06:00:00
THEN ORIFICE V_AB SETTING = 0
ELSE ORIFICE V_AB SETTING = 1
PRIORITY 1
RULE HoldB
IF SIMULATION TIME < 08:00:00
THEN ORIFICE V_BR SETTING = 0
ELSE ORIFICE V_BR SETTING = 1
PRIORITY 1
'''
GUARD = '''RULE IsolateA
IF NODE B HEAD > 1.25
THEN ORIFICE V_AB SETTING = 0
PRIORITY 3
RULE IsolateB
IF NODE R HEAD > 1.25
THEN ORIFICE V_BR SETTING = 0
PRIORITY 3
RULE KeepAIsolated
IF NODE B HEAD > 1.05
AND LINK V_AB SETTING < 0.5
THEN ORIFICE V_AB SETTING = 0
PRIORITY 3
RULE KeepBIsolated
IF NODE R HEAD > 1.05
AND LINK V_BR SETTING < 0.5
THEN ORIFICE V_BR SETTING = 0
PRIORITY 3
RULE ReliefA
IF NODE A DEPTH > 2.20
AND NODE B HEAD < 1.05
THEN ORIFICE V_AB SETTING = 1
PRIORITY 5
RULE ReliefB
IF NODE B DEPTH > 2.20
AND NODE R HEAD < 1.05
THEN ORIFICE V_BR SETTING = 1
PRIORITY 5
'''
HIGH = '''Tailwater 00:00 0
Tailwater 03:00 0
Tailwater 04:00 1.60
Tailwater 07:00 1.60
Tailwater 09:00 0
Tailwater 24:00 0'''
LOW = 'Tailwater 00:00 0\nTailwater 24:00 0'
REPEAT = 'Pulse 09:00 0\nPulse 10:00 .45\nPulse 11:00 0\nPulse 24:00 0'
CASES = {
 '01_passive_free': ('', LOW, 'Pulse 24:00 0'),
 '02_passive_backwater': ('', HIGH, 'Pulse 24:00 0'),
 '03_timed_hold': (TIMED, HIGH, 'Pulse 24:00 0'),
 '04_head_guard': (TIMED + GUARD, HIGH, 'Pulse 24:00 0'),
 '05_repeat_storm': (TIMED + GUARD, HIGH, REPEAT),
 '06_stuck_closed': ('''RULE FailedValves
IF SIMULATION TIME >= 00:00:00
THEN ORIFICE V_AB SETTING = 0
AND ORIFICE V_BR SETTING = 0
PRIORITY 10''', HIGH, REPEAT),
}
for name, (controls, tailwater, pulse) in CASES.items():
    deck=BASE.format(case=name, controls=controls, tailwater=tailwater, pulse=pulse)
    if a.richards:
        rows=f'[LID_RICHARDS]\n; Explicit illustrative properties; alpha and Ss are inverse metres in US and SI.\nTrain OPTIONS {a.cells} {a.atol:g} {a.rtol:g} 30\nTrain 2 .03 4 1.8 .5 .0001\nTrain 3 .01 20 2.5 .5 .0001\n'
        deck=deck.replace('[LID_NODES]',rows+'[LID_NODES]').replace('Chained storage-node LIDs:','Richards 1D chained storage-node LIDs:')
        deck=deck.replace('IF NODE B HEAD > 1.25','IF NODE B HEAD > 2.25').replace('IF NODE B HEAD > 1.05','IF NODE B HEAD > 2.05').replace('AND NODE B HEAD < 1.05','AND NODE B HEAD < 2.05')
    (DEST / f'{name}.inp').write_text(deck)
print(f'Generated {len(CASES)} decks in {DEST}')
