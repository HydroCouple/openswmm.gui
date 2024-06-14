# Reference {#reference}

## Measurement Units {#measurement_units}

### US Customary {#us_customary}

|                                    |                                                                                    |
| :--------------------------------- | :--------------------------------------------------------------------------------- |
| Area (Subcatchment)                | acres                                                                              |
| Area (Storage Unit)                | square feet                                                                        |
| Area (Ponding)                     | square feet                                                                        |
| Capillary Suction                  | inches                                                                             |
| Concentration                      | milligrams / liter (mg/L) <br> micrograms / liter (ug/L) <br> counts / liter (#/L) |
| Decay Constant (Infiltration)      | 1 / hours                                                                          |
| Decay Constant (Pollutants)        | 1 / days                                                                           |
| Depression Storage                 | inches                                                                             |
| Depth                              | feet                                                                               |
| Diameter                           | feet                                                                               |
| Discharge Coefficient Orifice Weir | dimensionless cubic feet / second / feet<sup>n</sup> (CFS/ft<sup>n</sup>)          |
| Elevation                          | feet                                                                               |
| Evaporation                        | inches / day                                                                       |
| Flow                               | cubic feet / second (CFS) gallons / minute (GPM) million gallons / day (MGD)       |
| Head                               | feet                                                                               |
| Hydraulic Conductivity             | inches / hour                                                                      |
| Infiltration Rate                  | inches / hour                                                                      |
| Length                             | feet                                                                               |
| Manning's n                        | seconds / meter<sup>1/3</sup>                                                      |
| Pollutant Buildup                  | mass / acre <br> mass / length                                                     |
| Rainfall Intensity                 | inches / hour                                                                      |
| Rainfall Volume                    | inches                                                                             |
| Slope (Subcatchments)              | percent                                                                            |
| Slope (Cross Section)              | rise / run                                                                         |
| Street Cleaning Interval           | days                                                                               |
| Volume                             | cubic feet                                                                         |
| Width                              | feet                                                                               |

### SI Metric Units {#si_metric_units}

|                                    |                                                                                    |
| :--------------------------------- | :--------------------------------------------------------------------------------- |
| Area (Subcatchment)                | hectares                                                                           |
| Area (Storage Unit)                | square meters                                                                      |
| Area (Ponding)                     | square meters                                                                      |
| Capillary Suction                  | millimeters                                                                        |
| Concentration                      | milligrams / liter (mg/L) <br> micrograms / liter (ug/L) <br> counts / liter (#/L) |
| Decay Constant (Infiltration)      | 1 / hours                                                                          |
| Decay Constant (Pollutants)        | 1 / days                                                                           |
| Depression Storage                 | millimeters                                                                        |
| Depth                              | meters                                                                             |
| Diameter                           | meters                                                                             |
| Discharge Coefficient Orifice Weir | dimensionless cubic meters / second / meters<sup>n</sup> (CFS/meter<sup>n</sup>)   |
| Elevation                          | meters                                                                             |
| Evaporation                        | millimeters / day                                                                  |
| Flow                               | cubic meters / second (CMS) liters / second (LPS) million liters / day (MLD)       |
| Head                               | meters                                                                             |
| Hydraulic Conductivity             | millimeters / hour                                                                 |
| Infiltration Rate                  | millimeters / hour                                                                 |
| Length                             | meters                                                                             |
| Manning's n                        | seconds / meter<sup>1/3</sup>                                                      |
| Pollutant Buildup                  | mass / hectare <br> mass / length                                                  |
| Rainfall Intensity                 | millimeters / hour                                                                 |
| Rainfall Volume                    | millimeters                                                                        |
| Slope (Subcatchments)              | percent                                                                            |
| Slope (Cross Section)              | rise / run                                                                         |
| Street Cleaning Interval           | days                                                                               |
| Volume                             | cubic meters                                                                       |
| Width                              | meters                                                                             |

## Tables of Parameter Values {#table_of_parameter_values}

### Soil Characteristics {#soil_characteristics}

| Soil Texture Class | K    | Ψ     | ϕ     | FC    | WP    |
| :----------------- | ---- | ----- | ----- | ----- | ----- |
| Sand               | 4.74 |  1.93 | 0.437 | 0.062 | 0.024 |
| Loamy Sand         | 1.18 |  2.40 | 0.437 | 0.105 | 0.047 |
| Sandy Loam         | 0.43 | 4.33  | 0.453 | 0.190 | 0.085 |
| Loam               | 0.13 | 3.50  | 0.463 | 0.232 | 0.116 |
| Silt Loam          | 0.26 | 6.69  | 0.501 | 0.284 | 0.135 |
| Sandy Clay Loam    | 0.06 | 8.66  | 0.398 | 0.244 | 0.136 |
| Clay Loam          | 0.04 | 8.27  | 0.464 | 0.310 | 0.187 |
| Silty Clay Loam    | 0.04 | 10.63 | 0.471 | 0.342 | 0.210 |
| Sandy Clay         | 0.02 | 9.45  | 0.430 | 0.321 | 0.221 |
| Silty Clay         | 0.02 | 11.42 | 0.479 | 0.371 | 0.251 |
| Clay               | 0.01 | 12.60 | 0.475 | 0.378 | 0.265 |

|     |                                 |
| :-- | :------------------------------ |
| K   | = hydraulic conductivity, in/hr |
| Ψ   | = suction head, in.             |
| ϕ   | = porosity, fraction            |
| FC  | = field capacity, fraction      |
| WP  | = wilting point, fraction       |

Source: Rawls, W.J. et al., (1983). J. Hyd. Engr., 109:1316.

Note: The following relation between Ψ and K can be derived from this table:

Ψ = 3.237 K - 0.328  (R2 = 0.9)

### SCS Curve Numbers {#scs_curve_numbers}

SCS Runoff Curve Numbers for Hydrologic Soil Groups A - D (Antecedent moisture condition II)[^1]

| Land Use Description                                      | A   | B   | C   | D   |
| :-------------------------------------------------------- | --- | --- | --- | --- |
| Cultivated land                                           |     |     |     |     |
| Without conservation treatment                            | 72  | 81  | 88  | 91  |
| With conservation treatment                               | 62  | 71  | 78  | 81  |
| Pasture or range land                                     |     |     |     |     |
| Poor condition                                            | 68  | 79  | 86  | 89  |
| Good condition                                            | 39  | 61  | 74  | 80  |
| Meadow                                                    |     |     |     |     |
| Good condition                                            | 30  | 58  | 71  | 78  |
| Wood or forest land                                       |     |     |     |     |
| Thin stand, poor cover, no mulch                          | 45  | 66  | 77  | 83  |
| Good cover[^2]                                            | 25  | 55  | 70  | 77  |
| Open spaces, lawns, parks, golf courses, cemeteries, etc. |     |     |     |     |
| Good condition: grass cover on 75% or more of the area    | 39  | 61  | 74  | 80  |
| Fair condition: grass cover on 50% - 75% of the area      | 49  | 69  | 79  | 84  |
| Commercial and business areas (85% impervious)            | 89  | 92  | 94  | 95  |
| Industrial discricts (72% impervious)                     | 81  | 88  | 91  | 93  |
| Residential[^3]                                           |     |     |     |     |
| Average lot size (% Impervious[^4])                       |     |     |     |     |
| 1/8 ac or less (65)                                       | 77  | 85  | 90  | 92  |
| 1/4 ac (38)                                               | 61  | 75  | 83  | 87  |
| 1/3 ac (30)                                               | 57  | 72  | 81  | 86  |
| 1/2 ac (25)                                               | 54  | 70  | 80  | 85  |
| 1 ac (20)                                                 | 51  | 68  | 79  | 84  |
| Paved parking lots, roofs, driveways, etc.[^5]            | 98  | 98  | 98  | 98  |
| Streets and roads                                         |     |     |     |     |
| Paved with curbs and storm sewers[^5]                     | 98  | 98  | 98  | 98  |
| Gravel                                                    | 76  | 85  | 89  | 91  |
| Dirt                                                      | 72  | 82  | 87  | 89  |

[1] Source: SCS Urban Hydrology for Small Watersheds, 2nd Ed., (TR-55), June 1986.

[2] Good cover is protected from grazing and litter and brush cover soil.

[3] Curve numbers are computed assuming that the runoff from the house and driveway is directed toward the street with a minimum of roof water directed to lawns where additional infiltration could occur.

[4] The remaining pervious areas (lawn) are considered to be in good pasture condition for these curve numbers.

[5] In some warmer climates of the country a curve number of 95 may be used.

### Soil Group Definitions {#soil_group_definitions}

NRCS Hydrologic Soil Group Definitions

| Group | Meaning                                                                                                                                                                                                                                                                                                          | Saturated <br> Conductivity <br> (in/hr) |
| :---- | :--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------- |
| A     | Low runoff potential. <br> Water is transmitted freely through the <br> soil. Group A soils typically have less than <br> 10 percent clay and more than 90 percent <br> sand or gravel and have gravel or sand textures.                                                                                         | > 1.42                                   |
| B     | Moderately low runoff potential. <br> Water transmission through the soil is <br> unimpeded. Group B soils typically have <br> between 10 percent and 20 percent clay <br> and 50 percent to 90 percent sand <br> and have loamy sand or sandy loam textures.                                                    | 0.57 - 1.42                              |
| C     | Moderately high runoff potential. <br> Water transmissionthrough the soil is <br> somewhat restricted. Group C soils <br> typically have between 20 percent and 40 <br> percent clay and less than 50 percent sand <br> and have loam, silt loam, sandy clay loam, <br> clay loam, and silty clay loam textures. | 0.06 - 0.57                              |
| D     | High runoff potential. <br> Water movement through the soil is <br> restricted or very restricted. Group D soils <br> typically have greater than 40 percent <br> clay, less than 50 percent sand, and have clayey <br> textures.                                                                                | < 0.06                                   |

Source: Hydrology National Engineering Handbook, Chapter 7, Natural Resources Conservation Service, U.S. Department of Agriculture, January 2009.

### Depression Storage {#depression_storage}

Typical Depression Storage Values

|                     |                    |
| :------------------ | :----------------- |
| Impervious surfaces | 0.05 - 0.10 inches |
| Lawns               | 0.10 - 0.20 inches |
| Pasture             | 0.20 inches        |
| Forest litter       | 0.30 inches        |

(Source: ASCE,(1992), Design & Construction of Urban Stormwater Management Systems, New York, NY)

### Manning's n - Overland Flow {#mannings_n-overland_flow}

| Surface                   | Manning's n |
| :------------------------ | ----------- |
| Smooth asphalt            | 0.011       |
| Smooth concrete           | 0.012       |
| Ordinary concrete lining  | 0.013       |
| Good wood                 | 0.014       |
| Brick with cement mortar  | 0.014       |
| Vitrified clay            | 0.015       |
| Cast iron                 | 0.015       |
| Corrugated metal pipes    | 0.024       |
| Cement rubble surface     | 0.024       |
| Fallow soils (no residue) | 0.05        |
| Cultivated soils          |             |
| Residue cover < 20%       | 0.06        |
| Residue cover > 20%       | 0.17        |
| Range (natural)           | 0.13        |
| Grass                     |             |
| Short, prairie            | 0.15        |
| Dense                     | 0.24        |
| Bermuda grass             | 0.41        |
| Woods                     |             |
| Light underbrush          | 0.40        |
| Dense underbrush          | 0.80        |

Source: McCuen, R. et al. (1996), Hydrology, FHWA-SA-96-067, Federal Highway Administration, Washington, DC

### Manning's n - Closed Conduits {#mannings_n-closed_conduits}

| Conduit Material                                                   | Manning's n   |
| :----------------------------------------------------------------- | ------------- |
| Asbestos-cement pipe                                               | 0.011 - 0.015 |
| Brick                                                              | 0.013 - 0.017 |
| Cast iron pipe                                                     |               |
| - Cement-lined & seal coated                                       | 0.011 - 0.015 |
| Concrete (monolithic)                                              |               |
| - Smooth forms                                                     | 0.012 - 0.014 |
| - Rough forms                                                      | 0.015 - 0.017 |
| Concrete pipe                                                      | 0.011 - 0.015 |
| Corrugated-metal pipe <br> (1/2-in. x 2-2/3-in. <br> corrugations) |               |
| - Plain                                                            | 0.022 - 0.026 |
| - Paved invert                                                     | 0.018 - 0.022 |
| - Spun asphalt lined                                               | 0.011 - 0.015 |
| Plastic pipe (smooth)                                              | 0.011 - 0.015 |
| Vitrified clay                                                     |               |
| - Pipes                                                            | 0.011 - 0.015 |
| - Liner plates                                                     | 0.013 - 0.017 |

Source: ASCE (1982). Gravity Sanitary Sewer Design and Construction, ASCE Manual of Practice No. 60, New York, NY.

### Manning's n - Open Channels {#mannings_n-open_channels}

| Channel Type                                                                  | Manning's n   |
| :---------------------------------------------------------------------------- | ------------- |
| Lined Channels                                                                |               |
| - Asphalt                                                                     | 0.013 - 0.017 |
| - Brick                                                                       | 0.012 - 0.018 |
| - Concrete                                                                    | 0.011 - 0.020 |
| - Rubble or riprap                                                            | 0.020 - 0.035 |
| - Vegetal                                                                     | 0.030 - 0.40  |
| Excavated or dredged                                                          |               |
| - Earth, straight and uniform                                                 | 0.020 - 0.030 |
| - Earth, winding, fairly uniform                                              | 0.025 - 0.040 |
| - Rock                                                                        | 0.030 - 0.045 |
| - Unmaintained                                                                | 0.050 - 0.140 |
| Natural channels (minor <br> streams, top width at flood <br> stage < 100 ft) |               |
| - Fairly regular section                                                      | 0.030 - 0.070 |
| - Irregular section with pools                                                | 0.040 - 0.100 |

Source: ASCE (1982). Gravity Sanitary Sewer Design and Construction, ASCE Manual of Practice No. 60, New York, NY.

### Water Quality Characteristics of Urban Runoff {#water_quality_characteristics}

| Constituent      | Event Mean Concentrations |
| :--------------- | ------------------------- |
| TSS (mg/L)       | 180 - 548                 |
| BOD (mg/L)       | 12 - 19                   |
| COD (mg/L)       | 82 - 178                  |
| Total P (mg/L)   | 0.42 - 0.88               |
| Soluble P (mg/L) | 0.15 - 0.28               |
| TKN (mg/L)       | 1.90 - 4.18               |
| NO2/NO3-N (mg/L) | 0.86 - 2.2                |
| Total Cu (ug/L)  | 43 - 118                  |
| Total Pb (ug/L)  | 182 - 443                 |
| Total Zn (ug/L)  | 202 - 633                 |

Source: U.S. Environmental Protection Agency. (1983). Results of the Nationwide Urban Runoff Program (NURP), Vol. 1, NTIS PB 84-185552), Water Planning Division, Washington, DC.

### Culvert Code Numbers {#culvert_code_numbers}

**Circular Concrete**

- 1 Square edge with headwall
- 2 Groove end with headwall
- 3 Groove end projecting

**Circular Corrugated Metal Pipe**

- 4 Headwall
- 5 Mitered to slope
- 6 Projecting

**Circular Pipe, Beveled Ring Entrance**

- 7 45 deg. bevels
- 8 33.7 deg. bevels

**Rectangular Box; Flared Wingwalls**

- 9 30-75 deg. wingwall flares
- 10 90 or 15 deg. wingwall flares
- 11 0 deg. wingwall flares (straight sides)

**Rectangular Box;Flared Wingwalls and Top Edge Bevel**

- 12 45 deg flare; 0.43D top edge bevel
- 13 18-33.7 deg. flare; 0.083D top edge bevel

**Rectangular Box, 90-deg Headwall, Chamfered / Beveled Inlet Edges**

- 14 chamfered 3/4-in.
- 15 beveled 1/2-in/ft at 45 deg (1:1)
- 16 beveled 1-in/ft at 33.7 deg (1:1.5)

**Rectangular Box, Skewed Headwall, Chamfered / Beveled Inlet Edges**

- 17 3/4 in. chamfered edge, 45 deg skewed headwall
- 18 3/4 in. chamfered edge, 30 deg skewed headwall
- 19 3/4 in. chamfered edge, 15 deg skewed headwall
- 20 45 deg beveled edge, 10-45 deg skewed headwall

**Rectangular Box, Non-offset Flared Wingwalls, 3/4 in. Chamfer at Top of Inlet**

- 21 45 deg (1:1) wingwall flare
- 22 8.4 deg (3:1) wingwall flare
- 23 18.4 deg (3:1) wingwall flare, 30 deg inlet skew

**Rectangular Box, Offset Flared Wingwalls, Beveled Edge at Inlet Top**

- 24 45 deg (1:1) flare, 0.042D top edge bevel
- 25 33.7 deg (1.5:1) flare, 0.083D top edge bevel
- 26 18.4 deg (3:1) flare, 0.083D top edge bevel

**Corrugated Metal Box**

- 27 90 deg headwall
- 28 Thick wall projecting
- 29 Thin wall projecting

**Horizontal Ellipse Concrete**

- 30 Square edge with headwall
- 31 Grooved end with headwall
- 32 Grooved end projecting

**Vertical Ellipse Concrete**

- 33 Square edge with headwall
- 34 Grooved end with headwall
- 35 Grooved end projecting

**Pipe Arch, 18 in. Corner Radius, Corrugated Metal**

- 36 90 deg headwall
- 37 Mitered to slope
- 38 Projecting

**Pipe Arch, 18 in. Corner Radius, Corrugated Metal**

- 39 Projecting
- 40 No bevels
- 41 33.7 deg bevels

**Pipe Arch, 31 in. Corner Radius,Corrugated Metal**

- 42 Projecting
- 43 No bevels
- 44 33.7 deg. bevels

**Arch, Corrugated Metal**

- 45 90 deg headwall
- 46 Mitered to slope
- 47 Thin wall projecting

**Circular Culvert**

- 48 Smooth tapered inlet throat
- 49 Rough tapered inlet throat

**Elliptical Inlet Face**

- 50 Tapered inlet, beveled edges
- 51 Tapered inlet, square edges
- 52 Tapered inlet, thin edge projecting

**Rectangular**

- 53 Tapered inlet throat

**Rectangular Concrete**

- 54 Side tapered, less favorable edges
- 55 Side tapered, more favorable edges
- 56 Slope tapered, less favorable edges
- 57 Slope tapered, more favorable edges

### Culvert Inlet Loss Coefficients {#culvert_inlet_loss_coefficients}

| Type of Structure and Design of Enterence                | Coefficient |
| :------------------------------------------------------- | ----------- |
| - Pipe, Concrete                                         |             |
| Projecting from fill, socket end (groove-end)            | 0.2         |
| Projecting from fill, sq. cut end                        | 0.5         |
| Headwall or headwall and wingwalls                       |             |
| Socket end of pipe (groove-end                           | 0.2         |
| Square-edge                                              | 0.5         |
| Rounded (radius = D/12                                   | 0.2         |
| Mitered to conform to fill slope                         | 0.7         |
| \*End-Section conforming to fill slope                   | 0.5         |
| Beveled edges, 33.70 or 450 bevels                       | 0.2         |
| Side- or slope-tapered inlet                             | 0.2         |
|                                                          |             |
| - Pipe. or Pipe-Arch. Corrugated Metal                   |             |
| Projecting from fill (no headwall)                       | 0.9         |
| Headwall or headwall and wingwalls square-edge           | 0.5         |
| Mitered to conform to fill slope, paved or unpaved slope | 0.7         |
| \*End-Section conforming to fill slope                   | 0.5         |
| Beveled edges, 33.70 or 450 bevels                       | 0.2         |
| Side- or slope-tapered inlet                             | 0.2         |
|                                                          |             |
| - Box, Reinforced Concrete                               |             |
| Headwall parallel to embankment (no wingwalls)           |             |
| Square-edged on 3 edges                                  | 0.5         |
| Rounded on 3 edges to radius of D/12 or B/12             |             |
| or beveled edges on 3 sides                              | 0.2         |
| Wingwalls at 300 to 750 to barrel                        |             |
| Square-edged at crown                                    | 0.4         |
| Crown edge rounded to radius of D/12 or beveled top edge | 0.2         |
| Wingwall at 100 to 250 to barrel                         |             |
| Square-edged at crown                                    | 0.5         |
| Wingwalls parallel (extension of sides)                  |             |
| Square-edged at crown                                    | 0.7         |
| Side- or slope-tapered inlet                             | 0.2         |

\*Note: "End Sections conforming to fill slope," made of either metal or concrete, are the sections commonly available from manufacturers. From limited hydraulic tests they are equivalent in operation to a headwall in both inlet and outlet control. Some end sections, incorporating a closed taper in their design have a superior hydraulic performance. These latter sections can be designed using the information given for the beveled inlet.

Source: Federal Highway Administration (2005). Hydraulic Design of Highway Culverts, Publication No. FHWA-NHI-01-020.

### Standard Elliptical Pipe Sizes {#standard_elliptical_pipe_sizes}

| Code | Minor Axis (in) | Major Axis (in) | Minor Axis (mm) | Major Axis (mm) |
| ---- | --------------- | --------------- | --------------- | --------------- |
| 1    | 14              | 23              | 356             | 584             |
| 2    | 19              | 30              | 483             | 762             |
| 3    | 22              | 34              | 559             | 864             |
| 4    | 24              | 38              | 610             | 965             |
| 5    | 27              | 42              | 686             | 1067            |
| 6    | 29              | 45              | 737             | 1143            |
| 7    | 32              | 49              | 813             | 1245            |
| 8    | 34              | 53              | 864             | 1346            |
| 9    | 38              | 60              | 965             | 1524            |
| 10   | 43              | 68              | 1092            | 1727            |
| 11   | 48              | 76              | 1219            | 1930            |
| 12   | 53              | 83              | 1346            | 2108            |
| 13   | 58              | 91              | 1473            | 2311            |
| 14   | 63              | 98              | 1600            | 2489            |
| 15   | 68              | 106             | 1727            | 2692            |
| 16   | 72              | 113             | 1829            | 2870            |
| 17   | 77              | 121             | 1956            | 3073            |
| 18   | 82              | 128             | 2083            | 3251            |
| 19   | 87              | 136             | 2210            | 3454            |
| 20   | 92              | 143             | 2337            | 3632            |
| 21   | 97              | 151             | 2464            | 3835            |
| 22   | 106             | 166             | 2692            | 4216            |
| 23   | 116             | 180             | 2946            | 4572            |

Note: The Minor Axis is the maximum width for a vertical ellipse and the full depth for a horizontal ellipse while the Major Axis is the maximum width for a horizontal ellipse and the full depth for a vertical ellipse.

Source: Concrete Pipe Design Manual, American Concrete Pipe Association, 2011 (www.concrete-pipe.org).

### Standard Arch Pipe Sizes {#standard_arch_pipe_sizes}

**Concrete Arch Pipes**

| Code | Rise (in) | Span (in) | Rise (mm) | Span (mm) |
| ---- | --------- | --------- | --------- | --------- |
| 1    | 11        | 18        | 279       | 457       |
| 2    | 13.5      | 22        | 343       | 559       |
| 3    | 15.5      | 26        | 394       | 660       |
| 4    | 18        | 28.5      | 457       | 724       |
| 5    | 22.5      | 36.25     | 572       | 921       |
| 6    | 26.625    | 43.75     | 676       | 1111      |
| 7    | 31.3125   | 51.125    | 795       | 1299      |
| 8    | 36        | 58.5      | 914       | 1486      |
| 9    | 40        | 65        | 1016      | 1651      |
| 10   | 45        | 73        | 1143      | 1854      |
| 11   | 54        | 88        | 1372      | 2235      |
| 12   | 62        | 102       | 1575      | 2591      |
| 13   | 72        | 115       | 1829      | 2921      |
| 14   | 77.5      | 122       | 1969      | 3099      |
| 15   | 87.125    | 138       | 2213      | 3505      |
| 16   | 96.875    | 154       | 2461      | 3912      |
| 17   | 106.5     | 168.75    | 2705      | 4286      |

**Corrugated Steel, 2-2/3 x 1/2 in. Corrugation**

| Code | Rise (in) | Span (in) | Rise (mm) | Span (mm) |
| ---- | --------- | --------- | --------- | --------- |
| 18   | 13        | 17        | 330       | 432       |
| 19   | 15        | 21        | 381       | 533       |
| 20   | 18        | 24        | 457       | 610       |
| 21   | 20        | 28        | 508       | 711       |
| 22   | 24        | 35        | 610       | 889       |
| 23   | 29        | 42        | 737       | 1067      |
| 24   | 33        | 49        | 838       | 1245      |
| 25   | 38        | 57        | 965       | 1448      |
| 26   | 43        | 64        | 1092      | 1626      |
| 27   | 47        | 71        | 1194      | 1803      |
| 28   | 52        | 77        | 1321      | 1956      |
| 29   | 57        | 83        | 1448      | 2108      |

**Corrugated Steel, 3 x 1 in. Corrugation**

| Code | Rise (in) | Span (in) | Rise (mm) | Span (mm) |
| ---- | --------- | --------- | --------- | --------- |
| 30   | 31        | 40        | 787       | 1016      |
| 31   | 36        | 46        | 914       | 1168      |
| 32   | 41        | 53        | 1041      | 1346      |
| 33   | 46        | 60        | 1168      | 1524      |
| 34   | 51        | 66        | 1295      | 1676      |
| 35   | 55        | 73        | 1397      | 1854      |
| 36   | 59        | 81        | 1499      | 2057      |
| 37   | 63        | 87        | 1600      | 2210      |
| 38   | 67        | 95        | 1702      | 2413      |
| 39   | 71        | 103       | 1803      | 2616      |
| 40   | 75        | 112       | 1905      | 2845      |
| 41   | 79        | 117       | 2007      | 2972      |
| 42   | 83        | 128       | 2108      | 3251      |
| 43   | 87        | 137       | 2210      | 3480      |
| 44   | 91        | 142       | 2311      | 3607      |

**Structural Plate, 18 in. Corner Radius**

| Code | Rise (in) | Span (in) | Rise (mm) | Span (mm) |
| ---- | --------- | --------- | --------- | --------- |
| 45   | 55        | 73        | 1397      | 1854      |
| 46   | 57        | 76        | 1448      | 1930      |
| 47   | 59        | 81        | 1499      | 2057      |
| 48   | 61        | 84        | 1549      | 2134      |
| 49   | 63        | 87        | 1600      | 2210      |
| 50   | 65        | 92        | 1651      | 2337      |
| 51   | 67        | 95        | 1702      | 2413      |
| 52   | 69        | 98        | 1753      | 2489      |
| 53   | 71        | 103       | 1803      | 2616      |
| 54   | 73        | 106       | 1854      | 2692      |
| 55   | 75        | 112       | 1905      | 2845      |
| 56   | 77        | 114       | 1956      | 2896      |
| 57   | 79        | 117       | 2007      | 2972      |
| 58   | 81        | 123       | 2057      | 3124      |
| 59   | 83        | 128       | 2108      | 3251      |
| 60   | 85        | 131       | 2159      | 3327      |
| 61   | 87        | 137       | 2210      | 3480      |
| 62   | 89        | 139       | 2261      | 3531      |
| 63   | 91        | 142       | 2311      | 3607      |
| 64   | 93        | 148       | 2362      | 3759      |
| 65   | 95        | 150       | 2413      | 3810      |
| 66   | 97        | 152       | 2464      | 3861      |
| 67   | 100       | 154       | 2540      | 3912      |
| 68   | 101       | 161       | 2565      | 4089      |
| 69   | 103       | 167       | 2616      | 4242      |
| 70   | 105       | 169       | 2667      | 4293      |
| 71   | 107       | 171       | 2718      | 4343      |
| 72   | 109       | 178       | 2769      | 4521      |
| 73   | 111       | 184       | 2819      | 4674      |
| 74   | 113       | 186       | 2870      | 4724      |
| 75   | 115       | 188       | 2921      | 4775      |
| 76   | 118       | 190       | 2997      | 4826      |
| 77   | 119       | 197       | 3023      | 5004      |
| 78   | 121       | 199       | 3073      | 5055      |

**Structural Plate, 31 in. Corner Radius**

| Code | Rise (in) | Span (in) | Rise (mm) | Span (mm) |
| ---- | --------- | --------- | --------- | --------- |
| 79   | 112       | 159       | 2845      | 4039      |
| 80   | 114       | 162       | 2896      | 4115      |
| 81   | 116       | 168       | 2946      | 4267      |
| 82   | 118       | 170       | 2997      | 4318      |
| 83   | 120       | 173       | 3048      | 4394      |
| 84   | 122       | 179       | 3099      | 4547      |
| 85   | 124       | 184       | 3150      | 4674      |
| 86   | 126       | 187       | 3200      | 4750      |
| 87   | 128       | 190       | 3251      | 4826      |
| 88   | 130       | 195       | 3302      | 4953      |
| 89   | 132       | 198       | 3353      | 5029      |
| 90   | 134       | 204       | 3404      | 5182      |
| 91   | 136       | 206       | 3454      | 5232      |
| 92   | 138       | 209       | 3505      | 5309      |
| 93   | 140       | 215       | 3556      | 5461      |
| 94   | 142       | 217       | 3607      | 5512      |
| 95   | 144       | 223       | 3658      | 5664      |
| 96   | 146       | 225       | 3708      | 5715      |
| 97   | 148       | 231       | 3759      | 5867      |
| 98   | 150       | 234       | 3810      | 5944      |
| 99   | 152       | 236       | 3861      | 5994      |
| 100  | 154       | 239       | 3912      | 6071      |
| 101  | 156       | 245       | 3962      | 6223      |
| 102  | 158       | 247       | 4013      | 6274      |

Source: Modern Sewer Design (Fourth Edition), American Iron and Steel Institute, Washington, DC, 1999.

## Visual Object Properties {#visual_object_properties}

### Rain Gage Properties

|                   |                                                                                                                                                                                                                                                                                                                                                                                                                                                                |
| :---------------- | :------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Name              | User-assigned rain gage name.                                                                                                                                                                                                                                                                                                                                                                                                                                  |
| X-Coordinate      | Horizontal location of the rain gage on the Study Area Map. If left blank then the rain gage will not appear on the map.                                                                                                                                                                                                                                                                                                                                       |
| Y-Coordinate      | Vertical location of the rain gage on the Study Area Map. If left blank then the rain gage will not appear on the map.                                                                                                                                                                                                                                                                                                                                         |
| Description       | Click the ellipsis button (or press Enter) to edit an optional description of the rain gage.                                                                                                                                                                                                                                                                                                                                                                   |
| Tag               | Optional label used to categorize or classify the rain gage.                                                                                                                                                                                                                                                                                                                                                                                                   |
| Rain Format       | Format in which the rain data are supplied: <br> INTENSITY: each rainfall value is an average rate in inches/hour (or mm/hour) over the recording interval. <br> VOLUME: each rainfall value is the volume of rain that fell in the recording interval (in inches or millimeters). <br> CUMULATIVE: each rainfall value represents the cumulative rainfall that has occurred since the start of the last series of non-zero values (in inches or millimeters). |
| Rain Interval     | Recording time interval between gage readings in decimal hours or hours:minutes format.                                                                                                                                                                                                                                                                                                                                                                        |
| Snow Catch Factor | Factor that corrects gage readings for snowfall.                                                                                                                                                                                                                                                                                                                                                                                                               |
| Data Source       | Source of rainfall data; either TIMESERIES for user-defined time series data or FILE for an external data file.                                                                                                                                                                                                                                                                                                                                                |
| TIME SERIES       |                                                                                                                                                                                                                                                                                                                                                                                                                                                                |
| - Series Name     | Name of time series with rainfall data if Data Source selection was TIMESERIES; leave blank otherwise (double-click to edit the series).                                                                                                                                                                                                                                                                                                                       |
| DATA FILE         |                                                                                                                                                                                                                                                                                                                                                                                                                                                                |
| - File Name       | Name of external file containing rainfall data (see Rainfall Files).                                                                                                                                                                                                                                                                                                                                                                                           |
| - Station No.     | Recording gage station number.                                                                                                                                                                                                                                                                                                                                                                                                                                 |
| - Rain Units      | Depth units (IN or MM) for rainfall values in user-prepared files (other standard file formats have fixed units depending on the format).                                                                                                                                                                                                                                                                                                                      |

### Subcatchment Properties

|                 |                                                                                                                                                                                                                                                                   |
| :-------------- | :---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Name            | User-assigned subcatchment name.                                                                                                                                                                                                                                  |
| X-Coordinate    | Horizontal location of the subcatchment's centroid on the Study Area Map. If left blank then the subcatchment will not appear on the map.                                                                                                                         |
| Y-Coordinate    | Vertical location of the subcatchment's centroid on the Study Area Map. If left blank then the subcatchment will not appear on the map.                                                                                                                           |
| Description     | Click the ellipsis button (or press Enter) to edit an optional description of the subcatchment.                                                                                                                                                                   |
| Tag             | Optional label used to categorize or classify the subcatchment.                                                                                                                                                                                                   |
| Rain Gage       | Name of the rain gage associated with the subcatchment.                                                                                                                                                                                                           |
| Outlet          | Name of the node or subcatchment that recieves the subcatchment's runoff.                                                                                                                                                                                         |
| Area            | Area of the subcatchment, including any LID controls (acres or hectares).                                                                                                                                                                                         |
| Width           | Characteristic width of the overland flow path for sheet flow runoff (feet or meters).(More...)                                                                                                                                                                   |
| % Slope         | Average percent slope of the subcatchment.                                                                                                                                                                                                                        |
| % Imperv        | Percent of the land area (not including any LIDs) which is impervious.                                                                                                                                                                                            |
| N-Imperv        | Manning's n for overland flow over the impervious portion of the subcatchment. (Typical Values).                                                                                                                                                                  |
| N-Perv          | Manning's n for overland flow over the pervious portion of the subcatchment. (Typical Values).                                                                                                                                                                    |
| Dstore-Imperv   | Depth of depression storage on the impervious portion of the subcatchment (inches or millimeters). (Typical Values).                                                                                                                                              |
| Dstore-Perv     | Depth of depression storage on the pervious portion of the subcatchment (inches or millimeters) (Typical Values).                                                                                                                                                 |
| % Zero-Imperv   | Percent of the impervious area with no depression storage.                                                                                                                                                                                                        |
| Subarea Routing | Choice of internal routing of runoff between pervious and impervious areas: <br> IMPERV: runoff from pervious area flows to impervious area <br> PERV: runoff from impervious flows to pervious area <br> OUTLET: runoff from both areas flows directly to outlet |
| Percent Routed  | Percent of runoff routed between subareas.                                                                                                                                                                                                                        |
| Infiltration    | Click the ellipsis button (or press Enter) to edit infiltration parameters for the subcatchment.                                                                                                                                                                  |
| LID Controls    | Click the ellipsis button (or press Enter) to edit the use of low impact development controls in the subcatchment.                                                                                                                                                |
| Groundwater     | Click the ellipsis button (or press Enter) to edit groundwater flow parameters for the subcatchment.                                                                                                                                                              |
| Snow Pack       | Name of snow pack parameter set (if any) assigned to the subcatchment.                                                                                                                                                                                            |
| Land Uses       | Click the ellipsis button (or press Enter) to assign land uses to the subcatchment. Only needed if pollutant buildup/washoff modeled.                                                                                                                             |
| Initial Buildup | Click the ellipsis button (or press Enter) to specify initial quantities of pollutant buildup over the subcatchment.                                                                                                                                              |
| Curb Length     | Total length of curbs in the subcatchment (any length units). Used only when pollutant buildup is normalized to curb length.                                                                                                                                      |
| N-Perv Pattern  | Name of optional monthly Time Pattern adjustments applied to pervious Manning's n (N-Perv). Leave blank if not applicable.                                                                                                                                        |
| Dstore Pattern  | Name of optional monthly Time Pattern adjustments applied to both depression storage (Dstore) values. Leave blank if not applicable.                                                                                                                              |
| Infil. Pattern  | Name of optional monthly Time Pattern adjustments applied to the pervious area's hydraulic conductivity. Leave blank if not applicable.                                                                                                                           |

Note: The adjustment factors provided in a subcatchment's Infiltration Pattern will override those supplied for Conductivity in the project's Climate Adjustment factors.

#### Subcatchment Width {#subcatchment_width}

An initial estimate of the characteristic width is given by the subcatchment area divided by the average maximum overland flow length. The maximum overland flow length is the length of the flow path from the outlet to the furthest drainage point of the subcatchment. Maximum lengths from several different possible flow paths should be averaged. These paths should reflect slow flow, such as over pervious surfaces, more than rapid flow over pavement, for example. Adjustments should be made to the width parameter to produce good fits to measured runoff hydrographs.

### Junction Properties

|                 |                                                                                                                                                                                                                                                                                                   |
| :-------------- | :------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| Name            | User-assigned junction name.                                                                                                                                                                                                                                                                      |
| X-Coordinate    | Horizontal location of the junction on the Study Area Map. If left blank then the junction will not appear on the map.                                                                                                                                                                            |
| Y-Coordinate    | Vertical location of the junction on the Study Area Map. If left blank then the junction will not appear on the map.                                                                                                                                                                              |
| Description     | Click the ellipsis button (or press Enter) to edit an optional description of the junction.                                                                                                                                                                                                       |
| Tag             | Optional label used to categorize or classify the junction.                                                                                                                                                                                                                                       |
| Inflows         | Click the ellipsis button (or press Enter) to assign external direct, dry weather, or RDII inflows to the junction.                                                                                                                                                                               |
| Treatment       | Click the ellipsis button (or press Enter) to edit a set of treatment functions for pollutants entering the node.                                                                                                                                                                                 |
| Invert El.      | Invert elevation of the junction (feet or meters).                                                                                                                                                                                                                                                |
| Max. Depth      | Maximum depth at the junction (i.e., the distance from the invert to the ground surface) (feet or meters). If zero, then the distance from the invert to the top of the highest connecting link will be used.                                                                                     |
| Initial Depth   | Depth of water at the junction at the start of the simulation (feet or meters).                                                                                                                                                                                                                   |
| Surcharge Depth | Additional depth of water beyond the maximum depth that is allowed before the junction floods (feet or meters). This parameter can be used to simulate bolted manhole covers or force main connections.                                                                                           |
| Ponded Area     | Area occupied by ponded water atop the junction after flooding occurs (sq. feet or sq. meters). If the ALLOW PONDING analysis option is turned on, a non-zero value of this parameter will allow ponded water to be stored and subsequently returned to the drainage system when capacity exists. |

### Outfall Properties

|                  |                                                                                                                                                                                                                                                                                                                                                                                                                                                   |
| :--------------- | :------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| Name             | User-assigned outfall name.                                                                                                                                                                                                                                                                                                                                                                                                                       |
| X-Coordinate     | Horizontal location of the outfall on the Study Area Map. If left blank then the outfall will not appear on the map.                                                                                                                                                                                                                                                                                                                              |
| Y-Coordinate     | Vertical location of the outfall on the Study Area Map. If left blank then the outfall will not appear on the map.                                                                                                                                                                                                                                                                                                                                |
| Description      | Click the ellipsis button (or press Enter) to edit an optional description of the outfall.                                                                                                                                                                                                                                                                                                                                                        |
| Tag              | Optional label used to categorize or classify the outfall.                                                                                                                                                                                                                                                                                                                                                                                        |
| Inflows          | Click the ellipsis button (or press Enter) to assign external direct, dry weather or RDII inflows to the outfall.                                                                                                                                                                                                                                                                                                                                 |
| Treatment        | Click the ellipsis button (or press Enter) to edit a set of treatment functions for pollutants entering the node.                                                                                                                                                                                                                                                                                                                                 |
| Invert El.       | Invert elevation of the outfall (feet or meters).                                                                                                                                                                                                                                                                                                                                                                                                 |
| Tide Gate        | YES -  tide gate present to prevent backflow <br> NO - no tide gate present                                                                                                                                                                                                                                                                                                                                                                       |
| Route To         | Optional name of a subcatchment that receives the outfall's discharge.                                                                                                                                                                                                                                                                                                                                                                            |
| Type             | Type of outfall boundary condition: <br> FREE: outfall stage determined by minimum of critical flow depth and normal flow depth in the connecting conduit <br> NORMAL: outfall stage based on normal flow depth in the connecting conduit <br> FIXED: outfall stage set to a fixed value <br> TIDAL: outfall stage given by a table of tide elevation versus time of day <br> TIMESERIES: outfall stage supplied from a time series of elevations |
| Fixed Stage      | Water elevation for a FIXED type of outfall (feet or meters).                                                                                                                                                                                                                                                                                                                                                                                     |
| Tidal Curve Name | Name of the Tidal Curve relating water elevation to hour of the day for a TIDAL outfall (double-click to edit the curve).                                                                                                                                                                                                                                                                                                                         |
| Time Series Name | Name of time series containing time history of outfall stage for a TIMESERIES outfall (double-click to edit the series).                                                                                                                                                                                                                                                                                                                          |

### Flow Divider Properties

|                     |                                                                                                                                                                                                                                                                                                                                              |
| :------------------ | :------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Name                | User-assigned divider name.                                                                                                                                                                                                                                                                                                                  |
| X-Coordinate        | Horizontal location of the divider on the Study Area Map. If left blank then the divider will not appear on the map.                                                                                                                                                                                                                         |
| Y-Coordinate        | Vertical location of the divider on the Study Area Map. If left blank then the divider will not appear on the map.                                                                                                                                                                                                                           |
| Description         | Click the ellipsis button (or press Enter) to edit an optional description of the divider.                                                                                                                                                                                                                                                   |
| Tag                 | Optional label used to categorize or classify the divider.                                                                                                                                                                                                                                                                                   |
| Inflows             | Click the ellipsis button (or press Enter) to assign external direct, dry weather or RDII inflows to the divider.                                                                                                                                                                                                                            |
| Treatment           | Click the ellipsis button (or press Enter) to edit a set of treatment functions for pollutants entering the node.                                                                                                                                                                                                                            |
| Invert El.          | Invert elevation of the divider (feet or meters).                                                                                                                                                                                                                                                                                            |
| Max. Depth          | Maximum depth at the divider (i.e., distance from the invert to the ground surface) (feet or meters). If zero then the distance from the invert to the top of  the highest connecting link will be used.                                                                                                                                     |
| Initial Depth       | Depth of water at the divider at the start of the simulation (feet or meters)                                                                                                                                                                                                                                                                |
| Surcharge Depth     | Additional depth of water beyond the maximum depth that is allowed before the divider floods (feet or meters). This parameter can be used to simulate bolted manhole covers or force main connections.                                                                                                                                       |
| Ponded Area         | Area occupied by ponded water atop the divider after flooding occurs (sq. feet or sq. meters). If the Allow Ponding simulation option is turned on, a non-zero value of this parameter will allow ponded water to be stored and subsequently returned to the drainage system when capacity exists.                                           |
| Diverted Link       | Name of link which receives the diverted flow.                                                                                                                                                                                                                                                                                               |
| Type                | Type of flow divider. Choices are: <br> CUTOFF: diverts all inflow above a defined cutoff value <br> OVERFLOW: diverts all inflow above the flow capacity of the non-diverted link <br> TABULAR: uses a Diversion Curve to express diverted flow as a function of the total inflow <br> WEIR: uses a weir equation to compute diverted flow. |
| **CUTOFF DIVIDER**  |                                                                                                                                                                                                                                                                                                                                              |
| - Cutoff Flow       | Cutoff flow value used for a CUTOFF divider (flow units).                                                                                                                                                                                                                                                                                    |
| **TABULAR DIVIDER** |                                                                                                                                                                                                                                                                                                                                              |
| - Curve Name        | Name of Diversion Curve used with a TABULAR divider (double-click to edit the curve).                                                                                                                                                                                                                                                        |
| **WEIR DIVIDER**    |                                                                                                                                                                                                                                                                                                                                              |
| - Min. Flow         | Minimum flow at which diversion begins for a WEIR divider (flow units).                                                                                                                                                                                                                                                                      |
| - Max. Depth        | Vertical height of WEIR opening (feet or meters).                                                                                                                                                                                                                                                                                            |
| - Coefficient       | Product of WEIR's discharge coefficient and its length. Weir coefficients are typically in the range of 2.65 to 3.10 per foot, for flows in CFS.                                                                                                                                                                                             |

Note: Flow dividers are operational only for Steady Flow and Kinematic Wave flow routing. For Dynamic Wave flow routing they behave as Junction nodes.

### Storage Unit Properties

|                 |                                                                                                                                                                     |
| :-------------- | :------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| Name            | User-assigned storage unit name.                                                                                                                                    |
| X-Coordinate    | Horizontal location of the storage unit on the Study Area Map. If left blank then the storage unit will not appear on the map.                                      |
| Y-Coordinate    | Vertical location of the storage unit on the Study Area Map. If left blank then the storage unit will not appear on the map.                                        |
| Description     | Click the ellipsis button (or press Enter) to edit an optional description of the storage unit.                                                                     |
| Tag             | Optional label used to categorize or classify the storage unit.                                                                                                     |
| Inflows         | Click the ellipsis button (or press Enter) to assign external direct, dry weather, or RDII inflows to the storage unit.                                             |
| Treatment       | Click the ellipsis button (or press Enter) to edit a set of treatment functions for pollutants within the storage unit.                                             |
| Invert El.      | Elevation of the bottom of the storage unit (feet or meters).                                                                                                       |
| Max. Depth      | Maximum depth of the storage unit (feet or meters).                                                                                                                 |
| Initial Depth   | Initial depth of water in the storage unit at the start of the simulation (feet or meters).                                                                         |
| Surcharge Depth | Additional depth above the maximum depth that allows the unit to pressurize before it overflows (feet or meters). Only used for covered units.                      |
| Evap. Factor    | The fraction of the potential evaporation from the storage unit's water surface that is actually realized.                                                          |
| Seepage Loss    | Click the ellipsis button (or press Enter) to specify optional soil properties that determine seepage loss through the bottom and sloped sides of the storage unit. |
| Storage Shape   | Click the ellipsis button (or press Enter) to specify how the storage pool surface area varies with depth above the bottom of the unit.                             |

### Conduit Properties

|                   |                                                                                                                                                               |
| :---------------- | :------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| Name              | User-assigned conduit name.                                                                                                                                   |
| Inlet Node        | Name of node on the inlet end of the conduit (which is normally the end at higher elevation).                                                                 |
| Outlet Node       | Name of node on the outlet end of the conduit (which is normally the end at lower elevation).                                                                 |
| Description       | Click the ellipsis button (or press Enter) to edit an optional description of the conduit.                                                                    |
| Tag               | Optional label used to categorize or classify the conduit.                                                                                                    |
| Shape             | Click the ellipsis button (or press Enter) to edit the geometric properties of the conduit's cross section.                                                   |
| Max. Depth        | Maximum depth of the conduit's cross section (feet or meters).                                                                                                |
| Length            | Conduit length (feet or meters).                                                                                                                              |
| Roughness         | Manning's roughness coefficient. <br> (Values for closed conduits) <br> (Values for open channels)                                                            |
| Inlet Offset      | Depth or elevation of the conduit invert above the node invert at the inlet end of the conduit (feet or meters).                                              |
| Outlet Offset     | Depth or elevation of the conduit invert above the node invert at the outlet end of the conduit (feet or meters).                                             |
| Initial Flow      | Initial flow in the conduit at the start of the simulation (flow units).                                                                                      |
| Maximum Flow      | Maximum flow allowed in the conduit (flow units) - use 0 or leave blank if not applicable.                                                                    |
| Entry Loss Coeff. | Head loss coefficient associated with energy losses at the entrance of the conduit. For culverts, refer to Culvert Inlet Loss Coefficients table.             |
| Exit Loss Coeff.  | Head loss coefficient associated with energy losses at the exit of the conduit. For culverts, use a value of 1.0.                                             |
| Avg. Loss Coeff.  | Head loss coefficient associated with energy losses along the length of the conduit.                                                                          |
| Flap Gate         | YES if a flap gate exists that prevents backflow through the conduit, or NO if no flap gate exists.                                                           |
| Culvert Code      | If the conduit is a culvert subject to possible inlet flow control click the ellipsis button (or press Enter) to select a code number for its inlet geometry. |
| Inlet Structure   | Click the ellipsis button (or press Enter) to assign an inlet structure to a street or trapezoidal conduit.                                                   |

### Pump Properties

|                |                                                                                                                               |
| :------------- | :---------------------------------------------------------------------------------------------------------------------------- |
| Name           | User-assigned pump name.                                                                                                      |
| Inlet Node     | Name of node on the inlet side of the pump.                                                                                   |
| Outlet Node    | Name of node on the outlet side of the pump.                                                                                  |
| Description    | Click the ellipsis button (or press Enter) to edit an optional description of the the pump.                                   |
| Tag            | Optional label used to categorize or classify the pump.                                                                       |
| Pump Curve     | Name of the Pump Curve which contains the pump's operating data (double-click to edit the curve). Enter \* for an Ideal pump. |
| Initial Status | Status of the pump (ON or OFF) at the start of the simulation.                                                                |
| Startup Depth  | Depth at inlet node when pump turns on (ft or m). Enter 0 if not applicable.                                                  |
| Shutoff Depth  | Depth at inlet node when pump shuts off (ft or m). Enter 0 if not applicable.                                                 |

### Orifice Properties

|                    |                                                                                                                                                                                       |
| :----------------- | :------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| Name               | User-assigned orifice name.                                                                                                                                                           |
| Inlet Node         | Name of node on the inlet side of the orifice.                                                                                                                                        |
| Outlet Node        | Name of node on the outlet side of the orifice.                                                                                                                                       |
| Description        | Click the ellipsis button (or press Enter) to edit an optional description of the orifice.                                                                                            |
| Tag                | Optional label used to categorize or classify the orifice.                                                                                                                            |
| Type               | Type of orifice (SIDE or BOTTOM).                                                                                                                                                     |
| Shape              | Orifice shape (CIRCULAR or RECT_CLOSED).                                                                                                                                              |
| Height             | Height of orifice opening when fully open (feet or meters). Corresponds to the diameter of a circular orifice or the height of a rectangular orifice.                                 |
| Width              | Width of rectangular orifice when fully opened (feet or meters)                                                                                                                       |
| Inlet Offset       | Depth or elevation of bottom of orifice above invert of inlet node (feet or meters).                                                                                                  |
| Discharge Coeff.   | Discharge coefficient (unitless). A typical value is 0.65.                                                                                                                            |
| Flap Gate          | YES if a flap gate exists which prevents backflow through the orifice, or NO if no flap gate exists.                                                                                  |
| Time to Open/Close | The time to open a closed (or close an open) gated orifice in decimal hours. Use 0 or leave blank if timed openings/closings do not apply. Use Control Rules to adjust gate position. |

### Weir Properties

|                  |                                                                                                                                                                                                                                                                                                                                                                                                                                                      |
| :--------------- | :--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Name             | User-assigned weir name.                                                                                                                                                                                                                                                                                                                                                                                                                             |
| Inlet Node       | Name of node on inlet side of weir.                                                                                                                                                                                                                                                                                                                                                                                                                  |
| Outlet Node      | Name of node on outlet side of weir.                                                                                                                                                                                                                                                                                                                                                                                                                 |
| Description      | Click the ellipsis button (or press Enter) to edit an optional description of the weir.                                                                                                                                                                                                                                                                                                                                                              |
| Tag              | Optional label used to categorize or classify the weir.                                                                                                                                                                                                                                                                                                                                                                                              |
| Type             | Weir type: TRANSVERSE, SIDEFLOW, V-NOTCH, TRAPEZOIDAL or ROADWAY.                                                                                                                                                                                                                                                                                                                                                                                    |
| Height           | Vertical height of weir opening (feet or meters)                                                                                                                                                                                                                                                                                                                                                                                                     |
| Length           | Horizontal length of weir opening (feet or meters)                                                                                                                                                                                                                                                                                                                                                                                                   |
| Side Slope       | Slope (width-to-height) of side walls for a V-NOTCH or TRAPEZOIDAL weir.                                                                                                                                                                                                                                                                                                                                                                             |
| Inlet Offset     | Depth or elevation of bottom of weir opening from invert of inlet node (feet or meters).                                                                                                                                                                                                                                                                                                                                                             |
| Discharge Coeff. | Discharge coefficient for flow through the central portion of the weir (for flow in CFS when using US units or CMS when using SI units). Typical values are: 3.33 US (1.84 SI) for sharp crested transverse weirs, 2.5 - 3.3 US (1.38 - 1.83 SI) for broad crested rectangular weirs, 2.4 - 2.8 US (1.35 - 1.55 SI) for V-notch (triangular) weirs. Discharge over Roadway weirs with a non-zero road width is computed using the FHWA HDS-5 method. |
| Flap Gate        | YES if the weir has a flap gate that prevents backflow, NO if it does not.                                                                                                                                                                                                                                                                                                                                                                           |
| End Coeff.       | Discharge coefficient for flow through the triangular ends of a TRAPEZOIDAL weir. See the recommended values for V-notch weirs listed above.                                                                                                                                                                                                                                                                                                         |
| End Contractions | Number of end contractions for a TRANSVERSE or SIDEFLOW weir whose length is shorter than the channel it is placed in. Either 0, 1, or 2 depending if no ends, one end, or both ends are beveled in from the side walls.                                                                                                                                                                                                                             |
| Can Surcharge    | YES if the weir can surcharge (have an upstream water level higher than the height of the opening) or NO if it cannot.                                                                                                                                                                                                                                                                                                                               |
| Coeff. Curve     | Name of an optional Weir Curve that allows the central Discharge Coefficient to vary as a function of head (in feet or meters). Does not apply to Roadway weirs.                                                                                                                                                                                                                                                                                     |
| **ROADWAY WEIR** |                                                                                                                                                                                                                                                                                                                                                                                                                                                      |
| Road Width       | Width of roadway and shoulders (feet or meters)                                                                                                                                                                                                                                                                                                                                                                                                      |
| Road Surface     | Type of road surface: PAVED or GRAVEL.                                                                                                                                                                                                                                                                                                                                                                                                               |

### Outlet Properties

|                |                                                                                                                                                                                                                                                                                                                                                                                                                                                                                     |
| :------------- | :---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Name           | User-assigned outlet name.                                                                                                                                                                                                                                                                                                                                                                                                                                                          |
| Inlet Node     | Name of node on inflow side of outlet.                                                                                                                                                                                                                                                                                                                                                                                                                                              |
| Outlet Node    | Name of node on discharge side of outlet.                                                                                                                                                                                                                                                                                                                                                                                                                                           |
| Description    | Click the ellipsis button (or press Enter) to edit an optional description of the outlet.                                                                                                                                                                                                                                                                                                                                                                                           |
| Tag            | Optional label used to categorize or classify the outlet.                                                                                                                                                                                                                                                                                                                                                                                                                           |
| Inlet Offset   | Depth or elevation of outlet above inlet node invert (ft or m).                                                                                                                                                                                                                                                                                                                                                                                                                     |
| Flap Gate      | YES if a flap gate exists which prevents backflow through the outlet, or NO if no flap gate exists.                                                                                                                                                                                                                                                                                                                                                                                 |
| Rating Curve   | Method of defining flow (Q) as a function of freeboard depth or head (y) across the outlet. <br> FUNCTIONAL/DEPTH - uses a power function Q = AyB where y is the freeboard depth above the outlet's opening. <br> FUNCTIONAL/HEAD - uses a power function Q = AyB where y is the head difference across the outlet. TABULAR/DEPTH - uses a tabulated curve of flow versus freeboard depth values. <br> TABULAR/HEAD - uses a tabulated curve of flow versus head difference values. |
| **FUNCTIONAL** |                                                                                                                                                                                                                                                                                                                                                                                                                                                                                     |
|  - Coefficient | Coefficient (A) for the functional relationship between depth or head and flow rate.                                                                                                                                                                                                                                                                                                                                                                                                |
|  - Exponent    | Exponent (B) used for the functional relationship between depth or head and flow rate.                                                                                                                                                                                                                                                                                                                                                                                              |
| **TABULAR**    |                                                                                                                                                                                                                                                                                                                                                                                                                                                                                     |
|  - Curve Name  | Name of Rating Curve containing the relationship between depth or head and flow rate (double-click to edit the curve).                                                                                                                                                                                                                                                                                                                                                              |

### Map Label Properties

|              |                                                                                                                                                                                                              |
| :----------- | :----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Text         | Text of label.                                                                                                                                                                                               |
| X-Coordinate | Horizontal location of the upper-left corner of the label on the study area map.                                                                                                                             |
| Y-Coordinate | Vertical location of the upper-left corner of the label on the study area map.                                                                                                                               |
| Anchor Node  | Name of node (or subcatchment) that anchors the label's position when the map is zoomed in (i.e., the pixel distance between the node and the label remains constant). Leave blank if anchoring is not used. |
| Font         | Click the ellipsis button (or press Enter) to modify the font used to draw the label.                                                                                                                        |

## Special Dialog Forms {#special_dialog_forms}

## Error Messages {#error_messages}

| Category        | Numbers   |
| :-------------- | :-------- |
| Run Time Errors | 101 - 107 |
| Property Errors | 108 - 195 |
| Format Errors   | 200 - 233 |
| File Errors     | 301 - 357 |
| Warnings        | 01 - 11   |

### Run Time Errors

**ERROR 101: memory allocation error.**

- There is not enough physical memory in the computer to analyze the study area.

**ERROR 103: cannot solve KW equations for Link xxx.**

- The internal solver for Kinematic Wave routing failed to converge for the specified link at some stage of the simulation.

**ERROR 105: cannot open ODE solver.**

- The system could not open its Ordinary Differential Equation solver.

**ERROR 107: cannot compute a valid time step.**

- A valid time step for runoff or flow routing calculations (i.e., a number greater than 0) could not be computed at some stage of the simulation.

### Property Errors

**ERROR 108: ambiguous outlet ID name for Subcatchment xxx.**

- The name of the element identified as the outlet of a subcatchment belongs to both a node and a subcatchment in the project's data base.

**ERROR 109: invalid parameter values for Aquifer xxx.**

- The properties entered for an aquifer object were either invalid numbers or were inconsistent with one another (e.g., the soil field capacity was higher than the porosity).

**ERROR 110: ground elevation is below water table for Subcatchment xxx.**

- The ground elevation assigned to a subcatchment's groundwater parameters cannot be below the initial water table elevation of the aquifer object used by the subcatchment.

**ERROR 111: invalid length for Conduit xxx.**

- Conduits cannot have zero or negative lengths.

**ERROR 112: elevation drop exceeds length for Conduit xxx.**

- The elevation drop across the ends of a conduit cannot be greater than the conduit's length. Check for errors in the length and in both the invert elevations and offsets at the conduit's upstream and downstream nodes.

**ERROR 113: invalid roughness for Conduit xxx.**

- Conduits cannot have zero or negative roughness values.

**ERROR 114: invalid number of barrels for Conduit xxx.**

- Conduits must consist of one or more barrels.

**ERROR 115: adverse slope for Conduit xxx.**

- Under Steady or Kinematic Wave routing, all conduits must have positive slopes.  This can usually be corrected by reversing the inlet and outlet nodes of the conduit (i.e., right click on the conduit and select Reverse from the popup menu that appears). Adverse slopes are permitted under Dynamic Wave routing.

**ERROR 117: no cross section defined for Link xxx.**

- A cross section geometry was never defined for the specified link.

**ERROR 119: invalid cross section for Link xxx.**

- Either an invalid shape or invalid set of dimensions was specified for a link's cross section.

**ERROR 121: missing or invalid pump curve assigned to Pump xxx.**

- Either no pump curve or an invalid type of curve was specified for a pump.

**ERROR 122: startup depth not higher than shutoff depth for Pump xxx.**

- Automatic startup for a pump always occurs at a wet well water level that is higher than its automatic shutoff level.

**ERROR 131: the following links form cyclic loops in the drainage system.**

- The Steady and Kinematic Wave flow routing methods cannot be applied to systems where a cyclic loop exists (i.e., a directed path along a set of links that begins and ends at the same node). Most often the cyclic nature of the loop can be eliminated by reversing the direction of one of its links (i.e., switching the inlet and outlet nodes of the link). The names of the links that form the loop will be listed following this message.

**ERROR 133: Node xxx has more than one outlet link.**

- Under Steady and Kinematic Wave flow routing, a junction node can have only a single outlet link.

**ERROR 134: Node xxx has illegal DUMMY link connections.**

- Only a single conduit with a DUMMY cross-section or Ideal-type pump can be directed out of a node; a node with an outgoing Dummy conduit or Ideal pump cannot have all of its incoming links be Dummy conduits and Ideal pumps; a Dummy conduit cannot have its upstream end connected to a storage node.

**ERROR 135: Divider xxx does not have two outlet links.**

- Flow divider nodes must have two outlet links connected to them.

**ERROR 136: Divider xxx has invalid diversion link.**

- The link specified as being the one carrying the diverted flow from a flow divider node was defined with a different inlet node.

**ERROR 137: Weir Divider xxx has invalid parameters.**

- The parameters of a Weir-type divider node either are non-positive numbers or are inconsistent (i.e., the value of the discharge coefficient times the weir height raised to the 3/2 power must be greater than the minimum flow parameter).

**ERROR 138: Node xxx has initial depth greater than maximum depth.**

- Self-explanatory.

**ERROR 139: Regulator xxx is the outlet of a non-storage node.**

- Under Steady or Kinematic Wave flow routing, orifices, weirs, and outlet links can only be used as outflow links from storage nodes.

**ERROR 140: Storage node xxx has negative volume at full depth.**

- The storage unit's Shape data (surface area v. depth) is producing a negative volume at full depth. This can occur when a storage node's surface area curve slopes downward at its highest depth which is below the node's full depth.

### Format Errors

**ERROR 200: one or more errors in input file.**

- This message appears when one or more input file parsing errors (the 200-series errors) occur.

**ERROR 201: too many characters in input line.**

- A line in the input file cannot exceed 1024 characters.

**ERROR 203: too few items at line n of input file.**

- Not enough data items were supplied on a line of the input file.

**ERROR 205: invalid keyword at line n of input file.**

- An unrecognized keyword was encountered when parsing a line of the input file.

**ERROR 207: duplicate ID name at line n of input file.**

- An ID name used for an object was already assigned to an object of the same category.

**ERROR 209: undefined object xxx at line n of input file.**

- A reference was made to an object that was never defined. An example would be if node 123 were designated as the outlet point of a subcatchment, yet no such node was ever defined in the study area.

**ERROR 211: invalid number xxx at line n of input file.**

- Either a string value was encountered where a numerical value was expected or an invalid number (e.g., a negative value) was supplied.

**ERROR 213: invalid date/time xxx at line n of input file.**

- An invalid format for a date or time was encountered. Dates must be entered as month/day/year and times as either decimal hours or as hour:minute:second.

**ERROR 217: control rule clause out of sequence at line n of input file.**

- Errors of this nature can occur when the format for writing control rules is not followed correctly (see Control Rule Format).

**ERROR 219: data provided for unidentified transect at line n of input file.**

- A GR line with Station-Elevation data was encountered in the [TRANSECTS] section of the input file after an NC line but before any X1 line that contains the transect's ID name.

**ERROR 221: transect station out of sequence at line n of input file.**

- The station distances specified for the transect of an irregular cross section must be in increasing numerical order starting from the left bank.

**ERROR 223: Transect xxx has too few stations.**

- A transect for an irregular cross section must have at least 2 stations defined for it.

**ERROR 225: Transect xxx has too many stations.**

- A transect cannot have more than 1500 stations defined for it.

**ERROR 227: Transect xxx has no Manning's N.**

- No Manning's N was specified for a transect (i.e., there was no NC line
  in the [TRANSECTS] section of the input file.

**ERROR 229: Transect xxx has invalid overbank locations.**

- The distance values specified for either the left or right overbank locations of a transect do not match any of the distances listed for the transect's stations.

**ERROR 231: Transect xxx has no depth.**

- All of the stations for a transect were assigned the same elevation.

**ERROR 233: invalid math expression at line n of input file.**

- A math expression used for a treatment function, a groundwater flow function or a control rule condition clause is either not correctly formed or contains undefined variables or math functions.

**ERROR 235: invalid infiltration parameters.**

- Examples are a Horton maximum infiltration rate lower than the minimum rate or a Green-Ampt initial moisture deficit greater than 1.

### File Errors

**ERROR 301: files share same names.**

- The input, report, and binary output files specified on the command line cannot have the same names.

**ERROR 303: cannot open input file.**

- The input file either does not exist or cannot be opened (e.g., it might be in use by another program).

**ERROR 305: cannot open report file.**

- The report file cannot be opened (e.g., it might reside in a directory to which the user does not have write privileges).

**ERROR 307: cannot open binary results file.**

- The binary output file cannot be opened (e.g., it might reside in a directory to which the user does not have write privileges).

**ERROR 309: error writing to binary results file.**

- There was an error in trying to write results to the binary output file (e.g., the disk might be full or the file size exceed the limit imposed by the operating system).

**ERROR 311: error reading from binary results file.**

- The command line version of SWMM could not read results saved to the binary output file when writing results to the report file.

**ERROR 313: cannot open scratch rainfall interface file.**

- SWMM could not open the temporary file it uses to collate data together from external rainfall files.

**ERROR 315: cannot open rainfall interface file xxx.**

- SWMM could not open the specified rainfall interface file, possibly because it does not exist or because the user does not have write privileges to its directory.

**ERROR 317: cannot open rainfall data file xxx.**

- An external rainfall data file could not be opened, most likely because it does not exist.

**ERROR 318: date out of sequence in rainfall data file xxx.**

- An external user-prepared rainfall data file must have its entries appear in chronological order. The first out-of-order entry will be listed.

**ERROR 319: unknown format for rainfall data file.**

- SWMM could not recognize the format used for a designated rainfall data
  file.

**ERROR 320: invalid format for rainfall interface file.**

- SWMM was trying to read data from a designated rainfall interface file with the wrong format (i.e., it may have been created for some other project or actually be some other type of file).

**ERROR 321: no data in rainfall interface file for gage xxx.**

- This message occurs when a project wants to use a previously saved rainfall interface file, but cannot find any data for one of its rain gages in the interface file. It can also occur if the gage uses data from a user-prepared rainfall file and the station id entered for the gage cannot be found in the file.

**ERROR 323: cannot open runoff interface file xxx.**

- A runoff interface file could not be opened, possibly because it does not exist or because the user does not have write privileges to its directory.

**ERROR 325: incompatible data found in runoff interface file.**

- SWMM was trying to read data from a designated runoff interface file with the wrong format (i.e., it may have been created for some other project or actually be some other type of file).

**ERROR 327: attempting to read beyond end of runoff interface file.**

- This error can occur when a previously saved runoff interface file is being used in a simulation with a longer duration than the one that created the interface file.

**ERROR 329: error in reading from runoff interface file.**

- A format error was encountered while trying to read data from a previously saved runoff interface file.

**ERROR 331: cannot open hot start interface file xxx.**

- A hotstart interface file could not be opened, possibly because it does not exist or because the user does not have write privileges to its directory.

**ERROR 333: incompatible data found in hot start interface file.**

- SWMM was trying to read data from a designated hot start interface file with the wrong format (i.e., it may have been created for some other project or actually be some other type of file).

**ERROR 335: error in reading from hot start interface file.**

- A format error was encountered while trying to read data from a previously saved hot start interface file.

**ERROR 336: no climate file specified for evaporation and/or wind speed.**

- This error occurs when the user specifies that evaporation or wind speed data will be read from an external climate file, but no name is supplied for the file.

**ERROR 337: cannot open climate file xxx.**

- An external climate data file could not be opened, most likely because it does not exist.

**ERROR 338: error in reading from climate file xxx.**

- SWMM was trying to read data from an external climate file with the wrong format.

**ERROR 339: attempt to read beyond end of climate file xxx.**

- The specified external climate does not include data for the period of time being simulated.

**ERROR 341: cannot open scratch RDII interface file.**

- SWMM could not open the temporary file it uses to store RDII flow data.

**ERROR 343: cannot open RDII interface file xxx.**

- An RDII interface file could not be opened, possibly because it does not exist or because the user does not have write privileges to its directory.

**ERROR 345: invalid format for RDII interface file.**

- SWMM was trying to read data from a designated RDII interface file with the wrong format (i.e., it may have been created for some other project or actually be some other type of file).

**ERROR 351: cannot open routing interface file xxx.**

- A routing interface file could not be opened, possibly because it does not exist or because the user does not have write privileges to its directory.

**ERROR 353: invalid format for routing interface file xxx.**

- SWMM was trying to read data from a designated routing interface file with the wrong format (i.e., it may have been created for some other project or actually be some other type of file).

**ERROR 355: mismatched names in routing interface file xxx.**

- The names of pollutants found in a designated routing interface file do not match the names used in the current project.

**ERROR 357: inflows and outflows interface files have same name.**

-In cases where a run uses one routing interface file to provide inflows for a set of locations and another to save outflow results, the two files cannot both have the same name.

\*\* ERROR 361: could not open external file used for Time Series xxx.

- The external file used to provide data for the named time series could not be opened, most likely because it does not exist.

**ERROR 363: invalid data in external file used for used for Time Series xxx.**

- The external file used to provide data for the named time series has one or more lines with the wrong format.

### Warning Messages

**WARNING 01: wet weather time step reduced to recording interval for Rain Gage xxx.**

- The wet weather time step was automatically reduced so that no period with rainfall would be skipped during a simulation.

**WARNING 02: maximum depth increased for Node xxx.**

- The maximum depth for the node was automatically increased to match the top of the highest connecting conduit.

**WARNING 03: negative offset ignored for Link xxx.**

- The link's stipulated offset was below the connecting node's invert so its actual offset was set to 0.

**WARNING 04: minimum elevation drop used for Conduit xxx.**

- The elevation drop between the end nodes of the conduit was below 0.001 ft (0.00035 m) so the latter value was used instead to calculate its slope.

**WARNING 05: minimum slope used for Conduit xxx.**

- The conduit's computed slope was below the user-specified Minimum Conduit Slope so the latter value was used instead.

**WARNING 06: dry weather time step increased to wet weather time step.**

- The user-specified time step for computing runoff during dry weather periods was lower than that set for wet weather periods and was automatically increased to the wet weather value.

**WARNING 07: routing time step reduced to wet weather time step.**

- The user-specified time step for flow routing was larger than the wet weather runoff time step and was automatically reduced to the runoff time step to prevent loss of accuracy.

**WARNING 08: elevation drop exceeds length for Conduit xxx.**

- The elevation drop across the ends of a conduit exceeds its length. The program computes the conduit's slope as the elevation drop divided by the length instead of using the more accurate right triangle method. The user should check for errors in the length and in both the invert elevations and offsets at the conduit's upstream and downstream nodes.

**WARNING 09: time series interval greater than recording interval for Rain Gage xxx.**

- The smallest time interval between entries in the precipitation time series used by the rain gage is greater than the recording time interval specified for the gage. If this was not actually intended then what appear to be continuous periods of rainfall in the time series will instead be read with time gaps in between them.

**WARNING 10a: crest elevation is below downstream invert for regulator Link xxx.**

- For Kinematic Wave or Steady Flow routing, the height of the opening on an orifice, weir, or outlet at a storage node is below the invert elevation of its downstream node. Users should check to see if the regulator's offset height or the downstream node's invert elevation is in error.

**WARNING 10b: crest elevation raised to downstream invert for regulator
Link xxx.**

- For Dynamic Wave routing, the height of the opening on an orifice, weir or outlet will be raised to the invert elevation of its downstream node if necessary.

**WARNING 11: non-matching attributes in Control Rule xxx.**

- A control rule's premise is comparing two different types of attributes to one another (for example, conduit flow and junction water depth).
