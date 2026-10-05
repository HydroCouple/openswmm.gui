# Richards 1D LID examples

Open `drainage.inp` or `backwater.inp` in the rebuilt SWMMVis app.
Both use four numerical cells per porous layer (eight porous cells total),
plus the surface reservoir. A native-soil BOTTOM drains as a system loss.
`backwater.inp` supplies an elevated outfall head through a conduit.

In Model → LID Control, inspect **Richards 1D** and the **Retention and
conductivity** tab for each porous layer. Numerical settings are expandable.
The live node section displays cell moisture and pressure. Select the existing
formulation to compare while preserving authored retention material data.

The van Genuchten parameters and specific storage are illustrative, not
calibrated defaults. Alpha and specific storage are in 1/m in both unit
systems. These examples do not enroll an active 2D aquifer bed.

Validation and remaining boundaries are documented in
`../../workplans/SURFACE_SUBSURFACE_IMPLEMENTATION_2026-10-05.md`.
