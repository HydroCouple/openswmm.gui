#!/usr/bin/env python3
# GPL-3.0-or-later
"""Write a synthetic OpenSWMM 2D results file (.2d.h5) of any size.

The layout copies what the engine writes (Default2DOutputPlugin): UGRID mesh
variables, one whole-mesh chunk per frame for face and edge hydraulics, shuffle
plus deflate level 4, and the static edge geometry. It is meant for timing the
GUI's 2D results display on large meshes, not for hydraulic checks: depth is a
rising water surface over an undulating sloped bed, velocity a smooth field,
and edge flux depth * (v . n) * length (not conservative between neighbours).

Example (5,999,648 triangles, 6 frames, ~1.5 GB):
    python -I make_synthetic_2d_results_h5.py --side 1732 --frames 6 out.2d.h5
"""
import argparse

import h5py
import numpy as np


def attrs(obj, **values):
    for key, value in values.items():
        obj.attrs[key] = np.bytes_(value) if isinstance(value, str) else value


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("output")
    parser.add_argument("--side", type=int, default=1732, help="squares per side; cells = 2*side^2")
    parser.add_argument("--frames", type=int, default=6)
    parser.add_argument("--spacing", type=float, default=2.0, help="square size in metres")
    args = parser.parse_args()

    n, h = args.side, args.spacing
    x0, y0 = 500000.0, 6100000.0
    i, j = np.meshgrid(np.arange(n + 1), np.arange(n + 1))
    nx_, ny_ = (i * h).ravel(), (j * h).ravel()
    node_x, node_y = x0 + nx_, y0 + ny_
    node_z = 10.0 - 0.001 * nx_ + 0.3 * np.sin(nx_ / 50.0) * np.cos(ny_ / 70.0)

    # Two counter-clockwise triangles per square: (a, b, c) and (a, c, d).
    sq_i, sq_j = np.meshgrid(np.arange(n), np.arange(n))
    a = (sq_j * (n + 1) + sq_i).ravel()
    b, c, d = a + 1, a + n + 2, a + n + 1
    tri = np.empty((2 * a.size, 3), dtype=np.int64)
    tri[0::2] = np.stack([a, b, c], axis=1)
    tri[1::2] = np.stack([a, c, d], axis=1)
    faces = tri.shape[0]
    face_nodes = np.full((faces, 4), -1, dtype=np.int32)
    face_nodes[:, :3] = tri

    px, py = nx_[tri], ny_[tri]  # local metres, [face, corner]
    face_xl, face_yl = px.mean(axis=1), py.mean(axis=1)
    face_z = node_z[tri].mean(axis=1)

    # Flux slot k is the edge between corners k+1 and k+2 (opposite corner k).
    ex = np.stack([px[:, (k + 2) % 3] - px[:, (k + 1) % 3] for k in range(3)], axis=1)
    ey = np.stack([py[:, (k + 2) % 3] - py[:, (k + 1) % 3] for k in range(3)], axis=1)
    length = np.hypot(ex, ey)
    unit_nx, unit_ny = ey / length, -ex / length  # outward for counter-clockwise faces
    pad = lambda v: np.concatenate([v, np.zeros((faces, 1))], axis=1)

    with h5py.File(args.output, "w") as f:
        attrs(f, Conventions="CF-1.11 UGRID-1.0", compression="4",
              institution="OpenSWMM / HydroCouple", precision="float32",
              report_2d_variables="DEFAULT", source="synthetic large-mesh fixture",
              title="OpenSWMM 2D Surface Routing Results", turbulence_model="NONE")
        f.attrs["coriolis_f_s-1"] = 0.0
        mesh = f.create_dataset("Mesh2", data=np.int32(0))
        attrs(mesh, cf_role="mesh_topology", face_coordinates="Mesh2_face_x Mesh2_face_y",
              face_node_connectivity="Mesh2_face_nodes",
              node_coordinates="Mesh2_node_x Mesh2_node_y", openswmm_crs="crs",
              topology_dimension="2")
        crs = f.create_dataset("crs", data=np.int32(0))
        attrs(crs, long_name="coordinate reference of the MODEL", model_crs="EPSG:25832",
              units="m")
        crs.attrs["metres_per_model_unit"] = 1.0

        for name, values, extra in (
            ("Mesh2_node_x", node_x, dict(openswmm_crs="crs", standard_name="projection_x_coordinate", units="m")),
            ("Mesh2_node_y", node_y, dict(openswmm_crs="crs", standard_name="projection_y_coordinate", units="m")),
            ("Mesh2_node_z", node_z, dict(positive="up", standard_name="altitude", units="m")),
            ("Mesh2_face_x", x0 + face_xl, dict(openswmm_crs="crs", units="m")),
            ("Mesh2_face_y", y0 + face_yl, dict(openswmm_crs="crs", units="m")),
            ("Mesh2_face_z", face_z, dict(units="m")),
            ("Mesh2_face_area", np.full(faces, h * h / 2), dict(location="face", long_name="face planimetric area", mesh="Mesh2", units="m2")),
            ("Mesh2_edge_length", pad(length), dict(location="edge", long_name="edge length", mesh="Mesh2", units="m")),
            ("Mesh2_edge_nx", pad(unit_nx), dict(location="edge", long_name="edge outward unit normal x component", mesh="Mesh2", units="1")),
            ("Mesh2_edge_ny", pad(unit_ny), dict(location="edge", long_name="edge outward unit normal y component", mesh="Mesh2", units="1")),
        ):
            attrs(f.create_dataset(name, data=values.astype(np.float64)), **extra)
        nodes = f.create_dataset("Mesh2_face_nodes", data=face_nodes)
        nodes.attrs["_FillValue"] = np.int32(-1)
        attrs(nodes, cf_role="face_node_connectivity", start_index="0")
        attrs(f.create_dataset("Mesh2_face_nv", data=np.full(faces, 3, dtype=np.int8)),
              location="face", long_name="vertices per face (3 = triangle, 4 = quadrilateral)",
              mesh="Mesh2")

        frames = args.frames
        time = f.create_dataset("time", data=45000.0 + np.arange(frames) * 600.0 / 86400.0,
                                maxshape=(None,), chunks=(64,))
        attrs(time, calendar="standard", standard_name="time",
              units="days since 1899-12-30 00:00:00")

        def series(name, shape_tail, **extra):
            ds = f.create_dataset(name, shape=(frames,) + shape_tail, dtype=np.float32,
                                  maxshape=(None,) + shape_tail, chunks=(1,) + shape_tail,
                                  shuffle=True, compression="gzip", compression_opts=4)
            attrs(ds, mesh="Mesh2", **extra)
            return ds

        depth_ds = series("Mesh2_face_depth", (faces,), location="face", long_name="overland flow depth", units="m")
        head_ds = series("Mesh2_face_head", (faces,), location="face", long_name="total hydraulic head", units="m")
        vx_ds = series("Mesh2_face_vx", (faces,), location="face", long_name="cell velocity X", units="m s-1")
        vy_ds = series("Mesh2_face_vy", (faces,), location="face", long_name="cell velocity Y", units="m s-1")
        flux_ds = series("Mesh2_edge_flux", (faces, 4), location="edge", long_name="normal flux through cell edges", units="m3 s-1")

        undulation = 0.3 * np.sin(face_xl / 50.0) * np.cos(face_yl / 70.0)
        for t in range(frames):
            depth = np.maximum(0.0, -0.25 + 0.08 * t - undulation)
            wet = depth > 0
            vx = np.where(wet, 0.5 + 0.2 * np.sin(face_yl / 200.0), 0.0)
            vy = np.where(wet, 0.1 * np.cos(face_xl / 150.0), 0.0)
            flux = depth[:, None] * (vx[:, None] * unit_nx + vy[:, None] * unit_ny) * length
            depth_ds[t] = depth
            head_ds[t] = face_z + depth
            vx_ds[t] = vx
            vy_ds[t] = vy
            flux_ds[t] = pad(flux)
            print(f"frame {t}: wet fraction {wet.mean():.3f}", flush=True)
    print(f"wrote {args.output}: {faces} faces, {node_x.size} nodes, {frames} frames")


if __name__ == "__main__":
    main()
