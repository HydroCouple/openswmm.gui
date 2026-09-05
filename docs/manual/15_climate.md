@page manual_climate 15 — Climate: Temperature, Evaporation, Wind, Snowmelt and Solar Radiation

## What you'll do

Give the model its meteorology: air temperature, evaporation, wind speed, the
global snowmelt parameters and areal depletion curves, and the monthly
adjustments applied to all of them. Then, if you are running heat transport, set
up the solar and radiative configuration that drives it.

## Where to find it

All five entries under **Model → Climate** open the **same** modal
**Climatology** dialog, each on a different tab:

The same five buttons sit in the **Climate** group of the **Model** ribbon tab.

| Menu entry | Opens on tab |
|---|---|
| **Model → Climate → Temperature** | **Temperature** |
| **Model → Climate → Evaporation** | **Evaporation** |
| **Model → Climate → Wind** | **Wind Speed** |
| **Model → Climate → Snow** | **Snow Melt** |
| **Model → Climate → Solar Radiation** | **Evaporation** |

The **Areal Depletion** and **Adjustments** tabs have no menu entry of their
own — reach them by opening any of the five and switching tabs.

**Solar Radiation is a shortcut, not a page.** SWMM has no separate solar-radiation
input section in the climatology block, so this button opens the Climatology
dialog on the **Evaporation** tab (solar energy enters the classic model through
Hargreaves ET). The real solar and radiative configuration — computed shortwave,
site latitude/longitude/timezone/elevation, atmosphere and cloud cover — belongs
to *heat transport* and lives in the Heat Configuration dialog; see
\ref climate_solar "Solar radiation and heat transport" below and
\ref manual_water_quality.

The dialog reads the model on open and writes on **OK**; it compares a signature
of every widget against the values it read, so pressing **OK** without editing
anything writes nothing.

\figtodo{15_climatology_tabs.png, The Climatology dialog showing its six tabs}

## Step-by-step

### Temperature tab — `[TEMPERATURE]`

| Control | What it does | Writes |
|---|---|---|
| **Source of Temperature Data:** | **No Data**; **Time Series**; **External Climate File** | `[TEMPERATURE]` source |
| **Time Series:** | The `[TIMESERIES]` holding air temperature — enabled for the *Time Series* source | `[TEMPERATURE] TIMESERIES` |
| **Climate File:** | Path to the climate file — enabled for the *External Climate File* source; stored relative to the project when possible | `[TEMPERATURE] FILE` |
| **Start reading file at** + date | Optional start date within the file | `[TEMPERATURE] FILE` start date |
| **Climate-file units:** | **Auto / file default**; **Tenths of degrees Celsius (C10)**; **Degrees Celsius (C)**; **Degrees Fahrenheit (F)** | `[TEMPERATURE]` units |

The file picker's filter is `Data files (*.dat *.txt)` plus *All files*. The
engine's climate-file reader accepts four formats and detects them on open:

| Format | Layout |
|---|---|
| `USER_PREPARED` | SWMM's own whitespace-delimited `StationID YYYY MM DD TMAX TMIN EVAP WIND` |
| `GHCND` | NCDC Global Historical Climatology Network — Daily |
| `TD3200` | NCDC TD3200 (NWS cooperative observer) |
| `DLY0204` | Canadian DLY02 / DLY04 fixed-format |

One climate file supplies temperature, pan evaporation and wind speed together —
which is why the Evaporation and Wind tabs both refer back to this page.

\figtodo{15_temperature_tab.png, The Temperature tab with an external climate file selected}

### Evaporation tab — `[EVAPORATION]`

The **Source of Evaporation Rates:** combo swaps the panel below it:

| Source | Panel |
|---|---|
| **Constant Value** | A single **Evaporation** rate in `in/day` or `mm/day` |
| **Monthly Averages** | A 12-row **Monthly Evaporation** table |
| **Time Series** | A **Time Series** picker |
| **Temperatures (Hargreaves)** | No inputs — a note explaining that evaporation is computed from the daily temperatures in the climate file selected on the Temperature page |
| **Climate File (pan)** | A 12-row **Monthly Pan Coefficients** table |

