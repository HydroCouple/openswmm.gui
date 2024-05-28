# EPA SWMM 5.2 User Guide

## Introducing EPA SWMM {#introducing_epa_swmm}

The EPA Storm Water Management Model (SWMM) is a dynamic rainfall-runoff simulation model used for single event or long-term (continuous) simulation of runoff quantity and quality from primarily urban areas. The runoff component of SWMM operates on a collection of subcatchment areas that receive precipitation and generate runoff and pollutant loads. The routing portion of SWMM transports this runoff through a system of pipes, channels, storage/treatment devices, pumps, and regulators. SWMM tracks the quantity and quality of runoff generated within each subcatchment, and the flow rate, flow depth, and quality of water in each pipe and channel during a simulation period comprised of multiple time steps.

![UrbanWetWeatherFlows](urbanwetweatherflows.png)

SWMM was first developed in 1971 and has undergone several major upgrades since then. It continues to be widely used throughout the world for planning, analysis and design related to storm water runoff, combined sewers, sanitary sewers, and other drainage systems in urban areas, with many applications in non-urban areas as well. The current edition, Version 5, is a complete re-write of the previous release. Running under Windows, SWMM 5 provides an integrated environment for editing study area input data, running hydrologic, hydraulic and water quality simulations, and viewing the results in a variety of formats. These include color-coded drainage area and conveyance system maps, time series graphs and tables, profile plots, and statistical frequency analyses.

### Hydrologic Modeling Features {#hydrologic_modeling_features}

SWMM accounts for various hydrologic processes that produce runoff from urban areas. These include:

- time-varying rainfall

- evaporation of standing surface water

- snow accumulation and melting

- rainfall interception from depression storage

- infiltration of rainfall into unsaturated soil layers

- percolation of infiltrated water into groundwater layers

- interflow between groundwater and the drainage system

- nonlinear reservoir routing of overland flow

- capture and retention of rainfall/runoff with various types of low impact development (LID) practices.

Spatial variability in all of these processes is achieved by dividing a study area into a collection of smaller, homogeneous subcatchment areas, each containing its own fraction of pervious and impervious sub-areas. Overland flow can be routed between sub-areas, between subcatchments, or between entry points of a drainage system.

### Hydraulic Modeling Features {#hydraulic_modeling_features}

SWMM also contains a flexible set of hydraulic modeling capabilities used to route runoff and external inflows through a drainage system network of pipes, channels, storage/treatment units and diversion structures. These include the ability to:

- handle networks of unlimited size

- use a wide variety of standard closed and open conduit shapes as well as natural channels

- model special elements such as storage/treatment units, curb and gutter inlets, flow dividers, pumps, weirs, and orifices

- apply external flows and water quality inputs from surface runoff, groundwater interflow, rainfall-dependent infiltration/inflow, dry weather sanitary flow, and user-defined inflows

- utilize either kinematic wave or full dynamic wave flow routing methods

- model various flow regimes, such as backwater, surcharging, reverse flow, and surface ponding

- apply user-defined dynamic control rules to simulate the operation of pumps, orifice openings, and weir crest levels.

### Water Quality Modeling Features {#water_quality_modeling_features}

In addition to modeling the generation and transport of runoff flows, SWMM can also estimate the production of pollutant loads associated with this runoff. The following processes can be modeled for any number of user-defined water quality constituents:

- dry-weather pollutant buildup over different land uses

- pollutant washoff from specific land uses during storm events

- direct contribution of rainfall deposition

- reduction in dry-weather buildup due to street cleaning

- reduction in washoff load due to BMPs

- entry of dry weather sanitary flows and user-specified external inflows at any point in the drainage system

- routing of water quality constituents through the drainage system

- reduction in constituent concentration through treatment in storage units or by natural processes in pipes and channels.

### Typical Applications of SWMM {#typical_applications_of_swmm}

Since its inception, SWMM has been used in thousands of sewer and stormwater studies throughout the world. Typical applications include:

- design and sizing of drainage system components for flood control

- sizing of detention facilities and their appurtenances for flood control and water quality protection

- flood plain mapping of natural channel systems

- designing control strategies for minimizing combined sewer overflows

- evaluating the impact of inflow and infiltration on sanitary sewer overflows

- generating non-point source pollutant loadings for waste load allocation studies

- evaluating the effectiveness of BMPs for reducing wet weather pollutant loadings.

### Steps in Using SWMM {#steps_in_using_swmm}

One typically carries out the following steps when using EPA SWMM to model a study area:

