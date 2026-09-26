#!/usr/bin/env python3
"""Generate deterministic HDF5 plot fixtures (requires numpy and h5py).

Uses the example mesh's topology and edge geometry, tiled to the requested
size. Fields vary over space/time; these are synthetic performance data,
not hydraulic simulation results. Whole-frame gzip chunks match engine output.

python3 tests/tools/generate_plot_benchmark.py build/plot-10000.2d.h5
SWMMVIS_PLOT_BENCHMARK_FILE="$PWD/build/plot-10000.2d.h5" \
  QT_QPA_PLATFORM=offscreen build/tests/gui/test_plot_batch_counts savedResultsBenchmark
Set SWMMVIS_PLOT_BENCHMARK_SCALAR=1 to also time/verify separate series reads.
For large scalar baselines, also set QTEST_FUNCTION_TIMEOUT=900000 (15 minutes).
"""
import argparse
from pathlib import Path

import h5py
import numpy as np


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    parser.add_argument("--cells", type=int, default=10000)
    parser.add_argument("--periods", type=int, default=1000)
    args = parser.parse_args()
    template = Path(__file__).resolve().parents[2] / "examples/demo_road_culvert/road_culvert.2d.h5"
    if args.cells <= 0 or args.periods <= 0:
        parser.error("cells and periods must be positive")

    with h5py.File(template, "r") as source:
        faces = source["Mesh2_face_nodes"][:]
        n = len(faces)
        if args.cells % n:
            parser.error(f"cells must be a multiple of the template's {n} faces")
        copies = args.cells // n
        nodes = len(source["Mesh2_node_x"])
        # Exclusive creation protects existing results from accidental overwrite.
        with h5py.File(args.output, "x") as dest:
            dest.attrs["title"] = "Synthetic plotting benchmark; not simulation results"
            for name in ("Mesh2_node_x", "Mesh2_node_y", "Mesh2_node_z"):
                values = source[name][:]
                offset = float(np.ptp(values)) + 10 if name.endswith("_x") else 0
                dest[name] = np.concatenate([values + i * offset for i in range(copies)])
            dest["Mesh2_face_nodes"] = np.concatenate([faces + i * nodes for i in range(copies)])
            dest["Mesh2_face_nodes"].attrs["start_index"] = 0
            for name in ("Mesh2_edge_length", "Mesh2_edge_nx", "Mesh2_edge_ny"):
                dest[name] = np.tile(source[name][:], (copies, 1))
            times = dest.create_dataset("time", data=np.arange(args.periods, dtype=float) * 60)
            times.attrs["units"] = "seconds since 2026-01-01 00:00:00"
            fields = {}
            for name, units, shape in (
                ("Mesh2_face_depth", "m", (args.periods, args.cells)),
                ("Mesh2_face_rainfall", "m s-1", (args.periods, args.cells)),
                ("Mesh2_edge_flux", "m3 s-1", (args.periods, args.cells, 3)),
            ):
                fields[name] = dest.create_dataset(name, shape=shape, dtype="f8",
                    chunks=(1, *shape[1:]), compression="gzip", compression_opts=4)
                fields[name].attrs["units"] = units
            rng = np.random.default_rng(20260926)
            phase = rng.uniform(0, 2 * np.pi, args.cells)
            rain_scale = rng.uniform(0.5, 1.5, args.cells)
            lengths = dest["Mesh2_edge_length"][:]
            nx, ny = dest["Mesh2_edge_nx"][:], dest["Mesh2_edge_ny"][:]
            for t in range(args.periods):
                depth = 0.3 + 0.1 * np.sin(t / 50 + phase)
                vx = 0.1 * np.cos(t / 70 + phase)
                vy = 0.07 * np.sin(t / 60 + phase)
                fields["Mesh2_face_depth"][t] = depth
                fields["Mesh2_face_rainfall"][t] = max(0, np.sin(t / 100)) * rain_scale * 1e-6
                fields["Mesh2_edge_flux"][t] = depth[:, None] * lengths * (vx[:, None] * nx + vy[:, None] * ny)
    print(f"Created {args.output}: {args.cells} cells x {args.periods} periods, {args.output.stat().st_size} bytes")


if __name__ == "__main__":
    main()