Two further controls apply to every source:

| Control | What it does | Writes |
|---|---|---|
| **Soil Recovery Pattern (optional):** | Monthly pattern scaling the infiltration recovery rate; the combo is editable and its empty entry means none | `[EVAPORATION] RECOVERY` |
| **Evaporate only during dry periods** | Suppresses evaporation while it is raining | `[EVAPORATION] DRY_ONLY` |

Units follow the project unit system throughout.

\figtodo{15_evaporation_tab.png, The Evaporation tab with monthly averages}

### Wind Speed tab — `[WINDSPEED]`

| Control | What it does | Writes |
|---|---|---|
| **Source of Wind Speed:** | **Monthly Averages** or **Use Climate File (see Temperature page)** | `[WINDSPEED]` |
| **Monthly Wind Speed** table | Twelve values in `mph` or `km/hr`; enabled for the monthly source | `[WINDSPEED] MONTHLY` |

Choosing the climate-file source here does nothing on its own — the file itself
is the one selected on the **Temperature** tab.

\figtodo{15_wind_tab.png, The Wind Speed tab with monthly averages}

### Snow Melt tab

The global snowmelt parameters, written to the `SNOWMELT` line. Per-surface
melt coefficients, initial snow depths and snow removal live on the individual
snow packs — see \ref manual_hydrology.

| Control | What it does |
|---|---|
| **Dividing Temperature** | Air temperature separating snow from rain (`deg F` or `deg C`) |
| **ATI Weight (fraction)** | Antecedent temperature index weight |
| **Negative Melt Ratio (fraction)** | Ratio of the negative-melt to the melt coefficient |
| **Elevation above MSL** | Site elevation (`feet` or `meters`) |
| **Latitude (degrees)** | Site latitude |
| **Longitude Correction (+/- minutes)** | Correction between standard and local solar time |

Elevation, latitude and longitude are the classic snowmelt-model site fields.
They are *not* the same fields the heat-transport solar model uses — those are
separate, and in different units; see below.

\figtodo{15_snowmelt_tab.png, The Snow Melt tab}

### Areal Depletion tab — `[ADC]`

A 10-row × 2-column table, **Impervious** and **Pervious**, giving the
**Fraction of Area Covered by Snow** at ten equally spaced ratios of snow depth
to depth-at-100 %-cover. Four preset buttons fill a column in one click:

- **Impervious: No Depletion** / **Pervious: No Depletion** — the constant curve
- **Impervious: Natural Area** / **Pervious: Natural Area** — the standard
  natural-area depletion curve

\figtodo{15_areal_depletion_tab.png, The Areal Depletion tab with the four preset buttons}

### Adjustments tab — `[ADJUSTMENTS]`

A 12-row (January–December) × 4-column table with the headers **Temp**, **Evap**,
**Rain** and **Cond**. The legend under the table states the convention exactly:
*Temp = +/- offset; Evap = multiplier; Rain = multiplier; Cond = multiplier* —
so a neutral month is `0` in the Temp column and `1` in the other three.
**Clear All** restores that neutral state (0 / 1 / 1 / 1) for all twelve months.

`Cond` adjusts hydraulic conductivity, which is how a seasonal soil adjustment is
applied to infiltration.

\figtodo{15_adjustments_tab.png, The Adjustments tab with monthly multipliers}

\videotodo{Setting up temperature from a climate file and adding monthly evaporation adjustments}

### Solar radiation and heat transport {#climate_solar}

Heat transport uses its own meteorology, written to `[SOLAR_RADIATION]`,
`[RADIATIVE_FLUXES]` and `[CLOUD_COVER]` alongside `[HEAT_SOURCES]` and
`[HEAT_FLUXES]`. It is edited in the **Heat Configuration** dialog
(**Model → Heat Configuration…**, ribbon **Model ▸ Data Objects ▸ Heat**), *not* in the Climatology
dialog, and is documented fully in \ref manual_water_quality. The summary here is
only so you know where to go.