1.  Specify a default set of options and object properties to use (see [Setting Project Defaults](#setting_project_defaults)).

2.  Draw a network representation of the physical components of the study area (see [Adding Objects](#adding_objects)).

3.  Edit the properties of the objects that make up the system (see [Editing Objects](#editing_objects)).

4.  Select a set of analysis options (see Setting [Analysis Options](#analysis_options)).

5.  Run a simulation (see [Initiating a Run](#initiating_a_run)).

6.  View the results of the simulation (see [Viewing Results](#viewing_results)).

For larger systems it will be more convenient to replace Step 2 by collecting study area data from various sources, such as CAD drawings or GIS files, and transferring these data into a SWMM input file whose format is described in the SWMM 5 User's Manual.

### What's New in Release 5.2.4 {#whats_new}

This is a maintenance release that addresses the following issues:

- Inconsistent reporting of Surface Runoff and Wet Weather Inflow pollutant mass in the Status Report.

- Missing limits on water flux rates between layers in several types of LID units.

- Incorrect geometry calculations for Street cross-sections with depressed gutters.

- Questionable behavior of conduit evaporation and seepage losses.

- Flickering of the Study Area Map when panning.

- Access Violation error when attempting to edit the vertices of a Storage Node polygon.

- Max/Full Depth values for orifices and weirs in the wrong column of the Summary Results Link Flow table.

- "Scrollbar property out of range" error for models with extremely long simulation periods.

Please consult the [SWMM 5 Updates and Bug Fixes](https://www.epa.gov/water-research/storm-water-management-model-swmm) file for a complete listing of all program updates.

## SWMM's Conceptual Model {#swmms_coneptual_model}

SWMM conceptualizes a drainage system as a series of water and material flows between several major environmental compartments. These compartments and the SWMM objects they contain include:

- The Atmospheric compartment, which generates precipitation and deposits pollutants onto the land surface compartment. SWMM uses Rain Gage objects to represent rainfall inputs to the system.

- The Land Surface compartment, which is represented through one or more Subcatchment objects. It receives precipitation from the Atmospheric compartment in the form of rain or snow; it sends outflow in the form of infiltration to the Groundwater compartment and also as surface runoff and pollutant loadings to the Transport compartment.

- The Groundwater compartment receives infiltration from the Land Surface compartment and transfers a portion of this inflow to the Transport compartment. This compartment is modeled using Aquifer objects.

- The Transport compartment contains a network of conveyance elements (channels, pipes, pumps, and regulators) and storage/treatment units that transport water to outfalls or to treatment facilities. Inflows to this compartment can come from surface runoff, groundwater interflow, sanitary dry weather flow, or from user-defined hydrographs. The components of the Transport compartment are modeled with Node and Link objects.

Not all compartments need appear in a particular SWMM model. For example, one could model just the transport compartment, using pre-defined hydrographs as inputs.

### Visual Objects {#visual_objects}

The figure below depicts how a collection of SWMM's visual objects might be arranged together to represent a stormwater drainage system. These objects can be displayed on a map in the SWMM workspace. Click on the name of any object to view its description.

![VisualObjects](visualobjects.gif)

#### Rain Gages {#rain_gages}

**Rain Gages** supply precipitation data for one or more subcatchment areas in a study region. The rainfall data can be either a user-defined time series or come from an external file. Several different popular rainfall file formats currently in use are supported, as well as a standard user-defined format.

The principal input properties of rain gages include:

- rainfall data type (e.g., intensity, volume, or cumulative volume)

- recording time interval (e.g., hourly, 15-minute, etc.)

- source of rainfall data (input time series or external file)

- name of rainfall data source

_See Also_

[Rain Gage Properties](#rain_gage_properties)

[Rainfall Files](#rainfall_files)

#### Subcatchments {#subcatchments}

**Subcatchments** are hydrologic units of land whose topography and drainage system elements direct surface runoff to a single discharge point. The user is responsible for dividing a study area into an appropriate number of subcatchments, and for identifying the outlet point of each subcatchment. Discharge outlet points can be either nodes of the drainage system or other subcatchments.

Subcatchments are divided into pervious and impervious subareas. Surface runoff can infiltrate into the upper soil zone of the pervious subarea, but not through the impervious subarea. Impervious areas are themselves divided into two subareas - one that contains depression storage and another that does not. Runoff flow from one subarea in a subcatchment can be routed to the other subarea, or both subareas can drain to the subcatchment outlet.

Infiltration of rainfall from the pervious area of a subcatchment into the unsaturated upper soil zone can be described using five different models:

- Classic Horton infiltration

- Modified Horton infiltration

- Green-Ampt infiltration

- Modified Green-Ampt Infiltration

- SCS Curve Number infiltration

To model the accumulation, re-distribution, and melting of precipitation that falls as snow on a subcatchment, it must be assigned a [Snow Pack](#snow_packs) object. To model groundwater flow between an [aquifer](#aquifers) underneath the subcatchment and a [node](#nodes) of the drainage system, the subcatchment must be assigned a set of Groundwater parameters. Pollutant buildup and washoff from subcatchments are associated with the [Land Uses](#land_uses) assigned to the subcatchment. Capture and retention of rainfall/runoff using different types of low impact development practices (such as bio-retention cells, infiltration trenches, porous pavement, vegetative swales, and rain barrels) can be modeled by assigning a set of pre-designed [LID controls](#lid_controls) to the subcatchment.

The other principal input parameters for subcatchments include:

- assigned rain gage

- outlet node or subcatchment

- total area

- percent impervious area

- average slope

- characteristic width of overland flow

- Manning's roughness (n) for overland flow on both pervious and impervious areas

- depression storage in both pervious and impervious areas

- percent of impervious area with no depression storage.

_See Also_

[Subcatchment Properties](#subcatchment_properties)

[Infiltration](#infiltration)

[Land Uses](#land_uses)

[LID Controls](#lid_controls)

[Aquifers](#aquifers)

[Snow Packs](#snow_packs)

#### Nodes {#nodes}

Nodes are points of a conveyance system that connect conveyance links together. There are several different categories of nodes that can be employed:

- [Junctions](#junctions)

- [Outfalls](#outfalls)

- [Flow Dividers](#flow_dividers)

- [Storage Units](#storage_units)

Nodes are also the points where [external inflows](#external_inflows) can enter a drainage system and where removal of pollutants through [treatment](#treatment) can occur.

##### Junctions {#junctions}

**Junctions** are drainage system nodes where links join together. Physically they can represent the confluence of natural surface channels, manholes in a sewer system, or pipe connection fittings. External inflows can enter the system at junctions. Excess water at a junction can become partially pressurized while connecting conduits are surcharged and can either be lost from the system or be allowed to pond atop the junction and subsequently drain back into the junction.

The principal input parameters for a junction are:

- invert (channel or manhole bottom) elevation

- height to ground surface

- ponded surface area when flooded (optional)

- external inflow data (optional).

See Also

[Junction Properties](#junctions_properties)

##### Outfalls {#outfalls}

**Outfalls** are terminal nodes of the drainage system used to define final downstream boundaries under Dynamic Wave flow routing. For other types of flow routing they behave as a junction. Only a single link can be connected to an outfall node, and the option exists to have the outfall discharge onto a subcatchment's surface.

The boundary conditions at an outfall can be described by any one of the following stage relationships:

- the critical or normal flow depth in the connecting conduit

- a fixed stage elevation

- a tidal stage described in a table of tide height versus hour of the day

- a user-defined time series of stage versus time.

The principal input parameters for outfalls include:

- invert elevation

- boundary condition type and stage description

- presence of a flap gate to prevent backflow through the outfall.

_See Also_

[Outfall Properties](#outfall_properties)

##### Flow Dividers {#flow_dividers}

**Flow Dividers** are drainage system nodes that divert inflows to a specific conduit in a prescribed manner. A flow divider can have no more than two conduit links on its discharge side. Flow dividers are only active under Steady Flow and Kinematic Wave routing and are treated as simple junctions under Dynamic Wave routing.

There are four types of flow dividers, defined by the manner in which inflows are diverted:

- **Cutoff** (diverts all inflow above a defined cutoff value)

- **Overflow** (diverts all inflow above the flow capacity of the non-diverted conduit)

- **Tabular** (uses a table that expresses diverted flow as a function of total inflow)

- **Weir** (treats diverted flow as linearly proportional to the inflow above a defined cutoff value).

The principal input parameters for a flow divider are:

- junction parameters (see [Junctions](#junctions))

- name of the link receiving the diverted flow

- method used for computing the amount of diverted flow.

See Also

[Divider Properties](#divider_properties)

##### Storage Units {#storage_units}

**Storage Units** are drainage system nodes that provide storage volume. Physically they could represent storage facilities as small as a catch basin or as large as a lake. The volumetric properties of a storage unit are described by a function or table of surface area versus height. In addition to receiving inflows and discharging outflows to other nodes in the drainage network, storage nodes can also lose water from surface evaporation and from seepage into native soil.

The principal input parameters for storage units include:

- invert (bottom) elevation

- maximum depth

- depth-surface area data

- evaporation potential

- seepage parameters (optional)

- external inflow data (optional).

See Also

[Storage Properties](#storage_properties)

#### Links {#links}

Links are the conveyence components of a drainage system and always lie between a pair of nodes.

Types of links include:

- [Conduits](#conduits)

- [Pumps](#pumps)

- [Regulators](#regulators)

##### Conduits {#conduits}

**Conduits** are pipes or channels that move water from one node to another in the conveyance system. Their cross-sectional shapes can be selected from a variety of standard open and closed geometries. Irregular natural cross-section shapes are also supported, as are user-defined closed shapes.

The principal input parameters for conduits are:

- names of the inlet and outlet nodes

- offset height or elevation of the conduit above the inlet and outlet node inverts

- conduit length

- Manning's roughness (n)

- cross-sectional geometry

- entrance/exit losses (optional)

- seepage rate (optional)

- presence of a flap gate to prevent reverse flow (optional)

- culvert type code number if the conduit acts as a culvert (optional)

- name of any inlet structure placed in a street or channel conduit (optional)

Conduits designated as culverts are checked continuously during dynamic wave flow routing to see if they operate under Inlet Control as defined in the Federal Highway Administration's publication Hydraulic Design of Highway Culverts (Publication No. FHWA-NHI-01-020, May 2005).

Street and channel conduits with inlet structures use the methods described in the Federal Highway Administration's publication Urban Drainage Design Manual - HEC-22 (Publication No. FHWA-NHI-10-009, August 2013) to determine the amount of flow they capture.

_See Also_

[Conduit Properties](#conduit_properties)

[Cross-Section Editor](#cross_section_editor)

[Culvert Code Numbers](#culvert_code_numbers)

[Inlets](#inlets)

##### Pumps {#pumps}

**Pumps** are links used to lift water to higher elevations. A pump curve describes the relation between a pump's flow rate and conditions at its inlet and outlet nodes. Five different types of pumps are supported:

|                                                                                                                                                                                                                                                                 |                     |
| :-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | :------------------ |
| **Type1** <br> An off-line pump with <br> a wet well where flow <br> increases incrementally <br> with available wet well <br> volume.                                                                                                                          | ![Pump1](pump1.gif) |
| **Type2** <br> An in-line pump <br> where flow increases <br> incrementally with <br> inlet node depth.                                                                                                                                                         | ![Pump2](pump2.gif) |
| **Type3** <br> An in-line pump <br> where flow varies <br> continuously with <br> head difference <br> between the inlet <br> and outlet nodes.                                                                                                                 | ![Pump3](pump3.gif) |
| **Type4** <br> A variable speed <br> in-line pump where flow <br> varies continuously <br> with inlet node depth.                                                                                                                                               | ![pump4](pump4.gif) |
| **Type5** <br> A variable speed <br> version of the Type3 <br> pump where the head <br> v. flow curve shifts <br> position depending on <br> the pump's speed <br> setting.                                                                                     | ![Pump5](pump5.gif) |
| **Ideal** <br> An "ideal" transfer <br> pump whose flow rate <br> equals the inflow rate at <br> its inlet node. No curve is <br> required. The pump must <br> be the only outflow <br> link from its inlet <br> node. Used mainly for <br> preliminary design. |                     |

The on/off status of pumps can be controlled dynamically by specifying startup and shutoff water depths at the inlet node or through user-defined [Control Rules](#control_rules). Rules can also be used to simulate variable speed drives that modulate pump flow.

The principal input parameters for a pump include:

- names of the inlet and outlet nodes

- name of its pump curve (or \* for an Ideal pump)

- initial on/off status

- startup and shutoff depths (optional).

_See Also_

[Pump Properties](#pump_properties)

[Control Rules](#control_rules)

#### Flow Regulators {#flow-regulators}

Flow Regulators are structures or devices used to control and divert flows within a conveyance system. They are typically used to:

- control releases from storage facilities

- prevent unacceptable surcharging

- divert flow to treatment facilities and interceptors.

SWMM can model the following types of flow regulators:

- [Orifices](#orifices)

- [Weirs](#weirs)

- [Outlets](#outlets)

##### Orifices {#orifices}

**Orifices** are used to model outlet and diversion structures in drainage systems which are typically openings in the wall of a manhole, storage facility, or control gate. They are internally represented in SWMM as a link connecting two nodes. An orifice can have either a circular or rectangular shape, be located either at the bottom or along the side of the upstream node, and have a flap gate to prevent backflow.

Orifices can be used as storage unit outlets under all types of flow routing. If not attached to a storage unit node, they can only be used in drainage networks that are analyzed with [Dynamic Wave](#dynamic_wave) flow routing.

The flow through an orifice is computed based on the area of its opening, its discharge coefficient, and the head difference across the orifice.

The height of an orifice's opening can be controlled dynamically through user-defined [Control Rules](#control_rules). This feature can be used to model gate openings and closings.

The principal input parameters for an orifice include:

- names of its inlet and outlet nodes

- configuration (bottom or side)

- shape (circular or rectangular)

- height above the inlet node invert

- discharge coefficient

- time to open or close (optional).

_See Also_

[Orifice Properties](#orifice_properties)

[Control Rules](#control_rules)

##### Weirs {#weirs}

**Weirs**, like orifices, are used to model outlet and diversion structures in a drainage system. Weirs are typically located in a manhole, along the side of a channel, or within a storage unit. They are internally represented in SWMM as a link connecting two nodes, where the weir itself is placed at the upstream node. A flap gate can be included to prevent backflow.

Five varieties of weirs are available, each incorporating a different formula for computing flow as a function of area, discharge coefficient and head difference across the weir:

- **Transverse** (rectangular shape)

- **Side flow** (rectangular shape)

- **V-notch** (triangular shape)

- **Trapezoidal** (trapezoidal shape)

- **Roadway** (broad crested rectangular weir used to model roadway crossings).

Weirs can be used as storage unit outlets under all types of flow routing. If not attached to a storage unit, they can only be used in drainage networks that are analyzed with [Dynamic Wave](dynamic_wave) flow routing.

The height of the weir crest above the inlet node invert can be controlled dynamically through user-defined [Control Rules](control_rules). This feature can be used to model inflatable dams.

Weirs can either be allowed to surcharge or not. A surcharged weir will use an equivalent orifice equation to compute the flow through it. Weirs placed in open channels would normally not be allowed to surcharge while those placed in closed diversion structures or those used to represent storm drain inlet openings would be allowed to.

The principal input parameters for a weir include:

- names of its inlet and outlet nodes

- shape and geometry

- crest height above the inlet node invert

- discharge coefficient.

_See Also_

[Weir Properties](#weir_properties)

[Control Rules](#control_rules)

##### Outlets {#outlets}

**Outlets** are flow control devices that are typically used to control outflows from storage units. They are used to model special head-discharge relationships that cannot be characterized by pumps, orifices, or weirs. Outlets are internally represented in SWMM as a link connecting two nodes. An outlet can also have a flap gate that restricts flow to only one direction.

Outlets attached to storage units are active under all types of flow routing. If not attached to a storage unit, they can only be used in drainage networks analyzed with [Dynamic Wave](#dynamic_wave) flow routing.

A user-defined rating curve determines an outlet's discharge flow as a function of either the freeboard depth above the outlet's opening or the head difference across it. [Control Rules](#control_rules) can be used to dynamically adjust this flow when certain conditions exist.

The principal input parameters for an outlet include:

- names of its inlet and outlet nodes

- height above the inlet node invert

- function or table containing its head (or depth) - discharge relationship.

_See Also_

[Outlet Properties](#outlet_properties)

[Control Rules](#control_rules)

#### Map Labels {#map_labels}

**Map Labels** are optional text labels added to SWMM's Study Area Map to help identify particular objects or regions of the map. The labels can be drawn in any Windows font, freely edited and be dragged to any position on the map.

_See Also_

[Map Label Properties](#map_label_properties)

### Non-visual Objects {#nonvisual_objects}

In addition to physical objects that can be displayed visually on a map, SWMM utilizes several classes of non-visual data objects to describe additional characteristics and processes within a study area.

- [Climatology Data](#climatology)

- [Hydrology Data](#hydrology)

- [Hydraulic Data](#hydraulics)

- [Water Quality Data](#water_quality)

- [Tabular Data](#tabular_data)

#### Climatology {#climatology}

The Climatology object in EPA SWMM describes the following climate-related variables used for computing runoff and snowmelt:

- [Temperature](#temperature)

- [Evaporation](#evaporation)

- [Wind Speed](#wind_speed)

- [Snowmelt](#snowmelt)

- [Areal Depletion](#areal_depletion)

- [Climate Adjustments](#climate_adjustments)

##### Temperature {#temperature}

Air temperature data are used when simulating snowfall and snowmelt processes during runoff calculations. They are also needed if the option to base evaporation rates on temperature is selected. If these processes are not being simulated then temperature data are not required. Air temperature data can be supplied to SWMM from one of the following sources:

- a user-defined time series of point values (values at intermediate times are interpolated)

- an external climate file containing daily minimum and maximum values (SWMM fits a sinusoidal curve through these values depending on the day of the year).

For user-defined time series, temperatures are in degrees F for US units and degrees C for metric units. The external climate file can also be used to supply evaporation and wind speed as well.

_See Also_

[Climate Files](#climate_files)

[Climatology Editor](#climatology_editor)

##### Evaporation {#evaporation}

Evaporation can occur for standing water on subcatchment surfaces, for subsurface water in groundwater aquifers, for water traveling through open channels, and for water held in storage units. Evaporation rates can be stated as:

- a single constant value

- a set of monthly average values

- a user-defined time series of values

- values computed from the daily temperatures contained in an external climate file

- daily values read from an external climate file.

These values represent potential rates. The actual amount of water evaporated will depend on the amount available.

If rates are read directly from a climate file, then a set of monthly pan coefficients should also be supplied to convert the pan evaporation data to free water-surface values. An option is also available to allow evaporation only during periods with no precipitation.

_See Also_

[Climate Files](#climate_files)

[Climatology Editor](#climatology_editor)

##### Wind Speed {#wind_speed}

Wind speed is an optional climatic variable that is only used for snowmelt calculations.  SWMM can use either a set of monthly average speeds or wind speed data contained in the same climate file used for daily minimum/maximum temperatures.

_See Also_

[Climatology Editor](#climatology_editor)

##### Snowmelt {#snowmelt}

Snowmelt parameters are climatic variables that apply across the entire study area when simulating snowfall and snowmelt. They include:

- the air temperature at which precipitation falls as snow

- heat exchange properties of the snow surface

- study area elevation, latitude, and longitude correction.

_See Also_

[Climatology Editor](#climatology_editor)

##### Areal Depletion {#areal_depletion}

Areal depletion refers to the tendency of accumulated snow to melt non-uniformly over the surface of a subcatchment. As the melting process proceeds, the area covered by snow gets reduced. This behavior is described by an Areal Depletion Curve that plots the fraction of total area that remains snow covered against the ratio of the actual snow depth to the depth at which there is 100% snow cover. A typical ADC for a natural area is shown below.

![ArealDepletion](arealdepletion.png)

Two such curves can be supplied to SWMM, one for impervious areas and another for pervious areas.

_See Also_

[Climatology Editor](#climatology_editor)

##### Climate Adjustments {#climate_adjustments}

Climate adjustments are optional modifications applied to the temperature, evaporation rate, and rainfall intensity that SWMM would otherwise use at each time step of a simulation. Separate sets of adjustments that vary periodically by month of the year can be assigned to these variables. They provide a simple way to examine the effects of future climate change without having to modify the original climatic time series.

In a similar manner, a set of monthly adjustments can be applied to the hydraulic conductivity used in computing rainfall infiltration on all pervious land surfaces, including those in all LID units, and exfiltration from all storage nodes and conduits. These can reflect the increase of hydraulic conductivity with increasing temperature or the effect that seasonal changes in land surface conditions, such as frozen ground, can have on infiltration capacity. They can be overridden for individual subcatchments (and their LID units) by assigning a monthly infiltration adjustment [Time Pattern](#time_pattern) to a subcatchment. Monthly adjustment time patterns for  depression storage and pervious surface roughness coefficient (Mannings n) can also be specified for individual subcatchments (see [Subcatchment Properties](#subcatchment_properties)).

#### Hydrology {#hydrology}

Aside from rain gages and subcatchments, the following hydrology-related objects are used by SWMM:

- [Aquifers](#aquifers)

- [Snow Packs](#snow_packs)

- [Unit Hydrographs](#unit_hydrographs)

- [LID Controls](#lid_controls)

##### Snow Packs {#snow_packs}

**Snow Pack** objects contain parameters that characterize the buildup, removal, and melting of snow over three types of sub-areas within a subcatchment:

- The **Plowable** snow pack area consists of a user-defined fraction of the total impervious area. It is meant to represent such areas as streets and parking lots where plowing and snow removal can be done.

- The **Impervious** snow pack area covers the remaining impervious area of a subcatchment.

- The **Pervious** snow pack area encompasses the entire pervious area of a subcatchment.

Each of these three areas is characterized by the following parameters:

- minimum and maximum snow melt coefficients

- minimum air temperature for snow melt to occur

- snow depth above which 100% areal coverage occurs

- initial snow depth

- initial and maximum free water content in the pack.

In addition, a set of snow removal parameters can be assigned to the Plowable area. These parameters consist of the depth at which snow removal begins and the fractions of snow moved onto various other areas.

Subcatchments are assigned a snow pack object through their Snow Pack property. A single snow pack object can be applied to any number of subcatchments. Assigning a snow pack to a subcatchment simply establishes the melt parameters and initial snow conditions for that subcatchment. Internally, SWMM creates a "physical" snow pack for each subcatchment, which tracks snow accumulation and melting for that particular subcatchment based on its snow pack parameters, its amount of pervious and impervious area, and the precipitation history it sees.

_See Also_

[Snow Pack Editor](#snow_pack_editor)

##### Aquifers {#aquifers}

**Aquifers** are sub-surface groundwater zones used to model the vertical movement of water infiltrating from the subcatchments that lie above them. They also permit the infiltration of groundwater into the drainage system, or exfiltration of surface water from the drainage system, depending on the hydraulic gradient that exists. Aquifers are only required in models that need to explicitly account for the exchange of groundwater with the drainage system or to establish baseflow and recession curves in natural channels and non-urban systems.

The parameters of an aquifer object can be shared by several subcatchments but there is no exchange of groundwater between subcatchments. A drainage system node can exchange groundwater with more than one subcatchment.

Aquifers are represented using two zones -- an un-saturated zone and a saturated zone. Their behavior is characterized using such parameters as soil porosity, hydraulic conductivity, evapotranspiration depth, bottom elevation, and loss rate to deep groundwater. In addition, the initial water table elevation and initial moisture content of the unsaturated zone must be supplied.

Aquifers are connected to subcatchments and to drainage system nodes as defined in a subcatchment's Groundwater Flow property. This property also contains parameters that govern the rate of groundwater flow between the aquifer's saturated zone and the drainage system node.

_See Also_

[Aquifer Editor](#aquifer_editor)

[Groundwater Flow Editor](#groundwater_flow_editor)

##### Unit Hydrographs {#unit_hydrographs}

**Unit Hydrographs** (UHs) estimate rainfall-dependent inflow/infiltration (RDII) into a sewer system. A UH set contains up to three such hydrographs, one for a short-term response, one for an intermediate-term response, and one for a long-term response. A UH group can have up to 12 UH sets, one for each month of the year. Each UH group is considered as a separate object by SWMM, and is assigned its own unique name along with the name of the rain gage that supplies rainfall data to it.

Each unit hydrograph is defined by three parameters:

- **R**: the fraction of rainfall volume that enters the sewer system

- **T**: the time from the onset of rainfall to the peak of the UH in hours

- **K**: the ratio of time to recession of the UH to the time to peak

![UnitHydrograph](unithydrograph.png)

A unit hydrograph can also have a set of Initial Abstraction (IA) parameters associated with it. These determine how much rainfall is lost to interception and depression storage before any excess rainfall is generated and transformed into RDII flow by the hydrograph.

To generate RDII into a drainage system node, the node must identify (through its Inflows property) the UH group and the area of the surrounding sewershed that contributes RDII flow.

[!tip]
An alternative to using unit hydrographs to define RDII flow is to create an external [RDII interface file](#rdii_interface_file), which contains RDII time series data.

[!tip]
Unit hydrographs could also be used to replace SWMM's main rainfall-runoff process that uses Subcatchment objects, provided that properly calibrated UHs are utilized. In this case what SWMM calls RDII inflow to a node would actually represent overland runoff.

_See Also_

[Inflows](#inflows)

[RDII Inflow Editor](#rdii_inflow_editor)

[Unit Hydrograph Editor](#unit_hydrograph_editor)

##### LID Controls {#lid_controls}

**LID Controls** are low impact development practices designed to capture surface runoff and provide some combination of detention, infiltration, and evapotranspiration to it. They are considered as properties of a given subcatchment, similar to how Aquifers and Snow Packs are treated. SWMM can explicitly model the following generic types of LID controls:

|                                   |                                                                                                                                                                                                                                                                    |
| :-------------------------------- | :----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| ![](streetplanter.zoom105.gif)    | _Bio-retention Cells_ are depressions that contain vegetation grown in an engineered soil mixture placed above a gravel drainage bed. They provide storage, infiltration and evaporation of both direct rainfall and runoff captured from surrounding areas.       |
| ![](raingarden.zoom45.gif)        | _Rain Gardens_ are a type of bio-retention cell consisting of just the engineered soil layer with no gravel bed below it.                                                                                                                                          |
| ![](greenroof.zoom54.gif)         | _Green Roofs_ are another variation of a bio-retention cell that have a soil layer laying atop a special drainage mat material that conveys excess percolated rainfall off of the roof.                                                                            |
| ![](infiltrench.zoom60.gif)       | _Infiltration Trenches_ are narrow ditches filled with gravel that intercept runoff from upslope impervious areas. They provide storage volume and additional time for captured runoff to infiltrate the native soil below.                                        |
| ![](permeablepavement.zoom50.gif) | _Continuous Permeable Pavement_ systems are excavated areas filled with gravel and paved over with a porous concrete or asphalt mix. _Block Paver_ systems consist of impervious paver blocks placed on a sand or pea gravel bed with a gravel storage layer below |
| ![](cistern.zoom57.gif)           | _Rain Barrels_ (or _Cisterns_) are containers that collect roof runoff during storm events and can either release or re-use the rainwater during dry periods.                                                                                                      |
| ![](downspout.zoom48.gif)         | _Rooftop Disconnection_ has downspouts discharge to pervious landscaped areas and lawns instead of directly into storm drains. It can also model roofs with directly connected drains that overflow onto pervious areas.                                           |
| ![](vegswale.zoom57.gif)          | _Vegetative Swales_ are channels or depressed areas with sloping sides covered with grass and other vegetation. They slow down the conveyance of collected runoff and allow it more time to infiltrate the native soil beneath it.                                 |

Bio-retention cells, infiltration trenches, and permeable pavement systems can contain optional drain systems in their gravel storage beds to convey excess captured runoff off of the site and prevent the unit from flooding. They can also have an impermeable floor or liner that prevents any infiltration into the native soil from occurring. Infiltration trenches and permeable pavement systems can also be subjected to a decrease in hydraulic conductivity over time due to clogging.

LID units that contain drains can have a removal percentage assigned to each pollutant discharged through the drain. LID's will also provide a reduction in pollutant mass load conveyed in their surface discharge due to the reduction in runoff flow volume they provide.

For more details on using LID controls within SWMM see the rollowing topics:

- [LID Representation](#lid_representation)

- [LID Utilization](#lid_utilization)

- [LID Placement](#lid_placement)

- [LID Results](#lid_results)

###### LID Representation {#lid_representation}

LID controls are represented by a combination of vertical layers whose properties are defined on a per-unit-area basis. This allows LIDs of the same design but differing areal coverage to easily be placed within different subcatchments in a study area.

During a simulation SWMM performs a moisture balance that keeps track of how much water moves between and is stored within each LID layer. As an example, the layers used to model a bio-retention cell and the flow pathways between them are shown below:

[BioRetentionCell]

The following table indicates which combination of layers applies to each type of LID (x means required, o means optional):

+---------+---------+---------+---------+---------+---------+---------+
| LID | Surface | P | Soil | Storage | Drain | D |
| Type | | avement | | | | rainage |
| | | | | | | Mat |
+---------+---------+---------+---------+---------+---------+---------+
| Bio-Re | x | | x | x | o |   |
| tention | | | | | | |
| Cell | | | | | | |
+---------+---------+---------+---------+---------+---------+---------+
| Rain | x | | x |   |   |   |
| Garden | | | | | | |
+---------+---------+---------+---------+---------+---------+---------+
| Green | x | | x |   |   | x |
| Roof | | | | | | |
+---------+---------+---------+---------+---------+---------+---------+
| Infil | x |   | | x | o |   |
| tration | | | | | | |
| Trench | | | | | | |
+---------+---------+---------+---------+---------+---------+---------+
| Pe | x | x | o | x | o |   |
| rmeable | | | | | | |
| P | | | | | | |
| avement | | | | | | |
+---------+---------+---------+---------+---------+---------+---------+
| Rain | | | | x | x |   |
| Barrel | | | | | | |
+---------+---------+---------+---------+---------+---------+---------+
| Rooftop | x | | | | x | |
| Discon | | | | | | |
| nection | | | | | | |
+---------+---------+---------+---------+---------+---------+---------+
| Veg | x | | | | | |
| etative | | | | | | |
| Swale | | | | | | |
+---------+---------+---------+---------+---------+---------+---------+

When a user adds a specific type of LID control object to a SWMM project the LID Control Editor is used to set the design properties of each relevant layer (such as thickness, void volume, hydraulic conductivity, drain characteristics, etc.). These LID objects can then be placed within selected subcatchments at any desired sizing (or areal coverage) by editing the subcatchment's LID Controls property.

###### LID Utilization {#lid_utilization}

Utilizing LID controls within a SWMM project is a two phase process
that:

1.  creates a set of scale-independent LID controls that can be deployed throughout the study area,

2.  assigns any desired mix and sizing of these controls to selected subcatchments.

Bear in mind that when LIDs are added to a subcatchment, the subcatchment's Area property is the total area of the subcatchment (both non-LID and LID portions) while the Percent Imperviousness and Width parameters apply only to the non-LID portion of the subcatchment.

To implement the first phase, one selects the Hydrology | LID Controls category from the Project Browser to add, edit or delete individual LID control objects. The LID Control Editor is used to edit the properties of the various component layers that comprise each LID control object.

For the second phase, for each subcatchment that will utilize LIDs, one selects the LID Controls property in the subcatchment's Property Editor to launch the LID Group Editor. This editor is used to add or delete individual LID controls from the subcatchment. For each control added the LID Usage Editor is used to specify the size of the control and what fraction of the subcatchment's impervious and pervious areas it captures.

###### LID Placement {#lid_placement}

There are two different approaches for placing LID controls within a subcatchment:

1.  place one or more controls in an existing subcatchment that will displace an equal amount of non-LID area from the subcatchment

2.  create a new subcatchment devoted entirely to just a single LID practice.

The first approach allows a mix of LIDs to be placed into a subcatchment, each treating a different portion of the runoff generated from the non-LID fraction of the subcatchment. Note that under this option the subcatchment's LIDs act in parallel -- it is not possible to make them act in series (i.e., have the outflow from one LID control become the inflow to another LID). Also, after LID placement the subcatchment's Percent Impervious and Width properties may require adjustment to compensate for the amount of original subcatchment area that has now been replaced by LIDs (see the figure below). For example, suppose that a subcatchment which is 40% impervious has 75% of that area converted to permeable pavement. After the LID is added the subcatchment's percent imperviousness should be changed to the percent of impervious area remaining divided by the percent of non-LID area remaining. This works out to (1 - 0.75)*40 / (100 - 0.75*40) or 14.3 %.

[LID Placement]

Under this first approach the runoff available for capture by the subcatchment's LIDs is the runoff generated from its non-LID area (after any internal re-routing  of runoff (e.g., impervious to pervious) has been made). Also note that Green Roofs and Roof Disconnection only treat the precipitation that falls directly on them and do not capture runoff from other impervious areas in their subcatchment.

The second approach allows LID controls to be strung along in series and also allows runoff from several different upstream subcatchments to be routed onto the LID subcatchment. If these single-LID subcatchments are carved out of existing subcatchments, then once again some adjustment of the Percent Impervious, Width and also the Area properties of the latter may be necessary. In addition, whenever an LID occupies the entire subcatchment the values assigned to the subcatchment's standard surface properties (such as imperviousness, slope, roughness, etc.) are overridden by those that pertain to the LID unit.

Normally both surface and drain outflows from LID units are routed to
the same outlet location assigned to the parent subcatchment. However
one can choose to return all LID outflow to the pervious area of the
parent subcatchment and/or route the drain outflow to a separate
designated outlet. (When both of these options are chosen, only the
surface outflow is returned to the pervious sub-area.)

###### LID Results {#lid_results}

The performance of the LID controls placed in a subcatchment is
reflected in the overall runoff, infiltration, and evaporation rates
computed for the subcatchment as normally reported by SWMM. SWMM's
Summary Report also contains a section entitled LID Performance Summary
that provides an overall water balance for each LID control placed in
each subcatchment. The components of this water balance include total
inflow, infiltration, evaporation, surface runoff, drain flow and
initial and final stored volumes, all expressed as inches (or mm) over
the LID's area.

Optionally, the entire time series of flux rates and moisture levels for
a selected LID control in a given subcatchment can be written to a tab
delimited text file for easy viewing and graphing in a spreadsheet
program.

#### Hydraulics {#hydraulics}

In addition to the nodes and links which characterize the physical
aspects of a drainage system in a SWMM model, the following data objects
can be used to augment the hydraulic description of the system:

· Transects

· Streets

· Inlets

· Inflows

· Controls

##### Transects {#transects}

Transects refer to the geometric data that describe how bottom elevation
varies with horizontal distance over the cross-section of a natural
channel or irregular-shaped conduit. The figure below displays an
example of a transect for a natural channel.

[transect]

Each transect must be given a unique name. Conduits refer to that name
to represent their shape. A special Transect Editor is available for
editing the station-elevation data of a transect. SWMM internally
converts these data into tables of area, top width, and hydraulic radius
versus channel depth. In addition, as shown in the diagram above, each
transect can have a left and right overbank section whose Manning's
roughness coefficient can be different from that of the main channel.
This feature can provide more realistic estimates of channel conveyance
under high flow conditions.

##### Streets {#streets}

Streets are a specialized form of transect that describes the typical
cross-section geometry of a street or roadway. The figure below shows a
half-street layout along with the dimensions a user needs to provide.

[Street]

Each street section object is assigned an ID name that a conduit can
refer to for describing its cross section geometry. A Street Section
Editor is available for providing a street section's dimensions and
whether it is one- or two-sided.

#### Inlets {#inlets}

Street inlets are curb and gutter openings that convey runoff from
streets into below-ground sewers. Drop inlets serve a similar purpose
for trapezoidal channels. SWMM can compute the amount of flow captured
by inlets and sent to designated sewer nodes using the FHWA HEC-22
methodology. The type, sizing, and spacing of street inlets will
determine if the spread and depth of water on roadways can be maintained
at acceptable levels.

To analyze street drainage with SWMM a site is represented as a dual
drainage system consisting of both street conduits along the ground
surface and sewer conduits below it.  An inlet structure will divert
some portion of the street flow it sees into a designated node of the
sewer system with the rest being bypassed to downstream streets. When an
inlet

##### Inlet Types {#inlet_types}

SWMM

##### Inlet Usage {#inlet_usage}

To add an analysis of street inlets to a SWMM project:

· Create one network layout for streets and another for sewers.

· Create a collection of street cross section objects.

· For each street conduit, set its Shape property to one of the available street sections.

· Create a set of inlet structure design objects.

· Place a particular inlet structure design into a selected street conduit, assigning it a sewer node that receives its captured flow.

· Assign surface runoff from subcatchments or other external inflows to street conduit nodes.

A similar set of steps would be used to add drop inlets into rectangular
or trapezoidal channels.

A summary of results for each street conduit (maximum flow depth and
pavement spread) and for each inlet (percent capture at peak flow,
frequency of bypass flow and frequency of sewer system backflow) will
appear as a separate Street Flow table in SWMM's Summary Results report.

##### Inlet Features {#inlet_features}

Some additional considerations when modeling inlets are:

· Conduits with inlets will be displayed on the Study Area Map with a [Inlet] symbol near their midpoint and show their downstream node connected to the inlet's capture node with a dotted line when the Map Option to display link symbols is turned on.

[InletsOnMap]

· The rim elevations of nodes that receive captured inlet flow do not have to match the invert elevations of the end node of the conduit containing the inlet.

· Two-sided street conduits (that are symmetric about the street crown) use pairs of inlets placed on each curb side of the street.

· Multiple inlets of the same design can be assigned to a conduit (as pairs for two-sided streets). For on-grade placement the flow captured by each inlet is determined sequentially, so that the approach flow to the next inlet in line is the bypass flow from the inlet before it.

· Flow captured by inlets is limited by the amount that its sewer node can receive before it floods. If the node has no such capacity remaining then any excess flow that would cause it to flood is sent back through the inlet and onto the street.

· Users can stipulate whether an inlet operates on-grade or on-sag or have SWMM decide based on the slopes of the conduits adjoining it. (On-sag refers to a sump or low point that all adjoining conduits slope towards.)

· Inlets can have a degree of clogging and a flow capture restriction assigned to them.

· For Kinematic Wave and Steady Flow routing it is recommended that storage nodes be used at the end of inlet conduits that converge at sag points since otherwise any non-captured flow will simply exit the system. This is not necessary for Dynamic Wave routing as any non-captured water will create a backwater effect raising water levels in the adjoining street conduits.

#### External Inflows {#external_inflows}

In addition to inflows originating from subcatchment runoff and
groundwater, drainage system nodes can receive three other types of
external inflows:

Direct Inflows

These are user-defined time series of inflows added directly into a
node. They can be used to perform flow and water quality routing in the
absence of any runoff computations (as in a study area where no
subcatchments are defined).

Dry Weather Inflows

These are continuous inflows that typically reflect the contribution
from sanitary sewage in sewer systems or base flows in pipes and stream
channels. They are represented by an average inflow rate that can be
periodically adjusted on a monthly, daily, and hourly basis by applying
Time Pattern multipliers to this average value.

Rainfall-Dependent Inflow/Infiltration (RDII)

These are stormwater flows that enter sanitary or combined sewers due to
"inflow" from direct connections of downspouts, sump pumps, foundation
drains, etc. as well as "infiltration" of subsurface water through
cracked pipes, leaky joints, poor manhole connections, etc. RDII can be
computed for a given rainfall record based on set of triangular unit
hydrographs (UH) that determine a short-term, intermediate-term, and
long-term inflow response for each time period of rainfall. Any number
of UH sets can be supplied for different sewershed areas and different
months of the year. RDII flows can also be specified in an external RDII
Interface file.

Direct, Dry Weather, and RDII inflows are properties associated with
each type of drainage system node (junctions, outfalls, flow dividers,
and storage units) and can be specified when nodes are edited. They can
be used to perform flow and water quality routing in the absence of any
runoff computations (as in a study area where no subcatchments are
defined). It is also possible to make the outflows generated from an
upstream drainage system be the inflows to a downstream system by using
interface files.

See Also

External Inflows Editor

#### Control Rules {#control_rules}

Control Rules determine how pumps and regulators in the conveyance
system will be adjusted over the course of a simulation. The use of
control rules is explained in the following topics:

· Example Rules

· Rule Format

· Condition Clauses

· Action Clauses

· Modulated Controls

· Named Variables

· Arithmetic Expressions

##### Example Rules {#example_rules}

The following are some example control rules.

; Simple time-based pump control

RULE R1

IF SIMULATION TIME > 8

THEN PUMP 12 STATUS = ON

ELSE PUMP 12 STATUS = OFF

; Multi-condition orifice gate control

RULE R2A

IF NODE 23 DEPTH > 12

AND LINK 165 FLOW > 100

THEN ORIFICE R55 SETTING = 0.5

RULE R2B

IF NODE 23 DEPTH > 12

AND LINK 165 FLOW > 200

THEN ORIFICE R55 SETTING = 1.0

RULE R2C

IF NODE 23 DEPTH <= 12

OR LINK 165 FLOW <= 100

THEN ORIFICE R55 SETTING = 0

; Pump station operation

RULE R3A

IF NODE N1 DEPTH > 5

THEN PUMP N1A STATUS = ON

RULE R3B

IF NODE N1 DEPTH > 7

THEN PUMP N1B STATUS = ON

RULE R3C

IF NODE N1 DEPTH <= 3

THEN PUMP N1A STATUS = OFF

AND PUMP N1B STATUS = OFF

; Modulated weir height control

RULE R4

IF NODE N2 DEPTH >= 0

THEN WEIR W25 SETTING = CURVE C25

##### Rule Format {#rule_format}

Each control rule is a series of statements of the form:

RULE  ruleID

IF    condition_1

AND   condition_2

OR   condition_3

AND   condition_4

Etc.

THEN  action_1

AND   action_2

Etc.

ELSE  action_3

AND   action_4

Etc.

PRIORITY value

where keywords are shown in boldface and ruleID is an ID label assigned
to the rule, condition_n is a Condition Clause, action_n is an Action
Clause, and value is a priority value (e.g., a number from 1 to 5).

Each rule clause must begin with one of the boldface keywords shown
above, and only one clause per line is allowed.

Only the RULE, IF and THEN portions of a rule are required; the ELSE and
PRIORITY portions are optional.

Blank lines between clauses are permitted and any text to the right of a
semicolon is considered a comment.

When mixing AND and OR clauses, the OR operator has higher precedence
than AND, i.e.,

IF A or B and C

is equivalent to

IF (A or B) and C.

If the interpretation was meant to be

IF A or (B and C)

then this can be expressed using two rules as in

IF A THEN ...

IF B and C THEN ...

The PRIORITY value is used to determine which rule applies when two or
more rules require that conflicting actions be taken on a link. A
conflicting rule with a higher priority value has precedence over one
with a lower value (e.g., PRIORITY 5 outranks PRIORITY 1). A rule
without a priority value always has a lower priority than one with a
value. For two rules with the same priority value, the rule that appears
first is given the higher priority.

##### Condition Clauses {#condition_clauses}

A Condition Clause of a Control Rule has the following formats:

object id attribute relation value

object id attribute relation object id attribute

where

---

object =  a category of object
id =  the object's ID label
attribute =  an attribute or property of the object
relation =  a relational operator (=, <>, <, <=, >, >=)
value =  an attribute value

---

Some examples of condition clauses are:

GAGE  G1   6-HR_DEPTH > 0.5

NODE  N23  DEPTH  >  10

NODE  N23  DEPTH  >  NODE 25 DEPTH

PUMP  P45  STATUS =  OFF

LINK  P45  TIMEOPEN >= 6:30

SIMULATION CLOCKTIME = 22:45:00

The objects and attributes that can appear in a condition clause are as
follows:

+-----------------------+-----------------------+-----------------------+
| Object | Attributes | Value |
+-----------------------+-----------------------+-----------------------+
| GAGE | INTENSITY | numerical value |
| | | |
| | n-HR_DEPTH | numerical value |
+-----------------------+-----------------------+-----------------------+
| NODE | DEPTH | numerical value |
| | | |
| | MAXDEPTH | numerical value |
| | | |
| | HEAD | numerical value |
| | | |
| | VOLUME | numerical value |
| | | |
| | INFLOW | numerical value |
+-----------------------+-----------------------+-----------------------+
| LINK or | FLOW | numerical value |
| | | |
| CONDUIT | FULLFLOW | numerical value |
| | | |
| | DEPTH | numerical value |
| | | |
| | MAXDEPTH | numerical value |
| | | |
| | VELOCITY | numerical value |
| | | |
| | LENGTH | numerical value |
| | | |
| | SLOPE | fractional value |
| | | |
| | STATUS | OPEN or CLOSED |
| | | |
| | TIMEOPEN | decimal hours or |
| | | hr:min |
| | TIMECLOSED | |
| | | decimal hours or |
| | | hr:min |
+-----------------------+-----------------------+-----------------------+
| PUMP | STATUS | ON or OFF |
| | | |
| | SETTING | pump curve multiplier |
| | | |
| | FLOW | numerical value |
+-----------------------+-----------------------+-----------------------+
| ORIFICE | SETTING | fraction open |
+-----------------------+-----------------------+-----------------------+
| WEIR | SETTING | fraction open |
+-----------------------+-----------------------+-----------------------+
| OUTLET | SETTING | rating curve |
| | | multiplier |
+-----------------------+-----------------------+-----------------------+
| SIMULATION | TIME | elapsed time in |
| | | decimal hours or |
| |   | hr:min:sec |
| | | |
| | DATE | month/day/year |
| | | |
| | MONTH | month of year (1 - |
| | | 12) |
| | DAY | |
| | | day of week (Sunday = |
| | DAYOFYEAR | 1) |
| | | |
| | CLOCKTIME | day of year |
| | | (month/day) |
| | | |
| | | time of day in |
| | | hr:min:sec |
+-----------------------+-----------------------+-----------------------+

Gage INTENSITY is the rainfall intensity for a specific rain gage in the
current simulation time period. Gage n-HR_DEPTH is a gage's total
rainfall depth over the past n hours where n is a number between 1 and 48.

TIMEOPEN is the duration a link has been in an OPEN or ON state or have
its SETTING be greater than zero; TIMECLOSED is the duration it has
remained in a CLOSED or OFF state or have its SETTING be zero.

##### Action Clauses {#action_clauses}

An Action Clause of a Control Rule can have one of the following
formats:

PUMP id STATUS = ON/OFF

CONDUIT id STATUS = OPEN/CLOSED

PUMP/ORIFICE/WEIR/OUTLET id SETTING = value

The meaning of SETTING depends on the object being controlled:

· for Pumps it is a multiplier applied to the flow computed from the pump curve (or relative pump speed for a TYPE5 pump).

· for Orifices it is the fractional amount that the orifice is fully open (orifice control is accomplished by lowering or raising a horizontal gate from the top of the orifice),

· for Weirs it is the fractional amount of the original freeboard that exists (i.e., weir control is accomplished by moving the crest height up or down),

· for Outlets it is a multiplier applied to the flow computed from the outlet's rating curve.

Some examples of action clauses are:

PUMP P67 STATUS = OFF

ORIFICE O212 SETTING = 0.5

##### Modulated Controls {#modulated_controls}

Modulated Controls are control rules that provide for a continuous
degree of control applied to a pump or flow regulator as determined by
the value of some controller variable, such as water depth at a node, or
by time. The functional relation between the control setting and the
controller variable can be specificed by using a Control Curve, a Time
Series, or a PID controller. Some examples of modulated control rules
are:

RULE MC1

IF NODE N2 DEPTH >= 0

THEN WEIR W25 SETTING = CURVE C25

RULE MC2

IF SIMULATION TIME > 0

THEN PUMP P12 SETTING = TIMESERIES TS101

RULE MC3

IF LINK L33 FLOW <> 1.6

THEN ORIFICE O12 SETTING = PID 0.1 0.0 0.0

Note how a modified form of the action clause is used to specify the
name of the control curve, time series or PID parameter set that defines
the degree of control. A PID parameter set contains three values -- a
proportional gain coefficient, an integral time (in minutes), and a
derivative time (in minutes). Also, by convention the controller
variable used in a Control Curve or PID Controller will always be the
object and attribute named in the last condition clause of the rule. As
an example, in rule MC1 above Curve C25 would define how the fractional
setting at Weir W25 varied with the water depth at Node N2. In rule MC3,
the PID controller adjusts the opening height of Orifice O12 to maintain
a flow of 1.6 in Link L33.

##### PID Controller {#pid_controller}

A PID (Proportional-Integral-Derivative) Controller is a generic
closed-loop control scheme that tries to maintain a desired set-point on
some process variable by computing and applying a corrective action that
adjusts the process accordingly. In the context of a hydraulic
conveyance system a PID controller might be used to adjust the opening
on a gated orifice to maintain a target flow rate in a specific conduit
or to adjust a variable speed pump to maintain a desired depth in a
storage unit. The classical PID controller has the form:

[PID_Controller]

where:

---

m(t) = controller output
  Kp = proportional coefficient (gain)
  Ti = integral time (minutes)
  Td = derivative time (minutes)
  e(t) = error (difference between setpoint and observed variable value)
  t = time.

---

The performance of a PID controller is determined by the values assigned
to the coefficients Kp, Ti, and Td.

The controller output m(t) has the same meaning as a link setting used
in a rule's Action Clause  while dt is the current flow routing time
step in minutes. Because link settings are relative values (with respect
to either a pump's standard operating curve or to the full opening
height of an orifice or weir) the error e(t) used by the controller is
also a relative value. It is defined as the difference between the
control variable setpoint x\* and its value at time t, x(t), normalized
to the setpoint value:

e(t) = (x* - x(t)) / x*

Note that for direct action control, where an increase in the link
setting causes an increase in the controlled variable, the sign of Kp
must be positive. For reverse action control, where the controlled
variable decreases as the link setting increases, the sign of Kp must be
negative. The user must recognize whether the control is direct or
reverse action and use the proper sign on Kp accordingly. For example,
adjusting an orifice opening to maintain a desired downstream flow or
downstream water level is direct action. Adjusting it to maintain an
upstream water level is reverse action. Controlling a pump to maintain a
fixed wet well water level would be reverse action while using it to
maintain a fixed downstream flow is direct action.

##### Named Variables {#named_variables}

Named Variables are aliases used to represent the triplet of <object
type | object name | object attribute> (or a doublet for Simulation
times) that appear in the condition clauses of control rules. They allow
condition clauses to be written as:

variable relation value

variable relation variable

where variable is defined on a separate line before its first use in a
rule using the format:

VARIABLE  name = object id attribute

Here is an example of using this feature:

VARIABLE  Dabc  =  NODE  abc  DEPTH

VARIABLE  Defg  =  NODE  efg  DEPTH

VARIABLE  P45   =  PUMP  45   STATUS

RULE 1

IF    Dabc > Defg

AND   P45 = OFF

THEN  PUMP 45 STATUS = ON

RULE 2

IF   Dabc < 1

THEN PUMP 45 STATUS = OFF

A variable is not allowed to have the same name as an object attribute.

Aside from saving some typing, named variables are required when using
arithmetic expressions in rule condition clauses.

##### Arithmetic Expressions {#arithmetic_expressions}

In addition to a simple condition placed on a single variable, a control
condition clause can also contain an arithmetic expression formed from
several variables whose value is compared against. Thus the format of a
condition clause can be extended as follows:

expression  relation  value

expression  relation  variable

where expression is defined on a separate line before its first use in a
rule using the format:

EXPRESSION  name = f(variable1, variable2, ...)

The function f(...) can be any well-formed mathematical expression
containing one or more named variables as well as any of the following
math functions (which are case insensitive) and operators:

§ abs(x) for absolute value of x

§ sgn(x) which is +1 for x >= 0 or -1 otherwise

§ step(x) which is 0 for x <= 0 and 1 otherwise

§ sqrt(x) for the square root of x

§ log(x) for logarithm base e of x

§ log10(x) for logarithm base 10 of x

§ exp(x) for e raised to the x power

§ the standard trig functions (sin, cos, tan, and cot)

§ the inverse trig functions (asin, acos, atan, and acot)

§ the hyperbolic trig functions (sinh, cosh, tanh, and coth)

§ the standard operators  +, -, \*, /, ^ (for exponentiation ) and any level of nested parentheses.

Here is an example of using this feature:

VARIABLE  P1_flow = LINK 1 FLOW

VARIABLE  P2_flow = LINK 2 FLOW

VARIABLE  O3_flow = Link 3 FLOW

EXPRESSION Net_Inflow = (P1_flow + P2_flow)/2 - O3_flow

RULE 1

IF   Net_Inflow > 0.1

THEN ORIFICE 3 SETTING = 1

ELSE ORIFICE 3 SETTING = 0.5

#### Water Quality {#water_quality}

Water quality related data are supplied to a SWMM model using the
following types of objects:

· Pollutants

· Land Uses

· Treatment

##### Pollutants {#pollutants}

SWMM can simulate the generation, inflow and transport of any number of
user-defined pollutants. Required information for each pollutant
includes:

· pollutant name

· concentration units (i.e., milligrams/liter, micrograms/liter, or counts/liter)

· concentration  in rainfall

· concentration in groundwater

· concentration in inflow/infiltration

· concentration in dry weather flow

· initial concentration throughout the conveyance system

· first-order decay coefficient.

Co-pollutants can also be defined in SWMM. For example, pollutant X can
have a co-pollutant Y, meaning that the runoff concentration of X will
have some fixed fraction of the runoff concentration of Y added to it.

Pollutant buildup and washoff from subcatchment areas are determined by
the land uses assigned to those areas. Input loadings of pollutants to
the drainage system can also originate from external time series inflows
as well as from dry weather inflows.

See Also

Pollutant Editor

Land Uses

Pollutant Buildup

Pollutant Washoff

External Inflows Editor

##### Pollutant Buildup {#pollutant_buildup}

???

##### Pollutant Washoff {#pollutant_washoff}

Pollutant washoff from a given land use category occurs during wet
weather periods and can be described in one of the following ways:

Exponential Washoff

The washoff load (W) in units of mass per hour is proportional to the
product of runoff raised to some power and to the amount of buildup
remaining, i.e.,

[ExponentialWashoff]

where C1 =  washoff coefficient, C2 = washoff exponent, q = runoff rate
per unit area (inches/hour or mm/hour), and B = pollutant buildup in
mass units. The buildup here is the total mass (not per area or per curb
length) and both buildup and washoff mass units are the same as used to
express the pollutant's concentration (milligrams, micrograms, or
counts).

Rating Curve Washoff

The rate of washoff W in mass per second is proportional to the runoff
rate raised to some power, i.e.,

[RatingCurveWashoff]

where C1 = washoff coefficient, C2 = washoff exponent, and Q = runoff
rate in user-defined flow units.

Event Mean Concentration

This is a special case of Rating Curve Washoff where the exponent is 1.0
and the coefficient C1 represents the washoff pollutant concentration in
mass per liter. The conversion between user-defined flow units used for
runoff and liters is handled internally by SWMM. (Typical EMC's for
selected constituents).

Note that in each case buildup is continuously depleted as washoff
proceeds, and washoff ceases when there is no more buildup available. It
is also possible to use the Event Mean Concentration option by itself,
without having to model any pollutant buildup at all.

BMP Removal Efficiency

Washoff loads for a given pollutant and land use category can be reduced
by a fixed percentage by specifying a BMP Removal Efficiency that
reflects the effectiveness of any BMP controls associated with the land
use.

Removal of pollutants in surface washoff can also occur when runoff is
captured by Low Impact Development (LID) controls. The concentration of
 a pollutant released from an LID unit's underdrain flow can be reduced
by a user-specified percentage. These removal percentages are assigned
through the LID Control Editor for each generic LID design.

##### Street Sweeping {#street_sweeping}

Street sweeping can be used on each land use category to periodically
reduce the accumulated buildup of specific pollutants. The parameters
that describe street sweeping include:

· days between sweeping

· days since the last sweeping at the start of the simulation

· the fraction of buildup of all pollutants that is available for removal by sweeping

· the fraction of available buildup for each pollutant removed by sweeping.

These parameters can be different for each land use and the last
parameter can vary also with pollutant.

#### Treatment {#treatment}

Removal of pollutants from the flow streams entering any drainage system
node is modeled by assigning a set of treatment functions to the node. A
treatment function can be any well-formed mathematical expression
involving:

· the pollutant concentration (use the pollutant name to represent its
concentration)

???

### Tabular Data {#tabular_data}

SWMM utilizes several forms of tabular data to describe the properties
of its various objects. These include:

· Curves

· Time Series

· Time Patterns

#### Curves {#curves}

Curve objects are used to describe a functional relationship between two
quantities. The following types of curves are used in SWMM:

Storage describes how the surface area of a Storage Unit node varies with water depth.
Shape describes how the width of a customized cross-sectional shape varies with height for a Conduit link.
Diversion relates diverted outflow to total inflow for a Flow Divider node or a Custom Inlet.
Tidal describes how the stage at an Outfall node changes by hour of the day.
Pump relates flow through a Pump link to the depth or volume at the upstream node or to the head delivered by the pump.
Rating relates flow through an Outlet link to the freeboard depth or head difference across the outlet; relates flow captured by a Custom Inlet drain to the depth of water  above it.
Control determines how the control setting of a pump or flow regulator varies as a function of some control variable (such as water level at a particular node) as specified in a Modulated Control rule; can also be used to adjust the flow from an LID unit's underdrain based on head.
Weir allows a weir's discharge coefficient to vary with the hydraulic head across it.

Each curve must be given a unique name and can be assigned any number of
data points.

See Also

Curve Editor

#### Time Series {#time_series}

Time Series objects are used to describe how certain object properties
vary with time. Time series can be used to describe:

· temperature data

· evaporation data

· rainfall data

· water stage at outfall nodes

· external inflow hydrographs at drainage system nodes

· external inflow pollutographs at drainage system nodes

· control settings for pumps and flow regulators.

Each time series must be given a unique name and can be assigned any
number of time-value data pairs. Time can be specified either as hours
from the start of a simulation or as an absolute date and time-of-day.
Time series data can either be entered directly into the program or be
accessed from a user-supplied Time Series file.

[icon_tip]For rainfall time series, it is only necessary to enter
periods with non-zero rainfall amounts. SWMM interprets the rainfall
value as a constant value lasting over the recording interval specified
for the rain gage that utilizes the time series. For all other types of
time series, SWMM uses interpolation to estimate values at times that
fall in between the recorded values.

[icon_tip]For times that fall outside the range of the time series, SWMM
will use a value of 0 for rainfall and external inflow time series, and
either the first or last series value for temperature, evaporation, and
water stage time series.

See Also

Time Series Editor

Time Series Files

#### Time Patterns {#time_patterns}

Time Patterns allow external dry weather flow (DWF) to vary in a
periodic fashion. They consist of a set of adjustment factors applied as
multipliers to a baseline DWF flow rate or pollutant concentration. The
different types of time patterns include:

Monthly one multiplier for each month of the year
Daily one multiplier for each day of the week
Hourly one multiplier for each hour from 12 AM to 11 PM
Weekend hourly multipliers for weekend days

Each time pattern must have a unique name and there is no limit on the
number of patterns that can be created. Each dry weather inflow (either
flow or quality) can have up to four patterns associated with it, one
for each type listed above.

Monthly time patterns can also be used to adjust the baseline values of
the following hydrological parameters:

· subcatchment depression storage

· subcatchment pervious surface roughness

· soil infiltration recovery rate

· groundwater evaporation rate.

See Also

Time Pattern Editor

Inflows

Subcatchment Properties

Climatology Editor

### Computational Methods {#computational_methods}

SWMM is a physically based, discrete-time simulation model. It employs
principles of conservation of mass, energy, and momentum wherever
appropriate. This section briefly describes the methods SWMM uses to
model stormwater runoff quantity and quality through the following
physical processes:

· Surface Runoff

· Infiltration

· Groundwater

· Snowmelt

· Flow Routing

· Surface Ponding

· Water Quality Routing

More detailed descriptions of SWMM

#### Surface Runoff {#surface_runoff}

The conceptual view of surface runoff used by SWMM is illustrated in the
figure below.

[SurfaceRunoff]

Each subcatchment surface is treated as a nonlinear reservoir. Inflow
comes from precipitation and the runoff from any designated upstream
subcatchments. Outflows consist of infiltration, evaporation, and
surface runoff. The capacity of this "reservoir" is the maximum
depression storage, which is the maximum surface storage provided by
ponding, surface wetting, and interception. Surface runoff, Q, occurs
only when the depth of water d in the "reservoir" exceeds the maximum
depression storage, ds, in which case the outflow is given by Manning's
equation. Depth of water over the subcatchment (d) is continuously
updated with time by solving numerically a water balance equation over
the subcatchment.

#### Infiltration {#infiltration}

Infiltration is the process of rainfall penetrating the ground surface
into the unsaturated soil zone of pervious subcatchments areas. SWMM
offers four choices for modeling infiltration:

Classical Horton Method

This method is based on empirical observations showing that infiltration
decreases exponentially from an initial maximum rate to some minimum
rate over the course of a long rainfall event. Input parameters required
by this method include the maximum and minimum infiltration rates, a
decay coefficient that describes how fast the rate decreases over time,
and the time it takes a fully saturated soil to completely dry (used to
compute the recovery of infiltration rate during dry periods).

Modified Horton Method

This is a modified version of the classical Horton Method that uses the
cumulative infiltration in excess of the minimum rate as its state
variable (instead of time along the Horton curve), providing a more
accurate infiltration estimate when low rainfall intensities occur. It
uses the same input parameters as does the traditional Horton Method.

Green-Ampt Method

This method for modeling infiltration assumes that a sharp wetting front
exists in the soil column, separating soil with some initial moisture
content below from saturated soil above. The input parameters required
are the initial moisture deficit of the soil, the soil's hydraulic
conductivity, and the suction head at the wetting front. The recovery
rate of moisture deficit during dry periods is empirically related to
the hydraulic conductivity.

Modified Green-Ampt Method

This method modifies the original Green-Ampt procedure by not depleting
moisture deficit in the top surface layer of soil during initial periods
of low rainfall as was done in the original method. This change can
produce more realistic infiltration behavior for storms with long
initial periods where the rainfall intensity is below the soil

#### Groundwater {#groundwater}

Shown below is a definitional sketch of the two-zone groundwater model
that is used in SWMM. The upper zone is unsaturated with a variable
moisture content of ?. The lower zone is fully saturated and therefore
its moisture content is fixed at the soil porosity ϕ.

[GroundWater]

The fluxes shown in the figure, expressed as volume per unit area per
unit time, consist of the following:

fI infiltration from the surface

fEU evapotranspiration from the upper zone which is a fixed fraction of the unused surface evaporation

fU percolation from the upper to lower zone which depends on the upper zone moisture content ? and depth dU

fEL evapotranspiration from the lower zone, which is a function of the depth of the upper zone dU

fL seepage from the lower zone to deep groundwater which depends on the lower zone depth dL

fG lateral groundwater interflow to the conveyance network which depends on the lower zone depth dL as well as depths in the receiving channel or node.

After computing the water fluxes that exist during a given time step, a
mass balance is written for the change in water volume stored in each
zone so that a new water table depth and unsaturated zone moisture
content can be computed for the next time step.

#### Snowmelt {#snowmelt}

The snowmelt routine in SWMM is a part of the runoff modeling process.
It updates the state of the snow packs associated with each subcatchment
by accounting for snow accumulation, snow redistribution by areal
depletion and removal operations, and snow melt via heat budget
accounting. Any snowmelt coming off the pack is treated as an additional
rainfall input onto the subcatchment.

At each runoff time step the following computations are made:

1.  Air temperature and melt coefficients are updated according to the calendar date.

2.  Any precipitation that falls as snow is added to the snow pack.

3.  Any excess snow depth on the plowable area of the pack is redistributed according to the removal parameters established for the pack.

4.  Areal coverages of snow on the impervious and pervious areas of the pack are reduced according to the Areal Depletion Curves defined for the study area.

5.  The amount of snow in the pack that melts to liquid water is found using:

· a heat budget equation for periods with rainfall, where melt rate increases with increasing air temperature, wind speed, and rainfall intensity

· a degree-day equation for periods with no rainfall, where melt rate equals the product of a melt coefficient and the difference between the air temperature and the pack's base melt temperature.

6.  If no melting occurs, the pack temperature is adjusted up or down based on the product of the difference between current and past air temperatures and an adjusted melt coefficient. If melting occurs, the temperature of the pack is increased by the equivalent heat content of the melted snow, up to the base melt temperature. Any remaining melt liquid beyond this is available to runoff from the pack.

7.  The available snow melt is then reduced by the amount of free water holding capacity remaining in the pack. The remaining melt is treated the same as an additional rainfall input onto the subcatchment.

#### Flow Routing {#flow_routing}

Flow routing within a conduit link in SWMM is governed by the
conservation of mass and momentum equations for gradually varied,
unsteady flow (i.e., the Saint Venant flow equations). The SWMM user has
a choice on the level of sophistication used to solve these equations:

· Steady Flow Routing

· Kinematic Wave Routing

· Dynamic Wave Routing

Each of these routing methods employs the Manning equation to relate
flow rate to flow depth and bed (or friction) slope. For user-designated
Force Main conduits, either the Hazen-Williams or Darcy-Weisbach
equation can be used when pressurized flow occurs.

##### Steady Flow Routing {#steady_flow_routing}

Steady Flow routing represents the simplest type of routing possible
(actually no routing) by assuming that within each computational time
step flow is uniform and steady. Thus it simply translates inflow
hydrographs at the upstream end of the conduit to the downstream end,
with no delay or change in shape. The normal flow equation is used to
relate flow rate to flow area (or depth).

This type of routing cannot account for channel storage, backwater
effects, entrance/exit losses, flow reversal or pressurized flow. It can
only be used with dendritic conveyance networks, where each node has
only a single outflow link (unless the node is a divider in which case
two outflow links are required). This form of routing is insensitive to
the time step employed and is really only appropriate for preliminary
analysis using long-term continuous simulations.

##### Kinematic Wave Routing {#kinematic_wave_routing}

This routing method solves the continuity equation along with a
simplified form of the momentum equation in each conduit. The latter
assumes that the slope of the water surface equal the slope of the
conduit.

The maximum flow that can be conveyed through a conduit is the full
normal flow value. Any flow in excess of this entering the inlet node is
either lost from the system or can pond atop the inlet node and be
re-introduced into the conduit as capacity becomes available.

Kinematic wave routing allows flow and area to vary both spatially and
temporally within a conduit. This can result in attenuated and delayed
outflow hydrographs as inflow is routed through the channel. However
this form of routing cannot account for backwater effects, entrance/exit
losses, flow reversal, or pressurized flow, and is also restricted to
dendritic network layouts. It can usually maintain numerical stability
with moderately large time steps, on the order of 1 to 5 minutes. If the
aforementioned effects are not expected to be significant then this
alternative can be an accurate and efficient routing method, especially
for long-term simulations.

##### Dynamic Wave Routing {#dynamic_wave_routing}

Dynamic Wave routing solves the complete one-dimensional Saint Venant
flow equations and therefore produces the most theoretically accurate
results. These equations consist of the continuity and momentum
equations for conduits and a volume continuity equation at nodes.

With this form of routing it is possible to represent pressurized flow
when a closed conduit becomes full, such that flows can exceed the full
normal flow value. Flooding occurs when the water depth at a node
exceeds the maximum available depth, and the excess flow is either lost
from the system or can pond atop the node and re-enter the drainage
system.

Dynamic wave routing can account for channel storage, backwater,
entrance/exit losses, flow reversal, and pressurized flow. Because it
couples together the solution for both water levels at nodes and flow in
conduits it can be applied to any general network layout, even those
containing multiple downstream diversions and loops. It is the method of
choice for systems subjected to significant backwater effects due to
downstream flow restrictions and with flow regulation via weirs and
orifices. This generality comes at a price of having to use much smaller
time steps, on the order of thirty seconds or less (SWMM can
automatically reduce the user-defined maximum time step as needed to
maintain numerical stability).

#### Ponding and Pressurization {#ponding_and_pressurization}

???

#### Water Quality Routing {#water_quality_routing}

Water quality routing within conduit links assumes that the conduit
behaves as a continuously stirred tank reactor (CSTR). Although a plug
flow reactor assumption might be more realistic, the differences will be
small if the travel time through the conduit is on the same order as the
routing time step. The concentration of a constituent exiting the
conduit at the end of a time step is found by integrating the
conservation of mass equation, using average values for quantities that
might change over the time step such as flow rate and conduit volume.

Water quality modeling within storage unit nodes follows the same
approach used for conduits. For other types of nodes that have no
volume, the quality of water exiting the node is simply the mixture
concentration of all water entering the node.

The pollutant concentration in both a conduit and a storage node will be
reduced by a first-order decay reaction if the pollutant
