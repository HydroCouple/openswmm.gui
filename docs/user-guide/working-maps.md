## Working with the Map {#working_with_map}

EPA SWMM can display a map of the study area being modeled. This section describes how you can manipulate this map to enhance your visualization of the system.

- [Selecting a map theme](#selecting_map_theme)

- [Setting the map's dimensions](#setting_maps_dimensions)

- [Utilizing a backdrop image](#utilizing_backdrop_image)

- [Measuring distances](#measuring_distances)

- [Zooming the map](#zooming_map)

- [Panning the map](#panning_map)

- [Viewing at full extent](#viewing_full_extent)

- [Finding an object](#finding_object)

- [Submitting a map query](#submitting_map_query)

- [Using the map legends](#using_map_legends)

- [Using the Overview Map](#using_overview_map)

- [Setting map display options](#setting_map_display_options)

- [Exporting the map](#exporting_map)

### Viewing Map Layers {#viewing_map_layers}

The layers that can be viewed on the Study Area consist of rain gages, subcatchments, nodes, links, labels, and the backdrop image. The display of each of these can be toggled on or off by selecting **View >> Layers** from the Main Menu or by right-clicking on the map and selecting **Layers** from the pop-up menu that appears.

### Selecting a Map Theme {#selecting_map_theme}

A map theme corresponds to a specific layer property whose value is drawn in color-coded fashion on the Study Area Map. The dropdown list boxes on the [Map Browser](#swmms_main-window) are used for selecting a theme to display for the subcatchment, node and link layers.

![](embim1.gif)

Methods for changing the color-coding associated with a theme are discussed in [Using the Map Legends](#using_map_legends).

### Setting the Map Dimensions

The physical dimensions of the map can be defined so that map coordinates can be properly scaled to the computer's video display.

To set the map's dimensions:

1. Select **View >> Dimensions** from the Main Menu.

2. Enter coordinates for the lower-left and upper-right corners of the map into the [Map Dimensions](#map-dimensions_dialog) dialog that appears or click the **Auto-Size** button to automatically set the dimensions based on the coordinates of the objects currently included in the map.

3. Select the distance units to use for these coordinates.

4. If the [Auto-Length](#status_bar) option is in effect, check the "Re-compute all lengths and areas" box if you would like SWMM to re-calculate all conduit lengths and subcatchment areas under the new set of map dimensions.

5. Click the **OK** button to resize the map.

[!tip]
If you are going to use a backdrop image with the automatic distance and area calculation feature, then it is recommended that you set the map dimensions immediately after creating a new project. Map distance units can be different from conduit length units. The latter (feet or meters) depend on whether flow rates are expressed in US or metric units. SWMM will automatically convert units if necessary.

[!tip]
If you just want to re-compute conduit lengths and subcatchment areas without changing the map's dimensions, then just check the Re-compute Lengths and Areas box and leave the coordinate boxes as they are.

### Utilizing a Backdrop Image {#utilizing_backdrop_image}

SWMM can display a Backdrop Image behind the Study Area Map. The backdrop image might be a street map, utility map, topographic map, site development plan, or any other relevant picture or drawing. For example, using a street map would simplify the process of adding sewer lines to the project since one could essentially digitize the drainage system's nodes and links directly on top of it.

[Backdrop Image](#backdropimage.png)

The Backdrop Image must be a Windows metafile, bitmap, PNG, or JPEG image created outside of SWMM. Once imported, its features cannot be edited, although its scale and viewing area will change as the map window is zoomed and panned. For this reason metafiles work better than bitmaps or JPEGs since they will not loose resolution when re-scaled. Most CAD and GIS programs have the ability to save their drawings and maps as metafiles.

Selecting **View >> Backdrop** from the [Main Menu](#main_menu) will display a sub-menu with the following commands:

- [Load](#loading_backdrop_image) (loads a backdrop image file into the project)

- **Unload** (unloads the backdrop image from the project)

- [Align](#aligning_backdrop_image) (aligns the drainage system schematic with the backdrop)

- [Resize](#resizing-backdrop_image) (resizes the map dimensions of the backdrop)

- **Watermark** (toggles the backdrop image appearance between normal and lightened)

The name of the backdrop file and its map dimensions are saved along with the rest of a project's data whenever the project is saved to file.

For best results in using a backdrop image:

- Use a metafile, not a bitmap.

- Dimension the Study Area Map so that its bounding rectangle has the same aspect ratio (width-to-height ratio) as the backdrop.

#### Loading a Backdrop Image {#loading_backdrop_image}

To load a backdrop image, select View >> Backdrop >> Load from the Main Menu. A Backdrop Image Selector dialog form will be displayed. The entries on this form are as follows:

Backdrop Image File

Enter the name of the file that contains the image. You can click the [FileBrowseBtn] button to bring up a standard Windows file selection dialog from which you can search for the image file.

World Coordinates File

If a "world" file exists for the image, enter its name here, or click the [FileBrowseBtn] button to search for it. A world file contains geo-referencing information for the image and can be created from the software that produced the image file or by using a text editor. It contains six lines with the following information:

Line 1: real world width of a pixel in the horizontal direction.

Line 2: X rotation parameter (not used).

Line 3: Y rotation parameter (not used).

Line 4: negative of the real world height of a pixel in the vertical direction.

Line 5: real world X coordinate of the upper left corner of the image.

Line 6: real world Y coordinate of the upper left corner of the image.

If no world file is specified, then the backdrop will be scaled to fit into the center of the map display window.

Scale Map to Backdrop Image

This option is only available when a world file has been specified. Selecting it forces the dimensions of the Study Area Map to coincide with those of the backdrop image. In addition, all existing objects on the map will have their coordinates adjusted so that they appear within the new map dimensions yet maintain their relative positions to one another. Selecting this option may then require that the backdrop be re-aligned so that its position relative to the drainage area objects is correct.

#### Aligning a Backdrop Image {#aligning_backdrop_image}

To align a backdrop image with the drainage system schematic on the Study Area Map:

1. Select View >> Backdrop >> Align from the Main Menu.

2. Move the backdrop image across the Study Area Map by moving the mouse with the left button held down until it lines up properly with the drainage system. Then release the button.

#### Resizing a Backdrop Image {#resizing_backdrop_image}

To resize a backdrop image select View >> Backdrop >> Resize from the Main Menu. A Backdrop Dimensions dialog will appear that allows you to specify X,Y coordinates for the lower left and upper right corners of the backdrop image. In addition, you can specify that the backdrop be resized to fit the current dimensions of the Study Area Map or that the map have its dimensions changed to match those of the backdrop. Note that with the latter option all objects currently on the map will have their coordinates changed so that their position relative to the lower left corner of the map is maintained.

### Measuring Distances {#measuring_distances}

To measure a distance or area on the Study Area Map:

1. Click the [Measure]  button on the Map Toolbar.

2. Left-click on the map where you wish to begin measuring from.

3. Move the mouse over the distance being measured, left-clicking at each intermediate location where the measured path changes direction.

4. Right-click the mouse or press <Enter> to complete the measurement.

5. The distance measured in project units (feet or meters) will be displayed in a dialog box. If the last point on the measured path coincides with the first point then the area of the enclosed polygon will also be displayed.

### Zooming the Map {#zooming_map}

To Zoom In on the Study Area Map:

1. Select View >> Zoom In from the Main Menu or click the [Zoomin]  button on the Map Toolbar.

2. To zoom in 100% (i.e., 2X), move the mouse to the center of the zoom area and click the left button.

3. To perform a custom zoom, move the mouse to the upper left corner of the zoom area and with the left button pressed down, draw a rectangular outline around the zoom area. Then release the left button.

To Zoom Out on the Study Area Map:

1. Select View >> Zoom Out from the Main Menu or click [Zoomout]  on the Map Toolbar.

2. The map will be returned to the view in effect at the previous zoom level.

You can also zoom in and out by rotating the mouse's scroll wheel.

### Panning the Map

To pan across the Study Area Map window:

1. Select View >> Pan from the Main Menu or click the [Pan] button on the Map Toolbar.

2. With the left button held down over any point on the map, drag the mouse in the direction you wish to pan in.

3. Release the mouse button to complete the pan.

You can also pan by simply moving the mouse with its scroll wheel pressed down.

To pan using the Overview Map:

1. If not already visible, bring up the Overview Map by selecting View >> Overview Map from the Main Menu or click the [world] button on the Main Toolbar.

2. If the Study Area Map has been zoomed in, an outline of the current viewing area will appear on the Overview Map. Position the mouse within this outline on the Overview Map.

3. With the left button held down, drag the outline to a new position.

4. Release the mouse button and the Study Area Map will be panned to an area corresponding to the outline on the Overview Map.

### Viewing at Full Extent

To view the Study Area Map at full extent, either:

a) select View >> Full Extent from the Main Menu, or

b) click the [Extents] button on the Map Toolbar.

### Finding an Object

To find an object on the Study Area Map whose name is known:

1. Select View >> Find Object from the Main Menu or click the [FIND] on the Main Toolbar.

2. In the Map Finder dialog that appears, select the type of object to find and enter its name.

[]

3. Click the Go button.

If the object exists, it will be highlighted on the map and in the Project Browser. If the map is currently zoomed in and the object falls outside the current map boundaries, the map will be panned so that the object comes into view.

[!tip]
User-assigned object names in SWMM are not case sensitive.
E.g., NODE123 is equivalent to Node123.

After an object is found, the Map Finder dialog will also list:

- the outlet connections for a subcatchment

- the connecting links for a node

- the connecting nodes for a link.

### Submitting a Map Query {#submitting_map_query}

A Map Query identifies objects on the Study Area Map that meet a specific criterion (e.g., nodes that flood, links with velocity below 2 ft/sec, etc.). It can also identify which subcatchments have LID controls and which nodes have external inflows.To submit a map query:

1.  Select a time period in which to query the map from the Map Browser.

2.  Select View >> Query from the Main Menu or click the [query]  button on the Main Toolbar.

3.  Fill in the following information in the Query dialog that appears:

[MapQuery]

- Select whether to search for Subcatchments, Nodes, Links, LID Subcatchments, or Inflow Nodes.

- Select a parameter to query or the type of LID or inflow to locate.

- Select the appropriate operator: Above, Below, or Equals.

- Enter a value to compare against.

4. Click the Go button. The number of objects that meet the criterion will be displayed in the Query dialog and each such object will be highlighted on the Study Area Map.

5. As a new time period is selected in the Map Browser, the query results are automatically updated.

6. You can submit another query using the dialog box or close it by clicking the button in the upper right corner.

After the Query dialog is closed the map will revert back to its original display.

### Using the Map Legends {#using_map_legends}

Map Legends associate a color with a range of values for the current theme being viewed. Separate legends exist for Subcatchments, Nodes, and Links. A Date/Time Legend is also available for displaying the date and clock time of the simulation period being viewed on the map.

[Legend]

To display or hide a map legend:

1. Select View >> Legends from the Main Menu or right click on the map and select Legends from the pop-up menu that appears.

2. Click on the type of legend whose display should be toggled on or off.

A visible legend can also be hidden by double clicking on it.

To move a legend to another location press the left mouse button over the legend, drag the legend to its new location with the button held down, and then release the button.

To edit a legend, either select View >> Legends >> Modify from the Main Menu or right click on the legend if it is visible. Then use the Legend Editor dialog that appears to modify the legend's colors and intervals.

### Using the Overview Map {#using_overview_map}

The Overview Map, as pictured below, allows one to see where in terms of the overall system the main Study Area Map is currently focused. The current zoom area is depicted by the rectangular outline displayed on the Overview Map. As you drag this rectangle to another position the view within the main map will be redrawn accordingly. The Overview Map can be toggled on and off by selecting View >> Overview Map from the Main Menu or by clicking the [world]  on the Main Toolbar. The Overview Map window can also be dragged to any position as well as be re-sized.

[OverviewMap]

### Setting Map Display Options {#setting_map_display_options}

The Map Options dialog is used to change the appearance of the Study Area Map. There are several ways to invoke it:

- select Tools >> Map Display Options from the Main Menu or,

- click the Options button [Options] on the Main Toolbar when the Study Area Map window has the focus or,

- right click on any empty portion of the map and select Options from the popup menu that appears.

A Map Options dialog will appear where you can set various display options, such as subcatchment fill style, node and link size, flow direction arrows, and background color.

### Exporting the Map {#exporting_map}

The full extent view of the study area map can be saved to file using either:

- Autodesk's DXF (Drawing Exchange Format) format,

- the Windows enhanced metafile (EMF) format,

- EPA SWMM's own ASCII text (.map) format.

The DXF format is readable by many Computer Aided Design (CAD) programs. Metafiles can be inserted into word processing documents and loaded into drawing programs for re-scaling and editing. Both formats are vector-based and will not lose resolution when they are displayed at different scales.

To export the map to a DXF, metafile, or text file:

1. Select File >> Export >> Map from the Main Menu.

2. In the Map Export dialog that appears select the format that you want the map saved in.

3. If you select DXF format, you have a choice of how nodes will be represented in the DXF file. They can be drawn as filled circles, as open circles, or as filled squares. Not all DXF readers can recognize the format used in the DXF file to draw a filled circle. Also note that map annotation, such as node and link ID labels, will not be exported, but map label objects will be.

4. After choosing a format, click OK and enter a name for the file in the Save As dialog that appears.
