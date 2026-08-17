#!/usr/bin/env python3
"""Generate the manual-test fixtures for the multi-column series feature.

Run:  python3 make_fixtures.py      (writes into this directory)

The four rain columns are given deliberately DIFFERENT shapes so that picking a
column in the GUI is visually unmistakable in the preview chart:

    RG_NORTH  early peak    (rises fast, done by 01:00)
    RG_SOUTH  late peak     (nothing until 01:00, peaks at 02:15)
    RG_EAST   uniform       (flat 0.20 in/hr through the whole storm)
    RG_WEST   double peak   (two humps, dry gap in the middle)

If a column selector is ignored and column 1 is silently used instead, the chart
shows the early-peak shape no matter what you pick -- that is the bug these
fixtures are built to expose.
"""

import datetime

START = datetime.datetime(2007, 1, 1, 0, 0)
STEP_MIN = 5
N = 37  # 0:00 .. 3:00 inclusive


def north(t):  # early peak, over by 01:00
    if t >= 60:
        return 0.0
    return round(0.90 * (t / 30.0 if t <= 30 else (60 - t) / 30.0), 3)


def south(t):  # late peak at 02:15
    if t < 60:
        return 0.0
    x = (t - 60) / 75.0 if t <= 135 else (180 - t) / 45.0
    return round(max(0.0, 1.20 * x), 3)


def east(t):  # uniform
    return 0.200


def west(t):  # double peak, dry 01:10..01:50
    if t <= 70:
        return round(0.60 * (t / 35.0 if t <= 35 else (70 - t) / 35.0), 3)
    if t < 110:
        return 0.0
    x = (t - 110) / 35.0 if t <= 145 else (180 - t) / 35.0
    return round(max(0.0, 0.75 * x), 3)


COLS = [("RG_NORTH", north), ("RG_SOUTH", south), ("RG_EAST", east), ("RG_WEST", west)]


def rows():
    for i in range(N):
        t = i * STEP_MIN
        yield START + datetime.timedelta(minutes=t), [f(t) for _, f in COLS]


def iso(dt):
    return dt.strftime("%Y-%m-%d %H:%M")


def us_ampm(dt):
    # 12-hour clock with no zero padding on the hour, e.g. 1/1/2007 1:30:00 PM
    return "%d/%d/%d %d:%02d:00 %s" % (
        dt.month, dt.day, dt.year,
        12 if dt.hour % 12 == 0 else dt.hour % 12,
        dt.minute,
        "AM" if dt.hour < 12 else "PM",
    )


def write(name, text):
    with open(name, "w", newline="\n") as fh:
        fh.write(text)
    print("wrote %-32s %d bytes" % (name, len(text)))


# 1. The main multi-column CSV.
out = ["; Manual-test fixture - multi-column rainfall, intensity in in/hr",
       "; Four gages with deliberately different shapes; see make_fixtures.py",
       "Date/Time," + ",".join(c for c, _ in COLS)]
for dt, vals in rows():
    out.append(iso(dt) + "," + ",".join("%.3f" % v for v in vals))
write("rain_multicolumn.csv", "\n".join(out) + "\n")

# 2. Same data, tab-separated.
out = ["Date/Time\t" + "\t".join(c for c, _ in COLS)]
for dt, vals in rows():
    out.append(iso(dt) + "\t" + "\t".join("%.3f" % v for v in vals))
write("rain_multicolumn.tsv", "\n".join(out) + "\n")

# 3. PCSWMM .tsf: 3-row header (IDs / parameter / units), AM-PM timestamps.
out = ["IDs:\t" + "\t".join(c for c, _ in COLS),
       "Date/Time\t" + "\t".join("Rainfall" for _ in COLS),
       "\t" + "\t".join("in./hr" for _ in COLS)]
for dt, vals in rows():
    out.append(us_ampm(dt) + "\t" + "\t".join("%.3f" % v for v in vals))
write("rain_multicolumn.tsf", "\n".join(out) + "\n")

# 4. Headerless: first line is DATA. The engine spends line 1 as the header row,
#    so the GUI must too -- expect N-1 points and a refusal to bind col_2.
out = []
for dt, vals in rows():
    out.append(iso(dt) + "," + ",".join("%.3f" % v for v in vals))
write("rain_headerless.csv", "\n".join(out) + "\n")

# 5. Quoted header containing a comma -- the delimiter sniff must not be fooled
#    (review finding B-8). Tab-delimited with a comma inside a quoted name.
out = ['Date/Time\t"Gage A, north"\t"Gage B, south"']
for dt, vals in rows():
    out.append(iso(dt) + "\t%.3f\t%.3f" % (vals[0], vals[1]))
write("rain_quoted_header.tsv", "\n".join(out) + "\n")

print("\nTotals: %d data rows per file, %s .. %s" %
      (N, iso(START), iso(START + datetime.timedelta(minutes=(N - 1) * STEP_MIN))))
