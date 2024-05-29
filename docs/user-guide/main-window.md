## SWMM's Main Window {#swmms_main_window}

SWMM's main window is pictured below. Click on a labeled element to learn more about it.

![MainWindow](mainwindow.zoom75.gif)

### Main Menu {#main_menu}

The **Main Menu** located across the top of the SWMM main window contains a collection of menus used for working with the program. These include:

- [File Menu](#file_menu)

- [Edit Menu](#edit_menu)

- [View Menu](#view_menu)

- [Project Menu](#project_menu)

- [Report Menu](#report_menu)

- [Tools Menu](#tools_menu)

- [Window Menu](#window_menu)

- [Help Menu](#help_menu)

### File Menu {#file_menu}

The File Menu contains commands for opening and saving data files and for printing:

| Command       | Description                                                                                                                                                                             |
| :------------ | :-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| New           | Creates a new SWMM project                                                                                                                                                              |
| Open          | Opens an existing project                                                                                                                                                               |
| Reopen        | Reopens a recently used project                                                                                                                                                         |
| Save          | Saves the current project                                                                                                                                                               |
| Save As       | Saves current project under a different name                                                                                                                                            |
| Export        | Exports the [Study Area Map](#study_area_map) to a file; <br> Exports current results to a [Hot Start file](#hot_start_files); <br> Exports the current result's Status/Summary reports |
| Combine       | Combines two [Routing Interface files](#routing_interface_files) together                                                                                                               |
| Page Setup    | Sets page margins and orientation for printing                                                                                                                                          |
| Print Preview | Previews a printout of the current active view (map, report, graph, or table)                                                                                                           |
| Print         | Prints the current view                                                                                                                                                                 |
| Exit          | Exits SWMM                                                                                                                                                                              |

### Edit Menu {#edit_menu}

The Edit Menu contains commands for editing and copying.

| Command       | Description                                                                                                              |
| :------------ | :----------------------------------------------------------------------------------------------------------------------- |
| Copy To       | Copies the currently active view (map, report, graph or table) to the clipboard or to a file                             |
| Select Object | Enables the user to select an object on the [Study Area Map](#study_area_map)                                            |
| Select Vertex | Enables the user to select a vertex of a subcatchment or link displayed on the Map                                       |
| Select Region | Enables the user to delineate a region on the Map for selecting multiple objects                                         |
| Select All    | Selects all objects when the Map is the active window or all cells of a table when a tabular report is the active window |
| Find Object   | Locates a specific object by name on the Map                                                                             |
| Edit Object   | Edits the properties of the currently selected object                                                                    |
| Delete Object | Deletes the currently selected object                                                                                    |
| Group Edit    | Edits a property for the group of objects that fall within the outlined region of the Map                                |
| Group Delete  | Deletes a group of objects that fall within the outlined region of the Map                                               |

### View Menu {#view_menu}

The View Menu contains commands for viewing the Study Area Map and the program's toolbars.

| Command     | Description                                                                |
| :---------- | :------------------------------------------------------------------------- |
| Dimensions  | Sets reference coordinates and distance units for the study area map       |
| Backdrop    | Allows a backdrop image to be added, positioned, and viewed behind the map |
| Pan         | Pans across the map                                                        |
| Zoom In     | Zooms in on the map                                                        |
| Zoom Out    | Zooms out on the map                                                       |
| Full Extent | Redraws the map at full extent                                             |
| Query       | Highlights objects on the map that meet specific criteria                  |
| Overview    | Toggles the display of the [Overview Map](#overview_map)                   |
| Layers      | Toggles display of object layers on the Map                                |
| Legends     | Controls display of the Map legends                                        |
| Toolbars    | Toggles display of the toolbar                                             |

The mouse wheel can also be used to pan, zoom in or zoom out of the map at any time without having to select the Pan,  Zoom In or Zoom Out commands.

### Project Menu {#project_menu}

The Project Menu includes commands related to the current project being analyzed.

| Command          | Description                                                  |
| :--------------- | :----------------------------------------------------------- |
| Summary          | Lists the number of each type of object in the project       |
| Details          | Shows a detailed listing of all project data                 |
| Defaults         | Edits a project's default properties                         |
| Calibration Data | Registers files containing calibration data with the project |
| Add a New Object | Adds a new object to the project                             |
| Run Simulation   | Runs a simulation                                            |

### Report Menu {#report_menu}

The Report Menu contains commands used to report analysis results in different formats.

| Command    | Description                                                 |
| :--------- | :---------------------------------------------------------- |
| Status     | Displays a status report for the most recent simulation run |
| Summary    | Displays summary results in tabular form                    |
| Graph      | Displays simulation results in graphical form               |
| Table      | Displays simulation results in tabular form                 |
| Statistics | Displays a statistical analysis of simulation results       |
| Customize  | Customizes the display of the currently active graph        |

### Tools Menu {#tools_menu}

The Tools Menu contains commands used to configure program preferences, Study Area Map display options, and external add-in tools.

| Command             | Description                                                                                                               |
| :------------------ | :------------------------------------------------------------------------------------------------------------------------ |
| Program Preferences | Sets program preferences, such as font size, confirm deletions, number of decimal places displayed, etc.                  |
| Map Display Options | Sets appearance options for the Map, such as object size, object annotation, flow direction arrows, and back-ground color |
| Configure Tools     | Adds, deletes, or modifies external add-in tools                                                                          |

### Window Menu {#window_menu}

The Window Menu contains commands for arranging and selecting windows within the SWMM workspace.

| Command     | Description                                                                                                    |
| :---------- | :------------------------------------------------------------------------------------------------------------- |
| Cascade     | Arranges windows in cascaded style, with the [Study Area Map](#study_area_map) filling the entire display area |
| Tile        | Minimizes the map and tiles the remaining windows vertically in the display area                               |
| Close All   | Closes all open windows except for the map                                                                     |
| Window List | Lists all open windows; the currently selected window has the focus and is denoted with a check mark           |

### Help Menu {#help_menu}

The Help Menu contains commands for getting help in using EPA SWMM.

| Command            | Description                                                   |
| :----------------- | :------------------------------------------------------------ |
| User Guide         | Displays the User Guide's Table of Contents                   |
| How do I           | Displays a list of topics covering the most common operations |
| Keyboard Shortcuts | Displays a list of keyboard shortcuts for main menu commands  |
| Measurement Units  | Shows measurement units for all of SWMM's parameters          |
| Error Messages     | Lists the meaning of all error messages                       |
| Tutorials          | Lists tutorials that show how to use EPA SWMM                 |
| Welcome Screen     | Displays SWMM's Welcome screen                                |
| About              | Displays information about the version of EPA SWMM being used |

### Keyboard Shortcuts {#keyboard_shortcuts}

Several main menu commands have keyboard shortcuts that can be used to to select them. They are listed below.

| Menu Command                  | Shortcut Key       |
| :---------------------------- | :----------------- |
| File \| New                   | Ctrl-N             |
| File \| Open                  | Ctrl-O             |
| File \| Save                  | Ctrl-S             |
| File \| Save As               | Ctrl-Alt-S         |
| File \| Exit                  | Alt-F4             |
| Edit \| Copy To               | Ctrl-C             |
| Edit \| Select All            | Ctrl-A             |
| Edit \| Find Object           | Ctrl-F             |
| Edit \| Edit Object           | F2                 |
| Edit \| Delete Object         | Ctrl-Delete        |
| Edit \| Group Edit            | Shift-F2           |
| View \| Query                 | Ctrl-Q             |
| Project \| Add a New <object> | Ctrl-Insert        |
| Project \| Run Simulation     | F9                 |
| Report \| Graph               | Time Series Ctrl-G |
| Window \| Cascade             | Shift-F5           |
| Window \| Tile                | Shift-F4           |
| Window \| Close All           | Shift-Ctrl-F4      |
| Help \| User Guide            | Ctrl-F1            |

In addition the F1 key can be used to bring up context-sensitive Help in most of SWMM's data editing windows.

## Main Toolbar {#main_toolbar}

The **Main Toolbar** provides shortcuts to teh following Main Menu commands:

|                          |                                                                                                                |
| :----------------------- | :------------------------------------------------------------------------------------------------------------- |
| ![](new.gif)             | Creates a new project                                                                                          |
| ![](open.gif)            | Opens and existing project                                                                                     |
| ![](save.gif)            | Saves the current project                                                                                      |
| ![](print.gif)           | Prints the currently active window                                                                             |
| ![](copy.gif)            | Copies the current selection to the clipboard of to a file                                                     |
| ![](find.gif)            | Find a specific object on the [Study Area Map](#study_area_map)                                                |
| ![](query.gif)           | Makes a visual query of the [Study Area Map](#study_area_map)                                                  |
| ![](world.gif)           | Toggles the display of the [Overview Map](#overview_map)                                                       |
| ![](run.gif)             | Runs a simulation                                                                                              |
| ![](report.gif)          | Displays a run's Status and Summary reports                                                                    |
| ![](profileplot.gif)     | Creates a profile plot of simulation results                                                                   |
| ![](timeseriesplot.gif)  | Creates a time series plot of simulation results                                                               |
| ![](timeseriestable.gif) | Creates a time series table of simulation results                                                              |
| ![](scatterplot.gif)     | Creates a scatter plot of simulation results                                                                   |
| ![](statistics.gif)      | Performs a statistical analysis of simulation results                                                          |
| ![](options.gif)         | Modify display options for the currently active view                                                           |
| ![](cascade.gif)         | Arranges windows in cascaded style, with the [Study Area Map](#study_area_map) filling the entire display area |

The toolbar can be made visible of invisible by selecting **View >> Toolbar** from the Main Menu.

## Map Toolbar {#map_toolbar}

The **Map Toolbar** contains buttons for selecting items and viewing the [Study Area Map](#study_area_map):

|                   |                                            |
| :---------------- | :----------------------------------------- |
| ![Select]()       | Selects an object on the map               |
| ![SelectVertex]() | Selects link or subcatchment vertex points |
| ![GroupSelect]()  | Selects a region on the map                |
| ![Pan]()          | Pans across the map                        |
| ![Zoomin]()       | Zooms in on the map                        |
| ![Zoomout]()      | Zooms out on the map                       |
| ![Extents]()      | Draws the map at full extent               |
| ![Measure]()      | Measures a length or area on the map       |

The mouse wheel can also be used to pan, zoom in or zoom out of the map at any time without having to select the Pan, Zoom In or Zoom Out buttons.

The Map Toolbar also contains buttons used to add objects to a project via the Study Area Map:

|                   |                                     |
| :---------------- | :---------------------------------- |
| ![RainGage]()     | Adds a rain gage to the map         |
| ![Subcatchment]() | Adds a subcatchment to the map      |
| ![Junction]()     | Adds a junction node to the map     |
| ![Outfall]()      | Adds an outfall node to the map     |
| ![Divider]()      | Adds a flow divider node to the map |
| ![StorageUnit]()  | Adds a storage unit node to the map |
| ![Conduit]()      | Adds a conduit link to the map      |
| ![PUMP]()         | Adds a pump link to the map         |
| ![Orifice]()      | Adds an orifice link to the map     |
| ![Weir]()         | Adds a weir link to the map         |
| ![Outlet]()       | Adds an outlet link to the map      |
| ![LABEL]()        | Adds a text label to the map        |

## Status Bar {#status_bar}

The Status Bar appears at the bottom of SWMM's Main Window and is divided into six sections:

![Status Bar](statusbar.gif)

**Auto-Length**

Indicates whether the  automatic computation of conduit lengths and subcatchment areas is turned on or off. The setting can be changed by clicking the drop down arrow.

[Offsets](#link_offset_conventions)

Indicates whether the positions of links above the invert of their connecting nodes are expressed as a Depth above the node invert or as the Elevation of the offset. Click the drop down arrow to change this option. If changed, a dialog box will appear asking if all existing offsets in the current project should be changed or not (i.e., convert Depth offsets to Elevation offsets or Elevation offsets to Depth offsets, depending on the option selected).

**Flow Units**

Displays the current flow units that are in effect. Click the drop down arrow to change the choice of flow units. Selecting a US flow unit means that all other quantities will be expressed in US units, while choosing a metric flow unit will force all quantities to be expressed in metric units. The units of previously entered data are not automatically adjusted if the unit system is changed.

**Run Status**

|                  |                                                                  |
| :--------------- | :--------------------------------------------------------------- |
| ![flag_white]()  | results are not available because no simulation has been run yet |
| ![flag_green]()  | results are up to date                                           |
| ![flag_yellow]() | results are out of date because project data have changed.       |
| ![flag_red]()    | results are not available because the last simulation had errors |

**Zoom Level**

Displays the current zoom level for the Study Area Map (100% is full scale).

**XY Location**

Displays the Study Area Map coordinates of the current position of the mouse pointer.

## Study Area Map {#study_area_map}

The Study Area Map provides a planar schematic diagram of the objects comprising a drainage system. Its pertinent features are as follows:

![Study Area Map](studyareamap.zoom77.gif)

- The location of objects and the distances between them do not necessarily have to conform to their actual physical scale.

- Selected properties of these objects, such as water quality at nodes or flow velocity in links, can be displayed by using different colors. The color-coding is described in a Legend, which can be edited.

- New objects can be directly added to the Map and existing objects can be selected for editing, deleting, and repositioning.

- A backdrop drawing (such as a street or topographic map) can be placed behind the Map for reference.

- The Map can be zoomed to any scale and panned from one position to another.

- Nodes and links can be drawn at different sizes, flow direction arrows added, and object symbols, ID labels and numerical property values displayed.

- The Map can be printed, copied onto the Windows clipboard, or exported as a DXF file or Windows metafile.

_See Also_

[Working with the Map](#working_with_the_map)

## Project Browser

The Project Browser appears when the Project tab on the left panel of SWMM's Main Window is pressed. It provides access to all of the data in a project.  The vertical sizes of the list boxes in the browser can be adjusted by using the splitter bar located just below the upper box. The width of the Browser panel can be adjusted by using the splitter bar located along its right edge

|                                      |                                                                                                                                                                                                                                                                                                                                                     |
| ------------------------------------ | :-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| [ProjectBrowser](projectbrowser.gif) | The upper list box displays the various categories of data objects available to a SWMMproject. The lower list box lists the name of each individual object of the currently selected data category.                                                                                                                                                 |
| ^                                    | The buttons between the two list boxes are used as follows: <br> [Add] adds a new object, <br> [Delete] deletes the selected object, <br> [edit] edits the selected object, <br> [MoveUp] moves the selected object up one position, <br> [MoveDown] moves the selected object down one position, <br> [Sort] sorts the objects in ascending order. |

Selections made in the Project Browser are coordinated with objects highlighted on the [Study Area Map](#study_area_map), and vice versa. For example, selecting a conduit in the Browser will cause that conduit to be highlighted on the map, while selecting it on the map will cause it to become the selected object in the Browser.

## Map Browser {#map_browser}

The Map Browser appears when the Map tab on the left panel of the SWMM's Main Window is selected. . It controls the mapping themes and time periods viewed on the Study Area Map. The width of the Map Browser panel can be adjusted by using the splitter bar located along its right edge. The Map Browser consists of the following three panels that control what results are displayed on the map:

+-----------------------------------+-----------------------------------+
| [MapBrowser] |   |
| | |
| |   |
| | |
| | The Themes panel selects a set of |
| | variables to view in color-coded |
| | fashion on the Map. |
| | |
| |   |
| | |
| |   |
| | |
| |   |
| | |
| |   |
| | |
| |   |
| | |
| |   |
| | |
| |   |
| | |
| | The Time Period panel selects |
| | which time period of the |
| | simulation results are viewed on |
| | the Map. |
| | |
| |   |
| | |
| |   |
| | |
| |   |
| | |
| |   |
| | |
| |   |
| | |
| |   |
| | |
| |   |
| | |
| |   |
| | |
| |   |
| | |
| | The Animator panel controls the |
| | animated display of the Study |
| | Area Map and all Profile Plots |
| | over time. |
| | |
| |   |
| | |
| |   |
+-----------------------------------+-----------------------------------+

### Map Browser - Themes {#map_browser-themes}

The Themes panel of the Map Browser is used to select a thematic variable to view in color-coded fashion on the Study Area Map.

+-----------------------------------+-----------------------------------+
| [MapThemes] |   |
| | |
| | Subcatchments -  selects the |
| | theme to display for the |
| | subcatchment areas shown on the |
| | Map. |
| | |
| |   |
| | |
| | Nodes - selects the theme to |
| | display for the drainage system |
| | nodes shown on the Map. |
| | |
| |   |
| | |
| | Links - selects the theme to |
| | display for the drainage system |
| | links shown on the Map. |
| | |
| |   |
+-----------------------------------+-----------------------------------+

### Map Browser - Time Period {#map_browser-time_period}

The Time Period panel of the Map Browser is used to select a time period in which to view computed results in thematic fashion on the Study Area Map.

+-----------------------------------+-----------------------------------+
| [MapTimePeriods] |   |
| | |
| | Date - selects the day for which |
| | simulation results will be |
| | viewed. |
| | |
| |   |
| | |
| | Time of Day - selects the hour of |
| | the current day for which |
| | simulation results will be |
| | viewed. |
| | |
| |   |
| | |
| | Elapsed Time - selects the |
| | elapsed time from the start of |
| | the simulation for which results |
| | will be viewed. |
| | |
| |   |
+-----------------------------------+-----------------------------------+

### Map Browser - Animator {#map+browser-animator}

The Animator panel of the Map Browser contains controls for animating the Study Area Map and all Profile Plots through time i.e., updating map color-coding and hydraulic grade line profile depths as the simulation time clock is automatically moved forward or back. The meaning of the control buttons are as follows:

+-----------------------------------+-----------------------------------+
| [MapAnimator] | [AnimatorFirst] Returns to the |
| | starting period. |
| | |
| | [AnimatorBack]  Starts animating |
| | backwards in time. |
| | |
| | [AnimatorStop]  Stops the |
| | animation. |
| | |
| | [AnimatorForward]  Starts |
| | animating forwards in time. |
+-----------------------------------+-----------------------------------+

The slider bar is used to adjust the animation speed.

## Property Editor {#property_editor}

The Property Editor (shown below) is used to edit the properties of data objects that can appear on the Study Area Map. It is invoked when one of these objects is selected (either on the map or in the Project Browser) and double-clicked or when the Project Browser's Edit button [edit] is clicked.

[PropertyEditor]

Key features of the Property Editor include:

· The Editor is a grid with two columns - one for the property's name and the other for its value.

· The columns can be re-sized by re-sizing the header at the top of the Editor with the mouse.

· A hint area is displayed at the bottom of the Editor with an expanded description of the property being edited. The size of this area can be adjusted by dragging the splitter bar located just above it.

· The Editor window can be moved and re-sized via the normal Windows operations.

· Depending on the property, the value field can be one of the following:

§ a text box in which you enter a value

§ a dropdown combo box from which you select a value from a list of choices

§ a dropdown combo box in which you can enter a value or select from a list of choices

§ an ellipsis button which you click to bring up a specialized editor

· The field in the Editor which currently has focus will have a focus rectangle drawn around it.

· Both the mouse and the Up and Down arrow keys on the keyboard can be used to move between fields.

· To begin editing the field with the focus, either begin typing a value or hit the Enter key.

· To have the program accept edits made in a property field, press the Enter key or move to another field. To cancel an edit, press the Esc key.

· The Property Editor can be hidden by clicking the button in the upper right corner of its title bar.

## Setting Program Preferences {#setting_program_preferences}

Program preferences allow one to customize certain program features. To set program preferences, select Program Preferences from the Tools menu. A Preferences dialog will appear containing two tabbed pages - one for General Preferences and one for Numerical Precision.

### General Preferences {#general_preferences}

The following preferences can be set on the General Preferences page of the Preferences dialog:

+-----------------------------------+-----------------------------------+
| Blinking Map Highlighter | Check to make the selected object |
| | on the Study Area Map blink on |
| | and off. |
+-----------------------------------+-----------------------------------+
| Flyover Map Labeling | Check to display the ID name and |
| | current theme value in a |
| | hint-style box whenever the mouse |
| | is placed over an object on the |
| | Map. |
+-----------------------------------+-----------------------------------+
| Confirm Deletions | Check to display a confirmation |
| | dialog box before deleting any |
| | object. |
+-----------------------------------+-----------------------------------+
| Automatic Backup File | Check to save a backup copy of a |
| | newly opened project to disk |
| | named with a .bak extension. |
+-----------------------------------+-----------------------------------+
| Report Elapsed Time by Default | Check to use elapsed time (rather |
| | than date/time) as the default |
| | for time series graphs and |
| | tables. |
+-----------------------------------+-----------------------------------+
| Prompt to Save Results | If left unchecked then simulation |
| | results are automatically saved |
| | to disk when the current project |
| | is closed. Otherwise the user |
| | will be asked if results should |
| | be saved. |
+-----------------------------------+-----------------------------------+
| Show Welcome Screen | Check to have SWMM display a |
| | welcome screen when started. |
+-----------------------------------+-----------------------------------+
| Clear Recent Project List | Check to clear the list of most |
| | recently used files appearing |
| | when File >> Reopen is selected |
| | from the Main Menu. |
+-----------------------------------+-----------------------------------+
| Style Theme | Selects a color theme to use for |
| | SWMM's |
| | |
| | user interface (see below) |
+-----------------------------------+-----------------------------------+

[WindowsTheme] [IcebergTheme] [QuartzTheme]

### Numerical Precision {#numerical_precision}

The Numerical Precision page of the Preferences dialog controls the number of decimal places displayed when simulation results are reported. Use the dropdown list boxes to select a specific Subcatchment, Node or Link variable, and then use the edit boxes next to them to select the number of decimal places to include when displaying computed results for the variable.

[PrecisionPreferences]

Note that the number of decimal places displayed for any particular input design parameter, such as slope, diameter, length, etc. is whatever the user enters.
