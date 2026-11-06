# Multispecies verification

Engine: `c5ca4cfd-dirty`

Equal-unit parent/product conversion in an isolated wet pool. A(t) = exp(-kt), B(t) = 1 - A(t).
Each case checks every native reporting period at the storage node and conduit, including the sum A+B.

| Case | Quality solver | Result | Maximum concentration error | Sum error |
| --- | --- | --- | --- | --- |
| parent_product_SEC | LEGACY | PASS | 4.145811415534606e-05 | 2.9802322387695312e-08 |
| parent_product_SEC_override | LEGACY | PASS | 8.25005962779013e-05 | 2.9802322387695312e-08 |
| parent_product_MIN | LEGACY | PASS | 4.145811415534606e-05 | 2.9802322387695312e-08 |
| parent_product_MIN_override | LEGACY | PASS | 8.25005962779013e-05 | 2.9802322387695312e-08 |
| parent_product_HR | LEGACY | PASS | 4.145811415534606e-05 | 2.9802322387695312e-08 |
| parent_product_HR_override | LEGACY | PASS | 8.25005962779013e-05 | 2.9802322387695312e-08 |
| parent_product_DAY | LEGACY | PASS | 4.145811415534606e-05 | 2.9802322387695312e-08 |
| parent_product_DAY_override | LEGACY | PASS | 8.25005962779013e-05 | 2.9802322387695312e-08 |
| parent_product_SEC | EULERIAN_ARD | PASS | 4.145811415534606e-05 | 2.9802322387695312e-08 |
| parent_product_SEC_override | EULERIAN_ARD | PASS | 8.25005962779013e-05 | 2.9802322387695312e-08 |
| parent_product_MIN | EULERIAN_ARD | PASS | 4.145811415534606e-05 | 2.9802322387695312e-08 |
| parent_product_MIN_override | EULERIAN_ARD | PASS | 8.25005962779013e-05 | 2.9802322387695312e-08 |
| parent_product_HR | EULERIAN_ARD | PASS | 4.145811415534606e-05 | 2.9802322387695312e-08 |
| parent_product_HR_override | EULERIAN_ARD | PASS | 8.25005962779013e-05 | 2.9802322387695312e-08 |
| parent_product_DAY | EULERIAN_ARD | PASS | 4.145811415534606e-05 | 2.9802322387695312e-08 |
| parent_product_DAY_override | EULERIAN_ARD | PASS | 8.25005962779013e-05 | 2.9802322387695312e-08 |
| parent_product_SEC | LAGRANGIAN | PASS | 4.145811415534606e-05 | 2.9802322387695312e-08 |
| parent_product_SEC_override | LAGRANGIAN | PASS | 8.25005962779013e-05 | 2.9802322387695312e-08 |
| parent_product_MIN | LAGRANGIAN | PASS | 4.145811415534606e-05 | 2.9802322387695312e-08 |
| parent_product_MIN_override | LAGRANGIAN | PASS | 8.25005962779013e-05 | 2.9802322387695312e-08 |
| parent_product_HR | LAGRANGIAN | PASS | 4.145811415534606e-05 | 2.9802322387695312e-08 |
| parent_product_HR_override | LAGRANGIAN | PASS | 8.25005962779013e-05 | 2.9802322387695312e-08 |
| parent_product_DAY | LAGRANGIAN | PASS | 4.145811415534606e-05 | 2.9802322387695312e-08 |
| parent_product_DAY_override | LAGRANGIAN | PASS | 8.25005962779013e-05 | 2.9802322387695312e-08 |

Tolerance: 5e-4 concentration units; 2e-6 for A+B. RATE_UNITS variants have the same physical rate.
The override cases double Tank k and halve Pipe k. They must therefore have different analytic curves.

Rain-fed wet-pool cases check the conserved sum A+B=tracer at Catchment and Tank and require positive product B.
These are self-consistency checks, without a closed-form concentration history or accuracy claim.

| Quality solver | Result | Catchment sum residual | Tank sum residual |
| --- | --- | --- | --- |
| LEGACY | PASS | 1.1920928955078125e-07 | 1.1920928955078125e-07 |
| EULERIAN_ARD | PASS | 1.1920928955078125e-07 | 1.1920928955078125e-07 |
| LAGRANGIAN | PASS | 1.1920928955078125e-07 | 1.1920928955078125e-07 |

Wet-pool tolerances: 2e-6 at Catchment; 2e-5 at Tank. Product B must exceed 0.001 in both.
Extended cases cover prescribed wall-area exchange, dry buildup conversion, SETPOINT injection and a fully covered bioretention layer profile. Checkpoint continuation is verified by engine tests.

## Extended profiles

Thresholds and reference definitions are given in the suite README.

| Case | Solver | Reference | Result | Metrics |
| --- | --- | --- | --- | --- |
| wall_geometry | LEGACY | analytic | PASS | `{"max_abs_error": 3.2609994482513116e-08, "max_normalized_mass_error": 6.431158228181744e-08}` |
| wall_geometry | EULERIAN_ARD | analytic | PASS | `{"max_abs_error": 3.2609994482513116e-08, "max_normalized_mass_error": 6.431158228181744e-08}` |
| dry_buildup | LEGACY | analytic | PASS | `{"max_abs_mass_error": 0.00022068171790579072, "max_mass_sum_error": 0.0}` |
| dry_buildup | EULERIAN_ARD | analytic | PASS | `{"max_abs_mass_error": 0.00022068171790579072, "max_mass_sum_error": 0.0}` |
| dry_buildup | LAGRANGIAN | analytic | PASS | `{"max_abs_mass_error": 0.00022068171790579072, "max_mass_sum_error": 0.0}` |
| lid_layers | LEGACY | self_consistency | PASS | `{"max_network_continuity_percent": 0.135, "max_sum_residual": 8.940696716308594e-08, "peak_product": [0.9167926907539368, 0.6260753870010376]}` |
| lid_layers | EULERIAN_ARD | self_consistency | PASS | `{"max_network_continuity_percent": null, "max_sum_residual": 8.940696716308594e-08, "peak_product": [0.9167926907539368, 0.6256158947944641]}` |
| lid_layers | LAGRANGIAN | self_consistency | PASS | `{"max_network_continuity_percent": null, "max_sum_residual": 8.940696716308594e-08, "peak_product": [0.9167926907539368, 0.6255945563316345]}` |
| setpoint | LEGACY | analytic | PASS | `{"max_abs_error": 0.0}` |
| setpoint | EULERIAN_ARD | analytic | PASS | `{"max_abs_error": 0.0}` |
| setpoint | LAGRANGIAN | analytic | PASS | `{"max_abs_error": 0.0}` |
