@page manual_extensions 26 — GUI extensions

GUI extensions add ribbon commands, dock panels, document views and preferences
to SWMMVis. They are compiled Qt plugins built against the matching GUI SDK.

The SDK, extension loader and host services are maintained in `openswmm.gui`
and documented with the GUI. Individual plugins can be maintained in separate
repositories and built against the installed SDK. Use **GUI Extensions** in
the documentation navigation for the author guide and API references.

Open **Tools → Plugins…** to see engine I/O plugins, process components and GUI
extensions. GUI extension rows show version, vendor, license, build compatibility,
load status and failure details. **Add directory…** adds an existing plugin
directory; **Open plugin folder** opens the user plugin directory.

Enable or disable an extension with its checkbox, then restart SWMMVis. Rescan
updates the inventory; it does not execute newly discovered code. Extensions
whose Qt, compiler, architecture or SDK version is incompatible remain listed
with the reason they could not load.

If an extension prevents normal startup, launch with **--no-extensions** or hold
**Shift** while launching. An interrupted initialization or extension job disables
the affected extension on the next launch and displays a recovery explanation.
Native extensions execute inside the application and should come from a trusted
author.

Extension dock visibility and layout follow normal window settings. Extension
document state and project-specific configuration are stored with the project.
Unavailable document kinds are retained so a project can be reopened after the
extension is restored. The shared **Output** dock contains an output tab for each
extension that writes messages.

The separate `openswmm.gui.data` extension adds **Data → USGS sites…** and
**Data → Elevation footprints…**. Its area chooser uses the current map viewport
or a chosen polygon from a loaded feature layer. Preview results respect polygon
holes and multiple parts. Select rows to highlight sites or product footprints;
save selected features to add a normal GIS layer with the reviewed area and
response provenance. Changing the review area clears the results.

This first Data package reviews and caches catalogue metadata. Terrain payload
downloads, observation imports and design storms remain later product packages.
The SDK and its documentation remain part of `openswmm.gui`.

Extension authors should read @ref gui_extension_sdk for the public API,
standalone CMake example, compatibility, ownership, threading and undo contracts.