The dialog's five tabs are **Sources**, **Fluxes**, **Radiative**, **Solar** and
**Cloud**.

- **Fluxes** — three module check boxes: **Surface exchange (latent +
  sensible)**, **Radiative exchange (shortwave + longwave)** and **LID layer
  conduction**. Radiative exchange must be on for anything below to matter.
- **Radiative** (`[RADIATIVE_FLUXES]`) — an *Incoming shortwave* group with
  three mutually exclusive sources: **Constant** (a value in W/m²),
  **Timeseries** (a series picker) or **Computed (solar position + Bird clear
  sky)**. Below it a *Radiative parameters* group holds **Water albedo Rs**,
  **Shade factor fs**, **Sky view fsky**, **Water emissivity**, **Land-cover
  emissivity**, **Brunt atmospheric coeff.** and **Longwave reflection RL**.
- **Solar** (`[SOLAR_RADIATION]`) — a *Site (needed for COMPUTED shortwave)*
  group with **Latitude (°, +N)**, **Longitude (°, +E)**, **Timezone (h from
  UTC)** and **Elevation (m)**, and an *Atmosphere (Bird clear-sky model)* group
  with **Aerosol depth at 380 nm**, **Aerosol depth at 500 nm**, **Precipitable
  water (cm)**, **Ozone column (cm)** and **Ground albedo (land)**.
- **Cloud** (`[CLOUD_COVER]`) — a **Cloud cover configured** check box gating
  **Fraction [0..1]**, **Shortwave atten. k**, **Shortwave atten. n**,
  **Longwave cloud k** and a **Fraction timeseries** picker.

Two things to keep straight:

1. **The site fields are duplicated, not shared.** The Climatology dialog's
   snowmelt latitude, longitude correction and elevation drive the classic
   snowmelt model; the Heat Configuration dialog's latitude, longitude, timezone
   and elevation drive the Bird clear-sky shortwave computation. They are
   separate inputs in separate sections, in different units (minutes of longitude
   correction versus degrees east; project length units versus metres). Set both
   if you are running both.
2. **Known API gap.** The engine exposes the shortwave and cloud timeseries
   *mode* but not the bound series *name*, so those two combos rebind rather
   than display what is currently set. Their "(keep current series)" entry is the
   no-op position — leave it alone unless you mean to change the binding.

\figtodo{15_heat_solar_tab.png, The Solar tab of the Heat Configuration dialog}

\figtodo{15_heat_radiative_tab.png, The Radiative tab with the three shortwave source options}

## Tips and gotchas

- **The Solar Radiation button does not open a solar page.** It is a shortcut to
  the Evaporation tab. For anything radiative, use **Model → Heat
  Configuration…**.
- **One climate file feeds three tabs.** Temperature, pan evaporation and wind
  all read the file chosen on the Temperature tab; there is no second file
  picker.
- **Hargreaves needs the file.** The *Temperatures (Hargreaves)* evaporation
  source has no inputs of its own — without a climate file on the Temperature
  tab it has nothing to compute from.
- **Neutral adjustments are 0 / 1 / 1 / 1**, not all zeros. Zeroing the
  multiplier columns would suppress evaporation, rainfall and infiltration
  entirely; use **Clear All**.
- **Nothing is written until OK**, and an unedited **OK** writes nothing at all —
  the dialog compares a signature of every widget against what it read.
- **Climate-file units matter.** A GHCND file in tenths of a degree read as whole
  degrees is off by a factor of ten; if the file has no unit marker, set
  **Climate-file units** explicitly rather than leaving it on *Auto*.

## Related

- \ref manual_hydrology — snow packs; rain gages and subcatchment hydrology
- \ref manual_water_quality — heat transport; the Heat Configuration dialog and the radiative model
- \ref manual_data_objects — the time series and monthly patterns these tabs pick from
- \ref manual_simulation_options — simulation dates and the reporting/routing steps the climate data is sampled on
- \ref manual_file_formats — climate and rain file formats SWMMVis reads
- \ref tutorial_transport — a model exercising heat transport end to end
